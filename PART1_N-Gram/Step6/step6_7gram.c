/*
 * step6_7gram.c - [STEP 6] 7그램 문장 생성 (해시테이블만 사용, 단일 파일)
 *
 * ===== 이 파일의 목적 =====
 * 7그램은 앞 6글자를 보고 다음 글자를 뽑는다.
 *   P(다음 | 앞1..앞6) = count(앞1..앞6, 다음) / count(앞1..앞6, ·)
 * 바로 앞 n의 파일(step6_5gram.c)을 복사해서 문맥을 앞 6글자로 늘렸다.
 * 단, 7그램은 키를 숫자 하나로 만들 수 없어서(64비트를 넘음) 해시테이블 칸에
 * "문맥"과 "다음 글자"를 따로 저장한다 ([7]). 이 차이가 5그램과의 가장 큰 차이다.
 *
 * ===== Step5에서는 7그램을 할 수 없었다 =====
 * Step5(단어 생성)는 n <= 6이 한계였다. 글자 종류가 2,450 + BOS/EOS = 2,452여서
 * 문맥 V^6 = 2.17 x 10^20 이 64비트 최댓값 1.8447 x 10^19 을 약 12배 넘었기 때문이다.
 * 문장 데이터는 글자 종류가 1,596 + BOS/EOS = 1,598로 작아져서
 *   V^6 = 1.6652 x 10^19  <  2^64 = 1.8447 x 10^19   (여유 1.108배)
 * 로 간신히 들어간다. 그래서 강의 2-8이 예시로 든 7그램이 가능해졌다.
 *
 * 여유가 11%뿐이므로 글자 종류가 늘면 바로 터진다.
 *   V^6 < 2^64  <=>  V <= 1,625   즉 글자 종류 1,623종이 상한 (현재 1,596종)
 * 넘치면 서로 다른 7그램이 조용히 같은 키로 합쳐지고 오류도 나지 않는다.
 * 그래서 [5]의 check_key_range가 실행할 때마다 V를 확인한다.
 * (n=8은 문맥이 V^7 = 2.66 x 10^22 이라 이 데이터로도 불가능하다)
 *
 * ===== 단어 생성(Step5)과 달라지는 점 3가지 (강의 2-8) =====
 * 1) 공백도 글자다  2) BOS/EOS가 문장 경계  3) 글자 번호를 코드포인트 전 범위에서 매긴다
 * 자세한 설명은 step6_3gram.c 머리말에 있다.
 *
 * ===== 입력 =====
 * ../../data/novel_sentences.txt (한 줄 = 한 문장)
 * step6_prepare.py 가 원본 novel_test_data.txt를 종결부호로 쪼개 만든 파일이다.
 *
 * ===== 목차 (구역 번호 / 시작 줄 / 이름 / 주요 함수) =====
 *   [1]    74줄 | 설정값과 전역 상태   | DATA_PATH, HASH_BITS, g_seq, g_n_grams ...
 *   [2]   132줄 | 시간 측정            | now_ms
 *   [3]   153줄 | 난수                 | rand30, rand_below
 *   [4]   190줄 | UTF-8                | utf8_len, utf8_decode, utf8_encode
 *   [5]   251줄 | 코퍼스와 압축 번호   | load_file, scan_sents, prepare_corpus, in_corpus
 *   [6]   492줄 | seq 만들기           | build_seq
 *   [7]   542줄 | 해시테이블 (문맥 + 다음 글자 키) | ht_slot, ht_init, ht_add, ht_get, ht_free
 *   [8]   660줄 | 세기                 | count_grams, build_model
 *   [9]   732줄 | 뽑기                 | ctx_key, ctx_total, pick, greedy
 *   [10]  816줄 | 문장 생성            | generate_sent, sent_to_string
 *   [11]  894줄 | 원본 일치율          | match_add
 *   [12]  920줄 | 빈도표 파일          | sorted_grams, write_table
 *   [13] 1081줄 | 결과 파일 자동 번호  | next_output_path
 *   [14] 1113줄 | 검증용 모드          | mode_dump, mode_rows, mode_map, mode_hist
 *   [15] 1259줄 | main                 | main, write_result
 *   (줄 번호가 실제 구역 위치와 맞는지는 step6_verify.py [7]이 검사한다)
 *
 * ===== 사용법 =====
 *   step6_7gram.exe [생성개수] [seed] [결과파일] [빈도표파일]
 *     기본값: 15  (현재 시각)  (자동 번호 step6_7gram_result_NN.txt)  step6_7gram_table.txt
 *   검증용 모드 (step6_verify.py가 사용)
 *     step6_7gram.exe --dump                 모든 7그램 "앞1 ~ 앞6 \t 다음 \t 빈도"
 *     step6_7gram.exe --rows                 문맥 합계 "앞1 ~ 앞6 \t 합계"
 *     step6_7gram.exe --map                  표준입력 "앞1 ~ 앞6 r" -> 뽑힌 글자
 *     step6_7gram.exe --hist <샘플수> <seed>  표준입력 "앞1 ~ 앞6" 문맥에서 여러 번 뽑은 횟수
 *   공백은 글자이므로 검증 모드의 입출력에서는 <SP> 로 적는다 ([5] 표시 이름).
 *
 * ===== 빌드 =====
 *   build.bat step6_7gram
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* ================================================================== */
/* [1] 설정값과 전역 상태                                               */
/*                                                                     */
/* [목적] 파일 경로, 크기 상수, 그리고 여러 구역이 함께 쓰는 데이터를    */
/*   한곳에 모아 둔다.                                                  */
/* [동작] 코퍼스(novel_sentences.txt) -> g_seq(번호 배열) -> 해시테이블  */
/*   g_ht에 7그램 횟수를 센다 ([8]). 문장을 만들 때도 g_ht만 조회한다.   */
/* [이유] 단일 파일이라 구역끼리 넘겨줄 값이 많다. 함수 인자로 전부      */
/*   넘기면 읽기 어려워져서 "프로그램 전체에 하나뿐인 데이터"는 전역     */
/*   (g_ 접두사)으로 두었다.                                            */
/*   g_ht는 [7]의 HashTable 형식이 있어야 선언할 수 있어서 [8]에 둔다.   */
/* [5그램과 달라진 점] 이름(결과·빈도표 파일)과 HASH_BITS(21 -> 22).     */
/*   서로 다른 7그램이 1,319,850개로 5그램(821,162개)의 약 1.6배다.      */
/* ================================================================== */
#define DATA_PATH      "../../data/novel_sentences.txt"
#define OUTPUT_PREFIX  "step6_7gram_result_"   /* 뒤에 01 ~ 99 + ".txt" */
#define DEFAULT_TABLE  "step6_7gram_table.txt"
#define MAX_RUN_NO     99
#define DEFAULT_COUNT  15      /* 생성 문장 수. 강의 2-8 예시처럼 눈으로 읽을 만큼만 */
#define MAX_LEN        400     /* 문장 최대 글자수. 넘으면 잘린 문장으로 표시 */

/* 글자 코드포인트의 상한. 이 데이터의 글자는 U+0020 ~ U+FF63 이므로 2^16으로 덮인다.
   (Python 독립 계산으로 전수 확인: 1바이트 80종 / 2바이트 1종(NBSP) / 3바이트 1,515종)
   4바이트 UTF-8(U+10000 이상, 이모지 등)이 나오면 [5]에서 오류로 멈춘다. */
#define CP_LIMIT       0x10000u

#define NAME_BYTES     12      /* 표시 이름: UTF-8 한 글자(최대 3바이트), "BOS", "<NBSP>", "<U+001F>" + 널 */

#define RAND30_RANGE   (1ULL << 30)

#define HASH_BITS      22      /* 2^22 = 4,194,304칸. 서로 다른 7그램 1,319,850개 -> 적재율 약 31.5% */
                               /* (2^21칸이면 62.9%로 Step4의 거부선 70%에 너무 가깝다) */

/* --- 코퍼스 --- */
static char  *g_text     = NULL;   /* 파일 전체. 줄 끝을 '\0'으로 바꿔 문장 문자열로도 쓴다 */
static char **g_sents    = NULL;   /* 문장 시작 위치 (g_text 안을 가리킨다) */
static long long g_sent_count = 0;
static long long g_char_count = 0;

