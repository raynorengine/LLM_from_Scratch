/* =====================================================================
 * [STEP 7 / Phase 3-a] 토큰 읽기 + 번호(ID) 매기기
 *
 * 하는 일
 *   step7-2b_tokens.txt 를 읽어 토큰마다 정수 번호를 붙이고 어휘표를 만든다.
 *   n그램은 아직 세지 않는다 (Phase 3-b).
 *
 * 왜 번호를 붙이나
 *   n그램 표가 저장하는 것은 "(앞 토큰들) 다음에 (이 토큰)이 몇 번 나왔다" 이다.
 *   문자열 `세기/NNG` 를 그대로 들고 다니면 비교할 때마다 글자를 하나씩 맞춰 봐야 한다.
 *   토큰 146만 개를 그렇게 처리하면 느리고 메모리도 많이 든다.
 *   번호로 바꾸면 정수 비교 한 번으로 끝난다.
 *
 * Step1~6 과 무엇이 다른가
 *   번호를 매기는 작업 자체는 Step4~6 에도 있었다 (step6_5gram.c 의 g_cp_to_id).
 *   다른 것은 **번호를 찾는 방법**이다.
 *     글자  : 코드포인트라는 이미 정해진 숫자가 있어 배열 첨자로 바로 쓴다 -> 배열 한 방
 *     토큰  : 문자열이라 첨자로 쓸 숫자가 없다 -> 해시 테이블이 필요하다
 *   앞 Step 에서 배열 한 줄이던 것이 자료구조 하나로 커졌다. 그래서 단계를 따로 뗐다.
 *
 * C 는 토큰의 내용을 몰라도 된다
 *   `세기/NNG` 가 한글인지 무엇인지 알 필요가 없다. 바이트 문자열로 해시하고 비교할 뿐이다.
 *   Step6 은 글자 경계를 찾으려고 UTF-8 을 해독해야 했지만 여기서는 필요 없다.
 *
 * 구역 목차
 *   [1]  38줄 | 설정과 전역
 *   [2]  85줄 | 문자열 저장소(arena)
 *   [3] 112줄 | 해시 테이블 - 토큰 문자열 -> 번호
 *   [4] 183줄 | 입력 읽기와 토큰 쪼개기
 *   [5] 256줄 | 번호 매기기 (사전순 정렬)
 *   [6] 300줄 | 어휘표 출력
 *   [7] 344줄 | 요약 보고서
 *   [8] 395줄 | main
 *
 * 사용법
 *   step7-3a_tokenid.exe [토큰파일] [어휘표] [보고서]
 *   인자를 생략하면 step7-2b_tokens.txt / step7-3a_vocab.txt / step7-3a_tokenid_result.txt
 *
 * 결과가 입력에 대해 항상 같으므로(난수 없음) 출력 파일에 실행 번호를 붙이지 않는다.
 * ===================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* =====================================================================
 * [1] 설정과 전역
 *
 * [목적] 파일 경로 기본값과 크기 상한을 한곳에 모은다.
 * [동작] 해시 칸 수는 2의 거듭제곱으로 잡아 나머지 연산을 비트 AND 로 대신한다.
 * [이유] 어휘가 16,917종이므로 2^15 = 32,768 칸이면 적재율 51.6% 다.
 *   Step4 에서 정한 거부선(70%)에 여유가 있다. 적재율이 높으면 충돌이 늘어
 *   한 칸을 찾는 데 여러 번 비교하게 된다.
 * ===================================================================== */
#define IN_DEFAULT      "step7-2b_tokens.txt"
#define VOCAB_DEFAULT   "step7-3a_vocab.txt"
#define REPORT_DEFAULT  "step7-3a_tokenid_result.txt"

#define HASH_BITS   15
#define HASH_SIZE   (1u << HASH_BITS)
#define HASH_MASK   (HASH_SIZE - 1u)

#define LOAD_LIMIT  70          /* 적재율 상한(%). 넘으면 오류로 멈춘다 */
#define MAX_TOKEN   256         /* 토큰 하나의 최대 바이트. 실측 최대는 훨씬 짧다 */

