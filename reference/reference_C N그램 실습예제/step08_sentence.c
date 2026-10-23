#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BOS_ID 0
#define EOS_ID 1
#define MAX_N 11
#define MAX_SENT_TOKENS 100
#define GEN_COUNT 10

/* ===== 어휘 해시 테이블 (문자열 → ID) ===== */
#define VOCAB_HASH_SIZE (1 << 17)
#define VOCAB_HASH_MASK (VOCAB_HASH_SIZE - 1)

typedef struct VocabEntry {
    char *word;
    int id;
    struct VocabEntry *next;
} VocabEntry;

static VocabEntry *vocab_table[VOCAB_HASH_SIZE];
static char **id_to_word;
static int id_cap = 65536;
static int id_count = 2; /* 0=BOS, 1=EOS */

static unsigned int str_hash(const char *s)
{
    unsigned int h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h & VOCAB_HASH_MASK;
}

static int vocab_find_or_add(const char *word)
{
    unsigned int h = str_hash(word);
    VocabEntry *e = vocab_table[h];
    while (e) {
        if (strcmp(e->word, word) == 0) return e->id;
        e = e->next;
    }
    if (id_count >= id_cap) {
        id_cap *= 2;
        id_to_word = realloc(id_to_word, sizeof(char *) * id_cap);
    }
    e = malloc(sizeof(VocabEntry));
    e->word = _strdup(word);
    e->id = id_count;
    e->next = vocab_table[h];
    vocab_table[h] = e;
    id_to_word[id_count] = e->word;
    return id_count++;
}

/* ===== n-gram 해시 테이블 ===== */
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
#define MAX_SENTENCES 300000
#define MAX_TOTAL_IDS 6000000

static int *all_ids;
static int *sent_start;
static int *sent_len;
static int sent_count = 0;
static int total_ids = 0;

static void store_sentence(int *buf, int len)
{
    if (len == 0 || sent_count >= MAX_SENTENCES) return;
    if (total_ids + len > MAX_TOTAL_IDS) return;
    sent_start[sent_count] = total_ids;
    sent_len[sent_count] = len;
    memcpy(all_ids + total_ids, buf, sizeof(int) * len);
    total_ids += len;
    sent_count++;
}

/* ===== 토큰 파일 파싱: <W>로 분리 ===== */
static int parse_line(const char *line, int *ids, int max_ids)
{
    int count = 0;
    const char *p = line;

    while (*p && *p != '\n' && *p != '\r' && count < max_ids) {
        /* <W> 구분자 찾기 */
        const char *delim = strstr(p, "<W>");
        const char *end = delim ? delim : p + strlen(p);

        /* 줄바꿈 제거 */
        while (end > p && (*(end-1) == '\n' || *(end-1) == '\r'))
            end--;

        if (end > p) {
            /* 토큰 추출 */
            int len = (int)(end - p);
            char tok[256];
            if (len >= 256) len = 255;
            memcpy(tok, p, len);
            tok[len] = 0;
            ids[count++] = vocab_find_or_add(tok);
        }

        if (delim)
            p = delim + 3; /* "<W>" 건너뛰기 */
        else
            break;
    }
    return count;
}