/* --- 글자 번호 (압축 번호) --- */
static int  g_chars = 0;    /* 실제로 나온 글자 종류 수 */
static int  g_bos   = 0;    /* = g_chars     (글자 번호 바로 다음) */
static int  g_eos   = 0;    /* = g_chars + 1 */
static int  g_vocab = 0;    /* = g_chars + 2 (BOS/EOS까지 포함한 번호 개수 V) */

static int  g_cp_to_id[CP_LIMIT];            /* 코드포인트 -> 번호. 안 나온 글자는 -1 */
static unsigned *g_id_to_cp = NULL;          /* 번호 -> 코드포인트 (문장 출력용). BOS/EOS는 0 */
static char (*g_names)[NAME_BYTES] = NULL;   /* 번호 -> 표시 이름 ("가", "<SP>", "BOS") */

/* --- seq: 코퍼스 전체를 번호 하나씩으로 늘어놓은 배열 ([6]) --- */
static int *g_seq = NULL;
static long long g_seq_n = 0;

/* --- 모델 통계. 모델 자체(해시테이블 g_ht)는 [8]에 있다 --- */
static int  g_n_grams   = 0;      /* 서로 다른 7그램 수 = 해시테이블의 채워진 칸 수 */
static int  g_n_ctx     = 0;      /* 서로 다른 문맥 (앞1 ~ 앞6) 수. [12]에서 센다 */
static long long g_total_grams = 0;   /* 7그램 총 개수 (중복 포함) */

/* ================================================================== */
/* [2] 시간 측정                                                        */
/*                                                                     */
/* [목적] 학습(세기)과 생성에 걸린 시간을 ms 단위로 잰다.                */
/* [동작] Windows에서는 고정밀 타이머 QueryPerformanceCounter의 눈금을    */
/*   초당 눈금수로 나눠 ms로 바꾼다. 그 밖에서는 clock()을 쓴다.         */
/* [이유] 표준 clock()은 Windows에서 1ms 단위라 짧은 작업이 0으로 나온다. */
/* ================================================================== */
static double now_ms(void)
{
#ifdef _WIN32
    LARGE_INTEGER freq;
    LARGE_INTEGER now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
#endif
}

/* ================================================================== */
/* [3] 난수                                                             */
/*                                                                     */
/* [목적] 0 ~ n-1 범위의 균등 난수를 만든다. 다음 글자를 뽑을 때 쓴다.   */
/* [동작] rand30: rand()를 두 번 불러 15비트 + 15비트 = 30비트 난수.      */
/*   rand_below: 30비트 난수 중 n의 배수 경계(limit)를 넘는 값은 버리고   */
/*   다시 뽑은 뒤 % n.                                                  */
/* [이유] MSVC rand()는 0 ~ 32,767(15비트)뿐인데 문맥 (BOS x 6)의 합계는  */
/*   문장 수 66,961이라 범위가 모자란다. hi, lo를 따로 받는 까닭: 한 식에 */
/*   rand()를 두 번 쓰면 호출 순서가 정해져 있지 않아 같은 seed에서도     */
/*   결과가 달라질 수 있다. 그냥 % n만 하면 앞쪽 값이 조금 더 자주        */
/*   나오므로(모듈로 편향) 경계를 넘는 값을 버린다.                      */
/* ================================================================== */
static unsigned long long rand30(void)
{
    unsigned long long hi = (unsigned long long)rand();   /* 0 ~ 32,767 */
    unsigned long long lo = (unsigned long long)rand();
    return (hi << 15) | lo;                               /* 0 ~ 2^30 - 1 */
}

static long long rand_below(long long n)
{
    unsigned long long limit;

    if (n <= 1) {
        return 0;
    }
    /* n의 배수 중 2^30 이하인 가장 큰 값. 이 선을 넘는 난수는 버린다 */
    limit = RAND30_RANGE - (RAND30_RANGE % (unsigned long long)n);
    for (;;) {
        unsigned long long r = rand30();
        if (r < limit) {
            return (long long)(r % (unsigned long long)n);
        }
    }
}

/* ================================================================== */
/* [4] UTF-8                                                            */
/*                                                                     */
/* [목적] 파일의 바이트를 글자(코드포인트)로 풀고, 출력할 때 다시        */
/*   바이트로 만든다.                                                   */
/* [동작] 글자 하나가 1 ~ 3바이트다.                                    */
/*   ' '  = 20                   (1바이트, ASCII)                       */
/*   NBSP = C2 A0                (2바이트)                              */
/*   '가' = EA B0 80             (3바이트)                              */
/*   3바이트의 비트 구조: 1110 1010 / 10 110000 / 10 000000             */
/*     첫 바이트 앞 4비트(1110) = "3바이트 글자", 뒤 4비트가 데이터     */
/*     나머지 바이트 앞 2비트(10) = "이어지는 바이트", 뒤 6비트가 데이터 */
/*     데이터 비트를 이어 붙이면 1010 110000 000000 = 0xAC00 = '가'     */
/* [이유] 표준 C에는 UTF-8 해석 함수가 없다. 필요한 만큼만 직접 만든다.  */
/* [5그램과 달라진 점] 없다.                                            */
/* ================================================================== */
static int utf8_len(unsigned char b)
{
    if (b < 0x80u)            return 1;   /* 0xxxxxxx : ASCII */
    if ((b & 0xE0u) == 0xC0u) return 2;   /* 110xxxxx */
    if ((b & 0xF0u) == 0xE0u) return 3;   /* 1110xxxx : 한글이 여기 */
    if ((b & 0xF8u) == 0xF0u) return 4;   /* 11110xxx : U+10000 이상 (이 데이터엔 없다) */
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

/* 코드포인트를 UTF-8 문자열로 (decode의 반대 방향). 1 ~ 3바이트 */
static void utf8_encode(unsigned cp, char *out)
{
    if (cp < 0x80u) {
        out[0] = (char)cp;
        out[1] = '\0';
    } else if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        out[2] = '\0';
    } else {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        out[3] = '\0';
    }
}

/* ================================================================== */
/* [5] 코퍼스와 압축 번호                                               */
/*                                                                     */
/* [목적] novel_sentences.txt를 읽어 문장 목록을 만들고, 실제로 나온     */
/*   글자에만 번호(압축 번호)를 매긴다.                                 */
/* [동작] 1) 파일 전체를 메모리로 읽는다                                 */
/*   2) 문장마다 글자를 코드포인트로 풀어 cps에 담고, 쓰인 글자를 표시    */
/*      "나비 산다\n사과\n" -> cps = [나 비 ' ' 산 다 -1 사 과 -1]        */
/*      (-1 = 문장 끝. 공백은 버리지 않고 하나의 글자로 넣는다)          */
/*   3) 코드포인트 오름차순으로 쓰인 글자에만 0, 1, 2 ... 를 준다          */
/*      ' '=0, '!'=1, ... (1,596종), 그다음 BOS=1596, EOS=1597            */
/* [이유] 코드포인트를 그대로 번호로 쓰면 U+FFFF까지 65,536개가 되어      */
/*   문맥 최댓값이 65,536^6 = 8.3 x 10^28 로 64비트를 아득히 넘는다.      */
/*   실제로 나온 1,596개만 번호를 받게 하면 V가 1,598로 줄어 문맥 V^6 이  */
/*   64비트에 들어간다. 즉 이 압축 번호가 7그램을 가능하게 하는 핵심이다.  */
/*   번호를 코드포인트 순서로 매기는 까닭(등장 순서가 아니라): 실행마다   */
/*   같은 번호가 나와야 같은 seed에서 결과가 재현된다. 후보를 번호        */
/*   순서로 훑어 누적하므로([9]) 번호가 바뀌면 뽑히는 글자도 바뀐다.      */
/*   파일은 "rb"로 연다: 텍스트 모드는 Windows에서 \r\n을 바꾼다.        */
/* [5그램과 달라진 점] check_key_range가 "키 V^5"가 아니라 "문맥 V^6"을   */
/*   검사한다. 키를 쪼갰으므로 64비트에 들어가야 하는 값이 문맥이다 ([7]). */
/* ================================================================== */
static long load_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    long size;

    if (!fp) {
        fprintf(stderr, "[오류] 데이터 파일을 열 수 없습니다: %s\n", path);
        fprintf(stderr, "       step6_prepare.py 를 먼저 실행하세요\n");
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

/* 글자의 표시 이름. 눈에 보이지 않는 글자는 꺾쇠로 적는다.
   데이터에 '<' 와 '>' 가 없음을 Python 독립 계산으로 확인했으므로 실제 글자와
   겹치지 않는다 (겹치면 빈도표를 되읽을 때 구분할 수 없다). */
static void disp_name(unsigned cp, char out[NAME_BYTES])
{
    if (cp == 0x20u) {                      /* 스페이스: 실측 437,534회로 가장 흔한 글자 */
        strcpy(out, "<SP>");
    } else if (cp == 0xA0u) {               /* NBSP: 실측 268회. 눈에는 공백처럼 보인다 */
        strcpy(out, "<NBSP>");
    } else if (cp < 0x20u || cp == 0x7Fu) { /* 제어문자 (이 데이터엔 없지만 방어) */
        snprintf(out, NAME_BYTES, "<U+%04X>", cp);
    } else {
        utf8_encode(cp, out);
    }
}

/* 문장마다 글자를 풀어 cps에 담는다. 줄 끝 '\n'은 '\0'으로 바꿔 g_sents[k]가 문자열이 되게 한다 */
static int scan_sents(long size, int *cps, long long *n_cps, char *used)
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
        g_text[end] = '\0';
        if (end > start && g_text[end - 1] == '\r') {   /* git이 \r\n으로 바꿔 놓았을 수 있다 */
            g_text[end - 1] = '\0';
        }

        if (g_text[start] != '\0') {   /* 빈 줄은 건너뛴다 */
            long p = start;
            g_sents[w++] = g_text + start;
            while (g_text[p] != '\0') {
                int len = utf8_len((unsigned char)g_text[p]);
                unsigned cp;
                if (len < 1 || len > 3) {
                    /* 4바이트 UTF-8(U+10000 이상)이나 깨진 바이트. 번호 체계가 CP_LIMIT 전제 */
                    fprintf(stderr, "[오류] 다룰 수 없는 바이트가 있습니다 (문장: %s)\n", g_text + start);
                    return 0;
                }
                cp = utf8_decode((const unsigned char *)g_text + p, len);
                if (cp >= CP_LIMIT) {
                    fprintf(stderr, "[오류] U+%04X 는 범위를 넘습니다 (문장: %s)\n", cp, g_text + start);
                    return 0;
                }
                used[cp] = 1;
                cps[n++] = (int)cp;
                p += len;
            }
            cps[n++] = -1;   /* 문장 끝 */
        }
        i = end + 1;
    }
    *n_cps = n;
    g_sent_count = w;
    g_char_count = n - w;   /* 문장 끝 표시(-1) 개수를 뺀 것이 글자 수 */
    return 1;
}