/* 특수 토큰. 실제 토큰 번호는 2번부터 시작한다.
   BOS/EOS 는 이 파일에 나오지 않는다. 문장 앞뒤에 붙이는 것은 Phase 3-b 의 몫이다.
   여기서 번호만 미리 잡아 두는 것은, 다음 Phase 가 번호 체계를 다시 만들지 않게 하려는 것이다. */
#define ID_BOS  0
#define ID_EOS  1
#define ID_BASE 2

typedef struct {
    char        *text;      /* 토큰 문자열 (arena 안을 가리킨다) */
    long long    freq;      /* 등장 횟수 */
    int          id;        /* 사전순 정렬 후 매겨지는 번호 */
} Vocab;

static Vocab *g_vocab = NULL;       /* 어휘 배열 */
static int    g_vocab_n = 0;        /* 어휘 종류 수 */
static int    g_vocab_cap = 0;

static long long g_sentences = 0;   /* 문장 수 */
static long long g_tokens = 0;      /* 토큰 총수 (<S> 포함) */

/* =====================================================================
 * [2] 문자열 저장소 (arena)
 *
 * [목적] 토큰 문자열 16,917개를 담을 메모리를 한 번에 크게 잡아 쓴다.
 * [동작] 큰 덩어리를 malloc 해 두고 끝에서부터 조금씩 떼어 준다.
 *   모자라면 새 덩어리를 추가한다 (앞 덩어리는 그대로 둔다).
 * [이유] 토큰마다 malloc 을 부르면 호출이 1만 번 넘고, 작은 조각마다 관리 비용이 붙는다.
 *   한 번에 크게 잡아 쪼개 쓰면 빠르고 단편화도 없다.
 *   앞 덩어리를 realloc 으로 늘리지 않는 이유: realloc 은 주소를 옮길 수 있는데
 *   이미 나눠 준 포인터들이 전부 옛 주소를 가리키고 있어 한꺼번에 망가진다.
 * ===================================================================== */
#define ARENA_CHUNK (1u << 20)      /* 1MB 씩 */

typedef struct Arena {
    char          *buf;
    size_t         used;
    size_t         cap;
    struct Arena  *next;
} Arena;

static Arena *g_arena = NULL;

static char *arena_dup(const char *s, size_t len)
{
    if (!g_arena || g_arena->used + len + 1 > g_arena->cap) {
        size_t cap = ARENA_CHUNK;
        if (len + 1 > cap) cap = len + 1;          /* 아주 긴 토큰도 담기게 */
        Arena *a = (Arena *)malloc(sizeof(Arena));
        if (!a) return NULL;
        a->buf = (char *)malloc(cap);
        if (!a->buf) { free(a); return NULL; }
        a->used = 0; a->cap = cap; a->next = g_arena;
        g_arena = a;
    }
    char *dst = g_arena->buf + g_arena->used;
    memcpy(dst, s, len);
    dst[len] = '\0';
    g_arena->used += len + 1;
    return dst;
}

static void arena_free(void)
{
    while (g_arena) {
        Arena *n = g_arena->next;
        free(g_arena->buf);
        free(g_arena);
        g_arena = n;
    }
}

/* =====================================================================
 * [3] 해시 테이블 - 토큰 문자열 -> 어휘 배열의 자리
 *
 * [목적] 같은 토큰이 또 나왔을 때 "이미 본 것"임을 빠르게 알아낸다.
 * [동작] 문자열에서 숫자(해시값)를 만들어 그 번호의 칸부터 살펴본다.
 *   칸이 비었으면 새 토큰, 차 있으면 문자열을 비교한다. 다르면 다음 칸으로 간다.
 *   (열린 주소법 + 선형 탐사. 체이닝보다 메모리가 적고 캐시에 유리하다)
 * [이유] 해시값은 "몇 번 칸부터 찾을지"만 정한다. 서로 다른 토큰이 같은 칸을
 *   가리키는 일(충돌)은 일어날 수 있고 정상이다. 같은 토큰인지는 반드시
 *   strcmp 로 **문자열을 직접 비교**해서 판단한다. 해시값만 믿으면 다른 토큰이
 *   같은 것으로 합쳐지고, 그래도 오류가 나지 않아 조용히 틀린 결과가 나온다.
 *
 * FNV-1a 해시를 쓴다. 바이트마다 XOR 후 소수를 곱하는 단순한 방식인데
 * 짧은 문자열에서 고르게 퍼지고 구현이 짧다.
 * ===================================================================== */
