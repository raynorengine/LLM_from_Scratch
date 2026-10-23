#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BOS_ID 0
#define EOS_ID 1
#define MAX_N 8
#define MAX_SENT_CHARS 200
#define GEN_COUNT 10

/* ===== 글자 → ID 매핑 ===== */
#define CHAR_HASH_SIZE (1 << 14)
#define CHAR_HASH_MASK (CHAR_HASH_SIZE - 1)

typedef struct CharEntry {
    int codepoint;
    int id;
    struct CharEntry *next;
} CharEntry;

static CharEntry *char_table[CHAR_HASH_SIZE];
static int *id_to_cp;
static int id_cap = 4096;
static int id_count = 2; /* 0=BOS, 1=EOS */

static int char_find_or_add(int cp)
{
    unsigned int h = (unsigned int)cp & CHAR_HASH_MASK;
    CharEntry *e = char_table[h];
    while (e) {
        if (e->codepoint == cp) return e->id;
        e = e->next;
    }
    if (id_count >= id_cap) {
        id_cap *= 2;
        id_to_cp = realloc(id_to_cp, sizeof(int) * id_cap);
    }
    e = malloc(sizeof(CharEntry));
    e->codepoint = cp;
    e->id = id_count;
    e->next = char_table[h];
    char_table[h] = e;
    id_to_cp[id_count] = cp;
    return id_count++;
}

/* ===== UTF-8 ===== */
static int next_codepoint(const unsigned char *s, int *cp)
{
    if (s[0] >= 0xE0 && s[0] <= 0xEF && s[1] && s[2]) {
        *cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    }
    if (s[0] >= 0xC0 && s[0] <= 0xDF && s[1]) {
        *cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        return 2;
    }
    if (s[0] > 0 && s[0] < 0x80) {
        *cp = s[0];
        return 1;
    }
    *cp = 0;
    return 1;
}

static void write_cp(FILE *f, int cp)
{
    if (cp <= 0x7F) {
        fputc(cp, f);
    } else if (cp <= 0x7FF) {
        fputc(0xC0 | (cp >> 6), f);
        fputc(0x80 | (cp & 0x3F), f);
    } else {
        fputc(0xE0 | ((cp >> 12) & 0x0F), f);
        fputc(0x80 | ((cp >> 6) & 0x3F), f);
        fputc(0x80 | (cp & 0x3F), f);
    }
}

/* ===== n-gram 해시 테이블 (step06 재사용) ===== */
#define HASH_SIZE (1 << 20)
#define HASH_MASK (HASH_SIZE - 1)

typedef struct { int next_id; int count; } NextOpt;

