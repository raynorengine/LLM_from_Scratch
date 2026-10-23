/* =====================================================================
 * [STEP 7 / 5단계] 원본 비교 - 생성한 문장은 새 문장인가, 베낀 것인가
 *
 * 하는 일
 *   3-c 가 생성한 토큰 열을 원본(step7-2b_tokens.txt)과 대조해 세 가지를 잰다.
 *     1) 통째 일치율      문장 전체가 원본 문장과 똑같은가
 *     2) 최장 복사 구간    원본과 연속으로 겹친 최대 토큰 수   <- 이게 진짜 지표
 *     3) 중복 생성         같은 문장을 두 번 뱉었는가
 *
 * 왜 2) 가 필요한가
 *   통째 일치만 재면 속는다. 아래는 통째 일치로는 0% 지만 거의 베낀 문장이다.
 *     원본: 돈/NNG 을/JKO 2/SN 배/NNB 로/JKB 걸/VV 지/EC 않/VX ...
 *     생성: 돈/NNG 을/JKO 2/SN 배/NNB 로/JKB 걸/VV 고/EC ...
 *           +----------- 6토큰 복사 -----------+
 *   3-b 에서 문맥당 평균 후보가 1.25개로 나왔다. 후보가 하나뿐인 문맥에서는
 *   원문 복사 말고 할 수 있는 게 없으므로, 이런 "부분 복사" 가 많을 수밖에 없다.
 *   그 양을 재지 않으면 7단계(백오프)가 무엇을 고쳤는지 말할 수 없다.
 *
 * 왜 복원된 문장이 아니라 토큰 열로 비교하나
 *   4단계 문장으로 비교하면 복원 손실(왕복 85.9%)이 섞인다. 모델은 원문과
 *   똑같은 토큰 열을 뽑았는데 kiwi.join() 이 글자를 다르게 복원해서 "새 문장"
 *   으로 집계되는 일이 생긴다. **모델이 암기했는지를 재는 자리에 토크나이저
 *   성능이 끼어들면 안 된다.**
 *
 * 구역 목차
 *   [1] 설정과 전역
 *   [2] 어휘표 읽기            (3-c 와 같은 코드)
 *   [3] 코퍼스를 번호열로       (3-c 와 같은 코드)
 *   [4] 역색인 - 토큰 번호 -> 원본에서 그 토큰이 나온 자리들
 *   [5] 최장 복사 구간
 *   [6] 통째 일치 / 중복 (문장 해시)
 *   [7] 생성 결과 읽기
 *   [8] 결과 파일 쓰기
 *   [9] main
 *
 * 사용법
 *   step7-5_compare.exe [3-c결과파일] [결과파일]
 *     생략하면 번호가 가장 큰 step7-3c_6gram_result_NN.txt 를 읽는다.
 *     출력은 입력과 같은 번호를 쓴다 (_02 -> step7-5_compare_result_02.txt).
 *     비교는 난수를 쓰지 않아 같은 입력이면 항상 같은 결과이므로 번호를
 *     새로 매기지 않는다. 4단계와 같은 방식이다.
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
 * [목적] 입력 경로와 패딩 길이를 한곳에 모은다.
 * [이유] CTX(=5)가 필요한 것은 원본 번호열에 BOS 5개가 끼어 있기 때문이다.
 *   문장 i 의 "진짜 토큰" 은 g_sent_start[i] + CTX 부터 시작하고,
 *   끝에 EOS 1개가 붙어 있으므로 길이는 g_sent_len[i] - CTX - 1 이다.
 * ===================================================================== */
#define N           6
#define CTX         (N - 1)

#define VOCAB_PATH  "step7-3a_vocab.txt"
#define TOKENS_PATH "step7-2b_tokens.txt"
#define GEN_PREFIX  "step7-3c_6gram_result_"
#define OUT_PREFIX  "step7-5_compare_result_"
#define MAX_RUN_NO  99

#define ID_BOS      0
#define ID_EOS      1

