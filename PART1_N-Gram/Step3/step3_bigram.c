/*
 * step3_bigram.c - [STEP 3] 바이그램: 세 가지 자료구조 구현을 같은 조건으로 실행·비교한다
 *
 * ===== 목적 =====
 * 바이그램은 "앞 글자가 주어졌을 때 다음 글자"의 확률 P(다음 | 앞)로 글자를 만든다.
 *   P(b | a) = count(a, b) / count(a, ·)
 * 같은 모델을 자료구조만 바꿔 세 번 구현하고(step3_bigram.h 참고), 이 파일이
 * 세 구현을 똑같은 입력·똑같은 seed로 돌려 시간·메모리·결과를 비교한다.
 *
 * ===== 전체 흐름 =====
 *   [1] words.txt 읽기 -> 실제 나온 글자에 번호 매기기 (세 구현 공통)
 *   [2] 모든 단어를 BOS ... EOS로 감싼 번호 배열(seq) 만들기
 *   [3] 구현마다: 학습(build) -> 20개 생성 -> 대량 생성 -> 탐욕적 디코딩, 각각 시간 측정
 *   [4] 세 구현의 결과가 같은지 비교
 *   [5] 결과 파일(step3_bigram_result_NN.txt) + 전체 빈도표(step3_bigram_table.txt) 저장
 *
 * ===== 사용법 =====
 *   step3_bigram.exe [생성개수] [seed] [결과파일] [빈도표파일]
 *     기본값: 20  (현재 시각)  (자동 번호)  step3_bigram_table.txt
 *
 *   검증용 모드 (step3_verify.py가 사용. <구현> = array | hash | csr)
 *   step3_bigram.exe --dump <구현>                        모든 쌍 "앞 \t 뒤 \t 빈도"
 *   step3_bigram.exe --rows <구현>                        행 합계 "앞 \t 합계"
 *   step3_bigram.exe --map  <구현>                        표준입력 "앞 r" -> 뽑힌 글자
 *   step3_bigram.exe --hist <구현> <샘플수> <seed>        표준입력 "앞" 한 행에서 대량으로 뽑은 횟수
 *
 * ===== 빌드 =====
 *   build.bat        (step3_bigram.c + step3_bigram_*.c 를 함께 컴파일)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "step3_bigram.h"

#define DATA_PATH      "../../data/korean_dictionary/words.txt"
#define OUTPUT_PREFIX  "step3_bigram_result_"   /* 뒤에 01 ~ 99 + ".txt" */
#define DEFAULT_TABLE  "step3_bigram_table.txt"
#define MAX_RUN_NO     99
#define DEFAULT_COUNT  20      /* 구현마다 만드는 샘플 단어 수 */
#define BULK_COUNT     10000   /* 시간 비교용 대량 생성 단어 수 */
#define MAX_LEN        20      /* 단어 최대 길이. 넘으면 잘린 단어로 표시 */

#define HANGUL_BASE    0xAC00u
#define HANGUL_COUNT   11172   /* 가(U+AC00) ~ 힣(U+D7A3) */
#define NAME_BYTES     8       /* 토큰 이름: UTF-8 한 글자(3바이트) 또는 "BOS"/"EOS" + 널 */

#define RAND30_RANGE   (1ULL << 30)

/* 세 구현. 이 순서대로 실행하고 결과 파일에도 이 순서로 쓴다 */
static const BigramModel *const g_models[] = { &model_array, &model_hash, &model_csr };
#define MODEL_COUNT ((int)(sizeof g_models / sizeof g_models[0]))

/* ================================================================== */
/* 시간 측정                                                            */
/*                                                                     */
/* [목적] 구현별 학습·생성 시간을 ms 단위로 잰다.                        */
/* [이유] 표준 clock()은 Windows에서 1ms 단위라서 20개 생성처럼 수 μs~수백 */
/*   μs 걸리는 작업은 0으로 나온다. Windows 고정밀 타이머                 */
/*   QueryPerformanceCounter(보통 100ns 단위)를 쓰고, 다른 OS에서는      */
/*   clock()으로 대신한다 (표준 C 유지 규칙: Windows 전용은 #ifdef로).   */
/* ================================================================== */
static double now_ms(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);   /* 1초에 몇 번 세는 타이머인지 */
    }
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
#endif
}

/* ================================================================== */
/* 난수 (Step2와 같은 코드)                                             */
/*                                                                     */
/* rand30   : MSVC rand()는 15비트(0~32,767)뿐이라 두 번 이어 붙여 30비트 */
/*            를 만든다. BOS 행 합계(420,309)처럼 32,767보다 큰 범위에서  */
/*            뽑아야 하기 때문이다. hi, lo를 따로 받는 이유: 한 식에 rand()를 */
/*            두 번 쓰면 호출 순서가 보장되지 않아 seed 재현이 깨질 수 있다. */
/* rand_below: 0 ~ n-1 균등 난수. "rand30 % n"만 하면 앞쪽 값이 조금 더    */
/*            자주 나오므로(모듈로 편향) n의 배수를 넘는 값은 버리고 다시. */
/* ================================================================== */
static unsigned long long rand30(void)
{
    unsigned long long hi = (unsigned long long)(rand() & 0x7FFF);
    unsigned long long lo = (unsigned long long)(rand() & 0x7FFF);
    return (hi << 15) | lo;
}