/* 문장 정렬·검색용 비교. 같은지만 보면 되므로 바이트 순 비교면 충분하다 */
static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int build_seq(const int *cps, long long n_cps);   /* [6] */

/* 문맥이 64비트에 들어가는지 확인한다. 7그램 문맥의 최댓값은 V^6 - 1.
   여유가 1.108배뿐이라(머리말) 이 검사가 특히 중요하다. 넘치면 서로 다른 7그램이
   조용히 같은 칸으로 합쳐지고 오류도 나지 않는다 ([7] 참고). */
static int check_key_range(void)
{
    const unsigned long long cap = 0xFFFFFFFFFFFFFFFFULL;
    const unsigned long long V = (unsigned long long)g_vocab;
    unsigned long long acc = 1;
    int i;

    /* V를 6번 곱하되, 곱하기 전에 "넘칠 것인가"를 나눗셈으로 확인한다.
       먼저 곱해 버리면 넘친 뒤라 검사 자체가 무의미해진다 */
    for (i = 0; i < 6; i++) {
        if (V == 0 || acc > cap / V) {
            fprintf(stderr, "[오류] 글자 종류가 너무 많아 7그램 문맥이 64비트를 넘습니다 (V = %d)\n", g_vocab);
            fprintf(stderr, "       V^6 < 2^64 이려면 V <= 1,625 (글자 종류 1,623종)이어야 합니다\n");
            return 0;
        }
        acc *= V;
    }
    return 1;
}

static int prepare_corpus(int need_sentences)
{
    static char used[CP_LIMIT];       /* 1칸 1바이트. 64KB. static이라 0으로 시작한다 */
    long size = load_file(DATA_PATH);
    int *cps;
    long long n_cps = 0;
    long long lines = 1;   /* 마지막 줄에 \n이 없을 수도 있어 1부터 */
    long long i;
    long c;

    if (size < 0) {
        return 0;
    }
    for (i = 0; i < size; i++) {
        lines += (g_text[i] == '\n');
    }
    /* 글자·문장 끝 표시 수는 바이트 수를 넘을 수 없다 (한 글자 >= 1바이트, 줄마다 \n 1바이트) */
    cps     = (int *)malloc(((size_t)size + 1u) * sizeof(int));
    g_sents = (char **)malloc((size_t)lines * sizeof(char *));
    if (!cps || !g_sents) {
        fprintf(stderr, "[오류] 메모리 할당 실패\n");
        return 0;
    }
    if (!scan_sents(size, cps, &n_cps, used)) {
        return 0;
    }

    /* 압축 번호: 코드포인트 오름차순으로 훑으며 쓰인 글자에만 번호 */
    g_chars = 0;
    for (c = 0; c < (long)CP_LIMIT; c++) {
        g_cp_to_id[c] = used[c] ? g_chars++ : -1;
    }
    g_bos   = g_chars;       /* 글자 번호(0 ~ g_chars-1) 바로 다음. 절대 겹치지 않는다 */
    g_eos   = g_chars + 1;
    g_vocab = g_chars + 2;

    if (!check_key_range()) {
        return 0;
    }

    /* 번호 -> 표시 이름 / 번호 -> 코드포인트 */
    g_names     = malloc((size_t)g_vocab * sizeof *g_names);
    g_id_to_cp  = malloc((size_t)g_vocab * sizeof *g_id_to_cp);
    if (!g_names || !g_id_to_cp) {
        fprintf(stderr, "[오류] 메모리 할당 실패\n");
        return 0;
    }
    for (c = 0; c < (long)CP_LIMIT; c++) {
        int id = g_cp_to_id[c];
        if (id >= 0) {
            disp_name((unsigned)c, g_names[id]);
            g_id_to_cp[id] = (unsigned)c;
        }
    }
    strcpy(g_names[g_bos], "BOS");
    strcpy(g_names[g_eos], "EOS");
    g_id_to_cp[g_bos] = 0;
    g_id_to_cp[g_eos] = 0;

    if (!build_seq(cps, n_cps)) {
        return 0;
    }
    free(cps);

    if (need_sentences) {   /* 원본 일치 조회(이진 탐색)용 정렬. 생성 모드에서만 필요 */
        qsort(g_sents, (size_t)g_sent_count, sizeof(char *), cmp_str);
    }
    return 1;
}

/* 생성한 문장이 원본에 통째로 있는가: 정렬된 문장 목록에서 이진 탐색 */
static int in_corpus(const char *sent)
{
    return bsearch(&sent, g_sents, (size_t)g_sent_count, sizeof(char *), cmp_str) != NULL;
}

/* 표시 이름("가", "<SP>", "BOS") -> 번호. 검증 모드에서 표준입력을 해석할 때 쓴다. 모르면 -1 */
static int name_to_id(const char *s)
{
    unsigned cp;
    int len;

    if (strcmp(s, "BOS") == 0) return g_bos;
    if (strcmp(s, "EOS") == 0) return g_eos;
    if (strcmp(s, "<SP>") == 0) return g_cp_to_id[0x20];
    if (strcmp(s, "<NBSP>") == 0) return g_cp_to_id[0xA0];
    if (s[0] == '<' && s[1] == 'U' && s[2] == '+') {
        unsigned v = 0;
        if (sscanf(s + 3, "%4X", &v) != 1 || v >= CP_LIMIT) return -1;
        return g_cp_to_id[v];
    }
    len = utf8_len((unsigned char)s[0]);
    if (len < 1 || len > 3 || strlen(s) != (size_t)len) return -1;
    cp = utf8_decode((const unsigned char *)s, len);
    if (cp >= CP_LIMIT) return -1;
    return g_cp_to_id[cp];
}

