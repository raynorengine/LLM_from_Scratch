#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HANGUL_BASE 0xAC00
#define HANGUL_END  0xD7A3
#define BOS_CP 0
#define EOS_CP 1
#define MAX_N 7       /* 최대 n-gram (6-gram까지 지원) */
#define GEN_COUNT 1000
#define MAX_WORD_LEN 50

/* ===== 해시 테이블 (가변 길이 키) ===== */
#define HASH_SIZE (1 << 20)
#define HASH_MASK (HASH_SIZE - 1)

typedef struct { int next_cp; int count; } NextOpt;

typedef struct Entry {
    int key[MAX_N];   /* 키: 앞 (n-1)개 코드포인트 */
    int key_len;
    NextOpt *opts;
    int opt_count, opt_cap, total;
    int *cum;
    struct Entry *next;
} Entry;

typedef struct {
    Entry *table[HASH_SIZE];
} NGramTable;

static unsigned int hash_key(const int *key, int len)
{
    unsigned int h = 0;
    for (int i = 0; i < len; i++)
        h = h * 31 + (unsigned int)key[i];
    return h & HASH_MASK;
}

static int key_eq(const int *a, const int *b, int len)
{
    for (int i = 0; i < len; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static Entry *find_entry(NGramTable *t, const int *key, int len)
{
    unsigned int h = hash_key(key, len);
    Entry *e = t->table[h];
    while (e) {
        if (e->key_len == len && key_eq(e->key, key, len)) return e;
        e = e->next;
    }
    return NULL;
}

static void add_entry(NGramTable *t, const int *key, int key_len, int next_cp)
{
    unsigned int h = hash_key(key, key_len);
    Entry *e = t->table[h];
    while (e) {
        if (e->key_len == key_len && key_eq(e->key, key, key_len)) {
            /* 기존 엔트리에서 next_cp 찾기 */
            for (int i = 0; i < e->opt_count; i++) {
                if (e->opts[i].next_cp == next_cp) {
                    e->opts[i].count++;
                    e->total++;
                    return;
                }
            }
            /* 새 옵션 추가 */
            if (e->opt_count >= e->opt_cap) {
                e->opt_cap *= 2;
                e->opts = realloc(e->opts, sizeof(NextOpt) * e->opt_cap);
            }
            e->opts[e->opt_count].next_cp = next_cp;
            e->opts[e->opt_count].count = 1;
            e->opt_count++;
            e->total++;
            return;
        }
        e = e->next;
    }
    /* 새 엔트리 */
    e = calloc(1, sizeof(Entry));
    memcpy(e->key, key, sizeof(int) * key_len);
    e->key_len = key_len;
    e->opt_cap = 8;
    e->opts = malloc(sizeof(NextOpt) * e->opt_cap);
    e->opts[0].next_cp = next_cp;
    e->opts[0].count = 1;
    e->opt_count = 1;
    e->total = 1;
    e->next = t->table[h];
    t->table[h] = e;
}

static void build_cumulative(NGramTable *t)
{
    for (int i = 0; i < HASH_SIZE; i++) {
        for (Entry *e = t->table[i]; e; e = e->next) {
            e->cum = malloc(sizeof(int) * e->opt_count);
            e->cum[0] = e->opts[0].count;
            for (int j = 1; j < e->opt_count; j++)
                e->cum[j] = e->cum[j-1] + e->opts[j].count;
        }
    }
}

static int count_contexts(NGramTable *t)
{
    int c = 0;
    for (int i = 0; i < HASH_SIZE; i++)
        for (Entry *e = t->table[i]; e; e = e->next) c++;
    return c;
}

static int count_ngrams(NGramTable *t)
{
    int c = 0;
    for (int i = 0; i < HASH_SIZE; i++)
        for (Entry *e = t->table[i]; e; e = e->next) c += e->opt_count;
    return c;
}

static void free_table(NGramTable *t)
{
    for (int i = 0; i < HASH_SIZE; i++) {
        Entry *e = t->table[i];
        while (e) {
            Entry *next = e->next;
            free(e->opts);
            free(e->cum);
            free(e);
            e = next;
        }
        t->table[i] = NULL;
    }
}

/* ===== UTF-8 ===== */
static int next_codepoint(const unsigned char *s, int *cp)
{
    if (s[0] >= 0xE0 && s[0] <= 0xEF && s[1] && s[2]) {
        *cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    }
    *cp = 0;
    return 1;
}

static void cp_to_utf8(int cp, unsigned char *u)
{
    u[0] = 0xE0 | ((cp >> 12) & 0x0F);
    u[1] = 0x80 | ((cp >> 6) & 0x3F);
    u[2] = 0x80 | (cp & 0x3F);
    u[3] = 0;
}

static int big_rand(int max)
{
    return (rand() * 32768 + rand()) % max;
}

static int hangul_len(const char *s)
{
    return (int)strlen(s) / 3;
}

/* ===== 사전 ===== */
#define MAX_DICT 430000
static char **dict;
static int dict_count = 0;

static int dict_search(const char *word)
{
    int lo = 0, hi = dict_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = strcmp(dict[mid], word);
        if (cmp == 0) return 1;
        if (cmp < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return 0;
}

/* ===== words.txt에서 n-gram 학습 ===== */
static void train_ngram(NGramTable *t, int n, FILE *fp)
{
    char line[4096];
    rewind(fp);

    while (fgets(line, sizeof(line), fp)) {
        /* 단어의 코드포인트 배열 추출 */
        int cps[256];
        int cp_count = 0;
        const unsigned char *p = (const unsigned char *)line;
        while (*p) {
            int cp;
            int len = next_codepoint(p, &cp);
            if (cp >= HANGUL_BASE && cp <= HANGUL_END && cp_count < 250)
                cps[cp_count++] = cp;
            p += len;
        }
        if (cp_count == 0) continue;

        /* BOS 패딩 + 코드포인트 + EOS */
        int padded[256 + MAX_N];
        int pad = n - 1;
        for (int i = 0; i < pad; i++)
            padded[i] = BOS_CP;
        for (int i = 0; i < cp_count; i++)
            padded[pad + i] = cps[i];
        padded[pad + cp_count] = EOS_CP;
        int total_len = pad + cp_count + 1;

        /* n-gram 추출 */
        for (int i = 0; i <= total_len - n; i++) {
            int key[MAX_N];
            for (int j = 0; j < n - 1; j++)
                key[j] = padded[i + j];
            int next_cp = padded[i + n - 1];
            add_entry(t, key, n - 1, next_cp);
        }
    }
}

/* ===== n-gram으로 단어 생성 ===== */
static int gen_word(NGramTable *t, int n, char *buf)
{
    int context[MAX_N];
    int ctx_len = n - 1;
    for (int i = 0; i < ctx_len; i++)
        context[i] = BOS_CP;

    int buf_len = 0;
    for (int step = 0; step < MAX_WORD_LEN; step++) {
        Entry *e = find_entry(t, context, ctx_len);
        if (!e || e->opt_count == 0) break;

        int r = big_rand(e->total);
        int lo = 0, hi = e->opt_count - 1;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (e->cum[mid] <= r) lo = mid + 1;
            else hi = mid;
        }
        int next_cp = e->opts[lo].next_cp;

        if (next_cp == EOS_CP) break;

        unsigned char u[4];
        cp_to_utf8(next_cp, u);
        memcpy(buf + buf_len, u, 3);
        buf_len += 3;

        /* 컨텍스트 시프트 */
        for (int i = 0; i < ctx_len - 1; i++)
            context[i] = context[i + 1];
        context[ctx_len - 1] = next_cp;
    }
    buf[buf_len] = 0;
    return buf_len;
}

#define MAX_LEN_BUCKET 16
#define MIN_NGRAM 2
#define MAX_NGRAM 6

int main(void)
{
    /* 사전 로드 */
    dict = malloc(sizeof(char *) * MAX_DICT);
    {
        FILE *df = fopen("korean-dict/words.txt", "r");
        if (!df) { fprintf(stderr, "words.txt를 열 수 없습니다.\n"); return 1; }
        char dline[4096];
        while (fgets(dline, sizeof(dline), df) && dict_count < MAX_DICT) {
            int len = (int)strlen(dline);
            while (len > 0 && (dline[len-1] == '\n' || dline[len-1] == '\r'))
                dline[--len] = 0;
            dict[dict_count] = malloc(len + 1);
            strcpy(dict[dict_count], dline);
            dict_count++;
        }
        fclose(df);
    }

    FILE *words_fp = fopen("korean-dict/words.txt", "r");
    if (!words_fp) { fprintf(stderr, "words.txt를 열 수 없습니다.\n"); return 1; }

    FILE *out = fopen("step06_result.txt", "w");
    fputc(0xEF, out); fputc(0xBB, out); fputc(0xBF, out);

    fprintf(out, "=== n-gram 비교 (n=%d~%d, 각 %d개 단어 생성) ===\n\n", MIN_NGRAM, MAX_NGRAM, GEN_COUNT);
    printf("=== n-gram 비교 (n=%d~%d, 각 %d개 단어 생성) ===\n\n", MIN_NGRAM, MAX_NGRAM, GEN_COUNT);

    /* 각 n에 대한 통계 저장 */
    int all_match[MAX_N] = {0};
    double all_avg_len[MAX_N] = {0};
    int all_contexts[MAX_N] = {0};
    int all_ngrams[MAX_N] = {0};
    int all_len_total[MAX_N][MAX_LEN_BUCKET] = {{0}};
    int all_len_match[MAX_N][MAX_LEN_BUCKET] = {{0}};

    srand((unsigned)time(NULL));

    for (int n = MIN_NGRAM; n <= MAX_NGRAM; n++) {
        printf("%d-gram 학습 중...\n", n);

        NGramTable *t = calloc(1, sizeof(NGramTable));
        train_ngram(t, n, words_fp);
        build_cumulative(t);
         all_contexts[n] = count_contexts(t);
         all_ngrams[n] = count_ngrams(t);

         printf("%d-gram 학습 완료: 고유 n-gram %d개, 문맥 %d개. 생성 중...\n",
             n, all_ngrams[n], all_contexts[n]);

        /* 생성 단어를 개별 파일에 저장 */
        char fname[64];
        sprintf(fname, "step06_words_%dgram.txt", n);
        FILE *wf = fopen(fname, "w");
        fputc(0xEF, wf); fputc(0xBB, wf); fputc(0xBF, wf);

        int match = 0, total_len = 0;
        char buf[512];
        for (int i = 0; i < GEN_COUNT; i++) {
            gen_word(t, n, buf);
            int len = hangul_len(buf);
            total_len += len;
            int bucket = len < MAX_LEN_BUCKET ? len : MAX_LEN_BUCKET - 1;
            all_len_total[n][bucket]++;
            int exists = dict_search(buf);
            if (exists) {
                match++;
                all_len_match[n][bucket]++;
            }
            fprintf(wf, "%s %s\n", buf, exists ? "[O]" : "[X]");
        }
        fclose(wf);
        all_match[n] = match;
        all_avg_len[n] = (double)total_len / GEN_COUNT;

        free_table(t);
        free(t);
    }
    fclose(words_fp);

    /* ===== 총괄 결과 출력 ===== */
    /* 헤더 */
    fprintf(out, "         ");
    printf("         ");
    for (int n = MIN_NGRAM; n <= MAX_NGRAM; n++) {
        fprintf(out, "  %d-gram    ", n);
        printf("  %d-gram    ", n);
    }
    fprintf(out, "\n");
    printf("\n");

    /* 고유 n-gram 수 */
    fprintf(out, "n-gram수 ");
    printf("n-gram수 ");
    for (int n = MIN_NGRAM; n <= MAX_NGRAM; n++) {
        fprintf(out, "%8d    ", all_ngrams[n]);
        printf("%8d    ", all_ngrams[n]);
    }
    fprintf(out, "\n");
    printf("\n");

    /* 고유 문맥 수 */
    fprintf(out, "문맥 수  ");
    printf("문맥 수  ");
    for (int n = MIN_NGRAM; n <= MAX_NGRAM; n++) {
        fprintf(out, "%8d    ", all_contexts[n]);
        printf("%8d    ", all_contexts[n]);
    }
    fprintf(out, "\n");
    printf("\n");

    /* 일치율 */
    fprintf(out, "일치율   ");
    printf("일치율   ");
    for (int n = MIN_NGRAM; n <= MAX_NGRAM; n++) {
        fprintf(out, "  %5.1f%%    ", 100.0 * all_match[n] / GEN_COUNT);
        printf("  %5.1f%%    ", 100.0 * all_match[n] / GEN_COUNT);
    }
    fprintf(out, "\n");
    printf("\n");

    /* 평균 길이 */
    fprintf(out, "평균길이 ");
    printf("평균길이 ");
    for (int n = MIN_NGRAM; n <= MAX_NGRAM; n++) {
        fprintf(out, "  %4.1f자    ", all_avg_len[n]);
        printf("  %4.1f자    ", all_avg_len[n]);
    }
    fprintf(out, "\n\n");
    printf("\n\n");

    /* 글자수별 일치율 */
    fprintf(out, "글자수별 일치율:\n");
    printf("글자수별 일치율:\n");
    fprintf(out, "길이 ");
    printf("길이 ");
    for (int n = MIN_NGRAM; n <= MAX_NGRAM; n++) {
        fprintf(out, "    %d-gram      ", n);
        printf("    %d-gram      ", n);
    }
    fprintf(out, "\n");
    printf("\n");

    for (int i = 1; i < MAX_LEN_BUCKET; i++) {
        int any = 0;
        for (int n = MIN_NGRAM; n <= MAX_NGRAM; n++)
            if (all_len_total[n][i] > 0) any = 1;
        if (!any) continue;

        if (i < MAX_LEN_BUCKET - 1) {
            fprintf(out, "%2d자 ", i);
            printf("%2d자 ", i);
        } else {
            fprintf(out, "%d+자", i);
            printf("%d+자", i);
        }

        for (int n = MIN_NGRAM; n <= MAX_NGRAM; n++) {
            int t = all_len_total[n][i], m = all_len_match[n][i];
            double pct = t > 0 ? 100.0 * m / t : 0;
            fprintf(out, " %3d/%3d(%5.1f%%) ", m, t, pct);
            printf(" %3d/%3d(%5.1f%%) ", m, t, pct);
        }
        fprintf(out, "\n");
        printf("\n");
    }

    fclose(out);
    printf("\n결과가 step06_result.txt에 저장되었습니다.\n");

    for (int i = 0; i < dict_count; i++) free(dict[i]);
    free(dict);
    return 0;
}
