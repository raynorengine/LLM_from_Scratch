/*
 * step5_bigram.c - [STEP 5] 바이그램 (트라이그램과 비교하기 위한 같은 구성의 단일 파일)
 *
 * ===== 이 파일의 목적 =====
 * 바이그램은 앞 1글자를 보고 다음 글자를 뽑는다.  P(c | b) = count(b, c) / count(b, ·)
 * Step3의 ② 해시 구현을 키만 64비트로 바꿔 이 파일 하나로 옮겨 왔다. 같은 폴더의
 * step5_trigram.c와 구역 순서·함수 이름·자료구조를 똑같이 맞춰서, 두 파일을 나란히 놓고
 * 비교(diff)하면 "n이 2에서 3으로 바뀐 부분"만 보이게 했다. 두 프로그램을 같은 seed로 돌려
 * 사전 일치율(생성 단어 중 words.txt에 있는 단어의 비율)을 직접 비교한다.
 *
 * ===== 자료구조: 해시테이블 하나 (2026-10-22 재작성) =====
 * 모델은 "쌍 키 -> 횟수" 해시테이블 하나뿐이다. 셀 때도, 뽑을 때도 이것만 쓴다.
 *   세기 [8] : 사전의 모든 쌍을 해시테이블에 센다
 *   뽑기 [9] : 다음 글자 후보 0 ~ 2,451번을 하나씩 해시테이블에 물어 횟수를 더해 간다
 * (처음 버전은 세기 뒤에 CSR 배열로 옮겨 이진 탐색으로 뽑았다. CSR은 뽑기를 빠르게
 *  하는 최적화일 뿐 N그램 원리와 관계가 없고 개념 이해를 방해해서 사용자 지시로 뺐다)
 *
 * ===== 목차 (구역 번호 / 시작 줄 / 이름 / 주요 함수) =====
 *   [1]    59줄 | 설정값과 전역 상태   | DATA_PATH, HASH_BITS, g_seq, g_n_grams ...
 *   [2]   111줄 | 시간 측정            | now_ms
 *   [3]   135줄 | 난수                 | rand30, rand_below
 *   [4]   166줄 | UTF-8                | utf8_len, utf8_decode, utf8_encode3
 *   [5]   214줄 | 코퍼스와 압축 번호   | load_file, scan_words, prepare_corpus, in_dictionary
 *   [6]   385줄 | seq 만들기           | build_seq
 *   [7]   423줄 | 해시테이블 (64비트 키) | ht_slot, ht_init, ht_add, ht_get, ht_free
 *   [8]   527줄 | 세기                 | count_grams, build_model
 *   [9]   584줄 | 뽑기                 | ctx_base, ctx_total, pick, greedy
 *   [10]  663줄 | 단어 생성            | generate_word, word_to_string
 *   [11]  720줄 | 사전 일치율          | match_add, count_dict_lengths
 *   [12]  766줄 | 빈도표 파일          | sorted_grams, write_table
 *   [13]  898줄 | 결과 파일 자동 번호  | next_output_path
 *   [14]  930줄 | 검증용 모드          | mode_dump, mode_rows, mode_map, mode_hist
 *   [15] 1054줄 | main                 | main, write_result
 *   (줄 번호가 실제 구역 위치와 맞는지는 step5_verify.py [7]이 검사한다)
 *
 * ===== 사용법 =====
 *   step5_bigram.exe [생성개수] [seed] [결과파일] [빈도표파일]
 *     기본값: 1000  (현재 시각)  (자동 번호 step5_bigram_result_NN.txt)  step5_bigram_table.txt
 *   검증용 모드 (step5_verify.py가 사용)
 *     step5_bigram.exe --dump                 모든 쌍 "앞 \t 다음 \t 빈도"
 *     step5_bigram.exe --rows                 문맥 합계 "앞 \t 합계"
 *     step5_bigram.exe --map                  표준입력 "앞 r" -> 뽑힌 글자
 *     step5_bigram.exe --hist <샘플수> <seed>  표준입력 "앞" 문맥에서 여러 번 뽑은 횟수
 *
 * ===== 빌드 =====
 *   build.bat step5_bigram
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
/* [동작] 코퍼스(words.txt) -> g_seq(번호 배열) -> 해시테이블 g_ht에     */
/*   쌍 횟수를 센다 ([8]). 단어를 만들 때도 g_ht만 조회한다 ([9]).       */
/* [이유] 단일 파일이라 구역끼리 넘겨줄 값이 많다. 함수 인자로 전부      */
/*   넘기면 읽기 어려워져서 "프로그램 전체에 하나뿐인 데이터"는 전역     */
/*   (g_ 접두사)으로 두었다. Step3의 함수 포인터 구조(구현 3개 교체)는    */
/*   구현이 하나뿐인 Step4부터는 필요 없어서 뺐다.                      */
/*   g_ht는 [7]의 HashTable 형식이 있어야 선언할 수 있어서 [8]에 둔다.    */
/* ================================================================== */
#define DATA_PATH      "../../data/korean_dictionary/words.txt"
#define OUTPUT_PREFIX  "step5_bigram_result_"   /* 뒤에 01 ~ 99 + ".txt" */
#define DEFAULT_TABLE  "step5_bigram_table.txt"
#define MAX_RUN_NO     99
#define DEFAULT_COUNT  1000    /* 생성 단어 수 = 사전 일치율 계산 단어 수 (강의와 같은 1,000개) */
#define MAX_LEN        20      /* 단어 최대 길이. 넘으면 잘린 단어로 표시 */
#define LEN_BUCKETS    10      /* 길이별 일치율: 1 ~ 9글자, 10글자 이상은 한 칸으로 묶음 */

