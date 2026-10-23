/* =====================================================================
 * [STEP 7 / 6단계] 특정 단어로 시작하는 문장 생성 (프롬프트)
 *
 * 하는 일
 *   3-c 는 항상 BOS 5개에서 출발했다. 즉 "아무 말이나 해 봐" 였다.
 *   6단계는 **출발 문맥을 우리가 채운다**. "이 말로 시작해서 이어 봐" 다.
 *
 *     3-c    : 문맥 = BOS BOS BOS BOS BOS
 *     6단계  : 문맥 = BOS BOS BOS 돈/NNG 을/JKO      <- 프롬프트가 BOS 를 밀어낸다
 *
 *   이것이 ChatGPT 에 글을 넣는 것과 같은 동작이다. 사람이 친 글이 모델의
 *   문맥이 되고, 모델은 그 뒤를 이어 쓴다. 장치는 같고 문맥 길이와 모델만 다르다.
 *
 * 여기서 처음으로 **생성이 실패할 수 있다**
 *   생성 도중에는 실재하는 전이만 따라가므로 다음 후보가 반드시 있다.
 *   그런데 프롬프트는 우리가 멋대로 만든 문맥이라 학습 데이터에 없을 수 있다.
 *   없으면 모델은 할 말이 없다. 이것이 7단계(백오프)가 존재하는 이유다.
 *   6단계는 그 문제를 **눈으로 보는** 단계이므로, 막히면 숨기지 않고
 *   왜 막혔는지를 결과 파일에 적는다.
 *
 * 문맥이 5개뿐이라는 한계도 여기서 드러난다
 *   프롬프트가 5토큰을 넘으면 BOS 가 전부 창 밖으로 밀려난다.
 *   그러면 모델은 **자기가 문장 첫머리에 있다는 사실 자체를 모른다.**
 *
 * Phase 3-c 와의 관계
 *   [1]~[5](설정 / 어휘표 / 코퍼스 / 세기 / 난수)는 3-c 와 같은 코드다.
 *   달라지는 것은 [6](출발 문맥)과 [7](프롬프트 진단) 뿐이다.
 *
 * 구역 목차
 *   [1] 설정과 전역
 *   [2] 어휘표 읽기        (3-c 와 같은 코드)
 *   [3] 코퍼스를 번호열로   (3-c 와 같은 코드)
 *   [4] n그램 세기         (3-c 와 같은 코드)
 *   [5] 난수               (3-c 와 같은 코드)
 *   [6] 프롬프트로 출발하는 생성
 *   [7] 프롬프트 진단
 *   [8] 결과 파일 자동 번호
 *   [9] 결과 파일 쓰기
 *  [10] main
 *
 * 사용법
 *   step7-6_prompt.exe "<프롬프트>" [개수=15] [seed=현재시각] [결과파일]
 *   step7-6_prompt.exe --probe "<프롬프트1>" "<프롬프트2>" ...   진단만 한다
 *
 *   프롬프트는 **토큰화된 문자열**로 준다:  "돈/NNG<W>을/JKO"
 *   한글 문장을 이 모양으로 바꾸려면:  python step7-6_verify.py --tokenize "돈을"
 *   (C 에서는 Kiwi 를 쓸 수 없다. 토큰화는 Kiwi 가 이미 있는 Python 쪽에 둔다)
 *   빈 프롬프트("")를 주면 3-c 와 똑같이 BOS 에서 출발한다. 비교용이다.
 * ===================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#endif

/* =====================================================================
 * 명령줄 인자를 UTF-8 로 받기 (Windows 전용)
 *
 * [목적] 프롬프트에 한글이 들어오므로, 인자를 UTF-8 로 읽어야 한다.
 * [동작] Windows 가 main 에 주는 argv 는 **현재 코드 페이지(CP949)** 로 변환된
 *   것이라, UTF-8 로 넘긴 한글이 깨진다. 그래서 원본 명령줄을 유니코드로 다시
 *   받아(CommandLineToArgvW) UTF-8 로 직접 변환한다.
 * [이유] 이 프로그램에서 처음 필요해졌다. 3-c 까지는 인자가 숫자와 파일명뿐이었다.
 *   (실제로 이 변환 없이 돌렸다가 "어휘에 없는 토큰: ??/NNG" 로 실패했다)
 *   표준 C 에는 이 문제를 풀 방법이 없어 Windows API 를 쓰고 _WIN32 로 감쌌다.
 * ===================================================================== */