static long long rand_below(long long n)
{
    unsigned long long un = (unsigned long long)n;
    unsigned long long limit = RAND30_RANGE - (RAND30_RANGE % un);
    unsigned long long x;

    do {
        x = rand30();
    } while (x >= limit);
    return (long long)(x % un);
}

/* ================================================================== */
/* UTF-8 (Step1과 같은 원리)                                            */
/*                                                                     */
/* 한글 한 글자는 UTF-8로 3바이트다. '가' = EA B0 80                     */
/*   1110 1010 / 10 110000 / 10 000000                                  */
/*   첫 바이트의 앞 4비트(1110) = "3바이트 글자", 뒤 4비트가 데이터       */
/*   나머지 바이트의 앞 2비트(10) = "이어지는 바이트", 뒤 6비트가 데이터  */
/*   데이터 비트를 이어 붙이면 1010 110000 000000 = 0xAC00 = '가'        */
/* ================================================================== */
static int utf8_len(unsigned char b)
{
    if (b < 0x80u)            return 1;   /* 0xxxxxxx : ASCII */
    if ((b & 0xE0u) == 0xC0u) return 2;   /* 110xxxxx */
    if ((b & 0xF0u) == 0xE0u) return 3;   /* 1110xxxx : 한글이 여기 */
    if ((b & 0xF8u) == 0xF0u) return 4;   /* 11110xxx */
    return 0;                             /* 10xxxxxx 는 첫 바이트가 될 수 없다 */
}

static unsigned utf8_decode(const unsigned char *p, int len)
{
    unsigned cp;
    int i;

    switch (len) {
        case 1: return p[0];
        case 2: cp = p[0] & 0x1Fu; break;
        case 3: cp = p[0] & 0x0Fu; break;
        case 4: cp = p[0] & 0x07u; break;
        default: return 0;
    }
    for (i = 1; i < len; i++) {
        cp = (cp << 6) | (p[i] & 0x3Fu);   /* 앞 2비트(10)를 떼고 6비트씩 이어 붙인다 */
    }
    return cp;
}

/* 한글 코드포인트(U+0800 ~ U+FFFF 범위)를 3바이트 UTF-8 문자열로 */
static void utf8_encode3(unsigned cp, char out[NAME_BYTES])
{
    out[0] = (char)(0xE0u | (cp >> 12));
    out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[2] = (char)(0x80u | (cp & 0x3Fu));
    out[3] = '\0';
}

/* ================================================================== */
/* [1][2] 코퍼스: words.txt -> 글자 번호 -> seq                          */
/* ================================================================== */
static char  *g_text     = NULL;   /* words.txt 전체. 줄 끝을 '\0'으로 바꿔 단어 문자열로도 쓴다 */
static char **g_words    = NULL;   /* 단어 문자열 포인터 (사전에 있는 단어인지 확인용, 정렬됨) */
static long long g_word_count = 0;
static long long g_char_count = 0;

static int  g_cp_to_id[HANGUL_COUNT];   /* (코드포인트 - 0xAC00) -> 번호. 안 나온 글자는 -1 */
static char (*g_names)[NAME_BYTES] = NULL;   /* 번호 -> 출력용 이름 ("가", "BOS", ...) */
static int  g_chars = 0;   /* 실제 나온 글자 종류 수 */
static int  g_vocab = 0;   /* g_chars + 2 */
static int  g_bos   = 0;
static int  g_eos   = 0;

static int      *g_seq   = NULL;   /* [BOS 나 비 EOS BOS 사 과 EOS ...] */
static long long g_seq_n = 0;

/* ------------------------------------------------------------------ */
/* 파일 전체를 메모리로 읽는다                                          */
/* [이유] 약 5MB라 한 번에 읽는 게 가장 단순하다. 두 번 훑어야 하므로     */
/*   (1차: 어떤 글자가 나오나 / 2차: 번호로 바꾸기) 메모리에 두면 파일을  */
/*   다시 열 필요가 없다. "rb"로 여는 이유: 텍스트 모드는 Windows에서     */
/*   \r\n을 \n으로 바꾸는 등 바이트를 건드린다.                         */
/* ------------------------------------------------------------------ */
static long load_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    long size;

    if (!fp) {
        fprintf(stderr, "[오류] 데이터 파일을 열 수 없습니다: %s\n", path);
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    g_text = (char *)malloc((size_t)size + 1u);
    if (!g_text || fread(g_text, 1, (size_t)size, fp) != (size_t)size) {
        fprintf(stderr, "[오류] 데이터 파일을 읽을 수 없습니다\n");
        fclose(fp);
        return -1;
    }
    g_text[size] = '\0';
    fclose(fp);
    return size;
}

