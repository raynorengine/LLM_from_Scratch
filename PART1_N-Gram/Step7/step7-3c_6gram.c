/* =====================================================================
 * [STEP 7 / Phase 3-c] 토큰 단위 6그램 문장 생성
 *
 * 하는 일
 *   Phase 3-b 와 똑같이 6그램을 센 뒤, 그 통계를 따라 토큰을 하나씩 뽑아
 *   문장을 만든다. 출력은 **토큰 열 그대로**다.
 *   읽을 수 있는 문장으로 바꾸는 것은 4단계(kiwi.join 역변환)의 몫이다.
 *   원본과 같은 문장인지 표시하는 것도 하지 않는다. 그것은 5단계다.
 *
 * Phase 3-b 와의 관계
 *   [1]~[4](설정 / 어휘표 / 코퍼스 / 세기)는 3-b 와 같은 코드다.
 *   3-b 의 [5][6](행 정렬, 빈도표 파일 출력)은 **가져오지 않았다.**
 *   생성은 메모리의 해시 테이블만 보면 되고, 빈도표 파일은 사람이 읽으라고
 *   만든 결과물이지 입력이 아니기 때문이다.
 *   빈도표를 파싱하는 대신 다시 세는 이유: 세기가 262ms 라 파싱보다 빠르다.
 *   (Step4~6 도 매 실행마다 처음부터 세고 나서 생성했다)
 *
 * 생성 방법
 *   문맥을 BOS 5개로 시작하고, 그 문맥의 후보 중 하나를 **빈도에 비례해** 뽑는다.
 *   뽑은 토큰을 붙이고 문맥을 한 칸 민다. EOS 가 나오면 문장이 끝난다.
 *
 * 구역 목차
 *   [1]  52줄 | 설정과 전역
 *   [2] 110줄 | 어휘표 읽기
 *   [3] 185줄 | 코퍼스를 번호열로
 *   [4] 261줄 | n그램 세기 (문맥 -> 후보 목록)
 *   [5] 363줄 | 난수
 *   [6] 400줄 | 뽑기와 문장 생성
 *   [7] 470줄 | 결과 파일 자동 번호
 *   [8] 500줄 | 결과 파일 쓰기
 *   [9] 560줄 | main
 *
 * 사용법
 *   step7-3c_6gram.exe [개수=15] [seed=현재시각] [결과파일]
 *
 * 난수를 쓰므로 seed 기본값은 현재 시각이고, 쓰인 seed 를 결과 파일 머리에 적는다.
 * 그 seed 를 인자로 주면 같은 결과가 나온다.
 * 결과 파일은 step7-3c_6gram_result_NN.txt (NN = 01~99, 기존 최대 + 1).
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
#define OUTPUT_PREFIX "step7-3c_6gram_result_"
#define MAX_RUN_NO    99
#define GEN_DEFAULT   15      /* 생성할 문장 수. Step6(15문장)과 맞춘다 */
#define MAX_GEN_TOK   200     /* 한 문장의 토큰 상한. 평균 22.69개라 충분히 여유 있다 */

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
 * [5] 난수
 *
 * [목적] 0 ~ n-1 범위의 고른 난수를 만든다. 다음 토큰을 뽑을 때 쓴다.
 * [동작] rand30: rand()를 두 번 불러 15비트 + 15비트 = 30비트 난수.
 *   rand_below: 30비트 난수 중 n의 배수 경계(limit)를 넘는 값은 버리고 다시 뽑은 뒤 % n.
 * [이유] MSVC의 rand()는 0 ~ 32,767(15비트)뿐인데, 문맥 (BOS 5개)의 빈도 합은
 *   문장 수 64,376이라 범위가 모자란다. 그대로 쓰면 뒤쪽 후보가 영영 안 뽑힌다.
 *   hi, lo 를 따로 받는 까닭: 한 식에 rand()를 두 번 쓰면 호출 순서가 정해져 있지
 *   않아 같은 seed 에서도 결과가 달라질 수 있다.
 *   그냥 나머지 연산만 하면 앞쪽 값이 조금 더 자주 나오므로(모듈로 편향)
 *   경계를 넘는 값을 버린다.
 *   (Step2에서 만들어 Step6까지 쓴 것과 같은 코드다)
 * ===================================================================== */
#define RAND30_RANGE (1ULL << 30)