static char **utf8_argv(int argc, char **argv)
{
#ifdef _WIN32
    int wargc = 0;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv || wargc != argc) return argv;      /* 이상하면 원래 것을 쓴다 */

    char **out = (char **)malloc((size_t)argc * sizeof(char *));
    if (!out) { LocalFree(wargv); return argv; }
    for (int i = 0; i < argc; i++) {
        int need = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);
        out[i] = (char *)malloc((size_t)(need > 0 ? need : 1));
        if (!out[i]) { LocalFree(wargv); return argv; }
        if (need > 0) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, out[i], need, NULL, NULL);
        else out[i][0] = '\0';
    }
    LocalFree(wargv);
    return out;
#else
    (void)argc;
    return argv;
#endif
}

/* =====================================================================
 * [1] 설정과 전역
 * ===================================================================== */
#define N           6
#define CTX         (N - 1)

#define VOCAB_PATH  "step7-3a_vocab.txt"
#define TOKENS_PATH "step7-2b_tokens.txt"
#define OUTPUT_PREFIX "step7-6_prompt_result_"
#define MAX_RUN_NO    99
#define GEN_DEFAULT   15
#define MAX_GEN_TOK   200
#define MAX_PROMPT    64

#define HASH_BITS   22
#define HASH_SIZE   (1u << HASH_BITS)
#define HASH_MASK   (HASH_SIZE - 1u)

#define ID_BOS      0
#define ID_EOS      1

static char **g_id_to_tok = NULL;
static int    g_vocab_n   = 0;

static int       *g_seq = NULL;
static long long  g_seq_len = 0;
static long long *g_sent_start = NULL;
static long long *g_sent_len = NULL;
static long long  g_sents = 0;
static long long  g_tokens = 0;

/* =====================================================================
 * [2] 어휘표 읽기  (Phase 3-c 와 같은 코드)
 * ===================================================================== */
#define VHASH_BITS  15
#define VHASH_SIZE  (1u << VHASH_BITS)
#define VHASH_MASK  (VHASH_SIZE - 1u)

static int *g_vslot = NULL;

static unsigned int str_hash(const char *s)
{
    unsigned int h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

static void vocab_index(int id)
{
    unsigned int pos = str_hash(g_id_to_tok[id]) & VHASH_MASK;
    while (g_vslot[pos] >= 0) pos = (pos + 1u) & VHASH_MASK;
    g_vslot[pos] = id;
}

static int vocab_find(const char *s, size_t len)
{
    char buf[256];
    if (len >= sizeof(buf)) return -1;
    memcpy(buf, s, len); buf[len] = '\0';

    unsigned int pos = str_hash(buf) & VHASH_MASK;
    for (;;) {
        int id = g_vslot[pos];
        if (id < 0) return -1;
        if (strcmp(g_id_to_tok[id], buf) == 0) return id;
        pos = (pos + 1u) & VHASH_MASK;
    }
}

static int load_vocab(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    int cap = 4096;
    g_id_to_tok = (char **)malloc((size_t)cap * sizeof(char *));
    if (!g_id_to_tok) { fclose(fp); return 0; }

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        char *t1 = strchr(line, '\t');
        if (!t1) continue;
        char *t2 = strchr(t1 + 1, '\t');
        if (!t2) continue;

        *t1 = '\0'; *t2 = '\0';
        int id = atoi(line);
        const char *tok = t1 + 1;

        while (id >= cap) {
            cap *= 2;
            char **nv = (char **)realloc(g_id_to_tok, (size_t)cap * sizeof(char *));
            if (!nv) { fclose(fp); return 0; }
            g_id_to_tok = nv;
        }
        size_t tl = strlen(tok);
        char *copy = (char *)malloc(tl + 1);
        if (!copy) { fclose(fp); return 0; }
        memcpy(copy, tok, tl + 1);
        g_id_to_tok[id] = copy;
        if (id + 1 > g_vocab_n) g_vocab_n = id + 1;
    }
    fclose(fp);

    g_vslot = (int *)malloc(VHASH_SIZE * sizeof(int));
    if (!g_vslot) return 0;
    for (unsigned int i = 0; i < VHASH_SIZE; i++) g_vslot[i] = -1;
    for (int i = 0; i < g_vocab_n; i++) vocab_index(i);
    return 1;
}

/* =====================================================================
 * [3] 코퍼스를 번호열로 + 패딩  (Phase 3-c 와 같은 코드)
 * ===================================================================== */