/* --- 어휘 --- */
static char **g_id_to_tok = NULL;
static int    g_vocab_n   = 0;

/* --- 코퍼스 --- */
static int       *g_seq = NULL;         /* 번호열 (BOS/EOS 패딩 포함) */
static long long  g_seq_len = 0;
static long long *g_sent_start = NULL;
static long long *g_sent_len = NULL;
static long long  g_sents = 0;
static long long  g_tokens = 0;

/* --- 생성 결과 --- */
#define MAX_GEN 1000
static int  *g_gen[MAX_GEN];            /* 생성 문장의 토큰 번호 배열 */
static int   g_gen_len[MAX_GEN];
static int   g_gen_n = 0;
static char  g_gen_seed[64] = "?";
static char  g_gen_n_hdr[64] = "?";

/* 문장 하나의 "진짜 토큰" 구간 */
static long long sent_tok_start(long long i) { return g_sent_start[i] + CTX; }
static long long sent_tok_len(long long i)   { return g_sent_len[i] - CTX - 1; }

/* =====================================================================
 * [2] 어휘표 읽기  (Phase 3-c 와 같은 코드)
 *
 * [목적] 3-a 가 정한 번호를 그대로 가져온다.
 * [이유] 번호를 여기서 다시 매기지 않는다. 다시 매기면 3-a 의 어휘표와 어긋나고,
 *   그러면 두 파일을 같이 쓰는 이 프로그램이 조용히 틀린다. 번호는 한 곳에서만 정한다.
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
 * [3] 코퍼스를 번호열로  (Phase 3-c 와 같은 코드)
 *
 * [목적] 원본 문장을 토큰 번호의 열로 바꾼다. 앞에 BOS 5개, 뒤에 EOS 1개.
 * [이유] 패딩을 3-c 와 똑같이 넣는 이유는 **문장 경계를 자동으로 막아 주기**
 *   때문이다. 생성 문장에는 BOS/EOS 가 들어 있지 않으므로, [5]의 연속 일치
 *   탐색이 문장 끝(EOS)에서 저절로 멈춘다. 경계 검사를 따로 할 필요가 없다.
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
 * [4] 역색인 - 토큰 번호 -> 원본에서 그 토큰이 나온 자리들
 *
 * [목적] "이 토큰이 원본 어디어디에 있나" 를 즉시 알 수 있게 한다.
 * [동작] 계수 정렬(counting sort)로 만든다.
 *     1) 번호별 등장 횟수를 센다            cnt[id]
 *     2) 누적합으로 각 번호의 시작 위치를 구한다   start[id]
 *     3) 한 번 더 훑으며 위치를 채워 넣는다   pos[ start[id]++ ] = p
 *   결과: 번호 id 가 나온 자리들은 pos[ start[id] .. start[id+1]-1 ] 에 있다.
 *
 * [이유] 이게 없으면 [5]의 연속 일치 탐색이 "생성 토큰마다 원본 146만 칸을
 *   전부 훑기" 가 된다. 역색인이 있으면 **그 토큰이 실제로 있는 자리만** 본다.
 *   계수 정렬을 쓴 것은 번호가 0 ~ 16,919 의 조밀한 정수라서다.
 *   연결 리스트로 만들면 자리들이 메모리에 흩어져 캐시를 놓치지만,
 *   계수 정렬은 같은 번호의 자리가 배열에 붙어 있어 순차로 읽힌다.
 * ===================================================================== */
static long long *g_occ_start = NULL;   /* 번호 id 의 자리 목록 시작 (길이 vocab_n + 1) */
static long long *g_occ_pos   = NULL;   /* 자리들 (길이 seq_len) */