#define HANGUL_BASE    0xAC00u
#define HANGUL_COUNT   11172   /* 가(U+AC00) ~ 힣(U+D7A3) */
#define NAME_BYTES     8       /* 토큰 이름: UTF-8 한 글자(3바이트) 또는 "BOS"/"EOS" + 널 */

#define RAND30_RANGE   (1ULL << 30)

#define HASH_BITS      19      /* 2^19 = 524,288칸. 서로 다른 쌍 171,425개 -> 적재율 약 33% */

/* --- 코퍼스 --- */
static char  *g_text     = NULL;   /* words.txt 전체. 줄 끝을 '\0'으로 바꿔 단어 문자열로도 쓴다 */
static char **g_words    = NULL;   /* 단어 문자열 포인터 (사전 조회용, 정렬됨) */
static long long g_word_count = 0;
static long long g_char_count = 0;

/* --- 글자 번호 --- */
static int  g_cp_to_id[HANGUL_COUNT];        /* (코드포인트 - 0xAC00) -> 번호. 안 나온 글자는 -1 */
static char (*g_names)[NAME_BYTES] = NULL;   /* 번호 -> 출력용 이름 ("가", "BOS", ...) */
static int  g_chars = 0;   /* 실제 나온 글자 종류 수 (2,450) */
static int  g_vocab = 0;   /* g_chars + 2 (BOS, EOS) */
static int  g_bos   = 0;
static int  g_eos   = 0;

/* --- seq: 모든 단어를 BOS ... EOS로 감싸 이어 붙인 번호 배열 --- */
static int      *g_seq   = NULL;
static long long g_seq_n = 0;

/* --- 모델 통계. 모델 자체(해시테이블 g_ht)는 [8]에 있다 --- */
static int  g_n_grams   = 0;      /* 서로 다른 쌍 수 = 해시테이블의 채워진 칸 수 */
static int  g_n_ctx     = 0;      /* 서로 다른 문맥(앞 글자) 수. [12]에서 표를 정렬할 때 센다 */
static long long g_total_grams = 0;   /* 쌍 총 개수 (중복 포함) */

/* ================================================================== */
/* [2] 시간 측정                                                        */
/*                                                                     */
/* [목적] 학습(세기)과 생성에 걸린 시간을 ms 단위로 잰다.          */
/* [동작] Windows에서는 고정밀 타이머 QueryPerformanceCounter의 눈금을    */
/*   "1초당 눈금 수"로 나눠 ms로 바꾼다. 다른 OS에서는 clock()을 쓴다.    */
/* [이유] 표준 clock()은 Windows에서 1ms 단위라 짧은 작업이 0으로 나온다. */
/*   Windows 전용 API는 #ifdef로 감싸 표준 C를 유지한다 (Step3과 같음).  */
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
/* [3] 난수                                                             */
/*                                                                     */
/* [목적] 0 ~ n-1 범위의 균등 난수를 만든다. 다음 글자를 뽑을 때 쓴다.   */
/* [동작] rand30: rand()를 두 번 불러 15비트 + 15비트 = 30비트 난수.      */
/*   rand_below: 30비트 난수 중 n의 배수 경계(limit)를 넘는 값은 버리고   */
/*   다시 뽑은 뒤 % n.                                                  */
/* [이유] MSVC rand()는 0 ~ 32,767(15비트)뿐인데 BOS 문맥 합계는 420,309라 */
/*   범위가 모자란다. hi, lo를 따로 받는 까닭: 한 식에 rand()를 두 번 쓰면 */
/*   호출 순서가 정해져 있지 않아 같은 seed에서도 결과가 달라질 수 있다.  */
/*   그냥 % n만 하면 앞쪽 값이 조금 더 자주 나오므로(모듈로 편향) 버린다. */
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
    unsigned long long limit = RAND30_RANGE - (RAND30_RANGE % un);   /* n의 배수 중 2^30 이하 최대 */
    unsigned long long x;

    do {
        x = rand30();
    } while (x >= limit);   /* limit 이상은 버린다: 남은 값은 n개 칸에 똑같이 나뉜다 */
    return (long long)(x % un);
}

/* ================================================================== */
/* [4] UTF-8                                                            */
/*                                                                     */
/* [목적] words.txt의 바이트를 글자(코드포인트)로 풀고, 출력할 때 다시    */
/*   바이트로 만든다.                                                   */
/* [동작] 한글 한 글자는 3바이트다. '가' = EA B0 80                       */
/*   1110 1010 / 10 110000 / 10 000000                                  */
/*   첫 바이트 앞 4비트(1110) = "3바이트 글자", 뒤 4비트가 데이터          */
/*   나머지 바이트 앞 2비트(10) = "이어지는 바이트", 뒤 6비트가 데이터     */
/*   데이터 비트를 이어 붙이면 1010 110000 000000 = 0xAC00 = '가'         */
/* [이유] 표준 C에는 UTF-8 해석 함수가 없다. 필요한 만큼만 직접 만든다.   */
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

/* 한글 코드포인트를 3바이트 UTF-8 문자열로 (decode의 반대 방향) */
static void utf8_encode3(unsigned cp, char out[NAME_BYTES])
{
    out[0] = (char)(0xE0u | (cp >> 12));
    out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[2] = (char)(0x80u | (cp & 0x3Fu));
    out[3] = '\0';
}

