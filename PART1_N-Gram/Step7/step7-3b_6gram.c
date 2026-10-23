/* =====================================================================
 * [STEP 7 / Phase 3-b] 토큰 단위 6그램 세기
 *
 * 하는 일
 *   step7-3a_vocab.txt (번호표) 와 step7-2b_tokens.txt (토큰 파일) 를 읽어
 *   "앞 5개 토큰 다음에 이 토큰이 몇 번 나왔다" 를 전부 센다.
 *   문장 생성은 아직 하지 않는다 (Phase 3-c).
 *
 * Step4~6 과 달라지는 것: 자료구조
 *   Step4~6 은 (문맥, 다음토큰) 쌍을 해시에 넣고, 생성할 때 후보를 0번부터
 *   V-1번까지 **전부 훑으며** "이 문맥 다음에 이 토큰이 나왔나" 를 물었다.
 *   Step7 은 **문맥 하나가 자기 후보 목록을 들고 있는** 구조로 바꾼다.
 *
 *   바꾸는 이유는 V 가 커져서가 아니라 **백오프(Phase 7) 때문**이다.
 *   백오프는 "후보가 부족하면 n 을 한 단계 낮춘다" 이므로 **후보가 몇 개인지**
 *   바로 알 수 있어야 한다. 앞 방식으로는 그걸 알려고 매번 16,920번을 훑어야 한다.
 *   (강의 참고 코드 step08_sentence.c 도 같은 구조다)
 *
 * 키는 정수 배열 그대로 쓴다 (int key[5]).
 *
 * 구역 목차
 *   [1]  43줄 | 설정과 전역
 *   [2]  96줄 | 어휘표 읽기
 *   [3] 171줄 | 코퍼스를 번호열로
 *   [4] 247줄 | n그램 세기 (문맥 -> 후보 목록)
 *   [5] 349줄 | 행 모으기와 정렬
 *   [6] 404줄 | 빈도표 출력
 *   [7] 462줄 | 요약 보고서
 *   [8] 520줄 | main
 *
 * 사용법
 *   step7-3b_6gram.exe [빈도표] [보고서]
 *   생략하면 step7-3b_6gram_table.txt / step7-3b_6gram_result.txt
 *
 * 난수를 쓰지 않으므로(세기만 한다) 출력 파일에 실행 번호를 붙이지 않는다.
 * ===================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* =====================================================================
 * [1] 설정과 전역
 *
 * [목적] n 과 파일 경로, 해시 크기를 한곳에 모은다.
 * [동작] N=6 이므로 문맥은 앞 5개 토큰이다.
 * [이유] 해시 칸을 2^22 로 잡은 것은 서로 다른 문맥이 100만 개를 넘을 수 있기
 *   때문이다. 전체 6그램이 152만 개이므로 문맥도 그 이하다.
 *   체이닝(같은 칸을 연결 리스트로 잇기)을 쓰므로 칸보다 항목이 많아도 동작은 하지만,
 *   한 칸에 여러 개가 매달릴수록 찾는 데 비교가 늘어난다.
 * ===================================================================== */
#define N           6                   /* n그램의 n */
#define CTX         (N - 1)             /* 문맥 길이 = 5 */

#define VOCAB_PATH  "step7-3a_vocab.txt"
#define TOKENS_PATH "step7-2b_tokens.txt"
#define TABLE_DEF   "step7-3b_6gram_table.txt"
#define REPORT_DEF  "step7-3b_6gram_result.txt"

#define HASH_BITS   22
#define HASH_SIZE   (1u << HASH_BITS)
#define HASH_MASK   (HASH_SIZE - 1u)

#define ID_BOS      0
#define ID_EOS      1

/* --- 어휘 --- */
static char **g_id_to_tok = NULL;       /* 번호 -> 토큰 문자열 */
static int    g_vocab_n   = 0;          /* 어휘 수 (BOS/EOS 포함) */