static int build_index(void)
{
    g_occ_start = (long long *)calloc((size_t)g_vocab_n + 1, sizeof(long long));
    g_occ_pos   = (long long *)malloc((size_t)g_seq_len * sizeof(long long));
    if (!g_occ_start || !g_occ_pos) return 0;

    /* 1) 세기. g_occ_start[id + 1] 자리에 넣어 두면 2)의 누적합이 그대로 시작 위치가 된다 */
    for (long long p = 0; p < g_seq_len; p++) g_occ_start[g_seq[p] + 1]++;

    /* 2) 누적합 */
    for (int i = 0; i < g_vocab_n; i++) g_occ_start[i + 1] += g_occ_start[i];

    /* 3) 채우기. fill 은 start 의 복사본이어야 한다 (start 를 망가뜨리면 안 된다) */
    long long *fill = (long long *)malloc((size_t)g_vocab_n * sizeof(long long));
    if (!fill) return 0;
    for (int i = 0; i < g_vocab_n; i++) fill[i] = g_occ_start[i];
    for (long long p = 0; p < g_seq_len; p++) g_occ_pos[fill[g_seq[p]]++] = p;
    free(fill);
    return 1;
}

/* 원본 번호열의 자리 p 가 몇 번째 문장에 속하는가 (이진 탐색) */
static long long sent_of_pos(long long p)
{
    long long lo = 0, hi = g_sents - 1;
    while (lo < hi) {
        long long mid = (lo + hi + 1) / 2;
        if (g_sent_start[mid] <= p) lo = mid; else hi = mid - 1;
    }
    return lo;
}

/* =====================================================================
 * [5] 최장 복사 구간
 *
 * [목적] 생성 문장이 원본과 **연속으로** 겹친 최대 토큰 수를 찾는다.
 * [동작] 생성 문장의 시작점 s 를 0부터 보면서, 그 토큰이 원본에 나온 자리마다
 *   맞는 데까지 따라간다.
 *
 *     생성:        돈/NNG 을/JKO 2/SN 배/NNB 로/JKB 걸/VV 고/EC
 *     원본 ... 그 돈/NNG 을/JKO 2/SN 배/NNB 로/JKB 걸/VV 지/EC ...
 *                  +--------- 6토큰에서 끊김 ---------+
 *
 * [이유] 가지치기 `m - s <= best` 가 중요하다. 남은 길이가 이미 찾은 최댓값
 *   이하면 더 볼 필요가 없다. 생성 문장은 앞부분을 길게 베끼는 경향이 있어
 *   best 가 일찍 커지고, 그 뒤 시작점은 대부분 건너뛴다.
 *   이게 없으면 <S>(원본에 40만 번 등장) 같은 흔한 토큰에서 시작할 때마다
 *   40만 자리를 헛되이 훑게 된다.
 *
 *   문장 경계는 따로 검사하지 않는다. 원본에는 문장 사이에 EOS/BOS 가 있고
 *   생성 문장에는 그 번호가 없으므로 비교가 거기서 저절로 실패한다.
 * ===================================================================== */
typedef struct {
    int       best;        /* 최장 복사 길이 (토큰) */
    long long at_sent;     /* 그 구간이 있던 원본 문장 번호 (0부터) */
    int       at_gen;      /* 생성 문장에서 그 구간이 시작한 자리 */
} CopyInfo;

static CopyInfo longest_copy(const int *gen, int m)
{
    CopyInfo info;
    info.best = 0; info.at_sent = -1; info.at_gen = 0;

    for (int s = 0; s < m; s++) {
        if (m - s <= info.best) break;          /* 남은 길이가 최댓값 이하면 끝 */

        int id = gen[s];
        long long from = g_occ_start[id], to = g_occ_start[id + 1];
        for (long long k = from; k < to; k++) {
            long long p = g_occ_pos[k];
            int L = 0;
            while (s + L < m && p + L < g_seq_len && g_seq[p + L] == gen[s + L]) L++;
            if (L > info.best) {
                info.best = L;
                info.at_sent = sent_of_pos(p);
                info.at_gen = s;
                if (L == m - s) break;          /* 이 시작점에서 더 늘릴 수 없다 */
            }
        }
    }
    return info;
}