typedef struct Entry {
    int key[MAX_N];
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

static void add_entry(NGramTable *t, const int *key, int key_len, int next_id)
{
    unsigned int h = hash_key(key, key_len);
    Entry *e = t->table[h];
    while (e) {
        if (e->key_len == key_len && key_eq(e->key, key, key_len)) {
            for (int i = 0; i < e->opt_count; i++) {
                if (e->opts[i].next_id == next_id) {
                    e->opts[i].count++;
                    e->total++;
                    return;
                }
            }
            if (e->opt_count >= e->opt_cap) {
                e->opt_cap *= 2;
                e->opts = realloc(e->opts, sizeof(NextOpt) * e->opt_cap);
            }
            e->opts[e->opt_count].next_id = next_id;
            e->opts[e->opt_count].count = 1;
            e->opt_count++;
            e->total++;
            return;
        }
        e = e->next;
    }
    e = calloc(1, sizeof(Entry));
    memcpy(e->key, key, sizeof(int) * key_len);
    e->key_len = key_len;
    e->opt_cap = 8;
    e->opts = malloc(sizeof(NextOpt) * e->opt_cap);
    e->opts[0].next_id = next_id;
    e->opts[0].count = 1;
    e->opt_count = 1;
    e->total = 1;
    e->next = t->table[h];
    t->table[h] = e;
}

static void build_cumulative(NGramTable *t)
{
    for (int i = 0; i < HASH_SIZE; i++)
        for (Entry *e = t->table[i]; e; e = e->next) {
            e->cum = malloc(sizeof(int) * e->opt_count);
            e->cum[0] = e->opts[0].count;
            for (int j = 1; j < e->opt_count; j++)
                e->cum[j] = e->cum[j-1] + e->opts[j].count;
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

static int big_rand(int max)
{
    return (rand() * 32768 + rand()) % max;
}

/* ===== 문장 저장 ===== */
#define MAX_SENTENCES 200000
#define MAX_TOTAL_IDS 8000000

static int *all_ids;
static int *sent_start;
static int *sent_len;
static int sent_count = 0;
static int total_ids = 0;

/* 문장 경계 판별 */
static int is_sent_end(int cp)
{
    return cp == '.' || cp == '!' || cp == '?';
}

static int is_hangul_char(int cp)
{
    return cp >= 0xAC00 && cp <= 0xD7A3;
}

static int is_space_char(int cp)
{
    return cp == ' ';
}

/* 포함할 글자인지 (한글, 공백, 구두점 등) */
static int is_valid_char(int cp)
{
    if (is_hangul_char(cp)) return 1; /* 한글 */
    if (cp == ' ' || cp == ',') return 1;         /* 공백, 쉼표 */
    if (cp == '.' || cp == '!' || cp == '?') return 1; /* 문장부호 */
    return 0;
}

static void flush_sentence(int *buf, int len)
{
    int start = 0;
    int end = len;
    int has_hangul = 0;

    while (start < end && is_space_char(id_to_cp[buf[start]]))
        start++;
    while (end > start && is_space_char(id_to_cp[buf[end - 1]]))
        end--;

    for (int i = start; i < end; i++) {
        if (is_hangul_char(id_to_cp[buf[i]])) {
            has_hangul = 1;
            break;
        }
    }

    if (!has_hangul || sent_count >= MAX_SENTENCES) return;
    if (total_ids + (end - start) > MAX_TOTAL_IDS) return;

    sent_start[sent_count] = total_ids;
    sent_len[sent_count] = end - start;
    memcpy(all_ids + total_ids, buf + start, sizeof(int) * (end - start));
    total_ids += end - start;
    sent_count++;
}

/* ===== n-gram 학습 ===== */
static void train_ngram(NGramTable *t, int n)
{
    int padded[4096 + MAX_N];

    for (int s = 0; s < sent_count; s++) {
        int slen = sent_len[s];
        int *ids = all_ids + sent_start[s];
        int pad = n - 1;

        for (int i = 0; i < pad; i++)
            padded[i] = BOS_ID;
        for (int i = 0; i < slen; i++)
            padded[pad + i] = ids[i];
        padded[pad + slen] = EOS_ID;
        int plen = pad + slen + 1;

        for (int i = 0; i <= plen - n; i++) {
            int key[MAX_N];
            for (int j = 0; j < n - 1; j++)
                key[j] = padded[i + j];
            add_entry(t, key, n - 1, padded[i + n - 1]);
        }
    }
}

/* ===== 문장 생성 ===== */
static void gen_sentence(NGramTable *t, int n, FILE *out, int idx)
{
    int context[MAX_N];
    int ctx_len = n - 1;
    for (int i = 0; i < ctx_len; i++)
        context[i] = BOS_ID;

    char num[16];
    sprintf(num, "%2d: ", idx);
    fputs(num, out);
    fputs(num, stdout);

    for (int step = 0; step < MAX_SENT_CHARS; step++) {
        unsigned int h = hash_key(context, ctx_len);
        Entry *e = t->table[h];
        while (e) {
            if (e->key_len == ctx_len && key_eq(e->key, context, ctx_len)) break;
            e = e->next;
        }
        if (!e || e->opt_count == 0) break;

        int r = big_rand(e->total);
        int lo = 0, hi = e->opt_count - 1;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (e->cum[mid] <= r) lo = mid + 1;
            else hi = mid;
        }
        int next_id = e->opts[lo].next_id;

        if (next_id == EOS_ID) break;

        int cp = id_to_cp[next_id];
        write_cp(out, cp);
        write_cp(stdout, cp);

        for (int i = 0; i < ctx_len - 1; i++)
            context[i] = context[i + 1];
        context[ctx_len - 1] = next_id;
    }

    fputc('\n', out);
    fputc('\n', stdout);
}

int main(void)
{
    memset(char_table, 0, sizeof(char_table));
    id_to_cp = malloc(sizeof(int) * id_cap);
    id_to_cp[0] = 0; /* BOS */
    id_to_cp[1] = 0; /* EOS */

    all_ids = malloc(sizeof(int) * MAX_TOTAL_IDS);
    sent_start = malloc(sizeof(int) * MAX_SENTENCES);
    sent_len = malloc(sizeof(int) * MAX_SENTENCES);

    /* ===== 데이터 로드 ===== */
    FILE *fp = fopen("data_all.txt", "r");
    if (!fp) {
        fprintf(stderr, "data_all.txt를 열 수 없습니다.\n");
        return 1;
    }

    int sent_buf[4096];
    int sent_buf_len = 0;
    int total_chars = 0;
    char line[8192];

    while (fgets(line, sizeof(line), fp)) {
        const unsigned char *p = (const unsigned char *)line;

        /* 빈 줄 = 문단 경계 = 문장 경계 */
        if (line[0] == '\n' || line[0] == '\r') {
            flush_sentence(sent_buf, sent_buf_len);
            sent_buf_len = 0;
            continue;
        }

        while (*p) {
            int cp;
            int len = next_codepoint(p, &cp);

            if (is_valid_char(cp)) {
                if (cp == ' ' && sent_buf_len == 0) {
                    p += len;
                    continue;
                }

                int id = char_find_or_add(cp);
                if (sent_buf_len < 4096)
                    sent_buf[sent_buf_len++] = id;
                total_chars++;

                if (is_sent_end(cp)) {
                    flush_sentence(sent_buf, sent_buf_len);
                    sent_buf_len = 0;
                }
            }
            p += len;
        }
    }
    flush_sentence(sent_buf, sent_buf_len);
    fclose(fp);

    printf("데이터 로드 완료: %d문장, %d글자, %d종 글자\n\n",
           sent_count, total_chars, id_count - 2);

    /* ===== n=3,5,7 학습 및 생성 ===== */
    int ns[] = {3, 5, 7};
    int num_ns = 3;

    srand((unsigned)time(NULL));

    FILE *out = fopen("step07_result.txt", "w");
    fputc(0xEF, out); fputc(0xBB, out); fputc(0xBF, out);

    fprintf(out, "=== 글자 단위 n-gram 문장 생성 ===\n");
    fprintf(out, "데이터: %d문장, %d글자, %d종 글자\n\n",
            sent_count, total_chars, id_count - 2);

    for (int ni = 0; ni < num_ns; ni++) {
        int n = ns[ni];
        printf("%d-gram 학습 중...\n", n);

        NGramTable *t = calloc(1, sizeof(NGramTable));
        train_ngram(t, n);
        build_cumulative(t);
        int contexts = count_contexts(t);
        int ngrams = count_ngrams(t);

        printf("%d-gram: 고유 n-gram %d개, 문맥 %d개\n\n", n, ngrams, contexts);
        fprintf(out, "--- %d-gram (고유 n-gram %d개, 문맥 %d개) ---\n", n, ngrams, contexts);

        for (int i = 0; i < GEN_COUNT; i++)
            gen_sentence(t, n, out, i + 1);

        fprintf(out, "\n");
        printf("\n");

        free_table(t);
        free(t);
    }

    fclose(out);
    printf("결과가 step07_result.txt에 저장되었습니다.\n");

    free(all_ids);
    free(sent_start);
    free(sent_len);
    free(id_to_cp);
    return 0;
}