/* ================================================================== */
/* [6] seq 만들기                                                       */
/*                                                                     */
/* [목적] 문장마다 시작·끝 표시를 붙여 하나의 번호 배열로 잇는다.        */
/*   [7][8]은 이 배열을 한 칸씩 훑으며 7그램을 센다.                     */
/* [동작] 7그램은 앞에 BOS 6개, 뒤에 EOS 1개를 붙인다.                   */
/*   cps = [나 비 -1 사 -1]   (-1 = 문장 끝)                             */
/*   seq = [BOS x6 나 비 EOS BOS x6 사 EOS]                              */
/*   "나비"의 7그램: (BOS,BOS,BOS,BOS,BOS,BOS,나)                        */
/*       (BOS,BOS,BOS,BOS,BOS,나,비) (BOS,BOS,BOS,BOS,나,비,EOS)         */
/*   "사"의 7그램: (BOS x6, 사) (BOS x5, 사, EOS)                        */
/*   EOS에서 다음 문장 BOS로 넘어가는 묶음은 [8]에서 건너뛴다             */
/*   길이 L 문장 -> 7그램 L+1개 (n과 관계없이 같은 개수)                  */
/*   seq 길이 = 글자 수 + 문장 수 x 7   (BOS 6개 + EOS 1개)               */
/* [이유] BOS가 6개인 까닭: 문맥이 앞 6글자라서 첫 글자를 뽑을 때도       */
/*   6글자가 필요하다. 모자란 앞자리를 BOS로 채운다 (3그램의 BOS 2개와    */
/*   같은 이유). EOS는 1개면 된다: EOS가 뽑히면 생성이 끝난다.            */
/* [5그램과 달라진 점] BOS 개수 4개 -> 6개, seq 길이의 x 5 -> x 7.        */
/* ================================================================== */
static int build_seq(const int *cps, long long n_cps)
{
    long long i;
    long long k = 0;
    int j;

    g_seq_n = g_char_count + g_sent_count * 7;   /* 글자 + 문장마다 BOS 6개 + EOS 1개 */
    g_seq   = (int *)malloc((size_t)g_seq_n * sizeof(int));
    if (!g_seq) {
        fprintf(stderr, "[오류] seq 메모리 할당 실패\n");
        return 0;
    }
    for (j = 0; j < 6; j++) {   /* 첫 문장의 BOS 6개. 개수가 많아 반복문으로 썼다 */
        g_seq[k++] = g_bos;
    }
    for (i = 0; i < n_cps; i++) {
        if (cps[i] == -1) {
            g_seq[k++] = g_eos;
            if (i + 1 < n_cps) {
                for (j = 0; j < 6; j++) {   /* 다음 문장이 있으면 그 문장의 BOS 6개 */
                    g_seq[k++] = g_bos;
                }
            }
        } else {
            g_seq[k++] = g_cp_to_id[(unsigned)cps[i]];
        }
    }
    g_seq_n = k;
    return 1;
}

/* ================================================================== */
/* [7] 해시테이블 (문맥 + 다음 글자 키)                                 */
/*                                                                     */
/* [목적] "7그램 -> 횟수"를 센다. 실제로 나온 7그램만 칸을 차지한다.     */
/*   7차원 배열은 1598^7 ≈ 2.66 x 10^22칸이라 상상할 수 없는 크기다.      */
/*   실제로 나온 7그램은 1,319,850개뿐이다.                              */
/* [동작] 오픈 어드레싱 + 선형 탐사 (5그램까지와 같다)                   */
/*   다만 칸 하나에 키가 두 칸으로 들어 있다:                            */
/*     ctxs[칸]  = 문맥 (앞 6글자를 V진법 6자리 수로 읽은 값)            */
/*     nexts[칸] = 다음 글자 번호                                        */
/*   "같은 키인가" = 문맥도 같고 다음 글자도 같은가 (두 칸 모두 비교)     */
/*   첫 칸 번호(ht_slot) = (문맥 x V + 다음) 을 피보나치 해싱            */
/* [이유] 5그램까지는 키 = (앞 글자들, 다음)을 V진법 n자리 수 하나로      */
/*   만들었다. 7그램이면 최댓값이 1598^7 ≈ 2.66 x 10^22 으로 unsigned     */
/*   long long의 최댓값(약 1.8 x 10^19)을 약 1,400배 넘는다. 넘친 값은    */
/*   잘려서 서로 다른 7그램이 같은 키로 합쳐지고, 오류도 나지 않는다.     */
/*   그래서 문맥과 다음 글자를 나눠 저장한다. 문맥은 앞 6글자뿐이라       */
/*   최댓값 1598^6 - 1 ≈ 1.665 x 10^19 으로 64비트에 들어간다 (여유 1.108배). */
/*   한편 ht_slot의 (문맥 x V + 다음)은 넘쳐도 괜찮다. 이 값은 "어느 칸부터 */
/*   찾아볼지"만 정한다. 넘쳐서 두 7그램이 같은 값이 되면 같은 칸 근처에서 */
/*   출발할 뿐(충돌)이고, 같은 7그램인지는 문맥과 다음 글자를 직접 비교해 */
/*   판단하므로 섞이지 않는다. 즉 "해시 값"은 넘쳐도 되고 "키"는 안 된다. */
/*   빈 칸 표시는 ctxs[칸] = HT_EMPTY(64비트 최댓값). 실제 문맥의 최댓값  */
/*   (1.665 x 10^19)보다 커서 겹치지 않는다 -- 여유가 11%뿐이지만 겹치지  */
/*   않는다는 것은 확실하다.                                             */
/* [5그램과 달라진 점] 키 배열 1개(keys) -> 2개(ctxs, nexts).             */
/*   ht_slot / ht_add / ht_get이 (키) 대신 (문맥, 다음)을 받는다.         */
/* ================================================================== */
#define HT_EMPTY   0xFFFFFFFFFFFFFFFFULL
#define FIB_MULT   11400714819323198485ULL   /* 2^64 / 황금비 (Knuth) */

typedef struct {
    unsigned long long *ctxs;    /* 칸마다 문맥 (앞 6글자). 빈 칸은 HT_EMPTY */
    int                *nexts;   /* 칸마다 다음 글자 번호 */
    int                *counts;  /* 칸마다 횟수 */
    unsigned            bits;    /* 칸 수 = 2^bits */
    unsigned long long  mask;    /* = 2^bits - 1 */
    long long           used;    /* 채워진 칸 수 = 서로 다른 7그램 수 */
} HashTable;

static size_t ht_slot(const HashTable *ht, unsigned long long ctx, int next)
{
    /* 칸 번호만 정하는 값이라 넘쳐도 된다 (위 [이유]). 곱셈의 넘친 부분은 버려진다 */
    unsigned long long h = ctx * (unsigned long long)g_vocab + (unsigned long long)next;
    return (size_t)((h * FIB_MULT) >> (64u - ht->bits));
}

static int ht_init(HashTable *ht, unsigned bits)
{
    size_t size = (size_t)1u << bits;
    size_t i;

    ht->bits   = bits;
    ht->mask   = ((unsigned long long)1u << bits) - 1u;
    ht->used   = 0;
    ht->ctxs   = (unsigned long long *)malloc(size * sizeof(unsigned long long));
    ht->nexts  = (int *)malloc(size * sizeof(int));
    ht->counts = (int *)calloc(size, sizeof(int));
    if (!ht->ctxs || !ht->nexts || !ht->counts) {
        fprintf(stderr, "[오류] 해시테이블 메모리 할당 실패 (칸 %zu개)\n", size);
        return 0;
    }
    for (i = 0; i < size; i++) {
        ht->ctxs[i] = HT_EMPTY;
    }
    return 1;
}

static int ht_add(HashTable *ht, unsigned long long ctx, int next)
{
    size_t i = ht_slot(ht, ctx, next);

    for (;;) {
        if (ht->ctxs[i] == ctx && ht->nexts[i] == next) {   /* 문맥과 다음 글자가 모두 같아야 같은 7그램 */
            ht->counts[i]++;
            return 1;
        }
        if (ht->ctxs[i] == HT_EMPTY) {
            if ((unsigned long long)(ht->used + 1) * 10u > (ht->mask + 1u) * 7u) {
                fprintf(stderr, "[오류] 해시테이블이 70%% 넘게 찼습니다. HASH_BITS를 키우세요\n");
                return 0;
            }
            ht->ctxs[i]   = ctx;
            ht->nexts[i]  = next;
            ht->counts[i] = 1;
            ht->used++;
            return 1;
        }
        i = (size_t)((i + 1u) & ht->mask);   /* 충돌: 바로 옆 칸 (끝이면 0번으로) */
    }
}