/* ===== n-gram 학습 ===== */
static void train_ngram(NGramTable *t, int n)
{
    int padded[1024];

    for (int s = 0; s < sent_count; s++) {
        int slen = sent_len[s];
        int *ids = all_ids + sent_start[s];
        int pad = n - 1;

        if (slen + pad + 1 > 1024) continue;

        for (int i = 0; i < pad; i++)
            padded[i] = BOS_ID;
        memcpy(padded + pad, ids, sizeof(int) * slen);
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

/* ===== 백오프 탐색: 높은 n부터 시도, 후보 부족하면 낮은 n으로 ===== */
#define MIN_BACKOFF 2
#define BACKOFF_THRESHOLD 1  /* 후보가 1개(원문 복사)이면 백오프 */

static Entry *find_entry(NGramTable *t, const int *key, int key_len)
{
    unsigned int h = hash_key(key, key_len);
    Entry *e = t->table[h];
    while (e) {
        if (e->key_len == key_len && key_eq(e->key, key, key_len)) return e;
        e = e->next;
    }
    return NULL;
}

static int sample_with_backoff(NGramTable **tables, int max_n, const int *history, int hist_len)
{
    /* max_n부터 MIN_BACKOFF까지 시도 */
    for (int n = max_n; n >= MIN_BACKOFF; n--) {
        int ctx_len = n - 1;
        if (ctx_len > hist_len) continue;

        /* history 끝에서 ctx_len개 추출 */
        const int *ctx = history + (hist_len - ctx_len);
        Entry *e = find_entry(tables[n], ctx, ctx_len);

        if (!e || e->opt_count == 0) continue;

        /* 후보가 충분하거나, 이미 가장 낮은 n이면 여기서 선택 */
        if (e->opt_count > BACKOFF_THRESHOLD || n == MIN_BACKOFF) {
            int r = big_rand(e->total);
            int lo = 0, hi = e->opt_count - 1;
            while (lo < hi) {
                int mid = (lo + hi) / 2;
                if (e->cum[mid] <= r) lo = mid + 1;
                else hi = mid;
            }
            return e->opts[lo].next_id;
        }
        /* 후보 부족 → 더 낮은 n으로 백오프 */
    }
    return EOS_ID; /* 아무것도 못 찾으면 문장 종료 */
}

/* ===== 문장 생성 (백오프) ===== */
static void gen_sentence_backoff(NGramTable **tables, int max_n, FILE *out, int idx,
                                  int *prompt_ids, int prompt_len)
{
    int history[512]; /* BOS + 프롬프트 + 생성된 토큰 */
    int hist_len = 0;

    /* BOS로 히스토리 시작 (max_n - 1개) */
    int pad = max_n - 1;
    for (int i = 0; i < pad; i++)
        history[hist_len++] = BOS_ID;

    fprintf(out, "%d: ", idx);

    /* 프롬프트 출력 및 히스토리에 추가 */
    int first = 1;
    for (int p = 0; p < prompt_len; p++) {
        if (!first) fputs("<W>", out);
        fputs(id_to_word[prompt_ids[p]], out);
        first = 0;
        history[hist_len++] = prompt_ids[p];
    }

    /* 토큰 생성 */
    for (int step = 0; step < MAX_SENT_TOKENS; step++) {
        int next_id = sample_with_backoff(tables, max_n, history, hist_len);

        if (next_id == EOS_ID) break;

        if (!first) fputs("<W>", out);
        fputs(id_to_word[next_id], out);
        first = 0;

        if (hist_len < 512)
            history[hist_len++] = next_id;
        else {
            /* 히스토리 오버플로우 방지: 앞부분 잘라내기 */
            memmove(history, history + 256, sizeof(int) * 256);
            hist_len = 256;
            history[hist_len++] = next_id;
        }
    }

    fputc('\n', out);
}

int main(void)
{
    memset(vocab_table, 0, sizeof(vocab_table));
    id_to_word = malloc(sizeof(char *) * id_cap);
    id_to_word[0] = "<BOS>";
    id_to_word[1] = "<EOS>";

    all_ids = malloc(sizeof(int) * MAX_TOTAL_IDS);
    sent_start = malloc(sizeof(int) * MAX_SENTENCES);
    sent_len = malloc(sizeof(int) * MAX_SENTENCES);

    /* ===== 토큰 파일 로드 ===== */
    FILE *fp = fopen("data_tokens.txt", "r");
    if (!fp) {
        fprintf(stderr, "data_tokens.txt를 열 수 없습니다.\n");
        return 1;
    }

    char line[65536];
    int ids_buf[4096];
    int total_tokens = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '\n' || line[0] == '\r') continue;
        int count = parse_line(line, ids_buf, 4096);
        if (count > 0) {
            store_sentence(ids_buf, count);
            total_tokens += count;
        }
    }
    fclose(fp);

    printf("데이터 로드 완료: %d문장, %d토큰, %d종 어휘\n\n",
           sent_count, total_tokens, id_count - 2);

    /* ===== 프롬프트 설정: "돈을" = 돈/NNG + 을/JKO ===== */
    const char *prompt_tokens[] = { "\xeb\x8f\x88/NNG", "\xec\x9d\x84/JKO" }; /* 돈/NNG, 을/JKO */
    int prompt_ids[16];
    int prompt_len = 2;
    for (int i = 0; i < prompt_len; i++) {
        prompt_ids[i] = vocab_find_or_add(prompt_tokens[i]);
    }
    printf("프롬프트: 돈을 (토큰 ID: %d, %d)\n\n", prompt_ids[0], prompt_ids[1]);

    /* ===== n=2~10 모두 학습 ===== */
    #define MAX_NGRAM 10
    NGramTable *tables[MAX_N]; /* tables[n]에 n-gram 테이블 */
    memset(tables, 0, sizeof(tables));

    for (int n = MIN_BACKOFF; n <= MAX_NGRAM; n++) {
        printf("%d-gram 학습 중...\n", n);
        tables[n] = calloc(1, sizeof(NGramTable));
        train_ngram(tables[n], n);
        build_cumulative(tables[n]);
        printf("%d-gram: 고유 n-gram %d개, 문맥 %d개\n",
               n, count_ngrams(tables[n]), count_contexts(tables[n]));
    }
    printf("\n");

    srand((unsigned)time(NULL));

    FILE *out = fopen("step08_raw.txt", "w");
    fputc(0xEF, out); fputc(0xBB, out); fputc(0xBF, out);

    fprintf(out, "=== 토큰 단위 n-gram 문장 생성 (백오프 n=2~%d, 프롬프트: 돈을) ===\n",
            MAX_NGRAM);
    fprintf(out, "데이터: %d문장, %d토큰, %d종 어휘\n\n",
            sent_count, total_tokens, id_count - 2);

    fprintf(out, "--- 백오프 (max_n=%d, 후보 %d개 이하면 낮은 n으로) ---\n",
            MAX_NGRAM, BACKOFF_THRESHOLD);

    for (int i = 0; i < GEN_COUNT * 3; i++)
        gen_sentence_backoff(tables, MAX_NGRAM, out, i + 1, prompt_ids, prompt_len);

    fprintf(out, "\n");

    fclose(out);
    printf("결과가 step08_raw.txt에 저장되었습니다.\n");

    for (int n = MIN_BACKOFF; n <= MAX_NGRAM; n++) {
        if (tables[n]) { free_table(tables[n]); free(tables[n]); }
    }
    free(all_ids);
    free(sent_start);
    free(sent_len);
    return 0;
}