/* --- 코퍼스 (전체 문장을 번호열 하나로 이어 붙이고, 문장 경계를 따로 기록) --- */
static int       *g_seq = NULL;         /* 번호열 (패딩 포함) */
static long long  g_seq_len = 0;
static long long *g_sent_start = NULL;  /* 문장 i 가 시작하는 자리 */
static long long *g_sent_len = NULL;    /* 문장 i 의 길이 (패딩 포함) */
static long long  g_sents = 0;
static long long  g_tokens = 0;         /* 패딩 제외한 실제 토큰 수 */

/* =====================================================================
 * [2] 어휘표 읽기
 *
 * [목적] Phase 3-a 가 정한 번호를 그대로 가져온다.
 * [동작] `번호 <탭> 토큰 <탭> 빈도` 형식을 읽어 번호 -> 토큰 배열을 만들고,
 *   토큰 -> 번호는 해시 테이블로 만든다.
 * [이유] 번호를 여기서 다시 매기지 않는다. 다시 매기면 Phase 3-a 의 어휘표와
 *   어긋날 수 있고, 그러면 두 파일을 같이 쓰는 뒤 단계가 조용히 틀린다.
 *   **번호는 한 곳에서만 정한다.**
 * ===================================================================== */
#define VHASH_BITS  15
#define VHASH_SIZE  (1u << VHASH_BITS)
#define VHASH_MASK  (VHASH_SIZE - 1u)

static int *g_vslot = NULL;             /* 칸 -> 번호. 빈 칸은 -1 */

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
        if (id < 0) return -1;                       /* 빈 칸 = 없는 토큰 */
        if (strcmp(g_id_to_tok[id], buf) == 0) return id;   /* 반드시 문자열로 확인 */
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
 * [3] 코퍼스를 번호열로 + 패딩
 *
 * [목적] 문장을 토큰 번호의 열로 바꾸고, 앞뒤에 BOS/EOS 를 붙인다.
 * [동작] 문장 `A B C` 는 N=6 일 때 이렇게 된다.
 *     BOS BOS BOS BOS BOS  A B C  EOS
 *     <------ 5개 ------>         <-1개->
 * [이유] BOS 가 n-1개 필요한 이유: 문맥이 앞 5개인데 첫 토큰을 뽑을 때는 앞이 0개다.
 *   채울 것이 없으면 생성을 시작할 자리가 없다.
 *   EOS 가 필요한 이유: 끝낼 시점을 모델이 정해야 한다. 없으면 길이 상한으로
 *   강제로 자르는 수밖에 없다.
 *   Step6(글자)에서 쓴 것과 똑같다. 단위만 글자에서 토큰으로 바뀌었다.
 *
 *   검산: 문장 길이 L 이면 n그램이 L+1 개 나온다 (BOS 패딩 뒤부터 EOS 까지).
 *   따라서 전체 n그램 = 토큰 총수 + 문장 수 이고, 이 값은 **n 과 무관하다**.
 *   패딩 개수를 틀리면 이 검산이 깨지므로 검증 항목으로 쓴다.
 * ===================================================================== */