/* ================================================================== */
/* [5] 코퍼스와 압축 번호                                               */
/*                                                                     */
/* [목적] words.txt를 읽어 단어 목록을 만들고, 실제로 나온 글자에만      */
/*   번호(압축 번호)를 매긴다.                                          */
/* [동작] 1) 파일 전체를 메모리로 읽는다                                 */
/*   2) 단어마다 글자를 코드포인트로 풀어 cps에 담고, 쓰인 글자를 표시     */
/*      "나비\n사과\n" -> cps = [나 비 -1 사 과 -1]  (-1 = 단어 끝)       */
/*   3) 코드포인트 오름차순으로 쓰인 글자에만 0, 1, 2 ... 를 준다          */
/*      가=0, 각=1, ... (2,450종), 그다음 BOS=2450, EOS=2451              */
/* [이유] 한글 11,172자 전부에 번호를 주면 안 쓰는 8,722자가 섞인다.      */
/*   실제로 나온 글자만 번호를 받게 해서 키·배열 크기를 줄인다 (Step3에서 */
/*   정한 방식). 번호는 "나온 글자 전체"를 알아야 매길 수 있으므로 한 번  */
/*   다 훑은 뒤에 매긴다. 파일은 "rb"로 연다: 텍스트 모드는 Windows에서   */
/*   \r\n을 바꾸는 등 바이트를 건드린다.                                 */
/* ================================================================== */
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

