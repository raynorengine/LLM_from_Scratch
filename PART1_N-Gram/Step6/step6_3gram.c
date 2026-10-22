/*
 * step6_3gram.c - [STEP 6] 3그램 문장 생성 (해시테이블만 사용, 단일 파일)
 *
 * ===== 이 파일의 목적 =====
 * Step5까지는 단어를 만들었다. Step6은 문장을 만든다.
 *   P(다음 | 앞1, 앞2) = count(앞1, 앞2, 다음) / count(앞1, 앞2, ·)
 * 식은 Step5의 트라이그램과 똑같다. 바뀌는 것은 "무엇을 한 덩어리로 보는가"다.
 *
 * ===== 단어 생성(Step5)과 달라지는 점 3가지 (강의 2-8) =====
 * 1) 공백도 글자다
 *    단어를 만들 때는 빈칸이 필요 없었다. 문장은 단어 사이에 빈칸이 있어야 문장
 *    꼴이 되므로, 공백과 구두점(. , ? ! " ...)을 하나의 글자로 취급해 n그램 통계에
 *    함께 넣는다.
 * 2) BOS/EOS가 단어 경계 -> 문장 경계
 *    Step5의 BOS/EOS는 단어의 시작·끝이었다. 여기서는 문장의 시작·끝이다.
 *    마침표도 글자이므로 모델이 "구두점 다음에 EOS"를 통계로 배운다.
 * 3) 글자 번호를 넓은 범위에서 매긴다
 *    Step5는 한글 완성형 11,172자만 다뤄서 used[11172] 배열로 충분했다. 소설에는
 *    공백·마침표·숫자·영문·전각기호가 섞여 나오므로 코드포인트 전 범위(U+0000 ~
 *    U+FFFF)를 받는 배열로 바꿨다. 실측 결과 이 데이터의 글자는 U+0020 ~ U+FF63
 *    이어서 2바이트 배열 하나로 덮인다 ([5]).
 *
 * ===== 입력 =====
 * ../../data/novel_sentences.txt (한 줄 = 한 문장)
 * step6_prepare.py 가 원본 novel_test_data.txt를 종결부호로 쪼개 만든 파일이다.
 * 분할을 C가 하지 않는 까닭: C와 step6_verify.py가 각자 분할 규칙을 구현하면 같은
 * 분할 버그를 함께 가지게 되어 교차검증이 성립하지 않는다. 전처리로 한 번만 쪼개고
 * 양쪽이 같은 파일을 읽는다.
 *
 * ===== 자료구조: 해시테이블 하나 =====
 * 모델은 "3그램 키 -> 횟수" 해시테이블 하나뿐이다. 셀 때도, 뽑을 때도 이것만 쓴다.
 *   세기 [8] : 코퍼스의 모든 3그램을 해시테이블에 센다
 *   뽑기 [9] : 다음 글자 후보 0 ~ 1,597번을 하나씩 해시테이블에 물어 횟수를 더해 간다
 * (CSR 같은 별도 색인 구조는 쓰지 않는다. 사용자 지시)
 *
 * ===== 목차 (구역 번호 / 시작 줄 / 이름 / 주요 함수) =====
 *   [1]    78줄 | 설정값과 전역 상태   | DATA_PATH, HASH_BITS, g_seq, g_n_grams ...
 *   [2]   140줄 | 시간 측정            | now_ms
 *   [3]   161줄 | 난수                 | rand30, rand_below
 *   [4]   198줄 | UTF-8                | utf8_len, utf8_decode, utf8_encode
 *   [5]   263줄 | 코퍼스와 압축 번호   | load_file, scan_sents, prepare_corpus, in_corpus
 *   [6]   501줄 | seq 만들기           | build_seq
 *   [7]   550줄 | 해시테이블 (64비트 키) | ht_slot, ht_init, ht_add, ht_get, ht_free
 *   [8]   659줄 | 세기                 | count_grams, build_model
 *   [9]   720줄 | 뽑기                 | ctx_base, ctx_total, pick, greedy
 *   [10]  799줄 | 문장 생성            | generate_sent, sent_to_string
 *   [11]  870줄 | 원본 일치율          | match_add
 *   [12]  903줄 | 빈도표 파일          | sorted_grams, write_table
 *   [13] 1052줄 | 결과 파일 자동 번호  | next_output_path
 *   [14] 1084줄 | 검증용 모드          | mode_dump, mode_rows, mode_map, mode_hist
 *   [15] 1214줄 | main                 | main, write_result
 *   (줄 번호가 실제 구역 위치와 맞는지는 step6_verify.py [7]이 검사한다)
 *
 * ===== 사용법 =====
 *   step6_3gram.exe [생성개수] [seed] [결과파일] [빈도표파일]
 *     기본값: 15  (현재 시각)  (자동 번호 step6_3gram_result_NN.txt)  step6_3gram_table.txt
 *   검증용 모드 (step6_verify.py가 사용)
 *     step6_3gram.exe --dump                 모든 3그램 "앞1 \t 앞2 \t 다음 \t 빈도"
 *     step6_3gram.exe --rows                 문맥 합계 "앞1 \t 앞2 \t 합계"
 *     step6_3gram.exe --map                  표준입력 "앞1 앞2 r" -> 뽑힌 글자
 *     step6_3gram.exe --hist <샘플수> <seed>  표준입력 "앞1 앞2" 문맥에서 여러 번 뽑은 횟수
 *   공백은 글자이므로 검증 모드의 입출력에서는 <SP> 로 적는다 ([5] 표시 이름).
 *
 * ===== 빌드 =====
 *   build.bat step6_3gram   (build.bat step6_3gram / step6_5gram / step6_7gram 중 하나)
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
/*   g_ht에 3그램 횟수를 센다 ([8]). 문장을 만들 때도 g_ht만 조회한다.   */
/* [이유] 단일 파일이라 구역끼리 넘겨줄 값이 많다. 함수 인자로 전부      */
/*   넘기면 읽기 어려워져서 "프로그램 전체에 하나뿐인 데이터"는 전역     */
/*   (g_ 접두사)으로 두었다.                                            */
/*   g_ht는 [7]의 HashTable 형식이 있어야 선언할 수 있어서 [8]에 둔다.   */
/* [Step5 트라이그램과 달라진 점]                                       */
/*   - DATA_PATH: words.txt -> novel_sentences.txt                      */
/*   - DEFAULT_COUNT: 1000 -> 15 (강의 2-8은 문장을 눈으로 읽고 판단한다) */
/*   - MAX_LEN: 20 -> 400 (문장 평균 28글자, 실측 최대 358글자)          */
/*   - CP_LIMIT 도입: 한글 전용 배열을 코드포인트 전 범위로 교체          */
/*   - LEN_BUCKETS 제거: 길이별 사전 일치율은 문장에 쓸 수 없다 ([11])    */
/* ================================================================== */
#define DATA_PATH      "../../data/novel_sentences.txt"
#define OUTPUT_PREFIX  "step6_3gram_result_"   /* 뒤에 01 ~ 99 + ".txt" */
#define DEFAULT_TABLE  "step6_3gram_table.txt"
#define MAX_RUN_NO     99
#define DEFAULT_COUNT  15      /* 생성 문장 수. 강의 2-8 예시처럼 눈으로 읽을 만큼만 */
#define MAX_LEN        400     /* 문장 최대 글자수. 넘으면 잘린 문장으로 표시 */