/* (문맥, 다음)의 횟수. 없으면 0. 뽑기([9])가 후보마다 부른다 */
static int ht_get(const HashTable *ht, unsigned long long ctx, int next)
{
    size_t i = ht_slot(ht, ctx, next);

    for (;;) {
        if (ht->ctxs[i] == ctx && ht->nexts[i] == next) {
            return ht->counts[i];
        }
        if (ht->ctxs[i] == HT_EMPTY) {
            return 0;   /* 빈 칸을 만났다 = 이 7그램은 한 번도 넣은 적이 없다 */
        }
        i = (size_t)((i + 1u) & ht->mask);
    }
}

static void ht_free(HashTable *ht)
{
    free(ht->ctxs);
    free(ht->nexts);
    free(ht->counts);
    ht->ctxs   = NULL;
    ht->nexts  = NULL;
    ht->counts = NULL;
}

/* ================================================================== */
/* [8] 세기                                                             */
/*                                                                     */
/* [목적] 코퍼스의 모든 7그램을 해시테이블 g_ht에 센다. 이것이 모델 전부다. */
/* [동작] seq를 한 칸씩 훑으며 7칸 창을 읽는다.                          */
/*   문맥 = 앞 6글자를 V진법 6자리 수로 읽은 값, 다음 = 7번째 글자        */
/*   ht_add(문맥, 다음) 으로 센다 (키를 두 칸으로 나눈 이유는 [7])        */
/*   예: seq = [BOS BOS BOS BOS BOS BOS 그 러 ...] 라면                   */
/*     (BOS x6, 그) (BOS x5, 그, 러) ... 를 각각 +1                       */
/* [이유] 7차원 배열은 만들 수 없어서 실제로 나온 7그램만 저장하는         */
/*   해시테이블을 쓴다 ([7] 참고). 세기가 끝난 해시테이블을 그대로 생성    */
/*   단계에서 조회하므로 다른 배열로 옮기지 않는다.                       */
/*   창의 앞 6칸(문맥) 중 하나라도 EOS면 문장 경계를 넘은 묶음이다.        */
/*   실제 문맥이 아니라서 세지 않는다.                                    */
/* [5그램과 달라진 점] 창 5칸 -> 7칸, EOS 검사 4칸 -> 6칸.                */
/*   EOS 검사와 문맥 만들기를 반복문으로 바꿨다: 칸이 6개라 한 줄에 늘어   */
/*   놓으면 너무 길고, 빠뜨리기 쉽다.                                     */
/* ================================================================== */
static HashTable g_ht;   /* 모델: (문맥, 다음) -> 횟수 */

static int count_grams(void)
{
    const unsigned long long V = (unsigned long long)g_vocab;
    long long i;

    if (!ht_init(&g_ht, HASH_BITS)) {
        return 0;
    }
    for (i = 0; i + 6 < g_seq_n; i++) {
        unsigned long long ctx;
        int j;
        int crossed = 0;

        for (j = 0; j < 6; j++) {               /* 문맥 칸에 EOS = 문장 사이에 걸친 묶음 */
            if (g_seq[i + j] == g_eos) {
                crossed = 1;
                break;
            }
        }
        if (crossed) {
            continue;
        }
        /* 한 자리씩 V진법으로 붙인다. 곱하기 전에 64비트로 바꾼다 (int끼리 곱하면 먼저 넘친다) */
        ctx = (unsigned long long)g_seq[i];
        for (j = 1; j < 6; j++) {
            ctx = ctx * V + (unsigned long long)g_seq[i + j];
        }
        if (!ht_add(&g_ht, ctx, g_seq[i + 6])) {   /* 7번째 칸 = 다음 글자 */
            return 0;
        }
        g_total_grams++;
    }
    return 1;
}

static int build_model(void)
{
    g_total_grams = 0;
    if (!count_grams()) {
        return 0;
    }
    g_n_grams = (int)g_ht.used;   /* 채워진 칸 하나 = 서로 다른 7그램 하나 */
    return 1;
}

/* 생성 단계에 남는 메모리: 해시테이블의 칸 전부 (빈 칸 포함).
   칸마다 문맥 8바이트 + 다음 4바이트 + 횟수 4바이트 */
static size_t model_memory(void)
{
    return (size_t)(g_ht.mask + 1u) * (sizeof(unsigned long long) + sizeof(int) + sizeof(int));
}

/* ================================================================== */
/* [9] 뽑기                                                             */
/*                                                                     */
/* [목적] 지금 문맥(앞 6글자) 다음에 올 글자를 빈도에 비례해서 하나 뽑는다. */
/* [동작] 1) ctx_key: 문맥 = 앞 6글자를 V진법 6자리 수로 읽은 값          */
/*      후보 b의 횟수 = ht_get(문맥, b)                                   */
/*   2) ctx_total: b = 0 ~ 1,597 전부 해시에 물어 횟수를 더한다 = 문맥 합계 */
/*   3) r = 0 ~ (합계 - 1) 난수를 받는다                                   */
/*   4) pick: b = 0, 1, 2, ... 순서로 다시 물으며 횟수를 누적하다가        */
/*      "누적 > r" 이 되는 첫 b를 고른다                                  */
/*   예: 문맥 (BOS x6) 뒤에는 문장 첫 글자가 온다. 문장이 66,961개라        */
/*     합계 = 66,961, r = 0 ~ 66,960                                      */
/* [이유] 해시테이블은 "이 7그램이 몇 번?"에는 바로 답하지만 "이 문맥      */
/*   다음에 올 수 있는 후보 목록"은 모른다. 키가 칸 전체에 흩어져 있어서다. */
/*   그래서 가능한 다음 글자 1,598개를 전부 물어본다. 나온 적 없는 후보는  */
/*   0이 돌아와 누적이 그대로이므로 뽑히지 않는다.                        */
/*   합계를 따로 저장하지 않고 매번 1,598개를 물어 구한다 (사용자 결정    */
/*   (B): 자료구조를 해시테이블 하나로 유지).                             */
/*   '>' 비교가 핵심이다. '>='를 쓰면                                     */
/*     - 마지막 후보의 몫을 앞 후보가 가져가고                            */
/*     - r = 0일 때 번호가 앞선, 한 번도 안 나온 글자(누적 0 >= 0)가 뽑힌다 */
/*   탐욕적 디코딩도 '>'로 비교해 동률이면 번호가 작은 후보가 남는다.     */
/* [5그램과 달라진 점] 문맥 인자 4개 -> 6개. 키를 더하는 대신             */
/*   (문맥, b)로 조회한다. 문맥을 배열로 받는다: 인자가 6개면 호출이       */
/*   길어지고 창을 밀 때 대입이 6줄이라 실수하기 쉽다.                    */
/* ================================================================== */
static unsigned long long ctx_key(const int *prev)
{
    /* 앞 6글자를 V진법 6자리 수로. 곱하기 전에 64비트로 바꾼다 ([8]과 같은 이유) */
    unsigned long long V = (unsigned long long)g_vocab;
    unsigned long long c = (unsigned long long)prev[0];
    int i;

    for (i = 1; i < 6; i++) {
        c = c * V + (unsigned long long)prev[i];
    }
    return c;
}

/* 문맥 합계 = 이 문맥 뒤에 온 7그램 횟수의 합. 한 번도 안 나온 문맥이면 0 */
static long long ctx_total(const int *prev)
{
    unsigned long long ctx = ctx_key(prev);
    long long total = 0;
    int b;

    for (b = 0; b < g_vocab; b++) {
        total += ht_get(&g_ht, ctx, b);
    }
    return total;
}

static int pick(const int *prev, long long r)
{
    unsigned long long ctx = ctx_key(prev);
    long long cum = 0;
    int b;

    for (b = 0; b < g_vocab; b++) {
        cum += ht_get(&g_ht, ctx, b);
        if (cum > r) {        /* '>=' 가 아니다. 위 [이유] 참고 */
            return b;
        }
    }
    return -1;   /* r >= 합계: 부른 쪽의 잘못 */
}

static int greedy(const int *prev)
{
    unsigned long long ctx = ctx_key(prev);
    int best = -1;
    int best_count = 0;
    int b;

    for (b = 0; b < g_vocab; b++) {
        int c = ht_get(&g_ht, ctx, b);
        if (c > best_count) {   /* 동률이면 먼저 본 쪽(번호가 작은 쪽)을 남긴다 */
            best_count = c;
            best = b;
        }
    }
    return best;
}