/* 단어마다 글자를 풀어 cps에 담는다. 줄 끝 '\n'은 '\0'으로 바꿔 g_words[k]가 문자열이 되게 한다 */
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
        g_text[end] = '\0';
        if (end > start && g_text[end - 1] == '\r') {   /* git이 \r\n으로 바꿔 놓았을 수 있다 */
            g_text[end - 1] = '\0';
        }

        if (g_text[start] != '\0') {   /* 빈 줄은 건너뛴다 */
            long p = start;
            g_words[w++] = g_text + start;
            while (g_text[p] != '\0') {
                int len = utf8_len((unsigned char)g_text[p]);
                unsigned cp = (len == 3) ? utf8_decode((const unsigned char *)g_text + p, 3) : 0;
                /* 한글이 아니면 멈춘다: 번호 체계가 "한글 + BOS + EOS"를 전제로 한다 */
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

/* 단어 정렬·검색용 비교. 같은지만 보면 되므로 바이트 순 비교면 충분하다 */
static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int build_seq(const int *cps, long long n_cps);   /* [6] */

static int prepare_corpus(int need_dictionary)
{
    static int used[HANGUL_COUNT];
    long size = load_file(DATA_PATH);
    int *cps;
    long long n_cps = 0;
    long long lines = 1;   /* 마지막 줄에 \n이 없을 수도 있어 1부터 */
    long long i;
    int c;

    if (size < 0) {
        return 0;
    }
    for (i = 0; i < size; i++) {
        lines += (g_text[i] == '\n');
    }
    /* 글자·단어 끝 표시 수는 바이트 수를 넘을 수 없다 (한 글자 >= 1바이트, 줄마다 \n 1바이트) */
    cps     = (int *)malloc(((size_t)size + 1u) * sizeof(int));
    g_words = (char **)malloc((size_t)lines * sizeof(char *));
    if (!cps || !g_words) {
        fprintf(stderr, "[오류] 메모리 할당 실패\n");
        return 0;
    }
    if (!scan_words(size, cps, &n_cps, used)) {
        return 0;
    }

    /* 압축 번호: 코드포인트 오름차순으로 훑으며 쓰인 글자에만 번호 */
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

    if (!build_seq(cps, n_cps)) {
        return 0;
    }
    free(cps);

    if (need_dictionary) {   /* 사전 조회(이진 탐색)용 정렬. 생성 모드에서만 필요 */
        qsort(g_words, (size_t)g_word_count, sizeof(char *), cmp_str);
    }
    return 1;
}

/* 생성한 단어가 사전에 있는가: 정렬된 단어 목록에서 이진 탐색 */
static int in_dictionary(const char *word)
{
    return bsearch(&word, g_words, (size_t)g_word_count, sizeof(char *), cmp_str) != NULL;
}

/* 이름("가", "BOS") -> 번호. 검증 모드에서 표준입력을 해석할 때 쓴다. 모르면 -1 */
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
/* [6] seq 만들기                                                       */
/*                                                                     */
/* [목적] 단어마다 시작·끝 표시를 붙여 하나의 번호 배열로 잇는다.        */
/*   [7][8]은 이 배열을 한 칸씩 훑으며 쌍을 센다.                        */
/* [동작] 바이그램은 앞에 BOS 1개, 뒤에 EOS 1개를 붙인다.                */
/*   cps = [나 비 -1 사 과 -1]                                          */
/*   seq = [BOS 나 비 EOS BOS 사 과 EOS]                                */
/*   쌍: (BOS,나) (나,비) (비,EOS) | (EOS,BOS)는 건너뜀 | (BOS,사) ...    */
/*   길이 L 단어 -> 쌍 L+1개, seq 길이 = 글자 수 + 단어 수 x 2           */
/* [이유] BOS가 1개인 까닭: 바이그램의 문맥은 앞 1글자뿐이라 첫 글자를    */
/*   뽑을 때 "앞에 BOS 하나"면 충분하다 (트라이그램은 2개가 필요하다).    */
/*   단어 사이 (EOS, BOS)는 실제 문맥이 아니므로 [8]에서 세지 않는다.    */
/* ================================================================== */
static int build_seq(const int *cps, long long n_cps)
{
    long long i;
    long long k = 0;

    g_seq_n = g_char_count + g_word_count * 2;   /* 글자 + 단어마다 BOS 1개 + EOS 1개 */
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
    return 1;
}

/* ================================================================== */
/* [7] 해시테이블 (64비트 키)                                           */
/*                                                                     */
/* [목적] "쌍 키 -> 횟수"를 센다. 실제로 나온 쌍만 칸을 차지한다.         */
/* [동작] 오픈 어드레싱 + 선형 탐사                                     */
/*   1) 해시 함수로 키의 첫 칸을 정한다                                  */
/*   2) 같은 키가 있으면 횟수 +1 / 빈 칸이면 새로 넣기 /                  */
/*      다른 키가 있으면(충돌) 바로 옆 칸으로 가서 다시 확인              */
/*   해시 함수 = 피보나치 해싱: 키 x (2^64 / 황금비) 의 상위 bits 비트.   */
/*   곱셈이 키의 모든 비트를 섞어 위쪽으로 올려 보내므로 위쪽 비트를 칸   */
/*   번호로 쓰면 규칙적인 키(같은 문맥의 키는 연속)도 고르게 흩어진다.    */
/* [이유] 키를 unsigned long long(64비트)으로 바꿨다. Step3은 int 키였다. */
/*   바이그램 키는 최대 2452^2 ≈ 600만이라 int로 충분하지만, 트라이그램   */
/*   키는 최대 2452^3 ≈ 147억으로 int(약 21억)를 넘는다. 두 파일의 해시   */
/*   코드를 똑같이 두려고 바이그램도 64비트 키를 쓴다.                    */
/*   빈 칸 표시는 HT_EMPTY(64비트 최댓값). 0은 (가,가) 쌍이라 쓸 수 없다. */
/*   칸 수가 2의 거듭제곱이면 "% 칸수" 대신 "& mask"로 끝 칸 다음 0번 칸  */
/*   으로 돌아갈 수 있다. 70% 넘게 차면 빈 칸까지 멀어져 느려지므로 실패. */
/* ================================================================== */
#define HT_EMPTY   0xFFFFFFFFFFFFFFFFULL
#define FIB_MULT   11400714819323198485ULL   /* 2^64 / 황금비 (Knuth). Step3의 2654435761의 64비트판 */

typedef struct {
    unsigned long long *keys;    /* 칸마다 키. 빈 칸은 HT_EMPTY */
    int                *counts;  /* 칸마다 횟수 */
    unsigned            bits;    /* 칸 수 = 2^bits */
    size_t              mask;    /* 칸 수 - 1 */
    long long           used;    /* 채워진 칸 수 = 서로 다른 키 수 */
} HashTable;

static size_t ht_slot(const HashTable *ht, unsigned long long key)
{
    /* 곱셈은 2^64에서 넘치는 부분이 버려진다(부호 없는 정수 규칙). 상위 bits 비트만 남긴다 */
    return (size_t)((key * FIB_MULT) >> (64u - ht->bits));
}

static int ht_init(HashTable *ht, unsigned bits)
{
    size_t size = (size_t)1 << bits;
    size_t i;

    ht->bits   = bits;
    ht->mask   = size - 1u;
    ht->used   = 0;
    ht->keys   = (unsigned long long *)malloc(size * sizeof(unsigned long long));
    ht->counts = (int *)calloc(size, sizeof(int));
    if (!ht->keys || !ht->counts) {
        fprintf(stderr, "[오류] 해시테이블 메모리 할당 실패 (2^%u칸)\n", bits);
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
            /* 새 키. 적재율 70%를 넘기기 직전이면 거부 (칸수 x 0.7 을 정수로 계산) */
            if ((ht->used + 1) * 10 > (long long)(ht->mask + 1u) * 7) {
                fprintf(stderr, "[오류] 해시테이블이 70%% 넘게 찼습니다. HASH_BITS를 늘리세요\n");
                return 0;
            }
            ht->keys[i]   = key;
            ht->counts[i] = 1;
            ht->used++;
            return 1;
        }
        i = (i + 1u) & ht->mask;   /* 충돌: 옆 칸으로. 끝 칸 다음은 0번 칸 */
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
        i = (i + 1u) & ht->mask;   /* ht_add와 같은 순서로 옆 칸을 찾아간다 */
    }
}

static void ht_free(HashTable *ht)
{
    free(ht->keys);
    free(ht->counts);
    ht->keys = NULL;
    ht->counts = NULL;
}

/* ================================================================== */
/* [8] 세기                                                             */
/*                                                                     */
/* [목적] 사전의 모든 쌍을 해시테이블 g_ht에 센다. 이것이 모델 전부다.    */
/* [동작] seq를 한 칸씩 훑으며 (앞, 다음) 두 번호로 키를 만들어 ht_add 한다. */
/*   키 = 앞 x V + 다음   (V = 2,452)                                     */
/*   예: seq = [BOS 사 회 EOS ...] 라면                                   */
/*     (BOS,사) (사,회) (회,EOS) 를 각각 +1                                */
/*   이 키는 V진법 두 자리 수와 같다. 자리마다 0 ~ 2,451이므로 서로 다른   */
/*   쌍은 반드시 서로 다른 키가 된다 (겹치지 않는다).                     */
/* [이유] 2차원 배열 freq[앞][다음]은 2452^2 ≈ 601만 칸인데 실제로 나온 쌍은 */
/*   17만 개(2.9%)뿐이라, 트라이그램과 같은 해시테이블을 쓴다 ([7] 참고). */
/*   세기가 끝난 해시테이블을 그대로 생성 단계에서 조회하므로 다른 배열로 */
/*   옮기지 않는다. 단어 사이 (EOS, 다음 단어의 BOS)는 실제 문맥이 아니라 */
/*   세지 않는다.                                                        */
/* ================================================================== */
static HashTable g_ht;   /* 모델: 쌍 키 -> 횟수 */

static int count_grams(void)
{
    unsigned long long V = (unsigned long long)g_vocab;
    long long i;

    if (!ht_init(&g_ht, HASH_BITS)) {
        return 0;
    }
    for (i = 0; i + 1 < g_seq_n; i++) {
        unsigned long long key;
        if (g_seq[i] == g_eos) {
            continue;   /* (EOS, 다음 단어의 BOS): 단어 사이는 문맥이 아니다 */
        }
        /* 곱하기 전에 64비트로 바꾼다. int끼리 곱하면 int 범위에서 먼저 넘친다 */
        key = (unsigned long long)g_seq[i] * V + (unsigned long long)g_seq[i + 1];
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
    g_n_grams = (int)g_ht.used;   /* 채워진 칸 하나 = 서로 다른 쌍 하나 */
    return 1;
}

/* 생성 단계에 남는 메모리: 해시테이블의 칸 전부 (빈 칸 포함). 칸마다 키 8바이트 + 횟수 4바이트 */
static size_t model_memory(void)
{
    return (g_ht.mask + 1u) * (sizeof(unsigned long long) + sizeof(int));
}

/* ================================================================== */
/* [9] 뽑기                                                             */
/*                                                                     */
/* [목적] 지금 문맥(앞 1글자) 다음에 올 글자를 빈도에 비례해서 하나 뽑는다. */
/* [동작] 1) ctx_base: 문맥의 첫 키 = 앞 x V                              */
/*      여기에 다음 글자 번호 b를 더하면 쌍 (앞, b)의 키가 된다            */
/*   2) ctx_total: b = 0 ~ 2,451 전부 해시에 물어 횟수를 더한다 = 문맥 합계 */
/*   3) r = 0 ~ (합계 - 1) 난수를 받는다                                   */
/*   4) pick: b = 0, 1, 2, ... 순서로 다시 물으며 횟수를 누적하다가        */
/*      "누적 > r" 이 되는 첫 b를 고른다                                  */
/*   예: 강의 예시(가가 3, 가대 2, 대소 2)에서 문맥 '가'                   */
/*     합계 = 5,  r = 0 ~ 4                                               */
/*     b 순서로 누적: 가에서 3, ... 대에서 5  (나머지 b는 0이라 그대로)     */
/*     r = 0,1,2 -> 누적 3 > r 이 처음 되는 곳 = 가 (확률 3/5)             */
/*     r = 3,4   -> 누적 5 > r 이 처음 되는 곳 = 대 (확률 2/5)             */
/* [이유] 해시테이블은 "(앞, b)가 몇 번?"에는 바로 답하지만               */
/*   "문맥 '앞' 다음에 올 수 있는 후보 목록"은 모른다. 키가 칸 전체에      */
/*   흩어져 있어서다. 그래서 가능한 다음 글자 2,452개를 전부 물어본다      */
/*   (Step3 ② hash_pick과 같은 구조, 키만 64비트). 나온 적 없는 후보는     */
/*   0이 돌아와 누적이 그대로이므로 뽑히지 않는다.                       */
/*   합계를 따로 저장하지 않고 매번 2,452개를 물어 구한다 (사용자 결정    */
/*   (B): 자료구조를 해시테이블 하나로 유지).                             */
/*   '>' 비교가 핵심이다. '>='를 쓰면 위 예에서                          */
/*     r = 3 -> 가의 누적 3 >= 3 이 참이라 대의 몫을 가가 가져가고         */
/*     r = 0 -> 실제 후보보다 번호가 앞선, 한 번도 안 나온 글자가 있으면   */
/*             그 글자(누적 0 >= 0)가 뽑힌다                             */
/*   탐욕적 디코딩도 '>'로 비교해 동률이면 번호가 작은 후보가 남는다.     */
/* ================================================================== */
static unsigned long long ctx_base(int prev)
{
    /* 곱하기 전에 64비트로 바꾼다 ([8]과 같은 이유) */
    return (unsigned long long)prev * (unsigned long long)g_vocab;
}

/* 문맥 합계 = 이 문맥 뒤에 온 쌍 횟수의 합. 한 번도 안 나온 문맥이면 0 */
static long long ctx_total(int prev)
{
    unsigned long long base = ctx_base(prev);
    long long total = 0;
    int b;

    for (b = 0; b < g_vocab; b++) {
        total += ht_get(&g_ht, base + (unsigned long long)b);
    }
    return total;
}

static int pick(int prev, long long r)
{
    unsigned long long base = ctx_base(prev);
    long long cum = 0;
    int b;

    for (b = 0; b < g_vocab; b++) {
        cum += ht_get(&g_ht, base + (unsigned long long)b);
        if (cum > r) {   /* '>' 가 핵심. '>='면 구간 끝 r이 앞 후보에 붙는다 */
            return b;
        }
    }
    return -1;   /* r >= 합계: 부른 쪽의 잘못 */
}

static int greedy(int prev)
{
    unsigned long long base = ctx_base(prev);
    int best = -1;
    int best_count = 0;
    int b;

    for (b = 0; b < g_vocab; b++) {
        int c = ht_get(&g_ht, base + (unsigned long long)b);
        if (c > best_count) {   /* '>' 이므로 동률이면 먼저 나온(번호 작은) 후보 유지 */
            best_count = c;
            best = b;
        }
    }
    return best;
}

/* ================================================================== */
/* [10] 단어 생성                                                       */
/*                                                                     */
/* [목적] 문맥에서 출발해 다음 글자 뽑기를 반복해 단어 하나를 만든다.     */
/* [동작] prev = BOS 에서 출발                                          */
/*   -> 문맥 (prev)로 다음 글자 c를 뽑는다                               */
/*   -> c가 EOS면 끝. 아니면 단어에 붙이고 prev = c (문맥을 한 칸 민다)   */
/*   예: BOS -> 사 -> 회 -> EOS  => "사회"                                */
/* [이유] 탐욕적 디코딩은 루프(예: 리->다->리->다)에 빠지면 EOS를 영영    */
/*   못 만날 수 있어 MAX_LEN자에서 끊고 잘림(~)으로 표시한다. EOS인지     */
/*   먼저 확인한 뒤 길이를 검사하므로 딱 20자에서 끝난 단어는 잘림이 아님. */
/*   문맥 합계가 0이 되는 일은 없다: 모든 글자는 사전 단어 안에서 뒤에     */
/*   글자나 EOS가 온다. 그래도 방어로 검사한다 (합계 0이면 rand_below(0)이 */
/*   되어 뽑을 수 없다).                                                 */
/* ================================================================== */
typedef struct {
    int len;
    int truncated;
    int ids[MAX_LEN];
} Word;

static int generate_word(int greedy_mode, Word *w)
{
    int prev = g_bos;   /* 바이그램 문맥: 앞 1글자. 처음엔 BOS */

    w->len = 0;
    w->truncated = 0;
    for (;;) {
        long long total = ctx_total(prev);
        int next;
        if (total == 0) {
            fprintf(stderr, "[오류] 한 번도 나오지 않은 문맥입니다 (%s)\n", g_names[prev]);
            return 0;
        }
        next = greedy_mode ? greedy(prev) : pick(prev, rand_below(total));
        if (next == g_eos) {
            break;
        }
        if (w->len == MAX_LEN) {
            w->truncated = 1;
            break;
        }
        w->ids[w->len++] = next;
        prev = next;   /* 문맥을 한 칸 민다 */
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
/* [11] 사전 일치율                                                     */
/*                                                                     */
/* [목적] 생성한 단어 중 words.txt에 실제로 있는 단어의 비율을 잰다.      */
/*   바이그램과 트라이그램을 숫자로 비교하는 지표다 (강의 2-6).           */
/* [동작] 단어마다 길이 칸(1 ~ 9, 10 이상은 10)을 정해                   */
/*   gen[칸]++ / 사전에 있으면 hit[칸]++ 하고 전체 합도 센다.             */
/*   비교용으로 사전 자체의 길이 분포 dict[칸]도 센다.                    */
/* [이유] 전체 일치율만 보면 "짧은 단어를 많이 만들어서 높은 것"인지      */
/*   "긴 단어도 잘 맞춰서 높은 것"인지 구분할 수 없다. 그래서 길이별 일치 */
/*   율과 생성 길이 분포를 같이 본다 (바이그램의 1글자 단어 과다 문제).   */
/*   사전 단어 길이는 바이트 수 / 3: [5]에서 모든 글자가 3바이트 한글임을 */
/*   확인했으므로 성립한다.                                              */
/* ================================================================== */
typedef struct {
    long long gen[LEN_BUCKETS + 1];    /* 길이 칸별 생성 수 (0번 칸은 안 씀) */
    long long hit[LEN_BUCKETS + 1];    /* 길이 칸별 사전 일치 수 */
    long long dict[LEN_BUCKETS + 1];   /* 길이 칸별 사전 단어 수 */
    long long total;
    long long hits;
} MatchStats;

static int len_bucket(int len)
{
    return (len >= LEN_BUCKETS) ? LEN_BUCKETS : len;
}

static void match_add(MatchStats *st, const Word *w, int in_dict)
{
    int b = len_bucket(w->len);
    st->gen[b]++;
    st->total++;
    if (in_dict) {
        st->hit[b]++;
        st->hits++;
    }
}

static void count_dict_lengths(MatchStats *st)
{
    long long k;
    for (k = 0; k < g_word_count; k++) {
        st->dict[len_bucket((int)(strlen(g_words[k]) / 3u))]++;
    }
}

/* ================================================================== */
/* [12] 빈도표 파일 (step5_bigram_table.txt)                             */
/*                                                                     */
/* [목적] 모델이 센 쌍 전체를 사람이 읽을 수 있는 표로 남긴다.            */
/* [동작] 한 줄 = 쌍 하나 "앞 \t 다음 \t 빈도 \t 문맥 안 확률(%)"          */
/*   1) sorted_grams: 해시테이블의 채워진 칸만 꺼내 표 순서로 정렬         */
/*      문맥 순서: BOS 먼저, 그다음 글자 번호(가나다) 순                   */
/*      문맥 안 순서: 빈도 내림차순, 같으면 번호 오름차순                 */
/*   2) 정렬하면 같은 문맥의 줄이 붙어 있으므로, 문맥 하나씩 묶어 합계를   */
/*      구하고 확률 = 빈도 / 합계 로 쓴다                                 */
/* [이유] 해시테이블의 칸 순서는 해시 함수가 흩어 놓은 순서라 뒤죽박죽이다. */
/*   표를 읽을 수 있게, 그리고 실행할 때마다 같은 파일이 나오게 정렬한다.  */
/*   이 정렬은 표를 출력하기 위한 것일 뿐 뽑기([9])에는 쓰지 않는다.       */
/*   Step3·Step4의 바이그램 빈도표와 형식을 똑같이 맞췄다. 그래서 이     */
/*   프로그램의 출력이 ../Step4/step4_bigram_table.txt와 바이트 단위로     */
/*   같아야 한다 (verify [6]이 확인). BOS를 맨 앞에 두려고 "BOS =         */
/*   -1"로 보고 비교한다 (disp_id). 확률을 소수 6자리로 쓰는 까닭: 소수    */
/*   2자리면 빈도 1(1/420,309)이 0.00%로 보인다.                          */
/* ================================================================== */
typedef struct {
    int prev;
    int next;
    int count;
} Gram;

/* 표 정렬용 번호: BOS만 -1로 바꿔 맨 앞에 오게 한다 */
static int disp_id(int id)
{
    return (id == g_bos) ? -1 : id;
}

static int cmp_gram_display(const void *x, const void *y)
{
    const Gram *a = (const Gram *)x;
    const Gram *b = (const Gram *)y;
    int ap = disp_id(a->prev);
    int bp = disp_id(b->prev);
    if (ap != bp) return (ap > bp) - (ap < bp);   /* 문맥(앞 글자) 먼저 비교 */
    if (a->count != b->count) {
        return (a->count < b->count) ? 1 : -1;    /* 같은 문맥 안: 빈도 내림차순 */
    }
    return (a->next > b->next) - (a->next < b->next);   /* 빈도도 같으면 번호 오름차순 */
}

/* 같은 문맥인가 (정렬된 배열에서 문맥 묶음의 끝을 찾을 때) */
static int same_ctx(const Gram *a, const Gram *b)
{
    return a->prev == b->prev;
}

/* 해시의 채워진 칸 -> Gram 배열, 표 순서로 정렬. 서로 다른 문맥 수(g_n_ctx)도 센다.
   다 쓰면 free 해야 한다. 실패하면 NULL */
static Gram *sorted_grams(void)
{
    unsigned long long V = (unsigned long long)g_vocab;
    size_t slots = g_ht.mask + 1u;
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
        /* 키 = 앞 x V + 다음 을 거꾸로 푼다 (V진법 두 자리 수의 각 자리) */
        g[k].next  = (int)(key % V);
        g[k].prev  = (int)(key / V);
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

    fprintf(out, "# 바이그램 빈도표\n");
    fprintf(out, "# input_file\t%s\n", DATA_PATH);
    fprintf(out, "# words\t%lld\n", g_word_count);
    fprintf(out, "# total_pairs\t%lld\n", g_total_grams);
    fprintf(out, "# unique_pairs\t%d\n", g_n_grams);
    fprintf(out, "# rows\t%d\n", g_n_ctx);
    fprintf(out, "#\n");
    fprintf(out, "# prev\tnext\tcount\tpercent\n");

    while (i < g_n_grams) {
        long long total = 0;
        int end = i;
        int j;
        while (end < g_n_grams && same_ctx(&g[end], &g[i])) {   /* 문맥 하나의 묶음 [i, end) */
            total += g[end].count;
            end++;
        }
        for (j = i; j < end; j++) {
            fprintf(out, "%s\t%s\t%d\t%.6f\n", g_names[g[j].prev], g_names[g[j].next],
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
/* [목적] 결과 파일 이름 step5_bigram_result_NN.txt의 NN을 정한다.        */
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
/* [14] 검증용 모드 (step5_verify.py가 호출)                            */
/*                                                                     */
/* [목적] 검증기가 C 내부를 들여다보지 않고 출력만으로 확인하게 한다.     */
/* [동작] --dump : 모든 쌍과 빈도 (Python 재계산과 전수 비교)            */
/*     해시테이블의 채워진 칸을 칸 순서대로 출력한다 (순서는 상관없음)    */
/*   --rows : 문맥마다 합계 (난수 범위가 맞는지)                          */
/*     [12]의 정렬 결과를 문맥별로 묶어 더한다                           */
/*   --map  : 표준입력 "앞 r" -> 뽑힌 글자 (구간 경계 한 칸 어긋남 검사)   */
/*   --hist : 한 문맥에서 여러 번 뽑은 횟수 (분포가 맞는지 카이제곱)        */
/*   --map과 --hist는 생성과 똑같은 ctx_total / pick 을 부른다            */
/* [이유] 글자를 명령줄 인자가 아니라 표준입력으로 받는 까닭: Windows는   */
/*   argv를 시스템 코드페이지(CP949)로 넘겨서 Python이 준 UTF-8 한글이    */
/*   깨진다. 표준입력은 보낸 바이트가 그대로 온다 (Step3에서 겪은 문제).  */
/*   --rows를 트라이그램과 같은 방식(정렬 후 묶기)으로 맞췄다. 문맥 합계는 */
/*   [2]가 Python의 독립 계산과 비교한다.                                */
/* ================================================================== */
static int mode_dump(void)
{
    unsigned long long V = (unsigned long long)g_vocab;
    size_t slots = g_ht.mask + 1u;
    size_t s;

    for (s = 0; s < slots; s++) {
        unsigned long long key = g_ht.keys[s];
        if (key != HT_EMPTY) {
            printf("%s\t%s\t%d\n", g_names[key / V], g_names[key % V], g_ht.counts[s]);
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
        printf("%s\t%lld\n", g_names[g[i].prev], total);
        i = end;
    }
    free(g);
    return 0;
}

/* 표준입력의 문맥 글자 이름 -> 번호. 1 = 성공, 0 = 모르는 이름, -1 = 입력 끝 */
static int read_ctx(int *prev)
{
    char name[32];

    if (scanf("%31s", name) != 1) {
        return -1;
    }
    *prev = name_to_id(name);
    return (*prev >= 0) ? 1 : 0;
}

static int mode_map(void)
{
    int prev = -1;
    int rc;

    while ((rc = read_ctx(&prev)) != -1) {
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
    int prev = -1;
    long long total = (read_ctx(&prev) == 1) ? ctx_total(prev) : 0;
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
/*   탐욕적 1개 -> srand(seed) 후 1,000개 생성(시간 측정) -> 사전 일치율   */
/*   집계 -> 결과 파일 (헤더 + 일치율 표 + 단어 목록)                    */
/* [이유] 출력 단어와 일치율 계산 단어를 같은 1,000개로 맞췄다 (사용자     */
/*   결정). 결과 파일에 일치율의 근거가 된 단어가 전부 남으므로 검증기가  */
/*   그 단어들로 일치율을 다시 계산할 수 있다. seed는 기본값이 현재 시각   */
/*   이고 헤더에 기록한다. 트라이그램에 같은 seed를 주면 같은 조건 비교다. */
/*   빈도표를 생성보다 먼저 쓰는 까닭: 서로 다른 문맥 수(g_n_ctx)를 표를   */
/*   정렬하면서 세기 때문에, 화면·결과 헤더에 쓰기 전에 표가 있어야 한다. */
/* ================================================================== */
static void write_word_line(FILE *out, const char *key, const Word *w, char *buf, int show)
{
    int in_dict;
    word_to_string(w, buf);
    in_dict = in_dictionary(buf);
    if (show) {
        printf("%6s  %s%s%s\n", key, buf, in_dict ? "  *" : "", w->truncated ? "  ~" : "");
    }
    fprintf(out, "%s\t%s%s%s\n", key, buf, in_dict ? "\t*" : "", w->truncated ? "\t~" : "");
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
    char share[32];
    int b;

    fprintf(out, "# 바이그램 생성 결과\n");
    fprintf(out, "# input_file\t%s\n", DATA_PATH);
    fprintf(out, "# words\t%lld\n", g_word_count);
    fprintf(out, "# chars\t%d\n", g_chars);
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
    fprintf(out, "#\n");
    fprintf(out, "# 사전 일치율 = 생성 단어 중 words.txt에 있는 단어의 비율 (아래 %d개 단어 기준)\n", count);
    fprintf(out, "# match\thits\ttotal\trate\n");
    format_rate(rate, sizeof rate, st->hits, st->total, 1);
    fprintf(out, "# match_all\t%lld\t%lld\t%s\n", st->hits, st->total, rate);
    fprintf(out, "# len\tgenerated\tin_dict\trate\tdict_share\n");
    for (b = 1; b <= LEN_BUCKETS; b++) {
        format_rate(rate, sizeof rate, st->hit[b], st->gen[b], 1);
        format_rate(share, sizeof share, st->dict[b], g_word_count, 2);
        fprintf(out, "# len_%d%s\t%lld\t%lld\t%s\t%s\n", b, (b == LEN_BUCKETS) ? "+" : "",
                st->gen[b], st->hit[b], rate, share);
    }
    fprintf(out, "#\n");
    fprintf(out, "# mark\t* = 사전에 있는 단어, ~ = 최대 길이에서 잘림\n");
    fprintf(out, "#\n");
}

int main(int argc, char **argv)
{
    static MatchStats st;
    char auto_path[64];
    char word_buf[MAX_LEN * 4 + 1];   /* 한 글자 최대 3바이트 + 여유 */
    const char *out_path;
    const char *table_path;
    unsigned seed;
    int count;
    Word *samples;
    Word greedy_word;
    double t0, build_ms, gen_ms;
    FILE *out;
    int k;
    int b;

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

    printf("[바이그램] 데이터 %s\n", DATA_PATH);
    printf("단어 %lld / 글자 %lld / 글자 종류 %d (BOS/EOS 포함 번호 %d개)\n",
           g_word_count, g_char_count, g_chars, g_vocab);
    printf("쌍 총 %lld / 서로 다른 쌍 %d / 문맥 %d\n", g_total_grams, g_n_grams, g_n_ctx);
    printf("seed %u / 생성 %d개\n", seed, count);

    /* --- 생성 --- */
    samples = (Word *)malloc((size_t)count * sizeof(Word));
    if (!samples || !generate_word(1, &greedy_word)) {
        return 1;
    }
    srand(seed);
    t0 = now_ms();
    for (k = 0; k < count; k++) {
        if (!generate_word(0, &samples[k])) {
            return 1;
        }
    }
    gen_ms = now_ms() - t0;

    /* --- 사전 일치율 --- */
    count_dict_lengths(&st);
    for (k = 0; k < count; k++) {
        word_to_string(&samples[k], word_buf);
        match_add(&st, &samples[k], in_dictionary(word_buf));
    }

    /* --- 결과 파일 --- */
    out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "[오류] 출력 파일을 쓸 수 없습니다: %s\n", out_path);
        return 1;
    }
    write_result(out, seed, count, table_path, build_ms, gen_ms, &st);

    printf("학습 %.3f ms / 생성 %.3f ms / 메모리 %zu bytes\n\n", build_ms, gen_ms, model_memory());
    printf("사전 일치율 %lld / %lld = %.1f%%\n", st.hits, st.total, 100.0 * (double)st.hits / (double)st.total);
    printf("%6s %8s %8s %8s %10s\n", "길이", "생성", "일치", "일치율", "사전비율");
    for (b = 1; b <= LEN_BUCKETS; b++) {
        char rate[32];
        char share[32];
        format_rate(rate, sizeof rate, st.hit[b], st.gen[b], 1);
        format_rate(share, sizeof share, st.dict[b], g_word_count, 2);
        printf("%5d%s %8lld %8lld %8s %10s\n", b, (b == LEN_BUCKETS) ? "+" : " ",
               st.gen[b], st.hit[b], rate, share);
    }
    printf("\n(처음 20개만 화면에 표시, 결과 파일에는 %d개 전부)\n", count);
    write_word_line(out, "greedy", &greedy_word, word_buf, 1);
    for (k = 0; k < count; k++) {
        char key[16];
        snprintf(key, sizeof key, "%d", k + 1);
        write_word_line(out, key, &samples[k], word_buf, k < 20);
    }
    fclose(out);

    printf("\n결과 파일 : %s\n", out_path);
    printf("빈도표    : %s\n", table_path);
    free(samples);
    ht_free(&g_ht);
    return 0;
}