/* =====================================================================
 * [6] 통째 일치 / 중복 (문장 해시)
 *
 * [목적] (a) 생성 문장이 원본 문장과 통째로 같은가, (b) 생성 문장끼리 겹치는가.
 * [동작] 토큰 번호 열 전체를 FNV-1a 로 해시해 칸을 정하고, 충돌 시 **번호 열을
 *   직접 비교**해 확인한다. 개방 주소법(빈 칸이 나올 때까지 다음 칸).
 * [이유] 해시값이 같다고 같은 문장으로 처리하면 서로 다른 문장이 하나로 합쳐지고
 *   오류도 나지 않는다. 3-a 에서 토큰 문자열을 직접 비교한 것과 같은 이유다.
 *
 *   (a)와 [5]의 "전체 복사" 는 다르다. 생성 문장이 **더 긴 원본 문장의 일부**와
 *   똑같을 수 있다. 그러면 [5]는 100% 지만 통째 일치는 아니다. 둘 다 보고한다.
 * ===================================================================== */
#define SHASH_BITS  18
#define SHASH_SIZE  (1u << SHASH_BITS)
#define SHASH_MASK  (SHASH_SIZE - 1u)

static long long *g_sslot = NULL;       /* 칸 -> 원본 문장 번호. 빈 칸은 -1 */

static unsigned int seq_hash(const int *a, long long n)
{
    unsigned int h = 2166136261u;
    for (long long i = 0; i < n; i++) {
        unsigned int v = (unsigned int)a[i];
        for (int b = 0; b < 4; b++) { h ^= (v >> (b * 8)) & 0xFFu; h *= 16777619u; }
    }
    return h;
}

static int seq_equal(const int *a, long long an, const int *b, long long bn)
{
    if (an != bn) return 0;
    return memcmp(a, b, (size_t)an * sizeof(int)) == 0;
}

static int build_sent_hash(void)
{
    g_sslot = (long long *)malloc(SHASH_SIZE * sizeof(long long));
    if (!g_sslot) return 0;
    for (unsigned int i = 0; i < SHASH_SIZE; i++) g_sslot[i] = -1;

    for (long long i = 0; i < g_sents; i++) {
        const int *a = g_seq + sent_tok_start(i);
        long long n = sent_tok_len(i);
        unsigned int pos = seq_hash(a, n) & SHASH_MASK;
        while (g_sslot[pos] >= 0) {
            long long j = g_sslot[pos];
            if (seq_equal(a, n, g_seq + sent_tok_start(j), sent_tok_len(j))) break;  /* 같은 문장이 또 나옴: 먼저 것만 둔다 */
            pos = (pos + 1u) & SHASH_MASK;
        }
        if (g_sslot[pos] < 0) g_sslot[pos] = i;
    }
    return 1;
}

/* 원본에 통째로 같은 문장이 있으면 그 번호, 없으면 -1 */
static long long find_whole(const int *gen, int m)
{
    unsigned int pos = seq_hash(gen, m) & SHASH_MASK;
    for (;;) {
        long long i = g_sslot[pos];
        if (i < 0) return -1;
        if (seq_equal(gen, m, g_seq + sent_tok_start(i), sent_tok_len(i))) return i;
        pos = (pos + 1u) & SHASH_MASK;
    }
}

/* =====================================================================
 * [7] 생성 결과 읽기
 *
 * [목적] 3-c 결과 파일의 "번호: 토큰열" 줄을 번호 배열로 바꾼다.
 * [이유] 어휘표에 없는 토큰이 나오면 오류로 끝낸다. 조용히 건너뛰면 비교가
 *   한 칸씩 밀려 결과가 틀리는데 아무도 모른다.
 * ===================================================================== */