/* ------------------------------------------------------------------ */
/* 1차: 단어마다 글자를 해석해 코드포인트 배열(cps)에 담고, 쓰인 글자 표시 */
/*                                                                     */
/* [동작] "나비\n사과\n" -> cps = [나 비 -1 사 과 -1]  (-1 = 단어 끝)     */
/*   동시에 줄 끝 '\n'을 '\0'으로 바꿔 g_words[k]가 "나비" 문자열이 되게 */
/*   한다 (생성한 단어가 사전에 있는지 찾을 때 쓴다).                     */
/* [이유] 번호는 "실제 나온 글자 전체"를 알아야 매길 수 있으므로        */
/*   (코드포인트 순서로 0, 1, 2 ...) 한 번 다 훑어본 뒤에 매긴다.        */
/*   한글이 아닌 글자가 있으면 오류로 멈춘다. 이 데이터는 전부 한글로    */
/*   확인돼 있고(STEP 1), 다른 글자가 섞이면 번호 체계가 깨지기 때문이다. */
/* ------------------------------------------------------------------ */
static int scan_words(long size, int *cps, long long *n_cps, int *used)
{
    long i = 0;
    long long n = 0;
    long long w = 0;

    while (i < size) {
        long start = i;
        long end = i;

        while (end < size && g_text[end] != '\n') {
            end++;
        }
        /* 줄 끝 정리: git(core.autocrlf)이 \r\n으로 바꿔 놓았을 수 있다 */
        g_text[end] = '\0';
        if (end > start && g_text[end - 1] == '\r') {
            g_text[end - 1] = '\0';
        }

        if (g_text[start] != '\0') {   /* 빈 줄은 건너뛴다 */
            long p = start;
            g_words[w++] = g_text + start;
            while (g_text[p] != '\0') {
                int len = utf8_len((unsigned char)g_text[p]);
                unsigned cp = (len == 3) ? utf8_decode((const unsigned char *)g_text + p, 3) : 0;
                if (cp < HANGUL_BASE || cp >= HANGUL_BASE + HANGUL_COUNT) {
                    fprintf(stderr, "[오류] 한글이 아닌 글자가 있습니다 (단어: %s)\n", g_text + start);
                    return 0;
                }
                used[cp - HANGUL_BASE] = 1;
                cps[n++] = (int)cp;
                p += len;
            }
            cps[n++] = -1;   /* 단어 끝 */
        }
        i = end + 1;
    }
    *n_cps = n;
    g_word_count = w;
    g_char_count = n - w;   /* 단어 끝 표시(-1) 개수를 뺀 것이 글자 수 */
    return 1;
}

/* 단어 정렬·검색용 비교 함수. 바이트 순 비교면 충분하다 (같은지만 보면 되므로) */
static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* ------------------------------------------------------------------ */
/* 코퍼스 준비 전체: 읽기 -> 1차 훑기 -> 번호 매기기 -> seq 만들기       */
/*                                                                     */
/* [번호 매기기] 코드포인트 오름차순으로 쓰인 글자에만 0, 1, 2 ...       */
/*   예: 데이터에 '가', '각', '나'만 있다면 가=0, 각=1, 나=2, BOS=3, EOS=4 */
/*   "압축 번호"다. 11,172자 중 실제 나온 2,450자만 번호를 받는다.        */
/* [seq] cps의 각 글자를 번호로 바꾸고, 단어마다 앞에 BOS, 뒤에 EOS.     */
/*   cps = [나 비 -1 사 과 -1] -> seq = [BOS 나 비 EOS BOS 사 과 EOS]    */
/*   seq 길이 = 글자 수 + 단어 수 x 2                                   */
/* ------------------------------------------------------------------ */
static int prepare_corpus(int need_dictionary)
{
    static int used[HANGUL_COUNT];
    long size = load_file(DATA_PATH);
    int *cps;
    long long n_cps = 0;
    long long lines = 1;   /* 마지막 줄에 \n이 없을 수도 있어 1부터 */
    long long i;
    long long k = 0;
    int c;

    if (size < 0) {
        return 0;
    }
    for (i = 0; i < size; i++) {
        lines += (g_text[i] == '\n');
    }
    /* 글자·단어 끝 표시 수는 바이트 수를 넘을 수 없다 (한 글자 >= 1바이트, 줄마다 \n 1바이트) */
    cps     = (int *)malloc(((size_t)size + 1u) * sizeof(int));
    g_words = (char **)malloc((size_t)lines * sizeof(char *));   /* 단어 수 <= 줄 수 */
    if (!cps || !g_words) {
        fprintf(stderr, "[오류] 메모리 할당 실패\n");
        return 0;
    }
    if (!scan_words(size, cps, &n_cps, used)) {
        return 0;
    }

    /* 번호 매기기: 코드포인트 오름차순으로 훑으며 쓰인 글자에만 번호 */
    g_chars = 0;
    for (c = 0; c < HANGUL_COUNT; c++) {
        g_cp_to_id[c] = used[c] ? g_chars++ : -1;
    }
    g_bos   = g_chars;       /* 한글 번호(0 ~ g_chars-1) 바로 다음. 절대 겹치지 않는다 */
    g_eos   = g_chars + 1;
    g_vocab = g_chars + 2;

    /* 번호 -> 이름 (출력용) */
    g_names = malloc((size_t)g_vocab * sizeof *g_names);
    if (!g_names) {
        return 0;
    }
    for (c = 0; c < HANGUL_COUNT; c++) {
        if (g_cp_to_id[c] >= 0) {
            utf8_encode3(HANGUL_BASE + (unsigned)c, g_names[g_cp_to_id[c]]);
        }
    }
    strcpy(g_names[g_bos], "BOS");
    strcpy(g_names[g_eos], "EOS");

    /* seq 만들기 */
    g_seq_n = g_char_count + g_word_count * 2;
    g_seq   = (int *)malloc((size_t)g_seq_n * sizeof(int));
    if (!g_seq) {
        return 0;
    }
    g_seq[k++] = g_bos;   /* 첫 단어의 BOS */
    for (i = 0; i < n_cps; i++) {
        if (cps[i] == -1) {
            g_seq[k++] = g_eos;
            if (i + 1 < n_cps) {
                g_seq[k++] = g_bos;   /* 다음 단어가 있으면 그 단어의 BOS */
            }
        } else {
            g_seq[k++] = g_cp_to_id[(unsigned)cps[i] - HANGUL_BASE];
        }
    }
    free(cps);

    /* 사전 검색용 정렬 (생성 모드에서만 필요) */
    if (need_dictionary) {
        qsort(g_words, (size_t)g_word_count, sizeof(char *), cmp_str);
    }
    return 1;
}

