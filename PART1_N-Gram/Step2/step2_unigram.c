/*
 * step2_unigram.c - [STEP 2] 유니그램: 글자 빈도표로 글자를 생성한다
 *
 * 목적
 *   STEP 1이 만든 빈도표(= 학습된 모델)를 불러와 글자를 뽑는다.
 *   - 탐욕적 디코딩 : 항상 최고 확률 글자만 선택 -> "다다다다다"
 *   - 확률적 샘플링 : 확률 분포대로 난수로 선택 -> "다는하다리" 처럼 다양하게
 *
 * 입력
 *   ../Step1/step1_char_freq_result.txt (STEP 1 결과. git에 커밋돼 있어 항상 존재)
 *   words.txt에서 다시 세지 않는다. STEP 1 = 학습/저장, STEP 2 = 불러오기/생성.
 *
 * 사용법
 *   step2_unigram.exe [글자수] [생성개수] [seed] [빈도표] [출력파일]
 *     기본값: 10  30  (현재 시각)  ../Step1/step1_char_freq_result.txt  (자동 번호)
 *
 *   - 출력파일을 생략하면 step2_unigram_result_01.txt ~ _99.txt 중
 *     "기존 최대 번호 + 1"로 저장한다. 실행할 때마다 결과가 쌓인다.
 *   - seed를 생략하면 현재 시각을 쓴다. 실행마다 다른 결과가 나오게 하기 위함.
 *     쓰인 seed는 결과 파일 헤더에 기록되므로, 그 값을 seed로 주면 같은 결과를 재현한다.
 *
 *   검증용 모드 (step2_verify.py가 사용)
 *   step2_unigram.exe --hist <샘플수> <seed> [빈도표]
 *     샘플을 대량으로 뽑아 글자별 횟수를 출력 (분포 검사용)
 *   step2_unigram.exe --map [빈도표]
 *     표준입력의 난수 r을 한 줄씩 받아 선택된 글자를 출력 (구간 경계 검사용)
 *
 * 빌드
 *   build.bat                (기본 대상이 step2_unigram)
 *   build.bat run            (빌드 후 실행)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define MAX_KINDS    11172  /* 한글 음절 전체 수. 빈도표의 글자 종류가 이보다 많을 수 없다 */
#define CHAR_BYTES   8      /* UTF-8 글자 1개(최대 4바이트) + 널 + 여유 */
#define LINE_BUF     256

#define DEFAULT_TABLE  "../Step1/step1_char_freq_result.txt"
#define OUTPUT_PREFIX  "step2_unigram_result_"   /* 뒤에 01 ~ 99 + ".txt" */
#define MAX_RUN_NO     99                        /* 파일명 번호가 두 자리라서 99까지 */
#define DEFAULT_LENGTH 10
#define DEFAULT_COUNT  30

/* rand30()이 만드는 난수 범위. 전체 빈도 합이 이보다 크면 뽑을 수 없는 글자가 생긴다 */
#define RAND30_RANGE (1ULL << 30)

/* ------------------------------------------------------------------ */
/* 빈도표                                                              */
/*                                                                     */
/* 글자는 UTF-8 바이트열 그대로 보관한다. STEP 1과 달리 코드포인트로   */
/* 바꿀 일이 없다. 뽑아서 그대로 출력만 하면 되기 때문이다.            */
/* ------------------------------------------------------------------ */
typedef struct {
    char      ch[CHAR_BYTES];
    long long count;
} Entry;

static Entry     g_table[MAX_KINDS];
static long long g_cum[MAX_KINDS];   /* 누적 빈도. g_cum[i] = count[0] + ... + count[i] */
static int       g_kinds = 0;
static long long g_total = 0;        /* = g_cum[g_kinds - 1] */