static int load_generated(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

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

        if (line[0] == '#') {                       /* 머리말에서 seed/count 를 집어 둔다 */
            char *t = strchr(line, '\t');
            if (t) {
                *t = '\0';
                char *name = line + 1;
                while (*name == ' ') name++;
                if (strcmp(name, "seed") == 0)  { strncpy(g_gen_seed, t + 1, sizeof(g_gen_seed) - 1); }
                if (strcmp(name, "count") == 0) { strncpy(g_gen_n_hdr, t + 1, sizeof(g_gen_n_hdr) - 1); }
            }
            continue;
        }
        /* 문장 줄은 "번호: 토큰열" 이다. 제목 줄에도 ": " 가 있으므로
           맨 앞이 숫자인지부터 확인한다 (그냥 strstr 로 찾으면 제목이 걸린다) */
        if (line[0] < '0' || line[0] > '9') continue;
        char *colon = strstr(line, ": ");
        if (!colon) continue;
        if (g_gen_n >= MAX_GEN) { fprintf(stderr, "생성 문장이 %d개를 넘습니다\n", MAX_GEN); break; }

        char *p = colon + 2;
        int cap = 64, n = 0;
        int *buf = (int *)malloc((size_t)cap * sizeof(int));
        if (!buf) { free(line); fclose(fp); return 0; }
        for (;;) {
            char *sep = strstr(p, "<W>");
            size_t tlen = sep ? (size_t)(sep - p) : strlen(p);
            if (tlen > 0) {
                int id = vocab_find(p, tlen);
                if (id < 0) {
                    fprintf(stderr, "어휘표에 없는 토큰: %.*s\n", (int)tlen, p);
                    free(buf); free(line); fclose(fp); return 0;
                }
                if (n >= cap) {
                    cap *= 2;
                    int *nb = (int *)realloc(buf, (size_t)cap * sizeof(int));
                    if (!nb) { free(buf); free(line); fclose(fp); return 0; }
                    buf = nb;
                }
                buf[n++] = id;
            }
            if (!sep) break;
            p = sep + 3;
        }
        g_gen[g_gen_n] = buf;
        g_gen_len[g_gen_n] = n;
        g_gen_n++;
    }
    free(line);
    fclose(fp);
    return g_gen_n > 0;
}

/* =====================================================================
 * [8] 결과 파일 쓰기
 *
 * [목적] 세 지표와 문장별 내역을 남긴다. 7단계(백오프)에서 같은 지표를 다시 재
 *   비교할 것이므로, **무엇을 어떻게 쟀는지**까지 파일에 적는다.
 * [이유] 숫자만 남기면 몇 주 뒤에 "이 85%가 무슨 85%였지" 가 된다.
 * ===================================================================== */
typedef struct {
    CopyInfo  copy;
    long long whole;       /* 통째 일치한 원본 문장 번호, 없으면 -1 */
    int       dup_of;      /* 앞선 생성 문장과 같으면 그 번호(1부터), 아니면 0 */
} Row;