static int build_corpus(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    long long cap = 1 << 20;
    g_seq = (int *)malloc((size_t)cap * sizeof(int));
    long long scap = 1 << 16;
    g_sent_start = (long long *)malloc((size_t)scap * sizeof(long long));
    g_sent_len   = (long long *)malloc((size_t)scap * sizeof(long long));
    if (!g_seq || !g_sent_start || !g_sent_len) { fclose(fp); return 0; }

    /* 한 줄이 길 수 있으므로 넉넉히 잡는다 (최장 문장 328글자 -> 토큰 수백 개) */
    size_t lcap = 1 << 16;
    char *line = (char *)malloc(lcap);
    if (!line) { fclose(fp); return 0; }

    while (fgets(line, (int)lcap, fp)) {
        size_t ll = strlen(line);
        /* 줄이 잘렸으면 이어 읽는다 */
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

        /* 공간 확보: BOS 5개 + 토큰들 + EOS 1개. 토큰 수는 길이로 넉넉히 잡는다 */
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
 * [4] n그램 세기 - 문맥 하나가 후보 목록을 들고 있는 구조
 *
 * [목적] "앞 5개 토큰(문맥) 다음에 어떤 토큰이 몇 번 나왔나" 를 모은다.
 * [동작] 문맥 5개를 키로 해시에서 자리를 찾고, 그 자리의 후보 목록에서
 *   다음 토큰을 찾아 빈도를 올린다. 없으면 후보를 추가한다.
 * [이유] 후보 목록을 문맥마다 따로 들고 있으면 `opt_count` 가 곧 "후보 수"다.
 *   백오프(Phase 7)가 이 값을 본다. 또 생성할 때 어휘 전체를 훑지 않아도 된다.
 *
 *   후보 목록을 **배열**로 둔 이유: 대부분의 문맥은 후보가 1~2개뿐이라
 *   연결 리스트를 쓰면 포인터가 데이터보다 커진다. 배열을 2배씩 늘리면
 *   추가 비용이 평균적으로 상수에 가깝다.
 * ===================================================================== */
typedef struct { int next_id; long long count; } NextOpt;

typedef struct Entry {
    int            key[CTX];        /* 문맥 */
    NextOpt       *opts;
    int            opt_count;
    int            opt_cap;
    long long      total;           /* 후보 빈도 합 */
    struct Entry  *chain;           /* 같은 칸에 걸린 다음 항목 */
} Entry;

static Entry **g_table = NULL;
static long long g_ctx_count = 0;       /* 서로 다른 문맥 수 */
static long long g_gram_count = 0;      /* 서로 다른 n그램 수 */
static long long g_total_grams = 0;     /* 전체 n그램 수 */

static unsigned int key_hash(const int *key)
{
    /* 정수 배열용 해시. 31을 곱해 가며 섞는다 (강의 참고 코드와 같은 방식) */
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
        /* 창을 한 칸씩 밀며 (앞 5개, 다음 1개) 를 센다 */
        for (long long i = start + CTX; i < start + len; i++)
            add_gram(&g_seq[i - CTX], g_seq[i]);
    }
}

/* =====================================================================
 * [5] 행 모으기와 정렬
 *
 * [목적] 빈도표를 **항상 같은 순서**로 쓰기 위해 모든 행을 모아 정렬한다.
 * [동작] (문맥 5개, 다음 1개) 를 번호 순서로 비교해 오름차순 정렬한다.
 * [이유] 해시 칸을 도는 순서로 쓰면 해시 함수에 따라 순서가 달라진다.
 *   그러면 Python 검증기가 같은 순서를 만들려고 **C 의 해시 함수까지 흉내 내야 한다.**
 *   번호 순 정렬은 해시와 무관하므로 양쪽이 독립적으로 같은 결과를 만든다.
 *   (Phase 3-a 에서 번호를 사전순으로 매긴 것과 같은 이유다)
 * ===================================================================== */
typedef struct { int ids[N]; long long count; long long ctx_total; } Row;

static int cmp_row(const void *a, const void *b)
{
    const Row *x = (const Row *)a, *y = (const Row *)b;
    for (int i = 0; i < N; i++)
        if (x->ids[i] != y->ids[i]) return (x->ids[i] < y->ids[i]) ? -1 : 1;
    return 0;
}

static Row *collect_rows(long long *out_n)
{
    Row *rows = (Row *)malloc((size_t)g_gram_count * sizeof(Row));
    if (!rows) { fprintf(stderr, "메모리 부족 (행 배열 %lld개)\n", g_gram_count); exit(1); }

    long long k = 0;
    for (unsigned int b = 0; b < HASH_SIZE; b++) {
        for (Entry *e = g_table[b]; e; e = e->chain) {
            for (int i = 0; i < e->opt_count; i++) {
                memcpy(rows[k].ids, e->key, sizeof(int) * CTX);
                rows[k].ids[CTX] = e->opts[i].next_id;
                rows[k].count = e->opts[i].count;
                rows[k].ctx_total = e->total;
                k++;
            }
        }
    }
    qsort(rows, (size_t)k, sizeof(Row), cmp_row);
    *out_n = k;
    return rows;
}

/* =====================================================================
 * [6] 빈도표 출력
 *
 * [목적] 사람이 읽을 수 있고 Python 검증기가 그대로 비교할 수 있는 표를 남긴다.
 * [동작] 탭으로 나눈 `문맥1 ... 문맥5 / 다음 / 빈도 / 퍼센트`.
 *   퍼센트는 **그 문맥 안에서의 비율**이다 (count / 문맥의 후보 빈도 합).
 * [이유] 번호가 아니라 토큰 문자열을 적는 것은 사람이 읽기 위해서다.
 *   토큰에 탭이나 줄바꿈이 들어갈 수 없으므로(Phase 1 정제가 막았다) 탭 구분이 안전하다.
 *   Step6 의 빈도표와 같은 형식이다.
 * ===================================================================== */
static int write_table(const char *path, Row *rows, long long n)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;

    /* 출력 버퍼를 크게 잡는다. 파일이 100MB 를 넘을 수 있어 기본 버퍼로는 느리다 */
    static char iobuf[1 << 20];
    setvbuf(fp, iobuf, _IOFBF, sizeof(iobuf));

    fprintf(fp, "# 토큰 단위 %d그램 빈도표\n", N);
    fprintf(fp, "# vocab_file\t%s\n", VOCAB_PATH);
    fprintf(fp, "# tokens_file\t%s\n", TOKENS_PATH);
    fprintf(fp, "# sentences\t%lld\n", g_sents);
    fprintf(fp, "# tokens\t%lld\n", g_tokens);
    fprintf(fp, "# vocab\t%d\n", g_vocab_n);
    fprintf(fp, "# total_%dgrams\t%lld\n", N, g_total_grams);
    fprintf(fp, "# unique_%dgrams\t%lld\n", N, g_gram_count);
    fprintf(fp, "# contexts\t%lld\n", g_ctx_count);
    fprintf(fp, "#\n");
    fprintf(fp, "# 퍼센트는 그 문맥 안에서의 비율이다 (count / 문맥의 후보 빈도 합).\n");
    fprintf(fp, "# 행 순서는 토큰 번호 오름차순이다 (해시 순서가 아니다).\n");
    fprintf(fp, "#\n");
    for (int i = 1; i <= CTX; i++) fprintf(fp, "# prev%d\t", i);
    fprintf(fp, "next\tcount\tpercent\n");

    for (long long i = 0; i < n; i++) {
        for (int j = 0; j < N; j++)
            fprintf(fp, "%s\t", g_id_to_tok[rows[i].ids[j]]);
        fprintf(fp, "%lld\t%.6f\n",
                rows[i].count, 100.0 * (double)rows[i].count / (double)rows[i].ctx_total);
    }
    fclose(fp);
    return 1;
}