/* 글자 코드포인트의 상한. 이 데이터의 글자는 U+0020 ~ U+FF63 이므로 2^16으로 덮인다.
   (Python 독립 계산으로 전수 확인: 1바이트 80종 / 2바이트 1종(NBSP) / 3바이트 1,515종)
   4바이트 UTF-8(U+10000 이상, 이모지 등)이 나오면 [5]에서 오류로 멈춘다. */
#define CP_LIMIT       0x10000u

#define NAME_BYTES     12      /* 표시 이름: UTF-8 한 글자(최대 3바이트), "BOS", "<NBSP>", "<U+001F>" + 널 */

#define RAND30_RANGE   (1ULL << 30)

#define HASH_BITS      19      /* 2^19 = 524,288칸. 서로 다른 3그램 171,031개 -> 적재율 약 32.6% */
                               /* (2^18칸이면 65.3%로 Step4의 거부선 70%에 너무 가깝다) */

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
static int  g_n_grams   = 0;      /* 서로 다른 3그램 수 = 해시테이블의 채워진 칸 수 */
static int  g_n_ctx     = 0;      /* 서로 다른 문맥 (앞1, 앞2) 수. [12]에서 표를 정렬할 때 센다 */
static long long g_total_grams = 0;   /* 3그램 총 개수 (중복 포함) */

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
/* [이유] MSVC rand()는 0 ~ 32,767(15비트)뿐인데 문맥 (BOS,BOS)의 합계는  */
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
/* [Step5와 달라진 점] Step5는 한글만 다뤄서 3바이트 전용 utf8_encode3   */
/*   하나로 충분했다. 소설에는 ASCII(공백·마침표·영문·숫자)와 2바이트    */
/*   NBSP가 섞여 있으므로 길이에 따라 1 ~ 3바이트를 모두 만드는          */
/*   utf8_encode로 바꿨다. utf8_len / utf8_decode는 이미 1 ~ 4바이트를   */
/*   처리할 수 있어 그대로 쓴다.                                        */
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
/* [이유] 코드포인트를 그대로 번호로 쓰면 U+FFFF까지 65,536개가 되어 키가 */
/*   훨씬 커진다. 실제로 나온 1,596개만 번호를 받게 하면 V가 1,598로      */
/*   줄고, 그래야 7그램 문맥 V^6 이 64비트에 들어간다 (step6_7gram.c).    */
/*   번호를 코드포인트 순서로 매기는 까닭(등장 순서가 아니라): 실행마다   */
/*   같은 번호가 나와야 같은 seed에서 결과가 재현된다. 후보를 번호        */
/*   순서로 훑어 누적하므로([9]) 번호가 바뀌면 뽑히는 글자도 바뀐다.      */
/*   강의 2-8 [보충]은 "처음 보는 글자마다 ID 부여"(등장 순서)라고 쓰지만 */
/*   재현성 때문에 Step5와 같은 코드포인트 순서를 유지했다.              */
/*   파일은 "rb"로 연다: 텍스트 모드는 Windows에서 \r\n을 바꾼다.        */
/* [Step5와 달라진 점]                                                  */
/*   - used/g_cp_to_id가 한글 11,172칸 -> 코드포인트 65,536칸            */
/*   - "한글이 아니면 오류"를 "4바이트 UTF-8이면 오류"로 완화            */
/*   - 표시 이름 도입: 공백은 눈에 안 보여서 빈도표·검증 입출력에 쓸 수   */
/*     없다. <SP> / <NBSP> / <U+001F> 로 적는다 (disp_name)              */
/*   - g_id_to_cp 추가: 문장을 출력할 때는 표시 이름이 아니라 글자 그대로 */
/*     써야 하므로 번호 -> 코드포인트도 들고 있어야 한다                 */
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