static int write_result(const char *path, const char *src, const Row *rows, double ms)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;

    long long whole_hits = 0, full_copy = 0, dup = 0;
    double ratio_sum = 0.0;
    long long copy_sum = 0, tok_sum = 0;
    int b25 = 0, b50 = 0, b75 = 0, b95 = 0, b100 = 0;

    for (int i = 0; i < g_gen_n; i++) {
        int m = g_gen_len[i];
        double r = (double)rows[i].copy.best / (double)m;
        ratio_sum += r;
        copy_sum += rows[i].copy.best;
        tok_sum += m;
        if (rows[i].whole >= 0) whole_hits++;
        if (rows[i].copy.best == m) full_copy++;
        if (rows[i].dup_of) dup++;
        if      (r >= 1.0)  b100++;
        else if (r >= 0.95) b95++;
        else if (r >= 0.75) b75++;
        else if (r >= 0.50) b50++;
        else                b25++;
    }
    double avg_ratio = 100.0 * ratio_sum / g_gen_n;

    fprintf(fp, "=== STEP 7 5단계: 원본 비교 (생성 문장은 새 문장인가) ===\n");
    fprintf(fp, "#\n");
    fprintf(fp, "# 입력\t%s\n", src);
    fprintf(fp, "# 원본\t%s (문장 %lld / 토큰 %lld)\n", TOKENS_PATH, g_sents, g_tokens);
    fprintf(fp, "# seed\t%s\t(생성에 쓰인 seed)\n", g_gen_seed);
    fprintf(fp, "# n\t%d\n", N);
    fprintf(fp, "# 생성 문장\t%d\n", g_gen_n);
    fprintf(fp, "# 비교 시간\t%.0fms\n", ms);
    fprintf(fp, "#\n");
    fprintf(fp, "# 비교 단위는 **토큰 열**이다. 4단계의 복원 문장으로 비교하면\n");
    fprintf(fp, "# kiwi.join() 의 복원 손실(왕복 85.9%%)이 섞여, 모델이 암기했는지를\n");
    fprintf(fp, "# 재는 자리에 토크나이저 성능이 끼어든다.\n");
    fprintf(fp, "#\n");

    fprintf(fp, "## 1. 통째 일치 - 원본 문장과 완전히 같은가\n");
    fprintf(fp, "#\n");
    fprintf(fp, "통째 일치\t%lld / %d\t%.1f%%\n", whole_hits, g_gen_n,
            100.0 * (double)whole_hits / g_gen_n);
    fprintf(fp, "#\n");
    fprintf(fp, "# Step6(글자 단위) 실측: 3그램 0%% / 5그램 6.7%% / 7그램 20.0%% (15문장)\n");
    fprintf(fp, "#\n\n");

    fprintf(fp, "## 2. 최장 복사 구간 - 원본과 연속으로 겹친 최대 토큰 수\n");
    fprintf(fp, "#\n");
    fprintf(fp, "# 통째 일치가 놓치는 '부분 복사'를 잡는 지표다. 마지막 한 토큰만\n");
    fprintf(fp, "# 달라도 통째 일치는 0%% 이지만 복사 비율은 96%% 로 나온다.\n");
    fprintf(fp, "#\n");
    fprintf(fp, "평균 복사 비율\t%.1f%%\t(문장마다 최장복사/길이 를 구해 평균)\n", avg_ratio);
    fprintf(fp, "평균 최장 복사\t%.1f토큰\t(평균 문장 길이 %.1f토큰)\n",
            (double)copy_sum / g_gen_n, (double)tok_sum / g_gen_n);
    fprintf(fp, "전체 복사\t%lld / %d\t%.1f%%\t(문장 전체가 원본 어딘가에 연속으로 존재)\n",
            full_copy, g_gen_n, 100.0 * (double)full_copy / g_gen_n);
    fprintf(fp, "#\n");
    fprintf(fp, "# 분포\n");
    fprintf(fp, "복사 비율 100%%\t%d\n", b100);
    fprintf(fp, "복사 비율 95~100%%\t%d\n", b95);
    fprintf(fp, "복사 비율 75~95%%\t%d\n", b75);
    fprintf(fp, "복사 비율 50~75%%\t%d\n", b50);
    fprintf(fp, "복사 비율 50%% 미만\t%d\n", b25);
    fprintf(fp, "#\n\n");

    fprintf(fp, "## 3. 중복 생성 - 같은 문장을 두 번 뱉었나\n");
    fprintf(fp, "#\n");
    fprintf(fp, "중복 문장\t%lld / %d\t%.1f%%\t(서로 다른 문장 %d개)\n",
            dup, g_gen_n, 100.0 * (double)dup / g_gen_n, g_gen_n - (int)dup);
    fprintf(fp, "#\n");
    fprintf(fp, "# 강의가 백오프의 효과로 든 것이 '중복 문장이 사라졌다' 이므로 같이 잰다.\n");
    fprintf(fp, "#\n\n");

    fprintf(fp, "## 문장별 내역\n");
    fprintf(fp, "#\n");
    fprintf(fp, "# mark: * = 원본 문장과 통째로 같음 / = = 전체가 원본 어딘가에 연속 존재 / D = 앞 문장과 중복 (겹치면 이어 붙여 \"*D\" 처럼 적는다)\n");
    fprintf(fp, "# src: 가장 길게 베낀 구간이 있던 원본 문장 번호 (1부터)\n");
    fprintf(fp, "#\n");
    fprintf(fp, "# no\tmark\tlen\tcopy\tratio\tsrc\tgen_at\n");
    for (int i = 0; i < g_gen_n; i++) {
        int m = g_gen_len[i];
        /* 표시는 겹칠 수 있다. 덮어쓰지 않고 이어 붙인다 ("*D" 처럼).
           한 글자로 덮어쓰면 통째 일치이면서 중복인 문장에서 정보가 사라진다 */
        char mark[4]; int mk = 0;
        if (rows[i].whole >= 0)          mark[mk++] = '*';
        else if (rows[i].copy.best == m) mark[mk++] = '=';
        if (rows[i].dup_of)              mark[mk++] = 'D';
        if (!mk)                         mark[mk++] = '-';
        mark[mk] = '\0';
        fprintf(fp, "%d\t%s\t%d\t%d\t%.0f%%\t%lld\t%d\n",
                i + 1, mark, m, rows[i].copy.best,
                100.0 * rows[i].copy.best / m,
                rows[i].copy.at_sent + 1, rows[i].copy.at_gen);
    }
    fprintf(fp, "\n");

    /* 가장 많이 베낀 문장 몇 개를 원본과 나란히 보여 준다 */
    fprintf(fp, "## 가장 길게 베낀 문장 (최장 복사 상위 3개)\n");
    fprintf(fp, "#\n");
    int order[MAX_GEN];
    for (int i = 0; i < g_gen_n; i++) order[i] = i;
    for (int i = 0; i < g_gen_n; i++)             /* 상위 3개만 필요하므로 선택 정렬 3회 */
        for (int j = i + 1; j < g_gen_n; j++)
            if (rows[order[j]].copy.best > rows[order[i]].copy.best) {
                int t = order[i]; order[i] = order[j]; order[j] = t;
            }
    for (int k = 0; k < 3 && k < g_gen_n; k++) {
        int i = order[k];
        long long si = rows[i].copy.at_sent;
        fprintf(fp, "[생성 %d번] %d토큰 중 %d토큰을 원본 %lld번 문장에서 연속 복사\n",
                i + 1, g_gen_len[i], rows[i].copy.best, si + 1);
        fprintf(fp, "  생성: ");
        for (int j = 0; j < g_gen_len[i]; j++) {
            if (j == rows[i].copy.at_gen) fprintf(fp, "[");
            if (j == rows[i].copy.at_gen + rows[i].copy.best) fprintf(fp, "]");
            if (j) fprintf(fp, " ");
            fprintf(fp, "%s", g_id_to_tok[g_gen[i][j]]);
        }
        if (rows[i].copy.at_gen + rows[i].copy.best == g_gen_len[i]) fprintf(fp, "]");
        fprintf(fp, "\n");
        if (si >= 0) {
            fprintf(fp, "  원본: ");
            long long st = sent_tok_start(si), n = sent_tok_len(si);
            for (long long j = 0; j < n; j++) {
                if (j) fprintf(fp, " ");
                fprintf(fp, "%s", g_id_to_tok[g_seq[st + j]]);
            }
            fprintf(fp, "\n");
        }
        fprintf(fp, "\n");
    }

    fclose(fp);
    return 1;
}