static int *g_slot = NULL;          /* 칸 -> 어휘 배열 첨자. 빈 칸은 -1 */

static unsigned int str_hash(const char *s, size_t len)
{
    unsigned int h = 2166136261u;               /* FNV offset basis */
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;                         /* FNV prime */
    }
    return h;
}

/* 토큰을 찾고, 없으면 추가한다. 반환값은 어휘 배열에서의 자리 */
static int vocab_put(const char *s, size_t len)
{
    unsigned int pos = str_hash(s, len) & HASH_MASK;

    for (;;) {
        int idx = g_slot[pos];
        if (idx < 0) break;                                  /* 빈 칸 = 처음 보는 토큰 */
        /* 길이까지 같은지 먼저 보면 strcmp 호출을 줄일 수 있지만,
           문자열이 짧아 차이가 없으므로 그냥 비교한다 */
        if (strlen(g_vocab[idx].text) == len &&
            memcmp(g_vocab[idx].text, s, len) == 0) {
            g_vocab[idx].freq++;
            return idx;
        }
        pos = (pos + 1u) & HASH_MASK;                        /* 충돌 -> 다음 칸 */
    }

    /* 새 토큰 추가 */
    if (g_vocab_n >= g_vocab_cap) {
        int ncap = g_vocab_cap ? g_vocab_cap * 2 : 4096;
        Vocab *nv = (Vocab *)realloc(g_vocab, (size_t)ncap * sizeof(Vocab));
        if (!nv) { fprintf(stderr, "메모리 부족 (어휘 배열)\n"); exit(1); }
        g_vocab = nv; g_vocab_cap = ncap;
    }
    char *copy = arena_dup(s, len);
    if (!copy) { fprintf(stderr, "메모리 부족 (문자열 저장소)\n"); exit(1); }

    g_vocab[g_vocab_n].text = copy;
    g_vocab[g_vocab_n].freq = 1;
    g_vocab[g_vocab_n].id   = -1;       /* 번호는 [5]에서 정렬한 뒤에 매긴다 */
    g_slot[pos] = g_vocab_n;
    return g_vocab_n++;
}

/* =====================================================================
 * [4] 입력 읽기와 토큰 쪼개기
 *
 * [목적] `form/tag<W>form/tag<W><S><W>...` 한 줄을 토큰 단위로 끊는다.
 * [동작] 파일을 통째로 읽어 줄 단위로 보고, 각 줄을 구분자 `<W>` 로 쪼갠다.
 * [이유] 파일을 "rb"(바이너리)로 여는 것은 Windows 의 텍스트 모드가 개행을
 *   바꿔 버리기 때문이다. 줄 끝 `\r` 이 토큰에 섞이면 `습니다/EF\r` 처럼
 *   없는 어휘가 생긴다. 그래서 `\r` 은 직접 떼어 낸다.
 *
 *   구분자를 strstr 로 찾는 이유: `<W>` 는 세 글자라 한 글자 비교로는 안 된다.
 *   토큰 안에 `<` 나 `>` 가 들어 있을 가능성은 없다 - Phase 1 의 정제가
 *   한글·숫자·영문·`,.!?`·공백만 남겼기 때문이다 (그래서 이 표기를 고른 것이다).
 * ===================================================================== */
static char *read_whole(const char *path, size_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    if (sz < 0) { fclose(fp); return NULL; }
    rewind(fp);

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[got] = '\0';
    *out_len = got;
    return buf;
}

