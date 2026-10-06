/*
 * step1_char_freq.c - [STEP 1] 국어사전 데이터의 글자(음절) 빈도 계산
 *
 * 목적
 *   words.txt(1줄 = 1단어)를 읽어 글자별 등장 횟수와 비율을 구한다.
 *   "한국어에서 각 글자가 얼마나 자주 나타나는가"에 답하는 프로그램.
 *
 * 출력
 *   - 콘솔 : 요약 + 상위 N개
 *   - 파일 : 전체 글자(기본 2,450종) 빈도표. 검증 프로그램이 읽는다.
 *
 * 사용법
 *   step1_char_freq.exe [입력파일] [상위N개] [출력파일]
 *   기본값: ../data/korean_dictionary/words.txt  10  step1_char_freq_result.txt
 *
 * 빌드
 *   build.bat step1_char_freq        (MSVC cl.exe 사용)
 *   build.bat step1_char_freq run    (빌드 후 실행)
 */

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#endif

/*
 * 한글 음절은 유니코드에서 '가'(U+AC00)부터 '힣'(U+D7A3)까지 번호가 연속이다.
 * 덕분에 (코드포인트 - 0xAC00)을 배열 인덱스로 그대로 쓸 수 있다.
 *
 * 그래서 이 프로그램에는 해시테이블이 없다. 크기 11,172 배열 하나가 전부다.
 * 해시 계산도, 충돌 처리도, 동적 확장도 필요 없다.
 */
#define HANGUL_BASE  0xAC00u
#define HANGUL_LAST  0xD7A3u
#define HANGUL_COUNT (HANGUL_LAST - HANGUL_BASE + 1u)  /* 11172 */

#define DEFAULT_INPUT  "../data/korean_dictionary/words.txt"
#define DEFAULT_OUTPUT "step1_char_freq_result.txt"
#define DEFAULT_TOPN   10

/* ------------------------------------------------------------------ */
/* UTF-8 디코딩                                                        */
/*                                                                     */
/* C의 char는 1바이트인데 한글 한 글자는 UTF-8로 3바이트다.            */
/*   '다' -> ED 8B A4                                                  */
/* 그래서 바이트를 그냥 하나씩 읽으면 한 글자가 세 조각으로 쪼개진다.  */
/* 선두 바이트를 보고 몇 바이트를 묶어야 하는지 먼저 알아내야 한다.    */
/* ------------------------------------------------------------------ */

/* 선두 바이트로 이 글자가 몇 바이트인지 판단. 잘못된 바이트면 0 */
static int utf8_len(unsigned char b)
{
    if (b < 0x80u)            return 1;  /* 0xxxxxxx : ASCII (개행 포함) */
    if ((b & 0xE0u) == 0xC0u) return 2;  /* 110xxxxx */
    if ((b & 0xF0u) == 0xE0u) return 3;  /* 1110xxxx : 한글이 여기 */
    if ((b & 0xF8u) == 0xF0u) return 4;  /* 11110xxx */
    return 0;                            /* 10xxxxxx 등은 선두가 될 수 없음 */
}

/* 바이트열 -> 코드포인트. 선두 바이트의 유효 비트 + 이어지는 6비트씩 */
static unsigned int utf8_decode(const unsigned char *p, int len)
{
    unsigned int cp;
    int i;

    switch (len) {
        case 1: return p[0];
        case 2: cp = p[0] & 0x1Fu; break;
        case 3: cp = p[0] & 0x0Fu; break;
        case 4: cp = p[0] & 0x07u; break;
        default: return 0;
    }
    for (i = 1; i < len; i++) {
        cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    return cp;
}

/* 코드포인트 -> UTF-8 바이트열 (출력용). 널 종료 문자열로 만든다 */
static void utf8_encode(unsigned int cp, char out[5])
{
    if (cp < 0x80u) {
        out[0] = (char)cp;
        out[1] = '\0';
    } else if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        out[2] = '\0';
    } else if (cp < 0x10000u) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        out[3] = '\0';
    } else {
        out[0] = (char)(0xF0u | (cp >> 18));
        out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[3] = (char)(0x80u | (cp & 0x3Fu));
        out[4] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* 정렬                                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned int cp;    /* 코드포인트 */
    long long    count; /* 등장 횟수 */
} Entry;

/* 빈도 내림차순. 같으면 코드포인트 오름차순(결과를 재현 가능하게) */
static int cmp_desc(const void *a, const void *b)
{
    const Entry *x = (const Entry *)a;
    const Entry *y = (const Entry *)b;

    if (x->count != y->count) {
        return (y->count > x->count) ? 1 : -1;
    }
    if (x->cp != y->cp) {
        return (x->cp > y->cp) ? 1 : -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 파일 전체를 메모리로 읽기 (약 5MB라 한 번에 올려도 무방)            */
/* ------------------------------------------------------------------ */
static unsigned char *read_all(const char *path, long *out_size)
{
    FILE *fp;
    long size;
    unsigned char *buf;
    size_t got;

    /* 반드시 바이너리 모드. 텍스트 모드는 Windows에서 개행을 변환한다 */
    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "[오류] 입력 파일을 열 수 없습니다: %s\n", path);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size < 0) {
        fprintf(stderr, "[오류] 파일 크기를 알 수 없습니다: %s\n", path);
        fclose(fp);
        return NULL;
    }

    buf = (unsigned char *)malloc((size_t)size + 1);
    if (!buf) {
        fprintf(stderr, "[오류] 메모리 할당 실패 (%ld bytes)\n", size);
        fclose(fp);
        return NULL;
    }

    got = fread(buf, 1, (size_t)size, fp);
    fclose(fp);

    buf[got] = '\0';
    *out_size = (long)got;
    return buf;
}