/* ================================================================== */
/* [10] 문장 생성                                                       */
/*                                                                     */
/* [목적] 문맥에서 출발해 다음 글자 뽑기를 반복해 문장 하나를 만든다.     */
/* [동작] prev[0..5] = (BOS x6) 에서 출발                                 */
/*   -> 문맥 prev로 다음 글자 c를 뽑는다                                  */
/*   -> c가 EOS면 끝. 아니면 문장에 붙이고 문맥 창을 한 칸 민다            */
/*      prev[0..4] = prev[1..5], prev[5] = c                              */
/*      (가장 오래된 글자가 빠지고 새 글자가 들어옴)                      */
/*   예: (BOS x6) -> 그 / (BOS x5, 그) -> 러 / (BOS x4, 그, 러) -> 나      */
/* [이유] 탐욕적 디코딩은 루프에 빠지면 EOS를 영영 못 만날 수 있어        */
/*   MAX_LEN자에서 끊고 잘림(~)으로 표시한다. EOS인지 먼저 확인한 뒤 길이를 */
/*   검사하므로 딱 400자에서 끝난 문장은 잘림이 아니다.                   */
/*   문맥 합계가 0이 되는 일은 없다: 지금 문맥은 코퍼스 어떤 문장의 일부이고 */
/*   그 문장에서 이 문맥 뒤에도 글자나 EOS가 있었다. 그래도 방어로 검사한다 */
/*   (합계 0이면 rand_below(0)이 되어 뽑을 수 없다).                      */
/* [5그램과 달라진 점] 문맥 변수 4개 -> 배열 prev[6]. 창 밀기를 memmove가  */
/*   아니라 반복문으로 쓴 까닭: 겹치는 영역을 앞으로 당기는 것이라        */
/*   한 칸씩 앞에서부터 옮기면 안전하고, 무엇을 하는지도 눈에 보인다.      */
/* ================================================================== */
typedef struct {
    int len;
    int truncated;
    int ids[MAX_LEN];
} Sent;

static int generate_sent(int greedy_mode, Sent *s)
{
    int prev[6];
    int i;

    for (i = 0; i < 6; i++) {   /* 7그램 문맥: 앞 6글자. 처음엔 BOS 6개 */
        prev[i] = g_bos;
    }
    s->len = 0;
    s->truncated = 0;
    for (;;) {
        long long total = ctx_total(prev);
        int next;
        if (total == 0) {
            fprintf(stderr, "[오류] 한 번도 나오지 않은 문맥입니다 (%s, %s, %s, %s, %s, %s)\n",
                    g_names[prev[0]], g_names[prev[1]], g_names[prev[2]],
                    g_names[prev[3]], g_names[prev[4]], g_names[prev[5]]);
            return 0;
        }
        next = greedy_mode ? greedy(prev) : pick(prev, rand_below(total));
        if (next == g_eos) {
            break;
        }
        if (s->len == MAX_LEN) {
            s->truncated = 1;
            break;
        }
        s->ids[s->len++] = next;
        for (i = 0; i < 5; i++) {   /* 문맥 창을 한 칸 민다 */
            prev[i] = prev[i + 1];
        }
        prev[5] = next;             /* 맨 뒤에 새 글자 */
    }
    return 1;
}

/* 번호 배열 -> 사람이 읽는 문장. 표시 이름이 아니라 글자 그대로 쓴다
   (<SP>가 아니라 진짜 공백. 결과 파일은 사람이 문장을 읽는 용도다) */
static void sent_to_string(const Sent *s, char *buf)
{
    char one[NAME_BYTES];
    size_t at = 0;
    int i;

    for (i = 0; i < s->len; i++) {
        utf8_encode(g_id_to_cp[s->ids[i]], one);
        strcpy(buf + at, one);
        at += strlen(one);
    }
    buf[at] = '\0';
}

/* ================================================================== */
/* [11] 원본 일치율                                                     */
/*                                                                     */
/* [목적] 생성한 문장 중 novel_sentences.txt에 통째로 있는 문장의 비율.   */
/* [동작] 문장마다 in_corpus로 확인해 total / hits를 센다.                */
/* [이유] 강의 2-8이 지적한 "원본 매칭의 어려움"이 여기서 드러난다.       */
/*   이 지표는 "암기"를 재는 용도다. n이 커질수록 모델이 원본 문장을       */
/*   그대로 재현하는 쪽으로 가는지 보는 것이다. 7그램은 문맥이 6글자라     */
/*   짧은 문장(6글자 이하)은 문맥이 BOS 패딩으로 거의 다 덮여 원본 문장을  */
/*   그대로 복원할 수밖에 없다 (Step5에서 본 "길이 n-2 이하는 반드시       */
/*   사전 단어"와 같은 현상).                                             */
/* [5그램과 달라진 점] 없다.                                              */
/* ================================================================== */
typedef struct {
    long long total;
    long long hits;
} MatchStats;

static void match_add(MatchStats *st, int found)
{
    st->total++;
    if (found) {
        st->hits++;
    }
}

/* ================================================================== */
/* [12] 빈도표 파일 (step6_7gram_table.txt)                             */
/*                                                                     */
/* [목적] 모델이 센 7그램 전체를 사람이 읽을 수 있는 표로 남긴다.        */
/*   step6_verify.py [6]이 이 파일을 바이트 단위로 재현해 전수 비교한다.  */
/* [동작] 한 줄 = 7그램 하나                                              */
/*   "앞1 \t ... \t 앞6 \t 다음 \t 빈도 \t 문맥 안 확률(%)"                */
/*   1) sorted_grams: 해시테이블의 채워진 칸만 꺼내 표 순서로 정렬         */
/*      문맥 순서: (BOS x6) -> (BOS x5, <SP>) ... 순                       */
/*      즉 BOS를 가장 작은 값으로 보고 앞1 ~ 앞6 순으로 비교               */
/*      문맥 안 순서: 빈도 내림차순, 같으면 번호 오름차순                 */
/*   2) 정렬하면 같은 문맥의 줄이 붙어 있으므로, 문맥 하나씩 묶어 합계를   */
/*      구하고 확률 = 빈도 / 합계 로 쓴다                                 */
/* [이유] 해시테이블의 칸 순서는 해시 함수가 흩어 놓은 순서라 뒤죽박죽이다. */
/*   표를 읽을 수 있게, 그리고 실행할 때마다 같은 파일이 나오게 정렬한다.  */
/*   이 정렬은 표를 출력하기 위한 것일 뿐 뽑기([9])에는 쓰지 않는다.       */
/*   공백은 <SP>로 적는다: 표가 탭 구분인데 공백을 그대로 쓰면 빈 칸처럼   */
/*   보여 사람도 검증기도 열을 구분할 수 없다 ([5]).                      */
/* [5그램과 달라진 점] 앞 글자 열 4개 -> 6개. 문맥을 낱개 필드가 아니라    */
/*   배열 prev[6]으로 들고 있다 (비교·출력을 반복문으로 쓰기 위해).        */
/*   그리고 다음 글자는 키에서 풀지 않고 nexts[칸]에서 바로 읽는다.        */
/* ================================================================== */
typedef struct {
    int prev[6];
    int next;
    int count;
} Gram;

/* 표 정렬용 번호: BOS를 -1로 본다 (문장 첫머리 문맥을 표 맨 위로) */
static int disp_id(int id)
{
    return (id == g_bos) ? -1 : id;
}

static int cmp_gram_display(const void *pa, const void *pb)
{
    const Gram *a = (const Gram *)pa;
    const Gram *b = (const Gram *)pb;
    int i;

    for (i = 0; i < 6; i++) {   /* 앞1부터 차례로 비교. 다르면 거기서 순서가 정해진다 */
        int da = disp_id(a->prev[i]);
        int db = disp_id(b->prev[i]);
        if (da != db) {
            return (da > db) - (da < db);
        }
    }
    if (a->count != b->count) {
        return (a->count < b->count) ? 1 : -1;    /* 같은 문맥 안: 빈도 내림차순 */
    }
    return (a->next > b->next) - (a->next < b->next);   /* 빈도도 같으면 번호 오름차순 */
}

/* 같은 문맥인가 (정렬된 배열에서 문맥 묶음의 끝을 찾을 때) */
static int same_ctx(const Gram *a, const Gram *b)
{
    int i;
    for (i = 0; i < 6; i++) {
        if (a->prev[i] != b->prev[i]) {
            return 0;
        }
    }
    return 1;
}

/* 해시의 채워진 칸 -> Gram 배열, 표 순서로 정렬. 서로 다른 문맥 수(g_n_ctx)도 센다.
   다 쓰면 free 해야 한다. 실패하면 NULL */