static void scan_tokens(char *text, size_t len)
{
    size_t i = 0;
    while (i < len) {
        /* --- 한 줄 잘라내기 --- */
        size_t line_end = i;
        while (line_end < len && text[line_end] != '\n') line_end++;

        size_t stop = line_end;
        if (stop > i && text[stop - 1] == '\r') stop--;      /* CRLF 대비 */

        if (stop > i) {
            g_sentences++;

            /* --- 줄을 <W> 로 쪼개기 --- */
            char *p = text + i;
            char *end = text + stop;
            *end = '\0';            /* strstr 가 줄 밖으로 넘어가지 않게 막는다 */

            for (;;) {
                char *sep = strstr(p, "<W>");
                size_t tlen = sep ? (size_t)(sep - p) : (size_t)(end - p);
                if (tlen > 0 && tlen < MAX_TOKEN) {
                    vocab_put(p, tlen);
                    g_tokens++;
                } else if (tlen >= MAX_TOKEN) {
                    fprintf(stderr, "토큰이 너무 깁니다 (%zu바이트)\n", tlen);
                    exit(1);
                }
                if (!sep) break;
                p = sep + 3;        /* "<W>" 세 바이트 건너뛰기 */
            }
        }
        i = (line_end < len) ? line_end + 1 : len;
    }
}

/* =====================================================================
 * [5] 번호 매기기 - 사전순으로 정렬한 뒤 붙인다
 *
 * [목적] 토큰마다 0부터 시작하는 정수를 준다.
 * [동작] 어휘를 바이트 사전순(strcmp 기준)으로 정렬하고 앞에서부터 2, 3, 4 ... 를 준다.
 *   0 과 1 은 BOS/EOS 로 비워 둔다.
 * [이유] **등장 순서로 매기지 않는 이유**는 검증 때문이다.
 *   등장 순서로 매겨도 입력이 같으면 결과는 같다. 하지만 Python 검증기가 같은 번호를
 *   독립적으로 다시 만들려면 C 가 파일을 읽은 순서까지 똑같이 흉내 내야 한다.
 *   사전순은 입력 순서와 무관하므로 어느 쪽이 먼저 읽든 같은 번호가 나온다.
 *   Step6 에서 글자 번호를 등장 순서가 아니라 코드포인트 오름차순으로 매긴 것과 같은 이유다.
 *
 *   정렬 기준은 strcmp = **UTF-8 바이트 순서**다. 한글 사전순과는 다르지만,
 *   Python 쪽에서 똑같이 바이트로 비교하면 결과가 일치한다. 기준이 무엇이냐보다
 *   양쪽이 같은 기준을 쓰느냐가 중요하다.
 * ===================================================================== */
static int cmp_vocab(const void *a, const void *b)
{
    const Vocab *x = (const Vocab *)a;
    const Vocab *y = (const Vocab *)b;
    return strcmp(x->text, y->text);
}

static void assign_ids(void)
{
    qsort(g_vocab, (size_t)g_vocab_n, sizeof(Vocab), cmp_vocab);
    for (int i = 0; i < g_vocab_n; i++)
        g_vocab[i].id = ID_BASE + i;
}

/* =====================================================================
 * [6] 어휘표 출력
 *
 * [목적] 다음 Phase 가 그대로 읽을 수 있는 표를 남기고, 사람도 확인할 수 있게 한다.
 * [동작] 탭으로 나눈 `번호 / 토큰 / 빈도` 세 칸.
 * [이유] 토큰 안에 탭이나 줄바꿈이 들어갈 수 없으므로(정제가 막았다) 탭 구분이 안전하다.
 *   BOS/EOS 는 이 파일에 나오지 않는 토큰이라 빈도가 0 이다. 그래도 표에 넣는 것은
 *   다음 Phase 가 번호 체계를 다시 만들지 않고 이 파일만 읽으면 되게 하려는 것이다.
 * ===================================================================== */