/* 키가 64비트에 들어가는지 확인한다. 3그램 키의 최댓값은 V^3 - 1.
   넘치면 서로 다른 n그램이 조용히 같은 키로 합쳐지므로 반드시 막아야 한다 ([7] 참고). */
static int check_key_range(void)
{
    const unsigned long long V = (unsigned long long)g_vocab;
    const unsigned long long cap = 0xFFFFFFFFFFFFFFFFULL;
    /* V^3 - 1 <= cap 인지 나눗셈으로 확인한다 (곱하면 넘쳐서 검사 자체가 무의미해진다) */
    if (V != 0 && V * V <= cap / V) {
        return 1;
    }
    fprintf(stderr, "[오류] 글자 종류가 너무 많아 3그램 키가 64비트를 넘습니다 (V = %d)\n", g_vocab);
    return 0;
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
/*   [7][8]은 이 배열을 한 칸씩 훑으며 3그램을 센다.                     */
/* [동작] 3그램은 앞에 BOS 2개, 뒤에 EOS 1개를 붙인다.                   */
/*   cps = [나 비 -1 사 -1]   (-1 = 문장 끝)                             */
/*   seq = [BOS BOS 나 비 EOS BOS BOS 사 EOS]                            */
/*   "나비"의 3그램: (BOS,BOS,나) (BOS,나,비) (나,비,EOS)                 */
/*         | (비,EOS,BOS) (EOS,BOS,BOS) 는 [8]에서 건너뜀 |               */
/*   "사"의 3그램: (BOS,BOS,사) (BOS,사,EOS)                              */
/*   길이 L 문장 -> 3그램 L+1개 (n과 관계없이 같은 개수)                  */
/*   seq 길이 = 글자 수 + 문장 수 x 3   (BOS 2개 + EOS 1개)               */
/* [이유] BOS가 2개인 까닭: 문맥이 앞 2글자라서 첫 글자를 뽑을 때도       */
/*   2글자가 필요하다. 모자란 앞자리를 BOS로 채운다. 덕분에 (BOS, 그)라는 */
/*   문맥이 생겨 "문장 첫 글자인 '그'"와 "문장 중간의 '그'"를 구분한다.   */
/*   EOS는 1개면 된다: EOS가 뽑히면 생성이 끝나 그 뒤 문맥은 필요 없다.   */
/*   문장 사이에 걸친 3그램은 실제 문맥이 아니므로 [8]에서 세지 않는다.   */
/* [Step5와 달라진 점] 코드는 같다. "단어"가 "문장"으로 바뀌었을 뿐이다.  */
/*   문장은 평균 28글자(단어는 3.69글자)라 seq가 훨씬 길다.               */
/* ================================================================== */
static int build_seq(const int *cps, long long n_cps)
{
    long long i;
    long long k = 0;

    g_seq_n = g_char_count + g_sent_count * 3;   /* 글자 + 문장마다 BOS 2개 + EOS 1개 */
    g_seq   = (int *)malloc((size_t)g_seq_n * sizeof(int));
    if (!g_seq) {
        fprintf(stderr, "[오류] seq 메모리 할당 실패\n");
        return 0;
    }
    g_seq[k++] = g_bos;   /* 첫 문장의 BOS 2개 */
    g_seq[k++] = g_bos;
    for (i = 0; i < n_cps; i++) {
        if (cps[i] == -1) {
            g_seq[k++] = g_eos;
            if (i + 1 < n_cps) {
                g_seq[k++] = g_bos;   /* 다음 문장이 있으면 그 문장의 BOS 2개 */
                g_seq[k++] = g_bos;
            }
        } else {
            g_seq[k++] = g_cp_to_id[(unsigned)cps[i]];
        }
    }
    g_seq_n = k;
    return 1;
}

/* ================================================================== */
/* [7] 해시테이블 (64비트 키)                                           */
/*                                                                     */
/* [목적] "3그램 키 -> 횟수"를 센다. 실제로 나온 3그램만 칸을 차지한다.   */
/*   3차원 배열 freq[a][b][c]는 1598^3 ≈ 40.8억 칸 x 4바이트 ≈ 16GB라     */
/*   만들 수 없다. 실제로 나온 3그램은 171,031개(0.004%)뿐이다.           */
/* [동작] 오픈 어드레싱 + 선형 탐사                                     */
/*   1) 해시 함수로 키의 첫 칸을 정한다                                  */
/*   2) 같은 키가 있으면 횟수 +1 / 빈 칸이면 새로 넣기 /                  */
/*      다른 키가 있으면(충돌) 바로 옆 칸으로 가서 다시 확인              */
/*   해시 함수 = 피보나치 해싱: 키 x (2^64 / 황금비) 의 상위 bits 비트.   */
/*   곱셈이 키의 모든 비트를 섞어 위쪽으로 올려 보내므로 위쪽 비트를 칸   */
/*   번호로 쓰면 규칙적인 키(같은 문맥의 키는 연속)도 고르게 흩어진다.    */
/* [이유] 키 = 앞 2글자와 다음 글자를 V진법 3자리 수로 읽은 값 ([8]).     */
/*   최댓값 1598^3 - 1 ≈ 40.8억 으로 unsigned long long(최대 약          */
/*   1.8 x 10^19)에 들어간다. int(약 21억)에는 안 들어간다.               */
/*   넘치면 서로 다른 3그램이 같은 키로 합쳐지고 오류도 나지 않는다.      */
/*   그래서 [5]의 check_key_range가 실행할 때마다 V를 확인한다.           */
/*   빈 칸 표시는 HT_EMPTY(64비트 최댓값). 0은 (공백,공백,공백)이라 쓸 수 */
/*   없다. 칸 수가 2의 거듭제곱이면 "% 칸수" 대신 "& mask"로 끝 칸 다음   */
/*   0번 칸으로 돌아갈 수 있다. 70% 넘게 차면 빈 칸까지 멀어져 느려지므로 */
/*   실패로 처리한다.                                                    */
/* [Step5와 달라진 점] 코드가 같다. HASH_BITS만 21 -> 19 (3그램이 65만개  */
/*   에서 17만개로 줄어서다. 글자 종류가 2,452 -> 1,596으로 작아진 영향)  */
/* ================================================================== */
#define HT_EMPTY   0xFFFFFFFFFFFFFFFFULL
#define FIB_MULT   11400714819323198485ULL   /* 2^64 / 황금비 (Knuth) */

typedef struct {
    unsigned long long *keys;    /* 칸마다 키. 빈 칸은 HT_EMPTY */
    int                *counts;  /* 칸마다 횟수 */
    unsigned            bits;    /* 칸 수 = 2^bits */
    unsigned long long  mask;    /* = 2^bits - 1 */
    long long           used;    /* 채워진 칸 수 = 서로 다른 키 수 */
} HashTable;

static size_t ht_slot(const HashTable *ht, unsigned long long key)
{
    /* 곱셈은 2^64에서 넘치는 부분이 버려진다(부호 없는 정수 규칙). 상위 bits 비트만 남긴다 */
    return (size_t)((key * FIB_MULT) >> (64u - ht->bits));
}

static int ht_init(HashTable *ht, unsigned bits)
{
    size_t size = (size_t)1u << bits;
    size_t i;

    ht->bits   = bits;
    ht->mask   = ((unsigned long long)1u << bits) - 1u;
    ht->used   = 0;
    ht->keys   = (unsigned long long *)malloc(size * sizeof(unsigned long long));
    ht->counts = (int *)calloc(size, sizeof(int));
    if (!ht->keys || !ht->counts) {
        fprintf(stderr, "[오류] 해시테이블 메모리 할당 실패 (칸 %zu개)\n", size);
        return 0;
    }
    for (i = 0; i < size; i++) {
        ht->keys[i] = HT_EMPTY;
    }
    return 1;
}

static int ht_add(HashTable *ht, unsigned long long key)
{
    size_t i = ht_slot(ht, key);

    for (;;) {
        if (ht->keys[i] == key) {
            ht->counts[i]++;
            return 1;
        }
        if (ht->keys[i] == HT_EMPTY) {
            if ((unsigned long long)(ht->used + 1) * 10u > (ht->mask + 1u) * 7u) {
                fprintf(stderr, "[오류] 해시테이블이 70%% 넘게 찼습니다. HASH_BITS를 키우세요\n");
                return 0;
            }
            ht->keys[i]   = key;
            ht->counts[i] = 1;
            ht->used++;
            return 1;
        }
        i = (size_t)((i + 1u) & ht->mask);   /* 충돌: 바로 옆 칸 (끝이면 0번으로) */
    }
}

/* 키의 횟수. 없는 키면 0. 뽑기([9])가 후보마다 부른다 */
static int ht_get(const HashTable *ht, unsigned long long key)
{
    size_t i = ht_slot(ht, key);

    for (;;) {
        if (ht->keys[i] == key) {
            return ht->counts[i];
        }
        if (ht->keys[i] == HT_EMPTY) {
            return 0;   /* 빈 칸을 만났다 = 이 키는 한 번도 넣은 적이 없다 */
        }
        i = (size_t)((i + 1u) & ht->mask);
    }
}

static void ht_free(HashTable *ht)
{
    free(ht->keys);
    free(ht->counts);
    ht->keys   = NULL;
    ht->counts = NULL;
}

/* ================================================================== */
/* [8] 세기                                                             */
/*                                                                     */
/* [목적] 코퍼스의 모든 3그램을 해시테이블 g_ht에 센다. 이것이 모델 전부다. */
/* [동작] seq를 한 칸씩 훑으며 (앞1, 앞2, 다음) 세 번호로 키를 만들어     */
/*   ht_add 한다.  키 = (앞1 x V + 앞2) x V + 다음   (V = 1,598)          */
/*   예: seq = [BOS BOS 그 러 나 ...] 라면                                */
/*     (BOS,BOS,그) (BOS,그,러) (그,러,나) 를 각각 +1                     */
/*   이 키는 V진법 세 자리 수와 같다. 자리마다 0 ~ 1,597이므로 서로 다른   */
/*   3그램은 반드시 서로 다른 키가 된다 (겹치지 않는다).                  */
/* [이유] 3차원 배열은 16GB라 만들 수 없어서 실제로 나온 3그램만 저장하는  */
/*   해시테이블을 쓴다 ([7] 참고). 세기가 끝난 해시테이블을 그대로 생성    */
/*   단계에서 조회하므로 다른 배열로 옮기지 않는다.                       */
/*   문맥 칸에 EOS가 있으면 문장 경계를 넘은 묶음이다 (?, EOS, BOS) /      */
/*   (EOS, BOS, BOS). 실제 문맥이 아니라서 세지 않는다.                   */
/* [Step5와 달라진 점] 없다. 세는 대상이 단어의 3그램에서 문장의 3그램으로 */
/*   바뀌었을 뿐 코드는 같다. 공백·구두점도 그냥 글자 번호라서 따로        */
/*   처리할 것이 없다 -- 이것이 "공백도 글자다"의 구현상 의미다.          */
/* ================================================================== */
static HashTable g_ht;   /* 모델: 3그램 키 -> 횟수 */

static int count_grams(void)
{
    const unsigned long long V = (unsigned long long)g_vocab;
    long long i;

    if (!ht_init(&g_ht, HASH_BITS)) {
        return 0;
    }
    for (i = 0; i + 2 < g_seq_n; i++) {
        unsigned long long key;
        if (g_seq[i] == g_eos || g_seq[i + 1] == g_eos) {
            continue;   /* (?, EOS, BOS) / (EOS, BOS, BOS): 문장 사이에 걸친 3그램 */
        }
        /* 곱하기 전에 64비트로 바꾼다. int끼리 곱하면 int 범위에서 먼저 넘친다 */
        key = ((unsigned long long)g_seq[i] * V + (unsigned long long)g_seq[i + 1]) * V
            + (unsigned long long)g_seq[i + 2];
        if (!ht_add(&g_ht, key)) {
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
    g_n_grams = (int)g_ht.used;   /* 채워진 칸 하나 = 서로 다른 3그램 하나 */
    return 1;
}

/* 생성 단계에 남는 메모리: 해시테이블의 칸 전부 (빈 칸 포함). 칸마다 키 8바이트 + 횟수 4바이트 */
static size_t model_memory(void)
{
    return (size_t)(g_ht.mask + 1u) * (sizeof(unsigned long long) + sizeof(int));
}

/* ================================================================== */
/* [9] 뽑기                                                             */
/*                                                                     */
/* [목적] 지금 문맥(앞 2글자) 다음에 올 글자를 빈도에 비례해서 하나 뽑는다. */
/* [동작] 1) ctx_base: 문맥의 첫 키 = (앞1 x V + 앞2) x V                  */
/*      여기에 다음 글자 번호 b를 더하면 3그램 (앞1, 앞2, b)의 키가 된다   */
/*   2) ctx_total: b = 0 ~ 1,597 전부 해시에 물어 횟수를 더한다 = 문맥 합계 */
/*   3) r = 0 ~ (합계 - 1) 난수를 받는다                                   */
/*   4) pick: b = 0, 1, 2, ... 순서로 다시 물으며 횟수를 누적하다가        */
/*      "누적 > r" 이 되는 첫 b를 고른다                                  */
/*   예: 문맥 (BOS,BOS) 뒤에는 문장 첫 글자가 온다. 문장이 66,961개라      */
/*     합계 = 66,961, r = 0 ~ 66,960                                      */
/*     b 순서로 누적하다가 누적 > r 이 처음 되는 글자가 뽑힌다             */
/* [이유] 해시테이블은 "(앞1, 앞2, b)가 몇 번?"에는 바로 답하지만        */
/*   "문맥 (앞1, 앞2) 다음에 올 수 있는 후보 목록"은 모른다. 키가 칸 전체에 */
/*   흩어져 있어서다. 그래서 가능한 다음 글자 1,598개를 전부 물어본다.     */
/*   나온 적 없는 후보는 0이 돌아와 누적이 그대로이므로 뽑히지 않는다.     */
/*   합계를 따로 저장하지 않고 매번 1,598개를 물어 구한다 (사용자 결정    */
/*   (B): 자료구조를 해시테이블 하나로 유지).                             */
/*   '>' 비교가 핵심이다. '>='를 쓰면                                     */
/*     - 마지막 후보의 몫을 앞 후보가 가져가고                            */
/*     - r = 0일 때 번호가 앞선, 한 번도 안 나온 글자(누적 0 >= 0)가 뽑힌다 */
/*   탐욕적 디코딩도 '>'로 비교해 동률이면 번호가 작은 후보가 남는다.     */
/* [Step5와 달라진 점] 후보 수가 2,452 -> 1,598로 줄었다. 조회 횟수가     */
/*   후보 수로 정해지므로 한 글자 뽑는 비용은 Step5보다 작다. 대신 문장이  */
/*   평균 28글자라 한 문장을 만드는 데 드는 뽑기 횟수는 훨씬 많다.         */
/* ================================================================== */
static unsigned long long ctx_base(int prev1, int prev2)
{
    /* 곱하기 전에 64비트로 바꾼다 ([8]과 같은 이유) */
    unsigned long long V = (unsigned long long)g_vocab;
    return ((unsigned long long)prev1 * V + (unsigned long long)prev2) * V;
}

/* 문맥 합계 = 이 문맥 뒤에 온 3그램 횟수의 합. 한 번도 안 나온 문맥이면 0 */
static long long ctx_total(int prev1, int prev2)
{
    unsigned long long base = ctx_base(prev1, prev2);
    long long total = 0;
    int b;

    for (b = 0; b < g_vocab; b++) {
        total += ht_get(&g_ht, base + (unsigned long long)b);
    }
    return total;
}

static int pick(int prev1, int prev2, long long r)
{
    unsigned long long base = ctx_base(prev1, prev2);
    long long cum = 0;
    int b;

    for (b = 0; b < g_vocab; b++) {
        cum += ht_get(&g_ht, base + (unsigned long long)b);
        if (cum > r) {        /* '>=' 가 아니다. 위 [이유] 참고 */
            return b;
        }
    }
    return -1;   /* r >= 합계: 부른 쪽의 잘못 */
}

static int greedy(int prev1, int prev2)
{
    unsigned long long base = ctx_base(prev1, prev2);
    int best = -1;
    int best_count = 0;
    int b;

    for (b = 0; b < g_vocab; b++) {
        int c = ht_get(&g_ht, base + (unsigned long long)b);
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
/* [동작] (prev1, prev2) = (BOS, BOS) 에서 출발                          */
/*   -> 문맥 (prev1, prev2)로 다음 글자 c를 뽑는다                        */
/*   -> c가 EOS면 끝. 아니면 문장에 붙이고 (prev1, prev2) = (prev2, c)    */
/*      (문맥 창을 한 칸 민다: 가장 오래된 글자가 빠지고 새 글자가 들어옴) */
/*   예: (BOS,BOS) -> 그 / (BOS,그) -> 러 / (그,러) -> 나 / ... -> EOS     */
/* [이유] 탐욕적 디코딩은 루프에 빠지면 EOS를 영영 못 만날 수 있어        */
/*   MAX_LEN자에서 끊고 잘림(~)으로 표시한다. EOS인지 먼저 확인한 뒤 길이를 */
/*   검사하므로 딱 400자에서 끝난 문장은 잘림이 아니다.                   */
/*   문맥 합계가 0이 되는 일은 없다: 문맥 (p, c)에 도달했다면 3그램        */
/*   (?, p, c)가 코퍼스 어딘가에 있었고, c가 EOS가 아니므로 그 문장에서    */
/*   c 뒤에도 글자나 EOS가 있었다 = (p, c, ?) 3그램이 있다. 그래도 방어로  */
/*   검사한다 (합계 0이면 rand_below(0)이 되어 뽑을 수 없다).             */
/* [Step5와 달라진 점] MAX_LEN이 20 -> 400. 문장은 평균 28글자라 20이면    */
/*   거의 전부 잘린다. 400은 실측 최대 문장 358글자보다 크게 잡은 값이다.  */
/* ================================================================== */
typedef struct {
    int len;
    int truncated;
    int ids[MAX_LEN];
} Sent;

static int generate_sent(int greedy_mode, Sent *s)
{
    int prev1 = g_bos;   /* 3그램 문맥: 앞 2글자. 처음엔 (BOS, BOS) */
    int prev2 = g_bos;

    s->len = 0;
    s->truncated = 0;
    for (;;) {
        long long total = ctx_total(prev1, prev2);
        int next;
        if (total == 0) {
            fprintf(stderr, "[오류] 한 번도 나오지 않은 문맥입니다 (%s, %s)\n",
                    g_names[prev1], g_names[prev2]);
            return 0;
        }
        next = greedy_mode ? greedy(prev1, prev2) : pick(prev1, prev2, rand_below(total));
        if (next == g_eos) {
            break;
        }
        if (s->len == MAX_LEN) {
            s->truncated = 1;
            break;
        }
        s->ids[s->len++] = next;
        prev1 = prev2;   /* 문맥 창을 한 칸 민다 */
        prev2 = next;
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
/*   Step5의 사전 일치율은 길이별로도 쪼갰지만(LEN_BUCKETS) 문장에는      */
/*   그대로 쓸 수 없다. 단어는 3.69글자라 같은 길이의 단어가 많아 길이별   */
/*   비교가 뜻이 있었지만, 문장은 평균 28글자에 최대 358글자라 길이별로    */
/*   나누면 칸마다 표본이 몇 개뿐이라 비율이 의미를 잃는다. 그래서 전체    */
/*   일치율만 센다.                                                      */
/*   이 지표는 "암기"를 재는 용도다. n이 커질수록 모델이 원본 문장을       */
/*   그대로 재현하는 쪽으로 가는지 보는 것이다. 반대로 "그럴듯한 새 문장"  */
/*   인지는 숫자로 못 재므로 결과 파일의 문장을 직접 읽어 판단한다         */
/*   (강의 2-8의 평가도 정성적이다).                                      */
/* [Step5와 달라진 점] 길이 칸(LEN_BUCKETS)과 사전 길이 분포를 뺐다.      */
/*   Step5는 단어 길이를 strlen/3으로 구했는데, 문장은 1~3바이트 글자가    */
/*   섞여 있어 그 계산이 성립하지 않는다 (Sent.len을 쓰면 되지만 위 이유로 */
/*   길이별 집계 자체를 뺐다).                                            */
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
/* [12] 빈도표 파일 (step6_3gram_table.txt)                             */
/*                                                                     */
/* [목적] 모델이 센 3그램 전체를 사람이 읽을 수 있는 표로 남긴다.        */
/*   step6_verify.py [6]이 이 파일을 바이트 단위로 재현해 전수 비교한다.  */
/* [동작] 한 줄 = 3그램 하나 "앞1 \t 앞2 \t 다음 \t 빈도 \t 문맥 안 확률(%)" */
/*   1) sorted_grams: 해시테이블의 채워진 칸만 꺼내 표 순서로 정렬         */
/*      문맥 순서: (BOS,BOS) -> (BOS,<SP>) (BOS,!) ... -> (<SP>,<SP>) 순   */
/*      즉 BOS를 가장 작은 값으로 보고 (앞1, 앞2) 순                      */
/*      문맥 안 순서: 빈도 내림차순, 같으면 번호 오름차순                 */
/*   2) 정렬하면 같은 문맥의 줄이 붙어 있으므로, 문맥 하나씩 묶어 합계를   */
/*      구하고 확률 = 빈도 / 합계 로 쓴다                                 */
/* [이유] 해시테이블의 칸 순서는 해시 함수가 흩어 놓은 순서라 뒤죽박죽이다. */
/*   표를 읽을 수 있게, 그리고 실행할 때마다 같은 파일이 나오게 정렬한다.  */
/*   이 정렬은 표를 출력하기 위한 것일 뿐 뽑기([9])에는 쓰지 않는다.       */
/*   BOS를 맨 앞에 두는 까닭: 문장 첫머리 문맥이 표 맨 위에 오게 하려고    */
/*   "BOS = -1"로 보고 비교한다 (disp_id). 확률을 소수 6자리로 쓰는 까닭:  */
/*   소수 2자리면 드문 3그램의 확률이 0.00%로 보인다.                     */
/* [Step5와 달라진 점] 글자 이름에 표시 이름을 쓴다. 표는 탭 구분인데     */
/*   공백이 글자여서 그대로 쓰면 빈 칸처럼 보여 사람도 검증기도 열을       */
/*   구분할 수 없다. 그래서 공백은 <SP>, NBSP는 <NBSP>로 적는다 ([5]).     */
/* ================================================================== */
typedef struct {
    int prev1;
    int prev2;
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
    int da[2], db[2];
    int i;

    da[0] = disp_id(a->prev1);  db[0] = disp_id(b->prev1);
    da[1] = disp_id(a->prev2);  db[1] = disp_id(b->prev2);
    for (i = 0; i < 2; i++) {   /* 앞1부터 차례로 비교. 다르면 거기서 순서가 정해진다 */
        if (da[i] != db[i]) {
            return (da[i] > db[i]) - (da[i] < db[i]);
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
    return a->prev1 == b->prev1 && a->prev2 == b->prev2;
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
        unsigned long long key = g_ht.keys[s];
        if (key == HT_EMPTY) {
            continue;
        }
        /* 키 = (앞1 x V + 앞2) x V + 다음 을 거꾸로 푼다 (V진법 세 자리 수의 각 자리) */
        g[k].next  = (int)(key % V);
        g[k].prev2 = (int)((key / V) % V);
        g[k].prev1 = (int)(key / V / V);
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

    fprintf(out, "# 3그램 빈도표 (문장)\n");
    fprintf(out, "# input_file\t%s\n", DATA_PATH);
    fprintf(out, "# sentences\t%lld\n", g_sent_count);
    fprintf(out, "# chars\t%lld\n", g_char_count);
    fprintf(out, "# char_kinds\t%d\n", g_chars);
    fprintf(out, "# vocab\t%d\n", g_vocab);
    fprintf(out, "# total_3grams\t%lld\n", g_total_grams);
    fprintf(out, "# unique_3grams\t%d\n", g_n_grams);
    fprintf(out, "# rows\t%d\n", g_n_ctx);
    fprintf(out, "#\n");
    fprintf(out, "# 공백은 <SP>, NBSP는 <NBSP> 로 적는다 (탭 구분 표에서 열을 구분하려고)\n");
    fprintf(out, "#\n");
    fprintf(out, "# prev1\tprev2\tnext\tcount\tpercent\n");

    while (i < g_n_grams) {
        long long total = 0;
        int end = i;
        int j;
        while (end < g_n_grams && same_ctx(&g[end], &g[i])) {   /* 문맥 하나의 묶음 [i, end) */
            total += g[end].count;
            end++;
        }
        for (j = i; j < end; j++) {
            fprintf(out, "%s\t%s\t%s\t%d\t%.6f\n", g_names[g[j].prev1], g_names[g[j].prev2],
                    g_names[g[j].next], g[j].count, 100.0 * (double)g[j].count / (double)total);
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
/* [목적] 결과 파일 이름 step6_3gram_result_NN.txt의 NN을 정한다.        */
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
/* [동작] --dump : 모든 3그램과 빈도 (Python 재계산과 전수 비교)         */
/*     해시테이블의 채워진 칸을 칸 순서대로 출력한다 (순서는 상관없음)    */
/*   --rows : 문맥 (앞1, 앞2)마다 합계 (난수 범위가 맞는지)               */
/*     [12]의 정렬 결과를 문맥별로 묶어 더한다                           */
/*   --map  : 표준입력 "앞1 앞2 r" -> 뽑힌 글자 (구간 경계 어긋남 검사)    */
/*   --hist : 한 문맥에서 여러 번 뽑은 횟수 (분포가 맞는지)                */
/*   --map과 --hist는 생성과 똑같은 ctx_total / pick 을 부른다            */
/* [이유] 글자를 명령줄 인자가 아니라 표준입력으로 받는 까닭: Windows는   */
/*   argv를 시스템 코드페이지(CP949)로 넘겨서 Python이 준 UTF-8 한글이    */
/*   깨진다. 표준입력은 보낸 바이트가 그대로 온다 (Step3에서 겪은 문제).  */
/*   --rows를 ctx_total로 구하지 않는 까닭: 문맥 38,029개 x 1,598번 조회   */
/*   라서 느리다. 문맥 합계는 Python의 독립 계산과 비교한다.              */
/* [Step5와 달라진 점] 글자를 표시 이름으로 주고받는다. 공백을 그대로     */
/*   보내면 scanf("%s")가 구분자로 읽어 버려 문맥을 받을 수 없다.          */
/*   <SP>는 공백이 없는 한 덩어리라서 그대로 토큰이 된다.                 */
/* ================================================================== */
static int mode_dump(void)
{
    unsigned long long V = (unsigned long long)g_vocab;
    size_t slots = (size_t)(g_ht.mask + 1u);
    size_t s;

    for (s = 0; s < slots; s++) {
        unsigned long long key = g_ht.keys[s];
        if (key != HT_EMPTY) {
            printf("%s\t%s\t%s\t%d\n", g_names[key / V / V], g_names[(key / V) % V],
                   g_names[key % V], g_ht.counts[s]);
        }
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
        while (end < g_n_grams && same_ctx(&g[end], &g[i])) {
            total += g[end].count;
            end++;
        }
        printf("%s\t%s\t%lld\n", g_names[g[i].prev1], g_names[g[i].prev2], total);
        i = end;
    }
    free(g);
    return 0;
}

/* 표준입력의 문맥 글자 이름 두 개 -> 번호. 1 = 성공, 0 = 모르는 이름, -1 = 입력 끝 */
static int read_ctx(int *prev1, int *prev2)
{
    char name1[32];
    char name2[32];

    if (scanf("%31s %31s", name1, name2) != 2) {
        return -1;
    }
    *prev1 = name_to_id(name1);
    *prev2 = name_to_id(name2);
    return (*prev1 >= 0 && *prev2 >= 0) ? 1 : 0;
}

static int mode_map(void)
{
    int prev1 = -1, prev2 = -1;
    int rc;

    while ((rc = read_ctx(&prev1, &prev2)) != -1) {
        long long r = -1;
        long long total = (rc == 1) ? ctx_total(prev1, prev2) : 0;
        if (scanf("%lld", &r) != 1 || r < 0 || r >= total) {
            fprintf(stderr, "[오류] 잘못된 질의 (문맥 합계 %lld, r %lld)\n", total, r);
            return 1;
        }
        printf("%s\n", g_names[pick(prev1, prev2, r)]);
    }
    return 0;
}

static int mode_hist(long long n, unsigned seed)
{
    long long *hist = (long long *)calloc((size_t)g_vocab, sizeof(long long));
    int prev1 = -1, prev2 = -1;
    long long total = (read_ctx(&prev1, &prev2) == 1) ? ctx_total(prev1, prev2) : 0;
    long long k;
    int b;

    if (!hist || total == 0) {
        fprintf(stderr, "[오류] 표준입력으로 문맥을 주세요 (있는 문맥이어야 함)\n");
        free(hist);
        return 1;
    }
    srand(seed);
    for (k = 0; k < n; k++) {
        hist[pick(prev1, prev2, rand_below(total))]++;
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
/* [Step5와 달라진 점] 결과 파일 머리에 사용자가 지정한 형식을 쓴다        */
/*   (데이터 / seed / 생성 상한 / [3-Gram (N개 패턴수)] / 샘플갯수).       */
/*   그 아래 '#' 줄은 step6_verify.py가 읽는 기계 판독용 값이다.           */
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
    fprintf(out, "[3-Gram (%d개 패턴수)]\n", g_n_grams);
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

    printf("[3그램 문장 생성] 데이터 %s\n", DATA_PATH);
    printf("문장 %lld / 글자 %lld / 글자 종류 %d (BOS/EOS 포함 번호 %d개)\n",
           g_sent_count, g_char_count, g_chars, g_vocab);
    printf("3그램 총 %lld / 서로 다른 3그램 %d / 문맥 %d\n", g_total_grams, g_n_grams, g_n_ctx);
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