/* 사전에 있는 단어인가: 정렬된 단어 목록에서 이진 탐색 */
static int in_dictionary(const char *word)
{
    return bsearch(&word, g_words, (size_t)g_word_count, sizeof(char *), cmp_str) != NULL;
}

/* 이름("가", "BOS") -> 번호. 검증 모드에서 표준입력·인자를 해석할 때 쓴다. 모르면 -1 */
static int name_to_id(const char *s)
{
    unsigned cp;

    if (strcmp(s, "BOS") == 0) return g_bos;
    if (strcmp(s, "EOS") == 0) return g_eos;
    if (utf8_len((unsigned char)s[0]) != 3 || strlen(s) != 3) return -1;
    cp = utf8_decode((const unsigned char *)s, 3);
    if (cp < HANGUL_BASE || cp >= HANGUL_BASE + HANGUL_COUNT) return -1;
    return g_cp_to_id[cp - HANGUL_BASE];
}

/* ================================================================== */
/* [3] 단어 생성                                                        */
/*                                                                     */
/* [동작] BOS에서 출발해 "다음 글자 뽑기"를 반복, EOS가 뽑히면 끝.        */
/*   확률적: r = 0 ~ (prev 행 합계 - 1) 난수 -> 구현의 pick(prev, r)      */
/*   탐욕적: 구현의 greedy(prev) (난수 없음)                             */
/*   예: BOS -> 사 -> 회 -> EOS  => "사회"                               */
/* [이유] 난수를 구현 안이 아니라 여기서 뽑는 까닭: 세 구현이 완전히       */
/*   같은 r 순서를 받게 하려는 것이다. 그래야 같은 seed에서 결과가 같은지  */
/*   비교할 수 있다.                                                    */
/*   최대 길이: 탐욕적 디코딩은 루프(예: 리->다->리->다)에 빠지면 EOS를   */
/*   영영 못 만날 수 있어서 MAX_LEN자에서 끊고 잘림(truncated)으로 표시.  */
/*   EOS인지 먼저 확인한 뒤 길이를 검사하므로, 딱 20자에서 EOS가 나온     */
/*   단어는 잘린 것으로 치지 않는다.                                     */
/* ================================================================== */
typedef struct {
    int len;
    int truncated;
    int ids[MAX_LEN];
} Word;

static int generate_word(const BigramModel *m, int greedy, Word *w)
{
    int prev = g_bos;

    w->len = 0;
    w->truncated = 0;
    for (;;) {
        int next = greedy ? m->greedy(prev)
                          : m->pick(prev, rand_below(m->row_total(prev)));
        if (next < 0) {
            fprintf(stderr, "[오류] %s: 다음 글자를 찾지 못했습니다 (앞 글자 %s)\n",
                    m->name, g_names[prev]);
            return 0;
        }
        if (next == g_eos) {
            break;
        }
        if (w->len == MAX_LEN) {
            w->truncated = 1;
            break;
        }
        w->ids[w->len++] = next;
        prev = next;
    }
    return 1;
}