static unsigned long long rand30(void)
{
    unsigned long long hi = (unsigned long long)rand();   /* 0 ~ 32,767 */
    unsigned long long lo = (unsigned long long)rand();
    return (hi << 15) | lo;                               /* 0 ~ 2^30 - 1 */
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

/* =====================================================================
 * [6] 뽑기와 문장 생성
 *
 * [목적] 문맥 다음에 올 토큰을 빈도에 비례해 하나 뽑고, 그것을 이어 문장을 만든다.
 * [동작] 뽑기: 0 ~ (후보 빈도 합 - 1) 에서 난수 r 을 하나 얻고, 후보를 앞에서부터
 *   훑으며 빈도를 누적해 r 보다 커지는 순간의 후보를 고른다.
 *     예) 후보가 A(3) B(1) C(2) 이면 합은 6.
 *         r=0,1,2 이면 A / r=3 이면 B / r=4,5 이면 C
 *   생성: 문맥을 BOS 5개로 시작해 한 토큰씩 뽑아 붙이고 문맥을 한 칸 민다.
 *   EOS 가 나오면 문장이 끝난다.
 * [이유] 누적 비교에 큰 부등호를 쓰는 것이 핵심이다. 등호를 붙이면(>=) 빈도 0인
 *   후보도 뽑히고 경계가 한 칸씩 밀린다. (Step2부터 이어진 규칙)
 *   후보를 번호 순서대로 훑는 이유: 같은 seed 에서 항상 같은 결과가 나와야
 *   재현이 가능하다. 후보 배열은 넣은 순서대로 쌓이므로 미리 번호순으로 정렬해 둔다.
 *
 *   후보 목록이 문맥마다 따로 있으므로 어휘 전체(16,920개)를 훑지 않는다.
 *   문맥당 후보는 평균 1.25개다.
 * ===================================================================== */
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

/* 후보를 번호순으로 정렬해 둔다. 재현성을 위해서다 (위 [이유] 참고) */
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
    return e->opts[e->opt_count - 1].next_id;     /* 도달하지 않는다 (방어용) */
}

/* 문장 하나를 만든다. out 에 토큰 번호를 담고 길이를 돌려준다 */
static int generate_sentence(int *out, int max_len, int *hit_limit)
{
    int ctx[CTX];
    for (int i = 0; i < CTX; i++) ctx[i] = ID_BOS;

    int len = 0;
    *hit_limit = 0;
    for (;;) {
        Entry *e = find_entry(ctx);
        if (!e) {                       /* 학습에 없던 문맥. 패딩으로 시작하므로 나오지 않는다 */
            *hit_limit = 1;
            break;
        }
        int next = pick_next(e);
        if (next == ID_EOS) break;      /* 모델이 끝내기로 했다 */
        if (len >= max_len) { *hit_limit = 1; break; }

        out[len++] = next;
        for (int i = 0; i < CTX - 1; i++) ctx[i] = ctx[i + 1];   /* 문맥을 한 칸 민다 */
        ctx[CTX - 1] = next;
    }
    return len;
}

/* =====================================================================
 * [7] 결과 파일 자동 번호
 *
 * [목적] step7-3c_6gram_result_NN.txt 의 NN 을 정한다.
 * [동작] 01~99 를 차례로 열어 보고 "있는 번호 중 최댓값 + 1" 을 쓴다.
 *   99 가 이미 있으면 덮어쓰지 않고 오류로 끝낸다.
 * [이유] 지운 번호를 다시 채우지 않아야 번호 순서 = 실행 순서가 된다.
 *   표준 C 에는 폴더 목록 함수가 없어서 fopen 으로 하나씩 확인한다.
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
        fprintf(stderr, "[오류] 결과 파일 번호가 %d까지 찼습니다. 기존 %s*.txt 를 정리하세요\n",
                MAX_RUN_NO, OUTPUT_PREFIX);
        return 0;
    }
    snprintf(buf, size, OUTPUT_PREFIX "%02d.txt", last + 1);
    return 1;
}

/* =====================================================================
 * [8] 결과 파일 쓰기
 *
 * [목적] 생성한 문장을 토큰 열 그대로 남긴다. 4단계가 이 파일을 읽어 역변환한다.
 * [동작] 머리에 조건(seed, 개수, 규모)을 적고, 문장마다 "번호: 토큰열" 한 줄.
 * [이유] seed 를 반드시 적는다. 그 seed 를 인자로 주면 같은 결과가 나와야 하고,
 *   검증기가 그것을 확인한다. 적어 두지 않으면 재현이 불가능해진다.
 *   토큰 사이는 <W>, 공백은 <S> 로 입력 파일과 같은 표기를 쓴다.
 * ===================================================================== */