/* 번호가 가장 큰 3-c 결과 파일 */
static int latest_generated(char *buf, size_t size)
{
    int last = 0;
    for (int n = 1; n <= MAX_RUN_NO; n++) {
        char t[256];
        snprintf(t, sizeof(t), GEN_PREFIX "%02d.txt", n);
        FILE *fp = fopen(t, "rb");
        if (fp) { fclose(fp); last = n; }
    }
    if (!last) return 0;
    snprintf(buf, size, GEN_PREFIX "%02d.txt", last);
    return 1;
}

/* =====================================================================
 * [9] main
 * ===================================================================== */
int main(int argc, char **argv)
{
    char gen_path[512], out_path[512];
    clock_t t0;

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (argc > 1) {
        snprintf(gen_path, sizeof(gen_path), "%s", argv[1]);
    } else if (!latest_generated(gen_path, sizeof(gen_path))) {
        fprintf(stderr, "3-c 결과 파일이 없습니다. 먼저 step7-3c_6gram.exe 를 실행하세요\n");
        return 1;
    }

    if (argc > 2) {
        snprintf(out_path, sizeof(out_path), "%s", argv[2]);
    } else {
        /* 출력 번호는 입력 번호를 따라간다. 비교는 결정적이라 새 번호를 매기지 않는다 */
        size_t gl = strlen(gen_path);
        if (gl < 6) { fprintf(stderr, "입력 파일명이 이상합니다: %s\n", gen_path); return 1; }
        snprintf(out_path, sizeof(out_path), OUT_PREFIX "%.2s.txt", gen_path + gl - 6);
    }

    if (!load_vocab(VOCAB_PATH)) { fprintf(stderr, "어휘표를 읽지 못했습니다: %s\n", VOCAB_PATH); return 1; }
    if (!build_corpus(TOKENS_PATH)) { fprintf(stderr, "원본을 읽지 못했습니다: %s\n", TOKENS_PATH); return 1; }
    if (!build_index()) { fprintf(stderr, "역색인 생성 실패\n"); return 1; }
    if (!build_sent_hash()) { fprintf(stderr, "문장 해시 생성 실패\n"); return 1; }
    if (!load_generated(gen_path)) { fprintf(stderr, "생성 결과를 읽지 못했습니다: %s\n", gen_path); return 1; }

    printf("원본 %lld문장 / %lld토큰, 생성 %d문장 (%s)\n", g_sents, g_tokens, g_gen_n, gen_path);

    Row *rows = (Row *)calloc((size_t)g_gen_n, sizeof(Row));
    if (!rows) return 1;

    t0 = clock();
    for (int i = 0; i < g_gen_n; i++) {
        rows[i].copy  = longest_copy(g_gen[i], g_gen_len[i]);
        rows[i].whole = find_whole(g_gen[i], g_gen_len[i]);
        rows[i].dup_of = 0;
        for (int j = 0; j < i; j++)         /* 중복: 앞의 생성 문장과 같은가 */
            if (seq_equal(g_gen[i], g_gen_len[i], g_gen[j], g_gen_len[j])) {
                rows[i].dup_of = j + 1; break;
            }
    }
    double ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;

    if (!write_result(out_path, gen_path, rows, ms)) {
        fprintf(stderr, "결과 파일을 쓰지 못했습니다: %s\n", out_path);
        return 1;
    }

    long long whole = 0, full = 0, dup = 0;
    double ratio_sum = 0.0;
    for (int i = 0; i < g_gen_n; i++) {
        if (rows[i].whole >= 0) whole++;
        if (rows[i].copy.best == g_gen_len[i]) full++;
        if (rows[i].dup_of) dup++;
        ratio_sum += (double)rows[i].copy.best / g_gen_len[i];
    }
    printf("통째 일치   %lld / %d (%.1f%%)\n", whole, g_gen_n, 100.0 * (double)whole / g_gen_n);
    printf("전체 복사   %lld / %d (%.1f%%)\n", full, g_gen_n, 100.0 * (double)full / g_gen_n);
    printf("평균 복사율 %.1f%%\n", 100.0 * ratio_sum / g_gen_n);
    printf("중복 문장   %lld / %d\n", dup, g_gen_n);
    printf("비교 %.0fms / 저장: %s\n", ms, out_path);
    return 0;
}