/* =====================================================================
 * [7] 요약 보고서
 * ===================================================================== */
static int write_report(const char *path, double build_ms, double write_ms,
                        long long rows_n)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;

    long long once = 0, max_opt = 0;
    for (unsigned int b = 0; b < HASH_SIZE; b++)
        for (Entry *e = g_table[b]; e; e = e->chain) {
            if (e->opt_count > max_opt) max_opt = e->opt_count;
            for (int i = 0; i < e->opt_count; i++)
                if (e->opts[i].count == 1) once++;
        }

    fprintf(fp, "=== STEP 7 Phase 3-b: 토큰 단위 %d그램 세기 결과 ===\n\n", N);
    fprintf(fp, "--- 이 파일이 무엇인가 ---\n");
    fprintf(fp, "토큰 번호열에서 \"앞 %d개 토큰 다음에 이 토큰이 몇 번 나왔다\" 를 전부 센 결과다.\n", CTX);
    fprintf(fp, "문장 생성은 아직 하지 않는다 (Phase 3-c).\n");
    fprintf(fp, "빈도표는 %s 에 있다 (크기가 커서 git 에는 올리지 않는다).\n\n", TABLE_DEF);

    fprintf(fp, "--- 규모 ---\n");
    fprintf(fp, "문장 수            : %lld\n", g_sents);
    fprintf(fp, "토큰 수(패딩 제외)  : %lld\n", g_tokens);
    fprintf(fp, "어휘               : %d (BOS/EOS 포함)\n", g_vocab_n);
    fprintf(fp, "전체 %dgram        : %lld\n", N, g_total_grams);
    fprintf(fp, "  검산: 토큰 %lld + 문장 %lld = %lld %s\n",
            g_tokens, g_sents, g_tokens + g_sents,
            (g_total_grams == g_tokens + g_sents) ? "(일치)" : "(불일치!)");
    fprintf(fp, "서로 다른 %dgram   : %lld\n", N, g_gram_count);
    fprintf(fp, "서로 다른 문맥      : %lld\n", g_ctx_count);
    fprintf(fp, "  문맥당 평균 후보  : %.2f\n",
            (double)g_gram_count / (double)g_ctx_count);
    fprintf(fp, "  문맥당 최대 후보  : %lld\n", max_opt);
    fprintf(fp, "  빈도 1인 %dgram  : %lld (%.1f%%)\n",
            N, once, 100.0 * (double)once / (double)g_gram_count);
    fprintf(fp, "\n");
    fprintf(fp, "--- 시간 ---\n");
    fprintf(fp, "세기      : %.1f ms\n", build_ms);
    fprintf(fp, "표 쓰기   : %.1f ms (%lld행)\n", write_ms, rows_n);
    fprintf(fp, "\n");
    fprintf(fp, "--- 해시 ---\n");
    fprintf(fp, "칸 수    : %u (2^%d)\n", HASH_SIZE, HASH_BITS);
    fprintf(fp, "문맥 수  : %lld  -> 칸당 평균 %.2f개\n",
            g_ctx_count, (double)g_ctx_count / (double)HASH_SIZE);
    fprintf(fp, "  체이닝이라 칸보다 항목이 많아도 동작하지만, 한 칸에 여러 개가\n");
    fprintf(fp, "  매달릴수록 찾을 때 비교가 늘어난다.\n");

    fclose(fp);
    return 1;
}