static void word_to_string(const Word *w, char *buf)
{
    int i;
    buf[0] = '\0';
    for (i = 0; i < w->len; i++) {
        strcat(buf, g_names[w->ids[i]]);
    }
}

/* ================================================================== */
/* [3] 구현 하나 실행: 학습 -> 20개 생성 -> 대량 생성 -> 탐욕적, 시간 측정 */
/*                                                                     */
/* [대량 생성을 따로 재는 이유] 20개 생성은 구현에 따라 수 μs ~ 수 ms라   */
/*   측정할 때마다 흔들린다. 10,000개를 만들면 차이가 분명해진다.         */
/*   대량 생성 결과는 저장하지 않고 체크섬(FNV-1a)만 남겨 세 구현이       */
/*   같은 단어들을 만들었는지 비교한다.                                  */
/* [같은 seed] 세 구현 모두 생성 직전에 srand(seed)를 다시 호출한다.      */
/*   그래야 세 구현이 같은 난수열을 받는다.                              */
/* ================================================================== */
typedef struct {
    double build_ms;
    double gen_ms;
    double bulk_ms;
    size_t memory;
    Word   greedy;
    Word  *samples;
    unsigned long long bulk_checksum;
    long long bulk_chars;
} RunResult;

/* FNV-1a: 값을 하나씩 섞어 넣는 간단한 해시. 같은 단어열이면 같은 값 */
static unsigned long long fnv_mix(unsigned long long h, int v)
{
    h ^= (unsigned long long)(unsigned)v;
    return h * 1099511628211ULL;
}

static int write_table(const BigramModel *m, const char *path, long long *unique_pairs);

static int run_model(const BigramModel *m, int count, unsigned seed, RunResult *res,
                     const char *table_path, long long *unique_pairs)
{
    double t0, t1;
    Word w;
    int k;
    int j;

    /* 학습 */
    t0 = now_ms();
    if (!m->build(g_seq, g_seq_n, g_vocab, g_eos)) {
        return 0;
    }
    t1 = now_ms();
    res->build_ms = t1 - t0;
    res->memory = m->memory();

    /* 20개 생성 */
    srand(seed);
    t0 = now_ms();
    for (k = 0; k < count; k++) {
        if (!generate_word(m, 0, &res->samples[k])) {
            return 0;
        }
    }
    t1 = now_ms();
    res->gen_ms = t1 - t0;

    /* 대량 생성 (체크섬만) */
    srand(seed);
    res->bulk_checksum = 14695981039346656037ULL;   /* FNV-1a 시작값 */
    res->bulk_chars = 0;
    t0 = now_ms();
    for (k = 0; k < BULK_COUNT; k++) {
        if (!generate_word(m, 0, &w)) {
            return 0;
        }
        for (j = 0; j < w.len; j++) {
            res->bulk_checksum = fnv_mix(res->bulk_checksum, w.ids[j]);
        }
        res->bulk_checksum = fnv_mix(res->bulk_checksum, -1);   /* 단어 구분 */
        res->bulk_chars += w.len;
    }
    t1 = now_ms();
    res->bulk_ms = t1 - t0;

    /* 탐욕적 디코딩 (난수 없음) */
    if (!generate_word(m, 1, &res->greedy)) {
        return 0;
    }

    /* 빈도표는 마지막 구현(csr)이 학습된 상태에서 한 번만 쓴다 (세 구현의 표는 같아야 하므로) */
    if (m == g_models[MODEL_COUNT - 1] && !write_table(m, table_path, unique_pairs)) {
        return 0;
    }

    m->release();
    return 1;
}

/* 두 단어가 같은가 */
static int same_word(const Word *a, const Word *b)
{
    return a->len == b->len && a->truncated == b->truncated
        && memcmp(a->ids, b->ids, (size_t)a->len * sizeof(int)) == 0;
}

/* ================================================================== */
/* [5] 전체 빈도표 파일 (step3_bigram_table.txt)                         */
/*                                                                     */
/* [형식] 한 줄 = 쌍 하나. "앞 \t 뒤 \t 빈도 \t 그 행 안에서의 확률(%)"   */
/*   행 순서: BOS 먼저, 그다음 글자 번호(= 가나다) 순. EOS 행은 없다       */
/*   (EOS 뒤에는 아무것도 오지 않는다).                                 */
/*   행 안 순서: 빈도 내림차순, 같으면 번호 오름차순.                    */
/* [이유] 2,189칸짜리 BOS 행을 가로로 쓰면 읽을 수 없어서 쌍마다 한 줄로  */
/*   쓴다 (사용자 결정). 0인 쌍은 쓰지 않는다. 확률을 소수 6자리로 쓰는   */
/*   이유: 소수 2자리면 빈도 1짜리(예: 1/420,309)가 0.00%로 보인다.      */
/* ================================================================== */
typedef struct {
    int next;
    long long count;
} NextCount;