static int build_corpus(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    long long cap = 1 << 21;
    g_seq = (int *)malloc((size_t)cap * sizeof(int));
    long long scap = 1 << 16;
    g_sent_start = (long long *)malloc((size_t)scap * sizeof(long long));
    g_sent_len   = (long long *)malloc((size_t)scap * sizeof(long long));
    if (!g_seq || !g_sent_start || !g_sent_len) { fclose(fp); return 0; }

    size_t lcap = 1 << 16;
    char *line = (char *)malloc(lcap);
    if (!line) { fclose(fp); return 0; }

    while (fgets(line, (int)lcap, fp)) {
        size_t ll = strlen(line);
        while (ll > 0 && line[ll - 1] != '\n' && !feof(fp)) {
            size_t ncap = lcap * 2;
            char *nl = (char *)realloc(line, ncap);
            if (!nl) { free(line); fclose(fp); return 0; }
            line = nl; lcap = ncap;
            if (!fgets(line + ll, (int)(lcap - ll), fp)) break;
            ll = strlen(line);
        }
        while (ll > 0 && (line[ll - 1] == '\n' || line[ll - 1] == '\r')) line[--ll] = '\0';
        if (ll == 0) continue;

        if (g_sents >= scap) {
            scap *= 2;
            long long *a = (long long *)realloc(g_sent_start, (size_t)scap * sizeof(long long));
            long long *b = (long long *)realloc(g_sent_len,   (size_t)scap * sizeof(long long));
            if (!a || !b) { free(line); fclose(fp); return 0; }
            g_sent_start = a; g_sent_len = b;
        }
        long long start = g_seq_len;

        long long need = g_seq_len + CTX + (long long)ll + 2;
        if (need > cap) {
            while (need > cap) cap *= 2;
            int *ns = (int *)realloc(g_seq, (size_t)cap * sizeof(int));
            if (!ns) { free(line); fclose(fp); return 0; }
            g_seq = ns;
        }

        for (int i = 0; i < CTX; i++) g_seq[g_seq_len++] = ID_BOS;

        char *p = line;
        for (;;) {
            char *sep = strstr(p, "<W>");
            size_t tlen = sep ? (size_t)(sep - p) : strlen(p);
            if (tlen > 0) {
                int id = vocab_find(p, tlen);
                if (id < 0) {
                    fprintf(stderr, "어휘표에 없는 토큰: %.*s\n", (int)tlen, p);
                    free(line); fclose(fp); return 0;
                }
                g_seq[g_seq_len++] = id;
                g_tokens++;
            }
            if (!sep) break;
            p = sep + 3;
        }
        g_seq[g_seq_len++] = ID_EOS;

        g_sent_start[g_sents] = start;
        g_sent_len[g_sents] = g_seq_len - start;
        g_sents++;
    }
    free(line);
    fclose(fp);
    return 1;
}

/* =====================================================================
 * [4] n그램 세기  (Phase 3-c 와 같은 코드)
 * ===================================================================== */
typedef struct { int next_id; long long count; } NextOpt;

typedef struct Entry {
    int            key[CTX];
    NextOpt       *opts;
    int            opt_count;
    int            opt_cap;
    long long      total;
    struct Entry  *chain;
} Entry;

static Entry **g_table = NULL;
static long long g_ctx_count = 0;
static long long g_gram_count = 0;
static long long g_total_grams = 0;

static unsigned int key_hash(const int *key)
{
    unsigned int h = 0;
    for (int i = 0; i < CTX; i++) h = h * 31u + (unsigned int)key[i];
    return h & HASH_MASK;
}