static int write_vocab(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;

    fprintf(fp, "# STEP 7 Phase 3-a: 어휘표 (토큰 -> 번호)\n");
    fprintf(fp, "#\n");
    fprintf(fp, "# 각 줄은 <번호> <탭> <토큰> <탭> <빈도> 이다.\n");
    fprintf(fp, "# 번호는 토큰을 UTF-8 바이트 사전순으로 정렬한 뒤 2번부터 붙인 것이다.\n");
    fprintf(fp, "# (0 = BOS 문장 시작, 1 = EOS 문장 끝. 둘 다 입력 파일에는 없고\n");
    fprintf(fp, "#  문장 앞뒤에 붙이는 것은 다음 Phase 의 몫이라 빈도가 0 이다)\n");
    fprintf(fp, "#\n");
    fprintf(fp, "# <S> 는 공백 토큰이다. 특별 취급하지 않고 번호 하나를 받는다.\n");
    fprintf(fp, "#\n");
    fprintf(fp, "# 어휘 %d종 (BOS/EOS 포함 %d) / 토큰 총수 %lld / 문장 %lld\n",
            g_vocab_n, g_vocab_n + 2, g_tokens, g_sentences);
    fprintf(fp, "#\n");
    fprintf(fp, "%d\t%s\t%d\n", ID_BOS, "BOS", 0);
    fprintf(fp, "%d\t%s\t%d\n", ID_EOS, "EOS", 0);
    for (int i = 0; i < g_vocab_n; i++)
        fprintf(fp, "%d\t%s\t%lld\n", g_vocab[i].id, g_vocab[i].text, g_vocab[i].freq);

    fclose(fp);
    return 1;
}

/* =====================================================================
 * [7] 요약 보고서
 *
 * [목적] 사람이 숫자만 보고 "제대로 읽었나"를 판단할 수 있게 한다.
 * [이유] 어휘표는 1만 7천 줄이라 눈으로 훑을 수 없다. 규모와 상위 빈도만 따로 뽑는다.
 * ===================================================================== */
static int cmp_by_freq(const void *a, const void *b)
{
    const Vocab *x = (const Vocab *)a;
    const Vocab *y = (const Vocab *)b;
    if (x->freq != y->freq) return (y->freq > x->freq) ? 1 : -1;
    return strcmp(x->text, y->text);
}

static int write_report(const char *path, const char *in_path, double load)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;

    long long once = 0;
    for (int i = 0; i < g_vocab_n; i++) if (g_vocab[i].freq == 1) once++;

    fprintf(fp, "=== STEP 7 Phase 3-a: 토큰 번호 매기기 결과 ===\n\n");
    fprintf(fp, "--- 이 파일이 무엇인가 ---\n");
    fprintf(fp, "토큰 파일(%s)을 C 가 읽어 토큰마다 정수 번호를 붙인 결과다.\n", in_path);
    fprintf(fp, "n그램은 아직 세지 않는다. 다음 Phase(3-b)의 일이다.\n\n");
    fprintf(fp, "번호를 붙이는 이유: n그램은 문자열이 아니라 정수를 센다.\n");
    fprintf(fp, "`세기/NNG` 를 그대로 비교하면 글자를 하나씩 맞춰 봐야 하지만,\n");
    fprintf(fp, "번호로 바꾸면 정수 비교 한 번이면 된다.\n\n");
    fprintf(fp, "어휘표는 %s 에 있다.\n\n", VOCAB_DEFAULT);

    fprintf(fp, "--- 규모 ---\n");
    fprintf(fp, "문장 수            : %lld\n", g_sentences);
    fprintf(fp, "토큰 총수(<S> 포함) : %lld\n", g_tokens);
    fprintf(fp, "어휘 종류          : %d  (BOS/EOS 포함 %d)\n", g_vocab_n, g_vocab_n + 2);
    fprintf(fp, "  그중 1회만 등장  : %lld (%.1f%%)\n",
            once, 100.0 * (double)once / (double)g_vocab_n);
    fprintf(fp, "평균 토큰/문장     : %.2f\n",
            (double)g_tokens / (double)g_sentences);
    fprintf(fp, "\n");
    fprintf(fp, "--- 해시 테이블 ---\n");
    fprintf(fp, "칸 수   : %u (2^%d)\n", HASH_SIZE, HASH_BITS);
    fprintf(fp, "적재율  : %.1f%% (상한 %d%%)\n", load, LOAD_LIMIT);
    fprintf(fp, "  적재율이 높으면 충돌이 늘어 한 칸을 찾는 데 여러 번 비교하게 된다.\n");
    fprintf(fp, "\n");

    /* 빈도 상위는 번호 순서를 흩뜨리지 않도록 복사본에서 정렬한다 */
    Vocab *tmp = (Vocab *)malloc((size_t)g_vocab_n * sizeof(Vocab));
    if (tmp) {
        memcpy(tmp, g_vocab, (size_t)g_vocab_n * sizeof(Vocab));
        qsort(tmp, (size_t)g_vocab_n, sizeof(Vocab), cmp_by_freq);
        fprintf(fp, "--- 빈도 상위 20 ---\n");
        int top = g_vocab_n < 20 ? g_vocab_n : 20;
        for (int i = 0; i < top; i++)
            fprintf(fp, "  %4d  %-16s %8lld\n", tmp[i].id, tmp[i].text, tmp[i].freq);
        free(tmp);
    }
    fprintf(fp, "\n--- 번호 순서 맨 앞 10 ---\n");
    for (int i = 0; i < (g_vocab_n < 10 ? g_vocab_n : 10); i++)
        fprintf(fp, "  %4d  %-16s %8lld\n", g_vocab[i].id, g_vocab[i].text, g_vocab[i].freq);

    fclose(fp);
    return 1;
}