static int write_result(const char *path, int count, unsigned int seed,
                        int **sents, int *lens, int *limits, double build_ms, double gen_ms)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;

    long long tok_sum = 0;
    int limited = 0;
    for (int i = 0; i < count; i++) { tok_sum += lens[i]; limited += limits[i]; }

    fprintf(fp, "=== STEP 7 Phase 3-c: 토큰 단위 %d그램 문장 생성 ===\n", N);
    fprintf(fp, "#\n");
    fprintf(fp, "# 출력은 토큰 열 그대로다. 읽을 수 있는 문장으로 바꾸는 것은\n");
    fprintf(fp, "# 4단계(kiwi.join 역변환)의 몫이다. 원본과 같은 문장인지 보는 것은 5단계다.\n");
    fprintf(fp, "#\n");
    fprintf(fp, "# seed\t%u\n", seed);
    fprintf(fp, "#   이 seed 를 두 번째 인자로 주면 같은 결과가 나온다.\n");
    fprintf(fp, "# count\t%d\n", count);
    fprintf(fp, "# n\t%d\n", N);
    fprintf(fp, "# vocab\t%d\n", g_vocab_n);
    fprintf(fp, "# sentences\t%lld\n", g_sents);
    fprintf(fp, "# tokens\t%lld\n", g_tokens);
    fprintf(fp, "# total_grams\t%lld\n", g_total_grams);
    fprintf(fp, "# unique_grams\t%lld\n", g_gram_count);
    fprintf(fp, "# contexts\t%lld\n", g_ctx_count);
    fprintf(fp, "# build_ms\t%.3f\n", build_ms);
    fprintf(fp, "# gen_ms\t%.3f\n", gen_ms);
    fprintf(fp, "# avg_tokens\t%.2f\n", (double)tok_sum / (double)count);
    fprintf(fp, "# hit_limit\t%d\t(길이 상한 %d 토큰에 걸려 잘린 문장 수)\n", limited, MAX_GEN_TOK);
    fprintf(fp, "#\n");

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

/* =====================================================================
 * [9] main
 * ===================================================================== */
int main(int argc, char **argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    int count = (argc > 1) ? atoi(argv[1]) : GEN_DEFAULT;
    unsigned int seed = (argc > 2) ? (unsigned int)strtoul(argv[2], NULL, 10)
                                   : (unsigned int)time(NULL);
    if (count <= 0) count = GEN_DEFAULT;

    if (!load_vocab(VOCAB_PATH)) {
        fprintf(stderr, "어휘표를 읽을 수 없습니다: %s\n", VOCAB_PATH);
        fprintf(stderr, "먼저 step7-3a_tokenid.exe 를 실행하세요.\n");
        return 1;
    }
    if (!build_corpus(TOKENS_PATH)) {
        fprintf(stderr, "토큰 파일을 읽을 수 없습니다: %s\n", TOKENS_PATH);
        return 1;
    }
    g_table = (Entry **)calloc(HASH_SIZE, sizeof(Entry *));
    if (!g_table) { fprintf(stderr, "메모리 부족 (해시 테이블)\n"); return 1; }

    clock_t t0 = clock();
    count_all();
    sort_all_opts();                    /* 재현성을 위해 후보를 번호순으로 */
    double build_ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;

    srand(seed);

    int **sents = (int **)malloc((size_t)count * sizeof(int *));
    int *lens = (int *)malloc((size_t)count * sizeof(int));
    int *limits = (int *)malloc((size_t)count * sizeof(int));
    if (!sents || !lens || !limits) { fprintf(stderr, "메모리 부족\n"); return 1; }

    t0 = clock();
    for (int i = 0; i < count; i++) {
        sents[i] = (int *)malloc((size_t)MAX_GEN_TOK * sizeof(int));
        if (!sents[i]) { fprintf(stderr, "메모리 부족\n"); return 1; }
        lens[i] = generate_sentence(sents[i], MAX_GEN_TOK, &limits[i]);
    }
    double gen_ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;

    char path[512];
    if (argc > 3) {
        snprintf(path, sizeof(path), "%s", argv[3]);
    } else if (!next_output_path(path, sizeof(path))) {
        return 1;
    }
    if (!write_result(path, count, seed, sents, lens, limits, build_ms, gen_ms)) {
        fprintf(stderr, "결과 파일을 쓸 수 없습니다: %s\n", path);
        return 1;
    }

    long long tok_sum = 0;
    for (int i = 0; i < count; i++) tok_sum += lens[i];
    printf("seed %u / 문장 %d개 / 평균 %.2f토큰\n", seed, count, (double)tok_sum / count);
    printf("세기 %.1fms / 생성 %.1fms\n", build_ms, gen_ms);
    printf("저장: %s\n", path);

    for (int i = 0; i < count; i++) free(sents[i]);
    free(sents); free(lens); free(limits);
    return 0;
}