static Gram *sorted_grams(void)
{
    unsigned long long V = (unsigned long long)g_vocab;
    size_t slots = (size_t)(g_ht.mask + 1u);
    Gram *g = (Gram *)malloc((size_t)g_n_grams * sizeof(Gram));
    size_t s;
    int k = 0;
    int i;

    if (!g) {
        fprintf(stderr, "[오류] 빈도표 메모리 할당 실패\n");
        return NULL;
    }
    for (s = 0; s < slots; s++) {
        unsigned long long rest = g_ht.ctxs[s];
        int j;
        if (rest == HT_EMPTY) {
            continue;
        }
        /* 다음 글자는 키에 섞여 있지 않고 따로 저장돼 있다 ([7]) */
        g[k].next = g_ht.nexts[s];
        /* 문맥(V진법 6자리 수)을 거꾸로 푼다: 나머지 = 맨 끝 자리, 몫 = 나머지 자리들 */
        for (j = 5; j >= 0; j--) {
            g[k].prev[j] = (int)(rest % V);
            rest /= V;
        }
        g[k].count = g_ht.counts[s];
        k++;
    }
    qsort(g, (size_t)g_n_grams, sizeof(Gram), cmp_gram_display);

    g_n_ctx = 0;
    for (i = 0; i < g_n_grams; i++) {
        if (i == 0 || !same_ctx(&g[i], &g[i - 1])) {
            g_n_ctx++;   /* 정렬돼 있으므로 문맥이 바뀌는 곳마다 하나 */
        }
    }
    return g;
}

static int write_table(const char *path)
{
    Gram *g = sorted_grams();
    FILE *out;
    int i = 0;

    if (!g) {
        return 0;
    }
    out = fopen(path, "wb");   /* "wb": UTF-8 바이트와 LF를 그대로 쓴다 */
    if (!out) {
        fprintf(stderr, "[오류] 빈도표 파일을 쓸 수 없습니다: %s\n", path);
        free(g);
        return 0;
    }

    fprintf(out, "# 7그램 빈도표 (문장)\n");
    fprintf(out, "# input_file\t%s\n", DATA_PATH);
    fprintf(out, "# sentences\t%lld\n", g_sent_count);
    fprintf(out, "# chars\t%lld\n", g_char_count);
    fprintf(out, "# char_kinds\t%d\n", g_chars);
    fprintf(out, "# vocab\t%d\n", g_vocab);
    fprintf(out, "# total_7grams\t%lld\n", g_total_grams);
    fprintf(out, "# unique_7grams\t%d\n", g_n_grams);
    fprintf(out, "# rows\t%d\n", g_n_ctx);
    fprintf(out, "#\n");
    fprintf(out, "# 공백은 <SP>, NBSP는 <NBSP> 로 적는다 (탭 구분 표에서 열을 구분하려고)\n");
    fprintf(out, "#\n");
    fprintf(out, "# prev1\tprev2\tprev3\tprev4\tprev5\tprev6\tnext\tcount\tpercent\n");

    while (i < g_n_grams) {
        long long total = 0;
        int end = i;
        int j;
        while (end < g_n_grams && same_ctx(&g[end], &g[i])) {   /* 문맥 하나의 묶음 [i, end) */
            total += g[end].count;
            end++;
        }
        for (j = i; j < end; j++) {
            int c;
            for (c = 0; c < 6; c++) {
                fprintf(out, "%s\t", g_names[g[j].prev[c]]);
            }
            fprintf(out, "%s\t%d\t%.6f\n", g_names[g[j].next],
                    g[j].count, 100.0 * (double)g[j].count / (double)total);
        }
        i = end;
    }

    fclose(out);
    free(g);
    return 1;
}

/* ================================================================== */
/* [13] 결과 파일 자동 번호                                             */
/*                                                                     */
/* [목적] 결과 파일 이름 step6_7gram_result_NN.txt의 NN을 정한다.        */
/* [동작] 01 ~ 99를 차례로 열어 보고 "있는 번호 중 최댓값 + 1"을 쓴다.     */
/*   99가 이미 있으면 덮어쓰지 않고 오류로 끝낸다.                       */
/* [이유] 지운 번호를 다시 채우지 않아야 번호 순서 = 실행 순서가 된다.     */
/*   표준 C에는 폴더 목록 함수가 없어서 fopen으로 하나씩 확인한다.        */
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
/* [14] 검증용 모드 (step6_verify.py가 호출)                            */
/*                                                                     */
/* [목적] 검증기가 C 내부를 들여다보지 않고 출력만으로 확인하게 한다.     */
/* [동작] --dump : 모든 7그램과 빈도 (Python 재계산과 전수 비교)         */
/*     해시테이블의 채워진 칸을 칸 순서대로 출력한다 (순서는 상관없음)    */
/*   --rows : 문맥 (앞1 ~ 앞6)마다 합계 (난수 범위가 맞는지)              */
/*   --map  : 표준입력 "앞1 ~ 앞6 r" -> 뽑힌 글자                         */
/*   --hist : 한 문맥에서 여러 번 뽑은 횟수 (분포가 맞는지)                */
/*   --map과 --hist는 생성과 똑같은 ctx_total / pick 을 부른다            */
/* [이유] 글자를 명령줄 인자가 아니라 표준입력으로 받는 까닭: Windows는   */
/*   argv를 시스템 코드페이지(CP949)로 넘겨서 Python이 준 UTF-8 한글이    */
/*   깨진다. 표준입력은 보낸 바이트가 그대로 온다 (Step3에서 겪은 문제).  */
/*   글자를 표시 이름으로 주고받는 까닭: 공백을 그대로 보내면             */
/*   scanf("%s")가 구분자로 읽어 버려 문맥을 받을 수 없다. <SP>는 공백이   */
/*   없는 한 덩어리라서 그대로 토큰이 된다.                               */
/* [5그램과 달라진 점] 출력·입력의 앞 글자 4개 -> 6개. 반복문으로 썼다.   */
/* ================================================================== */
static int mode_dump(void)
{
    unsigned long long V = (unsigned long long)g_vocab;
    size_t slots = (size_t)(g_ht.mask + 1u);
    size_t s;

    for (s = 0; s < slots; s++) {
        unsigned long long rest = g_ht.ctxs[s];
        int prev[6];
        int j;
        if (rest == HT_EMPTY) {
            continue;
        }
        for (j = 5; j >= 0; j--) {
            prev[j] = (int)(rest % V);
            rest /= V;
        }
        for (j = 0; j < 6; j++) {
            printf("%s\t", g_names[prev[j]]);
        }
        printf("%s\t%d\n", g_names[g_ht.nexts[s]], g_ht.counts[s]);
    }
    return 0;
}

static int mode_rows(void)
{
    Gram *g = sorted_grams();
    int i = 0;

    if (!g) {
        return 1;
    }
    while (i < g_n_grams) {
        long long total = 0;
        int end = i;
        int j;
        while (end < g_n_grams && same_ctx(&g[end], &g[i])) {
            total += g[end].count;
            end++;
        }
        for (j = 0; j < 6; j++) {
            printf("%s\t", g_names[g[i].prev[j]]);
        }
        printf("%lld\n", total);
        i = end;
    }
    free(g);
    return 0;
}

/* 표준입력의 문맥 글자 이름 6개 -> 번호. 1 = 성공, 0 = 모르는 이름, -1 = 입력 끝 */
static int read_ctx(int *prev)
{
    char name[6][32];
    int i;

    if (scanf("%31s %31s %31s %31s %31s %31s",
              name[0], name[1], name[2], name[3], name[4], name[5]) != 6) {
        return -1;
    }
    for (i = 0; i < 6; i++) {
        prev[i] = name_to_id(name[i]);
        if (prev[i] < 0) {
            return 0;
        }
    }
    return 1;
}

static int mode_map(void)
{
    int prev[6];
    int rc;

    while ((rc = read_ctx(prev)) != -1) {
        long long r = -1;
        long long total = (rc == 1) ? ctx_total(prev) : 0;
        if (scanf("%lld", &r) != 1 || r < 0 || r >= total) {
            fprintf(stderr, "[오류] 잘못된 질의 (문맥 합계 %lld, r %lld)\n", total, r);
            return 1;
        }
        printf("%s\n", g_names[pick(prev, r)]);
    }
    return 0;
}