/* =====================================================================
 * [8] main
 * ===================================================================== */
int main(int argc, char **argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    const char *table_path  = (argc > 1) ? argv[1] : TABLE_DEF;
    const char *report_path = (argc > 2) ? argv[2] : REPORT_DEF;

    if (!load_vocab(VOCAB_PATH)) {
        fprintf(stderr, "어휘표를 읽을 수 없습니다: %s\n", VOCAB_PATH);
        fprintf(stderr, "먼저 step7-3a_tokenid.exe 를 실행하세요.\n");
        return 1;
    }
    printf("어휘 %d종\n", g_vocab_n);

    if (!build_corpus(TOKENS_PATH)) {
        fprintf(stderr, "토큰 파일을 읽을 수 없습니다: %s\n", TOKENS_PATH);
        return 1;
    }
    printf("문장 %lld / 토큰 %lld\n", g_sents, g_tokens);

    g_table = (Entry **)calloc(HASH_SIZE, sizeof(Entry *));
    if (!g_table) { fprintf(stderr, "메모리 부족 (해시 테이블)\n"); return 1; }

    clock_t t0 = clock();
    count_all();
    double build_ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;

    long long rows_n = 0;
    Row *rows = collect_rows(&rows_n);

    t0 = clock();
    if (!write_table(table_path, rows, rows_n)) {
        fprintf(stderr, "빈도표를 쓸 수 없습니다: %s\n", table_path);
        return 1;
    }
    double write_ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
    free(rows);

    if (!write_report(report_path, build_ms, write_ms, rows_n)) {
        fprintf(stderr, "보고서를 쓸 수 없습니다: %s\n", report_path);
        return 1;
    }

    printf("전체 %dgram %lld / 서로 다른 %dgram %lld / 문맥 %lld\n",
           N, g_total_grams, N, g_gram_count, g_ctx_count);
    printf("세기 %.1fms / 표 쓰기 %.1fms\n", build_ms, write_ms);
    printf("저장: %s, %s\n", table_path, report_path);
    return 0;
}