static int key_eq(const int *a, const int *b)
{
    for (int i = 0; i < CTX; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static void add_gram(const int *key, int next_id)
{
    unsigned int pos = key_hash(key);
    Entry *e = g_table[pos];
    while (e) {
        if (key_eq(e->key, key)) break;
        e = e->chain;
    }
    if (!e) {
        e = (Entry *)malloc(sizeof(Entry));
        if (!e) { fprintf(stderr, "메모리 부족 (Entry)\n"); exit(1); }
        memcpy(e->key, key, sizeof(int) * CTX);
        e->opts = NULL; e->opt_count = 0; e->opt_cap = 0; e->total = 0;
        e->chain = g_table[pos];
        g_table[pos] = e;
        g_ctx_count++;
    }

    for (int i = 0; i < e->opt_count; i++) {
        if (e->opts[i].next_id == next_id) {
            e->opts[i].count++;
            e->total++;
            g_total_grams++;
            return;
        }
    }
    if (e->opt_count >= e->opt_cap) {
        int ncap = e->opt_cap ? e->opt_cap * 2 : 2;
        NextOpt *no = (NextOpt *)realloc(e->opts, (size_t)ncap * sizeof(NextOpt));
        if (!no) { fprintf(stderr, "메모리 부족 (후보 목록)\n"); exit(1); }
        e->opts = no; e->opt_cap = ncap;
    }
    e->opts[e->opt_count].next_id = next_id;
    e->opts[e->opt_count].count = 1;
    e->opt_count++;
    e->total++;
    g_gram_count++;
    g_total_grams++;
}

static void count_all(void)
{
    for (long long s = 0; s < g_sents; s++) {
        long long start = g_sent_start[s];
        long long len = g_sent_len[s];
        for (long long i = start + CTX; i < start + len; i++)
            add_gram(&g_seq[i - CTX], g_seq[i]);
    }
}

/* =====================================================================
 * [5] 난수  (Phase 3-c 와 같은 코드)
 * ===================================================================== */
#define RAND30_RANGE (1ULL << 30)

static unsigned long long rand30(void)
{
    unsigned long long hi = (unsigned long long)rand();
    unsigned long long lo = (unsigned long long)rand();
    return (hi << 15) | lo;
}

static long long rand_below(long long n)
{
    unsigned long long limit;
    if (n <= 1) return 0;
    limit = RAND30_RANGE - (RAND30_RANGE % (unsigned long long)n);
    for (;;) {
        unsigned long long r = rand30();
        if (r < limit) return (long long)(r % (unsigned long long)n);
    }
}

static Entry *find_entry(const int *key)
{
    unsigned int pos = key_hash(key);
    for (Entry *e = g_table[pos]; e; e = e->chain)
        if (key_eq(e->key, key)) return e;
    return NULL;
}

static int cmp_opt(const void *a, const void *b)
{
    const NextOpt *x = (const NextOpt *)a, *y = (const NextOpt *)b;
    return (x->next_id < y->next_id) ? -1 : (x->next_id > y->next_id) ? 1 : 0;
}

static void sort_all_opts(void)
{
    for (unsigned int b = 0; b < HASH_SIZE; b++)
        for (Entry *e = g_table[b]; e; e = e->chain)
            qsort(e->opts, (size_t)e->opt_count, sizeof(NextOpt), cmp_opt);
}

static int pick_next(const Entry *e)
{
    long long r = rand_below(e->total);
    long long acc = 0;
    for (int i = 0; i < e->opt_count; i++) {
        acc += e->opts[i].count;
        if (acc > r) return e->opts[i].next_id;   /* 등호를 붙이지 않는다 */
    }
    return e->opts[e->opt_count - 1].next_id;
}

/* =====================================================================
 * [6] 프롬프트로 출발하는 생성
 *
 * [목적] 주어진 토큰들로 문장을 시작하고 그 뒤를 모델이 잇게 한다.
 * [동작] BOS 5개를 깐 뒤 프롬프트를 그 뒤에 이어 붙이고, **마지막 5개**를
 *   문맥으로 삼는다. 프롬프트 길이만큼 BOS 가 창 밖으로 밀려난다.
 *
 *     프롬프트 2개: BOS BOS BOS BOS BOS 돈 을   ->  문맥 = BOS BOS BOS 돈 을
 *     프롬프트 7개: BOS BOS BOS BOS BOS a b c d e f g  ->  문맥 = c d e f g
 *                   (BOS 가 하나도 남지 않는다 = 문장 첫머리라는 정보가 사라진다)
 *
 *   생성한 문장에는 **프롬프트도 포함**한다. 프롬프트는 문장의 일부지
 *   따로 붙이는 머리말이 아니기 때문이다.
 *
 * [이유] 이렇게 하면 "모델이 그 토큰들을 스스로 뽑았을 때" 와 완전히 같은 상태가 된다.
 *   프롬프트를 특별 취급하는 코드가 전혀 없고, 그래서 결과를 3-c 와 직접 비교할 수 있다.
 *
 *   첫 문맥에 후보가 없으면(blocked) **생성을 하지 않는다.** 억지로 아무 토큰이나
 *   고르면 "모델이 만들 수 있는 것만 나온다" 는 성질이 깨져 5단계 검증이 무의미해진다.
 *   막혔다는 사실 자체가 7단계(백오프)가 고쳐야 할 문제다.
 * ===================================================================== */
static void prompt_context(const int *prompt, int plen, int *ctx)
{
    int hist[CTX + MAX_PROMPT];
    int n = 0;
    for (int i = 0; i < CTX; i++) hist[n++] = ID_BOS;
    for (int i = 0; i < plen; i++) hist[n++] = prompt[i];
    memcpy(ctx, hist + (n - CTX), sizeof(int) * CTX);   /* 마지막 5개 */
}

static int generate_with_prompt(const int *prompt, int plen,
                                int *out, int max_len, int *hit_limit, int *blocked)
{
    int ctx[CTX];
    prompt_context(prompt, plen, ctx);

    int len = 0;
    *hit_limit = 0;
    *blocked = 0;
    for (int i = 0; i < plen; i++) out[len++] = prompt[i];   /* 프롬프트도 문장의 일부 */

    for (;;) {
        Entry *e = find_entry(ctx);
        if (!e) {
            /* 첫 걸음에서만 일어난다. 그 뒤로는 실재하는 전이만 따라가므로
               도달한 문맥에는 반드시 후보가 있다 */
            if (len == plen) *blocked = 1; else *hit_limit = 1;
            break;
        }
        int next = pick_next(e);
        if (next == ID_EOS) break;
        if (len >= max_len) { *hit_limit = 1; break; }

        out[len++] = next;
        for (int i = 0; i < CTX - 1; i++) ctx[i] = ctx[i + 1];
        ctx[CTX - 1] = next;
    }
    return len;
}

/* =====================================================================
 * [7] 프롬프트 진단
 *
 * [목적] 생성하기 전에 "이 프롬프트로 모델이 말을 할 수 있는가" 를 먼저 본다.
 * [동작] 네 가지를 구한다.
 *     - 첫 문맥과 그 안에 남은 BOS 수 (문장 첫머리임을 아는가)
 *     - 그 문맥의 후보 수 / 빈도 합    (0이면 막힌 것)
 *     - 원본에서 이 프롬프트로 **시작하는** 문장 수 (갈 수 있는 길의 수)
 * [이유] 결과가 이상할 때 원인이 "모델이 나쁘다" 인지 "프롬프트가 데이터에
 *   없다" 인지 구분해야 한다. 진단 없이 문장만 보면 그 둘을 섞어 본다.
 * ===================================================================== */
typedef struct {
    int       ctx[CTX];
    int       bos_left;     /* 문맥에 남은 BOS 수 */
    int       opt_count;    /* 후보 수. 0이면 막힘 */
    long long total;        /* 후보 빈도 합 */
    long long starts;       /* 원본에서 이 프롬프트로 시작하는 문장 수 */
} Diag;

static long long count_starts(const int *prompt, int plen)
{
    long long hits = 0;
    if (plen == 0) return g_sents;
    for (long long s = 0; s < g_sents; s++) {
        long long st = g_sent_start[s] + CTX;            /* 패딩 다음이 첫 토큰 */
        if (g_sent_len[s] - CTX - 1 < plen) continue;    /* 문장이 프롬프트보다 짧다 */
        int ok = 1;
        for (int i = 0; i < plen; i++)
            if (g_seq[st + i] != prompt[i]) { ok = 0; break; }
        hits += ok;
    }
    return hits;
}

static Diag diagnose(const int *prompt, int plen)
{
    Diag d;
    prompt_context(prompt, plen, d.ctx);
    d.bos_left = 0;
    for (int i = 0; i < CTX; i++) if (d.ctx[i] == ID_BOS) d.bos_left++;

    Entry *e = find_entry(d.ctx);
    d.opt_count = e ? e->opt_count : 0;
    d.total = e ? e->total : 0;
    d.starts = count_starts(prompt, plen);
    return d;
}

static void fprint_tokens(FILE *fp, const int *ids, int n)
{
    for (int i = 0; i < n; i++) {
        if (i) fputc(' ', fp);
        fputs(g_id_to_tok[ids[i]], fp);
    }
}

/* =====================================================================
 * [8] 결과 파일 자동 번호  (Phase 3-c 와 같은 규칙)
 * ===================================================================== */
static int next_output_path(char *buf, size_t size)
{
    int last = 0;
    for (int n = 1; n <= MAX_RUN_NO; n++) {
        snprintf(buf, size, OUTPUT_PREFIX "%02d.txt", n);
        FILE *fp = fopen(buf, "rb");
        if (fp) { fclose(fp); last = n; }
    }
    if (last >= MAX_RUN_NO) {
        fprintf(stderr, "[오류] 결과 파일 번호가 %d까지 찼습니다\n", MAX_RUN_NO);
        return 0;
    }
    snprintf(buf, size, OUTPUT_PREFIX "%02d.txt", last + 1);
    return 1;
}

/* =====================================================================
 * [9] 결과 파일 쓰기
 *
 * [목적] 진단과 생성 문장을 남긴다. 형식은 3-c 와 같게 맞춘다.
 * [이유] 형식을 맞춰야 4단계(역변환)와 5단계(원본 비교) 프로그램을
 *   **고치지 않고 그대로** 이 파일에 쓸 수 있다.
 * ===================================================================== */
static int write_result(const char *path, const char *prompt_text,
                        int plen, Diag d,
                        int count, unsigned int seed,
                        int **sents, int *lens, int *limits,
                        int *blocks, double build_ms, double gen_ms)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;

    long long tok_sum = 0;
    int limited = 0, blocked = 0;
    for (int i = 0; i < count; i++) {
        tok_sum += lens[i]; limited += limits[i]; blocked += blocks[i];
    }

    fprintf(fp, "=== STEP 7 6단계: 프롬프트로 시작하는 %d그램 문장 생성 ===\n", N);
    fprintf(fp, "#\n");
    fprintf(fp, "# 3-c 는 BOS 5개에서 출발했다(= 아무 말이나). 여기서는 출발 문맥을\n");
    fprintf(fp, "# 우리가 채운다(= 이 말로 시작해서 이어 봐). 프롬프트도 문장의 일부다.\n");
    fprintf(fp, "#\n");
    fprintf(fp, "# prompt\t%s\n", prompt_text);
    fprintf(fp, "# prompt_tokens\t%d\n", plen);
    fprintf(fp, "# context\t");
    fprint_tokens(fp, d.ctx, CTX);
    fprintf(fp, "\n");
    fprintf(fp, "# bos_left\t%d\t(문맥에 남은 BOS. 0이면 문장 첫머리임을 모른다)\n", d.bos_left);
    fprintf(fp, "# candidates\t%d\t(이 문맥의 다음 토큰 후보 수. 0이면 막힘)\n", d.opt_count);
    fprintf(fp, "# context_total\t%lld\n", d.total);
    fprintf(fp, "# corpus_starts\t%lld\t(원본에서 이 프롬프트로 시작하는 문장 수)\n", d.starts);
    fprintf(fp, "#\n");
    fprintf(fp, "# seed\t%u\n", seed);
    fprintf(fp, "# count\t%d\n", count);
    fprintf(fp, "# n\t%d\n", N);
    fprintf(fp, "# vocab\t%d\n", g_vocab_n);
    fprintf(fp, "# sentences\t%lld\n", g_sents);
    fprintf(fp, "# tokens\t%lld\n", g_tokens);
    fprintf(fp, "# total_grams\t%lld\n", g_total_grams);
    fprintf(fp, "# contexts\t%lld\n", g_ctx_count);
    fprintf(fp, "# build_ms\t%.3f\n", build_ms);
    fprintf(fp, "# gen_ms\t%.3f\n", gen_ms);
    fprintf(fp, "# avg_tokens\t%.2f\n", (double)tok_sum / (double)count);
    fprintf(fp, "# hit_limit\t%d\n", limited);
    fprintf(fp, "# blocked\t%d\t(첫 문맥에 후보가 없어 생성하지 못한 횟수)\n", blocked);
    fprintf(fp, "#\n");

    if (d.opt_count == 0) {
        fprintf(fp, "## 막혔다 - 이 프롬프트로는 생성할 수 없다\n");
        fprintf(fp, "#\n");
        fprintf(fp, "# 위 context 가 학습 데이터(%lld문장)에 한 번도 나오지 않는다.\n", g_sents);
        fprintf(fp, "# 모델이 보는 것은 앞 %d개 토큰뿐이고, 그 %d개 조합을 본 적이 없으면\n", CTX, CTX);
        fprintf(fp, "# 다음에 무엇이 올지 알 길이 없다. 사람에게는 멀쩡한 한국어여도 그렇다.\n");
        fprintf(fp, "#\n");
        fprintf(fp, "# 이것이 7단계(백오프)가 고치려는 문제다. 백오프는 후보가 없거나 부족하면\n");
        fprintf(fp, "# n 을 한 단계씩 낮춰 더 짧은 문맥으로 이어 간다.\n");
        fprintf(fp, "#\n");
        fclose(fp);
        return 1;
    }

    for (int i = 0; i < count; i++) {
        fprintf(fp, "%d: ", i + 1);
        for (int j = 0; j < lens[i]; j++) {
            if (j) fprintf(fp, "<W>");
            fprintf(fp, "%s", g_id_to_tok[sents[i][j]]);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
    return 1;
}

/* 진단만 하는 모드의 결과 파일 */
static int write_probe(const char *path, int n, char **texts, int **prompts,
                       int *plens, Diag *ds)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;

    fprintf(fp, "=== STEP 7 6단계: 프롬프트 진단 (생성하지 않음) ===\n");
    fprintf(fp, "#\n");
    fprintf(fp, "# 프롬프트마다 '모델이 이 말로 시작할 수 있는가' 만 본다.\n");
    fprintf(fp, "# n=%d 이므로 문맥은 앞 %d개 토큰이다. 프롬프트가 %d개를 넘으면\n", N, CTX, CTX);
    fprintf(fp, "# BOS 가 전부 밀려나 모델은 문장 첫머리라는 사실을 잊는다.\n");
    fprintf(fp, "#\n");
    fprintf(fp, "# bos_left : 문맥에 남은 BOS 수\n");
    fprintf(fp, "# cand     : 그 문맥의 다음 토큰 후보 수. **0이면 막힘**\n");
    fprintf(fp, "# starts   : 원본에서 이 프롬프트로 시작하는 문장 수\n");
    fprintf(fp, "#\n");
    fprintf(fp, "# prompt\ttokens\tbos_left\tcand\tstarts\tverdict\n");
    for (int i = 0; i < n; i++) {
        fprintf(fp, "%s\t%d\t%d\t%d\t%lld\t%s\n",
                texts[i], plens[i], ds[i].bos_left, ds[i].opt_count, ds[i].starts,
                ds[i].opt_count ? "생성 가능" : "막힘");
    }
    fprintf(fp, "\n## 문맥 상세\n#\n");
    for (int i = 0; i < n; i++) {
        fprintf(fp, "[%s]\n", texts[i]);
        fprintf(fp, "  프롬프트 토큰: ");
        fprint_tokens(fp, prompts[i], plens[i]);
        fprintf(fp, "\n  첫 문맥      : ");
        fprint_tokens(fp, ds[i].ctx, CTX);
        fprintf(fp, "\n  후보         : %d개 (빈도 합 %lld)\n\n", ds[i].opt_count, ds[i].total);
    }
    fclose(fp);
    return 1;
}

/* 프롬프트 문자열("돈/NNG<W>을/JKO") -> 번호 배열. 실패하면 -1 */
static int parse_prompt(const char *text, int *out)
{
    int n = 0;
    const char *p = text;
    for (;;) {
        const char *sep = strstr(p, "<W>");
        size_t tlen = sep ? (size_t)(sep - p) : strlen(p);
        if (tlen > 0) {
            if (n >= MAX_PROMPT) { fprintf(stderr, "프롬프트가 %d토큰을 넘습니다\n", MAX_PROMPT); return -1; }
            int id = vocab_find(p, tlen);
            if (id < 0) {
                fprintf(stderr, "어휘에 없는 토큰입니다: %.*s\n", (int)tlen, p);
                fprintf(stderr, "  (step7-3a_vocab.txt 에 있는 토큰만 쓸 수 있습니다)\n");
                return -1;
            }
            out[n++] = id;
        }
        if (!sep) break;
        p = sep + 3;
    }
    return n;
}

/* =====================================================================
 * [10] main
 * ===================================================================== */
static int prepare(void)
{
    if (!load_vocab(VOCAB_PATH)) {
        fprintf(stderr, "어휘표를 읽을 수 없습니다: %s\n", VOCAB_PATH);
        return 0;
    }
    if (!build_corpus(TOKENS_PATH)) {
        fprintf(stderr, "토큰 파일을 읽을 수 없습니다: %s\n", TOKENS_PATH);
        return 0;
    }
    g_table = (Entry **)calloc(HASH_SIZE, sizeof(Entry *));
    if (!g_table) { fprintf(stderr, "메모리 부족 (해시 테이블)\n"); return 0; }
    count_all();
    sort_all_opts();
    return 1;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    argv = utf8_argv(argc, argv);       /* 프롬프트의 한글을 UTF-8 로 받는다 */

    if (argc < 2) {
        fprintf(stderr, "사용법: %s \"<프롬프트>\" [개수] [seed] [결과파일]\n", argv[0]);
        fprintf(stderr, "        %s --probe \"<프롬프트1>\" \"<프롬프트2>\" ...\n", argv[0]);
        fprintf(stderr, "프롬프트는 토큰화된 문자열입니다: \"돈/NNG<W>을/JKO\"\n");
        fprintf(stderr, "  python step7-6_verify.py --tokenize \"돈을\" 로 얻을 수 있습니다\n");
        return 1;
    }

    clock_t t0 = clock();
    if (!prepare()) return 1;
    double build_ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;

    char path[512];

    /* --- 진단 모드 --- */
    if (strcmp(argv[1], "--probe") == 0) {
        int n = argc - 2;
        if (n <= 0) { fprintf(stderr, "진단할 프롬프트를 하나 이상 주세요\n"); return 1; }
        int **prompts = (int **)malloc((size_t)n * sizeof(int *));
        int *plens = (int *)malloc((size_t)n * sizeof(int));
        Diag *ds = (Diag *)malloc((size_t)n * sizeof(Diag));
        if (!prompts || !plens || !ds) return 1;

        for (int i = 0; i < n; i++) {
            prompts[i] = (int *)malloc(MAX_PROMPT * sizeof(int));
            if (!prompts[i]) return 1;
            plens[i] = parse_prompt(argv[i + 2], prompts[i]);
            if (plens[i] < 0) return 1;
            ds[i] = diagnose(prompts[i], plens[i]);
            printf("%-34s 토큰 %2d  BOS %d  후보 %4d  시작문장 %4lld  %s\n",
                   argv[i + 2], plens[i], ds[i].bos_left, ds[i].opt_count,
                   ds[i].starts, ds[i].opt_count ? "생성 가능" : "막힘");
        }
        if (!next_output_path(path, sizeof(path))) return 1;
        if (!write_probe(path, n, argv + 2, prompts, plens, ds)) {
            fprintf(stderr, "결과 파일을 쓸 수 없습니다: %s\n", path);
            return 1;
        }
        printf("저장: %s\n", path);
        return 0;
    }

    /* --- 생성 모드 --- */
    int prompt[MAX_PROMPT];
    int plen = parse_prompt(argv[1], prompt);
    if (plen < 0) return 1;

    int count = (argc > 2) ? atoi(argv[2]) : GEN_DEFAULT;
    if (count <= 0) count = GEN_DEFAULT;
    unsigned int seed = (argc > 3) ? (unsigned int)strtoul(argv[3], NULL, 10)
                                   : (unsigned int)time(NULL);

    Diag d = diagnose(prompt, plen);
    printf("프롬프트 %d토큰 / 문맥의 BOS %d개 / 후보 %d개 / 원본 시작 문장 %lld개\n",
           plen, d.bos_left, d.opt_count, d.starts);

    srand(seed);

    int **sents = (int **)malloc((size_t)count * sizeof(int *));
    int *lens = (int *)malloc((size_t)count * sizeof(int));
    int *limits = (int *)malloc((size_t)count * sizeof(int));
    int *blocks = (int *)malloc((size_t)count * sizeof(int));
    if (!sents || !lens || !limits || !blocks) { fprintf(stderr, "메모리 부족\n"); return 1; }

    t0 = clock();
    for (int i = 0; i < count; i++) {
        sents[i] = (int *)malloc((size_t)MAX_GEN_TOK * sizeof(int));
        if (!sents[i]) { fprintf(stderr, "메모리 부족\n"); return 1; }
        lens[i] = generate_with_prompt(prompt, plen, sents[i], MAX_GEN_TOK,
                                       &limits[i], &blocks[i]);
    }
    double gen_ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;

    if (argc > 4) snprintf(path, sizeof(path), "%s", argv[4]);
    else if (!next_output_path(path, sizeof(path))) return 1;

    if (!write_result(path, argv[1], plen, d, count, seed,
                      sents, lens, limits, blocks, build_ms, gen_ms)) {
        fprintf(stderr, "결과 파일을 쓸 수 없습니다: %s\n", path);
        return 1;
    }

    if (d.opt_count == 0) {
        printf("막혔습니다. 이 문맥은 학습 데이터에 없습니다 (7단계 백오프가 필요한 지점)\n");
        printf("저장: %s\n", path);
        return 2;                       /* 성공도 실패도 아닌 '막힘'을 따로 알린다 */
    }

    long long tok_sum = 0;
    for (int i = 0; i < count; i++) tok_sum += lens[i];
    printf("seed %u / 문장 %d개 / 평균 %.2f토큰\n", seed, count, (double)tok_sum / count);
    printf("세기 %.1fms / 생성 %.1fms\n", build_ms, gen_ms);
    printf("저장: %s\n", path);

    for (int i = 0; i < count; i++) free(sents[i]);
    free(sents); free(lens); free(limits); free(blocks);
    return 0;
}