static int cmp_next_count(const void *x, const void *y)
{
    const NextCount *a = (const NextCount *)x;
    const NextCount *b = (const NextCount *)y;
    if (a->count != b->count) {
        return (a->count < b->count) ? 1 : -1;   /* 빈도 내림차순 */
    }
    return (a->next > b->next) - (a->next < b->next);   /* 번호 오름차순 */
}

static int write_table(const BigramModel *m, const char *path, long long *unique_pairs)
{
    NextCount *row = (NextCount *)malloc((size_t)g_vocab * sizeof(NextCount));
    FILE *out = fopen(path, "wb");   /* "wb": UTF-8 바이트와 LF를 그대로 쓴다 */
    long long unique = 0;
    long long total = 0;
    int r;

    if (!row || !out) {
        fprintf(stderr, "[오류] 빈도표 파일을 쓸 수 없습니다: %s\n", path);
        free(row);
        if (out) fclose(out);
        return 0;
    }

    /* 헤더에 서로 다른 쌍 수를 쓰려면 먼저 세야 한다 */
    for (r = 0; r < g_vocab; r++) {
        int b;
        for (b = 0; b < g_vocab; b++) {
            long long c = m->count(r, b);
            if (c > 0) {
                unique++;
                total += c;
            }
        }
    }

    fprintf(out, "# 바이그램 빈도표\n");
    fprintf(out, "# input_file\t%s\n", DATA_PATH);
    fprintf(out, "# words\t%lld\n", g_word_count);
    fprintf(out, "# total_pairs\t%lld\n", total);
    fprintf(out, "# unique_pairs\t%lld\n", unique);
    fprintf(out, "# rows\t%d\n", g_chars + 1);   /* 글자 행 + BOS 행 */
    fprintf(out, "#\n");
    fprintf(out, "# prev\tnext\tcount\tpercent\n");

    /* 행 순서: BOS 먼저, 그다음 글자 0 ~ g_chars-1 */
    for (r = -1; r < g_chars; r++) {
        int prev = (r < 0) ? g_bos : r;
        long long row_total = m->row_total(prev);
        int n = 0;
        int b;
        int i;

        for (b = 0; b < g_vocab; b++) {
            long long c = m->count(prev, b);
            if (c > 0) {
                row[n].next = b;
                row[n].count = c;
                n++;
            }
        }
        qsort(row, (size_t)n, sizeof(NextCount), cmp_next_count);
        for (i = 0; i < n; i++) {
            fprintf(out, "%s\t%s\t%lld\t%.6f\n", g_names[prev], g_names[row[i].next],
                    row[i].count, 100.0 * (double)row[i].count / (double)row_total);
        }
    }

    fclose(out);
    free(row);
    *unique_pairs = unique;
    return 1;
}

/* ================================================================== */
/* 결과 파일 자동 번호 (Step2와 같은 규칙)                              */
/* 01 ~ 99 중 "가장 큰 기존 번호 + 1". 지운 번호를 다시 채우지 않아       */
/* 번호 순서 = 실행 순서가 된다. 표준 C에 폴더 목록 함수가 없어 fopen으로. */
/* ================================================================== */
static int next_output_path(char *buf, size_t size)
{
    int n;
    int last = 0;

    for (n = 1; n <= MAX_RUN_NO; n++) {
        FILE *fp;
        snprintf(buf, size, OUTPUT_PREFIX "%02d.txt", n);
        fp = fopen(buf, "rb");
        if (fp) {
            fclose(fp);
            last = n;
        }
    }
    if (last >= MAX_RUN_NO) {
        fprintf(stderr, "[오류] 결과 파일 번호가 %d까지 찼습니다. 기존 %s*.txt 를 정리하세요\n",
                MAX_RUN_NO, OUTPUT_PREFIX);
        return 0;
    }
    snprintf(buf, size, OUTPUT_PREFIX "%02d.txt", last + 1);
    return 1;
}

/* ================================================================== */
/* 검증용 모드 (step3_verify.py가 호출)                                 */
/*                                                                     */
/* [목적] 검증기가 구현 내부를 들여다보지 않고 출력만으로 확인할 수 있게  */
/*   한다. 세 구현 각각에 대해 따로 호출된다.                            */
/*   --dump : 모든 쌍의 빈도 (Python 재계산과 전수 비교)                 */
/*   --rows : 행 합계 (난수 범위가 맞는지)                               */
/*   --map  : "앞 r" -> 뽑힌 글자 (구간 경계에서 한 칸 어긋남 검사)      */
/*   --hist : 한 행에서 대량으로 뽑은 횟수 (분포가 맞는지 카이제곱)       */
/* ================================================================== */
static const BigramModel *find_model(const char *name)
{
    int i;
    for (i = 0; i < MODEL_COUNT; i++) {
        if (strcmp(g_models[i]->name, name) == 0) {
            return g_models[i];
        }
    }
    fprintf(stderr, "[오류] 구현 이름은 array / hash / csr 중 하나여야 합니다: %s\n", name);
    return NULL;
}