/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    const char *in_path  = (argc > 1) ? argv[1] : DEFAULT_INPUT;
    int         topn     = (argc > 2) ? atoi(argv[2]) : DEFAULT_TOPN;
    const char *out_path = (argc > 3) ? argv[3] : DEFAULT_OUTPUT;

    unsigned char *buf;
    long size;
    long i;

    long long *count;        /* 글자별 빈도. 인덱스 = cp - 0xAC00 */
    long long total = 0;     /* 전체 글자수 (개행 제외) */
    long long newlines = 0;  /* 건너뛴 개행 수 */
    long long others = 0;    /* 한글 음절 범위를 벗어난 문자 수 */
    int kinds = 0;           /* 등장한 고유 글자 종류 */

    Entry *entries;
    int n = 0;
    int rank;
    char ch[5];
    FILE *out;

#ifdef _WIN32
    /*
     * Windows 콘솔은 기본 코드페이지가 UTF-8이 아니다.
     * 이걸 안 하면 한글이 전부 깨져서 출력된다.
     */
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (topn <= 0) {
        topn = DEFAULT_TOPN;
    }

    buf = read_all(in_path, &size);
    if (!buf) {
        return 1;
    }

    count = (long long *)calloc(HANGUL_COUNT, sizeof(long long));
    if (!count) {
        fprintf(stderr, "[오류] 빈도표 메모리 할당 실패\n");
        free(buf);
        return 1;
    }

    /* --- 한 글자씩 읽어 빈도 누적 --- */
    i = 0;
    while (i < size) {
        int len = utf8_len(buf[i]);
        unsigned int cp;

        if (len == 0) {           /* 깨진 바이트는 1바이트 건너뛰고 계속 */
            i++;
            others++;
            continue;
        }
        if (i + len > size) {     /* 파일 끝에서 잘린 경우 */
            break;
        }

        cp = utf8_decode(&buf[i], len);
        i += len;

        /*
         * 개행은 글자가 아니라 단어 구분자이므로 세지 않는다.
         * 이걸 세면 전체 글자수가 1,551,565 -> 1,971,874가 되고
         * '다'의 확률이 4.91%가 아니라 3.87%로 나온다.
         */
        if (cp == '\n' || cp == '\r') {
            newlines++;
            continue;
        }

        if (cp >= HANGUL_BASE && cp <= HANGUL_LAST) {
            count[cp - HANGUL_BASE]++;
            total++;
        } else {
            others++;         /* 현재 데이터에는 없지만 방어적으로 집계 */
        }
    }

    free(buf);

    /* --- 0이 아닌 것만 모아 정렬 --- */
    entries = (Entry *)malloc(HANGUL_COUNT * sizeof(Entry));
    if (!entries) {
        fprintf(stderr, "[오류] 정렬용 메모리 할당 실패\n");
        free(count);
        return 1;
    }
    for (i = 0; i < (long)HANGUL_COUNT; i++) {
        if (count[i] > 0) {
            entries[n].cp = HANGUL_BASE + (unsigned int)i;
            entries[n].count = count[i];
            n++;
        }
    }
    kinds = n;
    qsort(entries, (size_t)n, sizeof(Entry), cmp_desc);

    free(count);

    /* --- 콘솔 출력: 요약 + 상위 N개 --- */
    printf("입력 파일        : %s\n", in_path);
    printf("전체 글자수      : %lld\n", total);
    printf("고유 글자 종류   : %d\n", kinds);
    printf("건너뛴 개행      : %lld\n", newlines);
    printf("범위 외 문자     : %lld\n", others);
    printf("\n상위 %d개 글자\n", (topn < n) ? topn : n);
    printf("순위  글자      빈도      확률\n");
    printf("----  ----  --------  --------\n");

    for (rank = 0; rank < topn && rank < n; rank++) {
        utf8_encode(entries[rank].cp, ch);
        printf("%4d  %-4s  %8lld  %7.2f%%\n",
               rank + 1, ch, entries[rank].count,
               (total > 0) ? (double)entries[rank].count * 100.0 / (double)total : 0.0);
    }

    /* --- 파일 출력: 전체 글자 빈도표 (검증 프로그램용) --- */
    out = fopen(out_path, "wb");   /* UTF-8 바이트를 그대로 쓴다 */
    if (!out) {
        fprintf(stderr, "\n[오류] 출력 파일을 쓸 수 없습니다: %s\n", out_path);
        free(entries);
        return 1;
    }

    fprintf(out, "# 글자 빈도 계산 결과\n");
    fprintf(out, "# input_file\t%s\n", in_path);
    fprintf(out, "# total_chars\t%lld\n", total);
    fprintf(out, "# unique_chars\t%d\n", kinds);
    fprintf(out, "# newlines\t%lld\n", newlines);
    fprintf(out, "# out_of_range\t%lld\n", others);
    fprintf(out, "#\n");
    fprintf(out, "# rank\tchar\tcount\tpercent\n");

    for (rank = 0; rank < n; rank++) {
        utf8_encode(entries[rank].cp, ch);
        fprintf(out, "%d\t%s\t%lld\t%.6f\n",
                rank + 1, ch, entries[rank].count,
                (total > 0) ? (double)entries[rank].count * 100.0 / (double)total : 0.0);
    }

    fclose(out);
    printf("\n결과 파일 저장   : %s (%d행)\n", out_path, n);

    free(entries);
    return 0;
}