/* =====================================================================
 * [8] main
 * ===================================================================== */
int main(int argc, char **argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);        /* 한글 출력. 컴파일의 /utf-8 과 둘 다 필요하다 */
#endif
    const char *in_path     = (argc > 1) ? argv[1] : IN_DEFAULT;
    const char *vocab_path  = (argc > 2) ? argv[2] : VOCAB_DEFAULT;
    const char *report_path = (argc > 3) ? argv[3] : REPORT_DEFAULT;

    size_t len = 0;
    char *text = read_whole(in_path, &len);
    if (!text) {
        fprintf(stderr, "입력 파일을 열 수 없습니다: %s\n", in_path);
        fprintf(stderr, "먼저 step7-2b_tokenize.py 를 실행하세요.\n");
        return 1;
    }

    g_slot = (int *)malloc(HASH_SIZE * sizeof(int));
    if (!g_slot) { fprintf(stderr, "메모리 부족 (해시 테이블)\n"); free(text); return 1; }
    for (unsigned int i = 0; i < HASH_SIZE; i++) g_slot[i] = -1;

    scan_tokens(text, len);
    free(text);

    /* 적재율 검사: 넘치면 조용히 느려지는 대신 멈춘다 */
    double load = 100.0 * (double)g_vocab_n / (double)HASH_SIZE;
    if (load > (double)LOAD_LIMIT) {
        fprintf(stderr, "해시 적재율 %.1f%% > 상한 %d%%. HASH_BITS 를 키우세요.\n",
                load, LOAD_LIMIT);
        free(g_slot); arena_free(); free(g_vocab);
        return 1;
    }

    assign_ids();

    if (!write_vocab(vocab_path)) {
        fprintf(stderr, "어휘표를 쓸 수 없습니다: %s\n", vocab_path);
        free(g_slot); arena_free(); free(g_vocab);
        return 1;
    }
    if (!write_report(report_path, in_path, load)) {
        fprintf(stderr, "보고서를 쓸 수 없습니다: %s\n", report_path);
        free(g_slot); arena_free(); free(g_vocab);
        return 1;
    }

    printf("문장 %lld / 토큰 %lld / 어휘 %d종 (BOS/EOS 포함 %d)\n",
           g_sentences, g_tokens, g_vocab_n, g_vocab_n + 2);
    printf("해시 적재율 %.1f%%\n", load);
    printf("저장: %s, %s\n", vocab_path, report_path);

    free(g_slot);
    arena_free();
    free(g_vocab);
    return 0;
}