static int mode_dump(const BigramModel *m)
{
    int a;
    int b;
    for (a = 0; a < g_vocab; a++) {
        for (b = 0; b < g_vocab; b++) {
            long long c = m->count(a, b);
            if (c > 0) {
                printf("%s\t%s\t%lld\n", g_names[a], g_names[b], c);
            }
        }
    }
    return 0;
}

static int mode_rows(const BigramModel *m)
{
    int a;
    for (a = 0; a < g_vocab; a++) {
        printf("%s\t%lld\n", g_names[a], m->row_total(a));
    }
    return 0;
}

static int mode_map(const BigramModel *m)
{
    char name[32];
    long long r;

    while (scanf("%31s %lld", name, &r) == 2) {
        int prev = name_to_id(name);
        if (prev < 0 || r < 0 || r >= m->row_total(prev)) {
            fprintf(stderr, "[오류] 잘못된 질의: %s %lld\n", name, r);
            return 1;
        }
        printf("%s\n", g_names[m->pick(prev, r)]);
    }
    return 0;
}

/*
 * 앞 글자를 명령줄 인자가 아니라 표준입력으로 받는 이유:
 * Windows는 main의 argv를 시스템 코드페이지(CP949)로 넘겨준다. Python이 '사'를 인자로 주면
 * C에는 UTF-8이 아닌 CP949 바이트가 들어와 글자를 찾지 못한다. 표준입력은 보낸 바이트(UTF-8)가
 * 그대로 오므로 --map과 같은 방식으로 받는다.
 */
static int mode_hist(const BigramModel *m, long long n, unsigned seed)
{
    long long *hist = (long long *)calloc((size_t)g_vocab, sizeof(long long));
    char prev_name[32] = "";
    int prev;
    long long total;
    long long k;
    int b;

    if (scanf("%31s", prev_name) != 1) {
        fprintf(stderr, "[오류] 표준입력으로 앞 글자를 주세요\n");
        free(hist);
        return 1;
    }
    prev  = name_to_id(prev_name);
    total = (prev >= 0) ? m->row_total(prev) : 0;
    if (!hist || total <= 0) {
        fprintf(stderr, "[오류] 앞 글자를 찾을 수 없습니다: %s\n", prev_name);
        free(hist);
        return 1;
    }
    srand(seed);
    for (k = 0; k < n; k++) {
        hist[m->pick(prev, rand_below(total))]++;
    }
    for (b = 0; b < g_vocab; b++) {
        if (hist[b] > 0) {
            printf("%s\t%lld\n", g_names[b], hist[b]);
        }
    }
    free(hist);
    return 0;
}

static int run_check_mode(int argc, char **argv)
{
    const BigramModel *m;
    int rc = 1;

    if (argc < 3 || (strcmp(argv[1], "--hist") == 0 && argc < 5)) {
        fprintf(stderr, "사용법: --dump|--rows|--map <구현>  /  --hist <구현> <샘플수> <seed> (앞 글자는 표준입력)\n");
        return 1;
    }
    m = find_model(argv[2]);
    if (!m || !prepare_corpus(0)) {
        return 1;
    }
    if (!m->build(g_seq, g_seq_n, g_vocab, g_eos)) {
        return 1;
    }
    if (strcmp(argv[1], "--dump") == 0)      rc = mode_dump(m);
    else if (strcmp(argv[1], "--rows") == 0) rc = mode_rows(m);
    else if (strcmp(argv[1], "--map") == 0)  rc = mode_map(m);
    else if (strcmp(argv[1], "--hist") == 0)
        rc = mode_hist(m, atoll(argv[3]), (unsigned)strtoul(argv[4], NULL, 10));
    else fprintf(stderr, "[오류] 알 수 없는 모드: %s\n", argv[1]);
    m->release();
    return rc;
}

/* ================================================================== */
/* [4][5] 결과 출력                                                      */
/* ================================================================== */
static void write_word_line(FILE *out, const char *key, const Word *w, char *buf)
{
    word_to_string(w, buf);
    printf("%6s  %s%s%s\n", key, buf, in_dictionary(buf) ? "  *" : "", w->truncated ? "  ~" : "");
    fprintf(out, "%s\t%s%s%s\n", key, buf, in_dictionary(buf) ? "\t*" : "", w->truncated ? "\t~" : "");
}