static int mode_hist(long long n, unsigned seed)
{
    long long *hist = (long long *)calloc((size_t)g_vocab, sizeof(long long));
    int prev[6];
    long long total = (read_ctx(prev) == 1) ? ctx_total(prev) : 0;
    long long k;
    int b;

    if (!hist || total == 0) {
        fprintf(stderr, "[오류] 표준입력으로 문맥을 주세요 (있는 문맥이어야 함)\n");
        free(hist);
        return 1;
    }
    srand(seed);
    for (k = 0; k < n; k++) {
        hist[pick(prev, rand_below(total))]++;
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
    if (!prepare_corpus(0) || !build_model()) {
        return 1;
    }
    if (strcmp(argv[1], "--dump") == 0) return mode_dump();
    if (strcmp(argv[1], "--rows") == 0) return mode_rows();
    if (strcmp(argv[1], "--map") == 0)  return mode_map();
    if (strcmp(argv[1], "--hist") == 0 && argc >= 4) {
        return mode_hist(atoll(argv[2]), (unsigned)strtoul(argv[3], NULL, 10));
    }
    fprintf(stderr, "사용법: --dump | --rows | --map | --hist <샘플수> <seed>  (문맥은 표준입력)\n");
    return 1;
}

/* ================================================================== */
/* [15] main                                                            */
/*                                                                     */
/* [목적] 전체 흐름을 한 번 실행하고 결과 파일·빈도표를 남긴다.           */
/* [동작] 인자 해석 -> 코퍼스·seq -> 학습(세기, 시간 측정) -> 빈도표 ->    */
/*   탐욕적 1개 -> srand(seed) 후 15개 생성(시간 측정) -> 원본 일치 집계   */
/*   -> 결과 파일 (사용자 지정 형식 헤더 + 기계 판독용 메타 + 문장 목록)   */
/* [이유] seed는 기본값이 현재 시각이고 결과 파일 헤더에 기록한다. 그      */
/*   seed를 인자로 주면 같은 결과를 재현할 수 있다 (CLAUDE.md 결과 파일    */
/*   번호 규칙). 3그램·5그램·7그램에 같은 seed를 주면 같은 조건 비교다.    */
/*   빈도표를 생성보다 먼저 쓰는 까닭: 서로 다른 문맥 수(g_n_ctx)를 표를   */
/*   정렬하면서 세기 때문에, 헤더에 쓰기 전에 표가 있어야 한다.            */
/* [5그램과 달라진 점] 결과 파일의 [7-Gram] 표기뿐이다.                   */
/* ================================================================== */
static void write_sent_line(FILE *out, const char *key, const Sent *s, char *buf, int show)
{
    int found;
    sent_to_string(s, buf);
    found = in_corpus(buf);
    if (show) {
        printf("%6s  %s%s%s\n", key, buf, found ? "  *" : "", s->truncated ? "  ~" : "");
    }
    fprintf(out, "%s\t%s%s%s\n", key, buf, found ? "\t*" : "", s->truncated ? "\t~" : "");
}

/* 비율(%)을 문자열로. 분모가 0이면 "-" */
static void format_rate(char *buf, size_t size, long long num, long long den, int digits)
{
    if (den == 0) {
        snprintf(buf, size, "-");
    } else {
        snprintf(buf, size, "%.*f", digits, 100.0 * (double)num / (double)den);
    }
}

static void write_result(FILE *out, unsigned seed, int count, const char *table_path,
                         double build_ms, double gen_ms, const MatchStats *st)
{
    char rate[32];

    /* --- 사용자 지정 형식 --- */
    fprintf(out, "데이터: %lld문장 | %lld글자 | %d종 글자\n", g_sent_count, g_char_count, g_chars);
    fprintf(out, "seed: %u\n", seed);
    fprintf(out, "생성 상한: %d글자\n", MAX_LEN);
    fprintf(out, "\n");
    fprintf(out, "[7-Gram (%d개 패턴수)]\n", g_n_grams);
    fprintf(out, "샘플갯수 : %d개\n", count);
    fprintf(out, "\n");

    /* --- 기계 판독용 (step6_verify.py가 읽는다) --- */
    fprintf(out, "# input_file\t%s\n", DATA_PATH);
    fprintf(out, "# sentences\t%lld\n", g_sent_count);
    fprintf(out, "# chars\t%lld\n", g_char_count);
    fprintf(out, "# char_kinds\t%d\n", g_chars);
    fprintf(out, "# vocab\t%d\n", g_vocab);
    fprintf(out, "# total_grams\t%lld\n", g_total_grams);
    fprintf(out, "# unique_grams\t%d\n", g_n_grams);
    fprintf(out, "# contexts\t%d\n", g_n_ctx);
    fprintf(out, "# seed\t%u\n", seed);
    fprintf(out, "# count\t%d\n", count);
    fprintf(out, "# max_len\t%d\n", MAX_LEN);
    fprintf(out, "# table_file\t%s\n", table_path);
    fprintf(out, "# build_ms\t%.3f\n", build_ms);
    fprintf(out, "# gen_ms\t%.3f\n", gen_ms);
    fprintf(out, "# memory_bytes\t%zu\n", model_memory());
    format_rate(rate, sizeof rate, st->hits, st->total, 1);
    fprintf(out, "# in_corpus\t%lld\t%lld\t%s\n", st->hits, st->total, rate);
    fprintf(out, "#\n");
    fprintf(out, "# in_corpus = 생성 문장이 %s 에 통째로 있는 비율\n", DATA_PATH);
    fprintf(out, "# mark\t* = 원본에 통째로 있는 문장, ~ = 생성 상한에서 잘림\n");
    fprintf(out, "#\n");
}

int main(int argc, char **argv)
{
    static MatchStats st;
    char auto_path[64];
    char sent_buf[MAX_LEN * 3 + 1];   /* 한 글자 최대 3바이트 */
    const char *out_path;
    const char *table_path;
    unsigned seed;
    int count;
    Sent *samples;
    Sent greedy_sent;
    double t0, build_ms, gen_ms;
    FILE *out;
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

    /* --- 코퍼스 + 학습(세기) --- */
    if (!prepare_corpus(1)) {
        return 1;
    }
    t0 = now_ms();
    if (!build_model()) {
        return 1;
    }
    build_ms = now_ms() - t0;

    /* --- 빈도표 (문맥 수 g_n_ctx도 여기서 센다) --- */
    if (!write_table(table_path)) {
        return 1;
    }

    printf("[7그램 문장 생성] 데이터 %s\n", DATA_PATH);
    printf("문장 %lld / 글자 %lld / 글자 종류 %d (BOS/EOS 포함 번호 %d개)\n",
           g_sent_count, g_char_count, g_chars, g_vocab);
    printf("7그램 총 %lld / 서로 다른 7그램 %d / 문맥 %d\n", g_total_grams, g_n_grams, g_n_ctx);
    printf("seed %u / 생성 %d개\n", seed, count);

    /* --- 생성 --- */
    samples = (Sent *)malloc((size_t)count * sizeof(Sent));
    if (!samples || !generate_sent(1, &greedy_sent)) {
        return 1;
    }
    srand(seed);
    t0 = now_ms();
    for (k = 0; k < count; k++) {
        if (!generate_sent(0, &samples[k])) {
            return 1;
        }
    }
    gen_ms = now_ms() - t0;

    /* --- 원본 일치 --- */
    for (k = 0; k < count; k++) {
        sent_to_string(&samples[k], sent_buf);
        match_add(&st, in_corpus(sent_buf));
    }

    /* --- 결과 파일 --- */
    out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "[오류] 출력 파일을 쓸 수 없습니다: %s\n", out_path);
        return 1;
    }
    write_result(out, seed, count, table_path, build_ms, gen_ms, &st);

    printf("학습 %.3f ms / 생성 %.3f ms / 메모리 %zu bytes\n", build_ms, gen_ms, model_memory());
    printf("원본에 통째로 있는 문장 %lld / %lld = %.1f%%\n\n",
           st.hits, st.total, 100.0 * (double)st.hits / (double)st.total);

    write_sent_line(out, "greedy", &greedy_sent, sent_buf, 1);
    for (k = 0; k < count; k++) {
        char key[16];
        snprintf(key, sizeof key, "%d", k + 1);
        write_sent_line(out, key, &samples[k], sent_buf, 1);
    }
    fclose(out);

    printf("\n결과 파일 : %s\n", out_path);
    printf("빈도표    : %s\n", table_path);
    free(samples);
    ht_free(&g_ht);
    return 0;
}