static int load_table(const char *path)
{
    FILE *fp;
    char line[LINE_BUF];
    long long header_total = -1;

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "[오류] 빈도표 파일을 열 수 없습니다: %s\n", path);
        return 0;
    }

    while (fgets(line, sizeof line, fp)) {
        int rank;
        char ch[CHAR_BYTES];
        long long count;

        /*
         * git(core.autocrlf=true)이 체크아웃하면서 LF를 CRLF로 바꿔놓을 수 있다.
         * "rb"로 읽으면 \r이 그대로 남으므로 직접 잘라낸다.
         */
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0') {
            continue;
        }
        if (line[0] == '#') {
            /* 헤더의 전체 글자수는 아래에서 빈도 합계와 대조하는 데 쓴다 */
            sscanf(line, "# total_chars\t%lld", &header_total);
            continue;
        }

        /* 데이터 행: 순위 \t 글자 \t 빈도 \t 확률. 확률은 쓰지 않는다(정수 빈도로 계산) */
        if (sscanf(line, "%d\t%7s\t%lld", &rank, ch, &count) != 3 || count <= 0) {
            fprintf(stderr, "[오류] 빈도표 형식이 잘못됐습니다: %s\n", line);
            fclose(fp);
            return 0;
        }
        if (g_kinds >= MAX_KINDS) {
            fprintf(stderr, "[오류] 빈도표 글자 종류가 %d개를 넘습니다\n", MAX_KINDS);
            fclose(fp);
            return 0;
        }

        strcpy(g_table[g_kinds].ch, ch);
        g_table[g_kinds].count = count;
        g_total += count;
        g_cum[g_kinds] = g_total;   /* 누적 빈도는 읽으면서 바로 만든다 */
        g_kinds++;
    }
    fclose(fp);

    if (g_kinds == 0) {
        fprintf(stderr, "[오류] 빈도표가 비어 있습니다: %s\n", path);
        return 0;
    }
    /* 파일이 잘렸거나 행이 빠졌으면 여기서 걸린다 */
    if (header_total >= 0 && header_total != g_total) {
        fprintf(stderr, "[오류] 빈도 합계(%lld)가 헤더의 total_chars(%lld)와 다릅니다\n",
                g_total, header_total);
        return 0;
    }
    if ((unsigned long long)g_total > RAND30_RANGE) {
        fprintf(stderr, "[오류] 빈도 합계가 난수 범위(2^30)를 넘습니다: %lld\n", g_total);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* 난수                                                                */
/* ------------------------------------------------------------------ */

/*
 * MSVC의 rand()는 0 ~ 32,767(RAND_MAX, 15비트)만 낸다. 전체 빈도 합 1,551,565보다 훨씬 작다.
 * rand() % 1551565 를 쓰면 r이 32,767을 못 넘는다. '다'의 구간이 0 ~ 76,240이므로
 * 결국 '다'만 뽑힌다 -- 확률적 샘플링인데 탐욕적 디코딩과 같은 결과가 나오는 셈.
 * 그래서 15비트 두 개를 이어 붙여 30비트(0 ~ 1,073,741,823)를 만든다.
 *
 * - & 0x7FFF : RAND_MAX가 더 큰 컴파일러(gcc 등)에서도 정확히 15비트씩만 쓰게 한다
 * - hi, lo를 별도 문장으로 받는 이유: (rand() << 15) | rand() 로 쓰면 C는 두 rand()의
 *   호출 순서를 보장하지 않는다. 순서가 바뀌면 같은 seed에서 다른 결과가 나온다.
 */
static unsigned long long rand30(void)
{
    unsigned long long hi = (unsigned long long)(rand() & 0x7FFF);
    unsigned long long lo = (unsigned long long)(rand() & 0x7FFF);
    return (hi << 15) | lo;
}

/*
 * 0 ~ n-1 균등 난수.
 * rand30() % n 만 하면 2^30이 n으로 나눠떨어지지 않아 앞쪽 값이 아주 조금 더 자주 나온다
 * (모듈로 편향). n의 배수가 끝나는 지점(limit) 이상이 나오면 버리고 다시 뽑는다.
 * n = 1,551,565 기준 버려질 확률은 0.1% 정도라 속도 영향은 없다.
 */
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

/* ------------------------------------------------------------------ */
/* 샘플링: 누적 빈도 + 이진 탐색                                       */
/*                                                                     */
/*   글자  빈도   누적     r 범위                                      */
/*   다   76241  76241    0 ~ 76240                                    */
/*   하   48158  124399   76241 ~ 124398                               */
/*   ...                                                               */
/* r이 주어지면 "누적 > r" 인 첫 글자가 정답이다.                      */
/* 누적 배열은 오름차순이므로 이진 탐색으로 약 12번(log2 2450) 만에    */
/* 찾는다. 처음부터 하나씩 보면 최악 2,450번이다.                      */
/* ------------------------------------------------------------------ */
static int sample_index(long long r)
{
    int lo = 0;
    int hi = g_kinds - 1;

    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        /*
         * 경계 조건이 이 함수의 전부다. '>' 를 '>=' 로 쓰면 각 구간의 마지막 r이
         * 다음 글자로 넘어가는 한 칸 어긋남이 생긴다. 확률로는 1/155만 차이라
         * 출력만 봐서는 절대 모른다 -> step2_verify.py가 경계값을 전수 검사한다.
         */
        if (g_cum[mid] > r) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return lo;
}

/* 탐욕적 디코딩용: 빈도 최대 글자. 동률이면 빈도표에서 앞에 있는 글자 */
static int argmax_index(void)
{
    int best = 0;
    int i;

    for (i = 1; i < g_kinds; i++) {
        if (g_table[i].count > g_table[best].count) {
            best = i;
        }
    }
    return best;
}

/* ------------------------------------------------------------------ */
/* 검증용 모드                                                         */
/* ------------------------------------------------------------------ */

/* --hist: n번 뽑아 글자별 횟수를 빈도표 순서대로 출력 */
static int run_hist(long long n, unsigned int seed)
{
    static long long hist[MAX_KINDS];
    long long k;
    int i;

    srand(seed);
    for (k = 0; k < n; k++) {
        hist[sample_index(rand_below(g_total))]++;
    }
    for (i = 0; i < g_kinds; i++) {
        printf("%s\t%lld\n", g_table[i].ch, hist[i]);
    }
    return 0;
}

/* --map: 표준입력의 r마다 선택된 글자를 출력. 난수를 거치지 않고 탐색만 검사한다 */
static int run_map(void)
{
    long long r;

    while (scanf("%lld", &r) == 1) {
        if (r < 0 || r >= g_total) {
            fprintf(stderr, "[오류] r은 0 ~ %lld 범위여야 합니다: %lld\n", g_total - 1, r);
            return 1;
        }
        printf("%s\n", g_table[sample_index(r)].ch);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 결과 파일 자동 번호: step2_unigram_result_NN.txt                    */
/*                                                                     */
/* 01 ~ 99를 전부 확인해 "가장 큰 기존 번호 + 1"을 쓴다.               */
/* 빈 번호(중간에 지운 파일)를 채우지 않는 이유: 번호 순서가 곧 실행   */
/* 순서가 되도록. 표준 C에는 폴더 목록 함수가 없어서 fopen으로 확인.   */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    int length;
    int count;
    unsigned int seed;
    const char *table_path;
    const char *out_path;
    char auto_path[64];
    int greedy;
    int k;
    int j;
    FILE *out;

#ifdef _WIN32
    /* Windows 콘솔은 기본 코드페이지가 UTF-8이 아니다. 없으면 한글이 깨진다 */
    SetConsoleOutputCP(CP_UTF8);
#endif

    /* --- 검증용 모드 --- */
    if (argc > 1 && strcmp(argv[1], "--hist") == 0) {
        if (argc < 4) {
            fprintf(stderr, "사용법: step2_unigram.exe --hist <샘플수> <seed> [빈도표]\n");
            return 1;
        }
        if (!load_table((argc > 4) ? argv[4] : DEFAULT_TABLE)) {
            return 1;
        }
        return run_hist(atoll(argv[2]), (unsigned int)strtoul(argv[3], NULL, 10));
    }
    if (argc > 1 && strcmp(argv[1], "--map") == 0) {
        if (!load_table((argc > 2) ? argv[2] : DEFAULT_TABLE)) {
            return 1;
        }
        return run_map();
    }

    /* --- 일반 모드: 생성 --- */
    length     = (argc > 1) ? atoi(argv[1]) : DEFAULT_LENGTH;
    count      = (argc > 2) ? atoi(argv[2]) : DEFAULT_COUNT;
    /* seed 생략 시 현재 시각. 고정값이면 여러 번 실행해도 결과가 매번 같아진다 */
    seed       = (argc > 3) ? (unsigned int)strtoul(argv[3], NULL, 10) : (unsigned int)time(NULL);
    table_path = (argc > 4) ? argv[4] : DEFAULT_TABLE;
    out_path   = (argc > 5) ? argv[5] : NULL;
    if (out_path == NULL) {
        if (!next_output_path(auto_path, sizeof auto_path)) {
            return 1;
        }
        out_path = auto_path;
    }

    if (length <= 0) {
        length = DEFAULT_LENGTH;
    }
    if (count <= 0) {
        count = DEFAULT_COUNT;
    }

    if (!load_table(table_path)) {
        return 1;
    }

    out = fopen(out_path, "wb");   /* UTF-8 바이트와 LF를 그대로 쓴다 */
    if (!out) {
        fprintf(stderr, "[오류] 출력 파일을 쓸 수 없습니다: %s\n", out_path);
        return 1;
    }

    printf("빈도표           : %s (%d종, %lld자)\n", table_path, g_kinds, g_total);
    printf("seed             : %u\n", seed);
    printf("글자수 x 개수    : %d x %d\n", length, count);

    fprintf(out, "# 유니그램 생성 결과\n");
    fprintf(out, "# table_file\t%s\n", table_path);
    fprintf(out, "# table_kinds\t%d\n", g_kinds);
    fprintf(out, "# table_total\t%lld\n", g_total);
    fprintf(out, "# seed\t%u\n", seed);
    fprintf(out, "# length\t%d\n", length);
    fprintf(out, "# count\t%d\n", count);
    fprintf(out, "#\n");

    /* --- 탐욕적 디코딩: 난수를 쓰지 않으므로 몇 번을 돌려도 같다 --- */
    greedy = argmax_index();
    printf("\n[탐욕적 디코딩] 항상 최고 확률 글자만 선택\n      ");
    fprintf(out, "greedy\t");
    for (j = 0; j < length; j++) {
        printf("%s", g_table[greedy].ch);
        fprintf(out, "%s", g_table[greedy].ch);
    }
    printf("\n");
    fprintf(out, "\n");

    /* --- 확률적 샘플링 --- */
    srand(seed);   /* 같은 seed면 같은 결과. 결과 파일 헤더의 seed로 재현할 수 있는 근거 */
    printf("\n[확률적 샘플링] 확률 분포대로 선택\n");
    for (k = 1; k <= count; k++) {
        printf("%4d  ", k);
        fprintf(out, "%d\t", k);
        for (j = 0; j < length; j++) {
            const char *ch = g_table[sample_index(rand_below(g_total))].ch;
            printf("%s", ch);
            fprintf(out, "%s", ch);
        }
        printf("\n");
        fprintf(out, "\n");
    }

    fclose(out);
    printf("\n결과 파일 저장   : %s\n", out_path);
    return 0;
}