/* ================================================================== */
int main(int argc, char **argv)
{
    static RunResult results[MODEL_COUNT];
    char auto_path[64];
    char word_buf[MAX_LEN * 4 + 1];   /* 한 글자 최대 3바이트 + 여유 */
    const char *out_path;
    const char *table_path;
    unsigned seed;
    int count;
    int same = 1;
    long long unique_pairs = 0;
    FILE *out;
    int i;
    int k;

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);   /* 콘솔 한글 출력. 컴파일 옵션 /utf-8과 둘 다 필요 */
#endif

    if (argc > 1 && strncmp(argv[1], "--", 2) == 0) {
        return run_check_mode(argc, argv);
    }

    /* --- 인자 --- */
    count      = (argc > 1) ? atoi(argv[1]) : DEFAULT_COUNT;
    seed       = (argc > 2) ? (unsigned)strtoul(argv[2], NULL, 10) : (unsigned)time(NULL);
    out_path   = (argc > 3) ? argv[3] : NULL;
    table_path = (argc > 4) ? argv[4] : DEFAULT_TABLE;
    if (count <= 0) {
        count = DEFAULT_COUNT;
    }
    if (out_path == NULL) {
        if (!next_output_path(auto_path, sizeof auto_path)) {
            return 1;
        }
        out_path = auto_path;
    }

    /* --- [1][2] 코퍼스 --- */
    if (!prepare_corpus(1)) {
        return 1;
    }
    printf("데이터     : %s\n", DATA_PATH);
    printf("단어 / 글자: %lld / %lld (글자 종류 %d, BOS/EOS 포함 번호 %d개)\n",
           g_word_count, g_char_count, g_chars, g_vocab);
    printf("seed       : %u\n\n", seed);

    /* --- [3] 세 구현 실행 --- */
    for (i = 0; i < MODEL_COUNT; i++) {
        results[i].samples = (Word *)malloc((size_t)count * sizeof(Word));
        if (!results[i].samples) {
            return 1;
        }
        printf("[%s] %s ...\n", g_models[i]->name, g_models[i]->desc);
        if (!run_model(g_models[i], count, seed, &results[i], table_path, &unique_pairs)) {
            return 1;
        }
    }

    /* --- [4] 세 구현 결과 비교: 첫 구현(array)을 기준으로 나머지가 같은가 --- */
    for (i = 1; i < MODEL_COUNT; i++) {
        same &= same_word(&results[i].greedy, &results[0].greedy);
        same &= results[i].bulk_checksum == results[0].bulk_checksum;
        same &= results[i].bulk_chars == results[0].bulk_chars;
        for (k = 0; k < count; k++) {
            same &= same_word(&results[i].samples[k], &results[0].samples[k]);
        }
    }

    /* --- [5] 결과 파일 --- */
    out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "[오류] 출력 파일을 쓸 수 없습니다: %s\n", out_path);
        return 1;
    }
    fprintf(out, "# 바이그램 생성 결과\n");
    fprintf(out, "# input_file\t%s\n", DATA_PATH);
    fprintf(out, "# words\t%lld\n", g_word_count);
    fprintf(out, "# chars\t%d\n", g_chars);
    fprintf(out, "# vocab\t%d\n", g_vocab);
    fprintf(out, "# total_pairs\t%lld\n", g_char_count + g_word_count);
    fprintf(out, "# unique_pairs\t%lld\n", unique_pairs);
    fprintf(out, "# seed\t%u\n", seed);
    fprintf(out, "# count\t%d\n", count);
    fprintf(out, "# bulk_count\t%d\n", BULK_COUNT);
    fprintf(out, "# max_len\t%d\n", MAX_LEN);
    fprintf(out, "# table_file\t%s\n", table_path);
    fprintf(out, "#\n");
    fprintf(out, "# version\tbuild_ms\tgen%d_ms\tgen%d_ms\tmemory_bytes\n", count, BULK_COUNT);

    printf("\n%-6s %12s %12s %14s %14s\n", "구현", "학습(ms)", "생성20(ms)", "생성10000(ms)", "메모리(bytes)");
    for (i = 0; i < MODEL_COUNT; i++) {
        const RunResult *r = &results[i];
        fprintf(out, "# %s\t%.3f\t%.3f\t%.3f\t%zu\n",
                g_models[i]->name, r->build_ms, r->gen_ms, r->bulk_ms, r->memory);
        printf("%-6s %12.3f %12.3f %14.3f %14zu\n",
               g_models[i]->name, r->build_ms, r->gen_ms, r->bulk_ms, r->memory);
    }
    fprintf(out, "#\n# same_output\t%s\n", same ? "YES" : "NO");
    fprintf(out, "# mark\t* = 사전에 있는 단어, ~ = 최대 길이에서 잘림\n");
    printf("\n세 구현 결과 일치: %s (20개 샘플 + 대량 생성 체크섬 + 탐욕적)\n", same ? "YES" : "NO");

    for (i = 0; i < MODEL_COUNT; i++) {
        fprintf(out, "\n[%s] %s\n", g_models[i]->name, g_models[i]->desc);
        printf("\n[%s] %s\n", g_models[i]->name, g_models[i]->desc);
        write_word_line(out, "greedy", &results[i].greedy, word_buf);
        for (k = 0; k < count; k++) {
            char key[16];
            snprintf(key, sizeof key, "%d", k + 1);
            write_word_line(out, key, &results[i].samples[k], word_buf);
        }
    }
    fclose(out);

    printf("\n결과 파일 : %s\n", out_path);
    printf("빈도표    : %s (서로 다른 쌍 %lld개)\n", table_path, unique_pairs);
    return same ? 0 : 1;
}
