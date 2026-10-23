#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HANGUL_BASE 0xAC00
#define HANGUL_END  0xD7A3
#define HANGUL_COUNT (HANGUL_END - HANGUL_BASE + 1)

/* UTF-8 바이트열에서 한글 코드포인트 하나를 읽고 다음 위치를 반환 */
static int next_codepoint(const unsigned char *s, int *cp)
{
    if (s[0] >= 0xE0 && s[0] <= 0xEF && s[1] && s[2]) {
        *cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    }
    *cp = 0;
    return 1;
}

typedef struct {
    int codepoint;
    int count;
} CharFreq;

static int cmp_desc(const void *a, const void *b)
{
    return ((const CharFreq *)b)->count - ((const CharFreq *)a)->count;
}

int main(void)
{
    int freq[HANGUL_COUNT] = {0};
    char line[4096];

    FILE *fp = fopen("korean-dict/words.txt", "r");
    if (!fp) {
        fprintf(stderr, "words.txt 파일을 열 수 없습니다.\n");
        return 1;
    }

    while (fgets(line, sizeof(line), fp)) {
        const unsigned char *p = (const unsigned char *)line;
        while (*p) {
            int cp;
            int len = next_codepoint(p, &cp);
            if (cp >= HANGUL_BASE && cp <= HANGUL_END)
                freq[cp - HANGUL_BASE]++;
            p += len;
        }
    }
    fclose(fp);

    /* 사용된 글자만 수집 */
    int used = 0;
    for (int i = 0; i < HANGUL_COUNT; i++)
        if (freq[i] > 0) used++;

    CharFreq *result = malloc(sizeof(CharFreq) * used);
    int idx = 0;
    for (int i = 0; i < HANGUL_COUNT; i++) {
        if (freq[i] > 0) {
            result[idx].codepoint = HANGUL_BASE + i;
            result[idx].count = freq[i];
            idx++;
        }
    }

    qsort(result, used, sizeof(CharFreq), cmp_desc);

    /* 결과를 파일로 저장 */
    FILE *out = fopen("step01_result.csv", "w");
    if (!out) {
        fprintf(stderr, "결과 파일을 생성할 수 없습니다.\n");
        free(result);
        return 1;
    }

    /* UTF-8 BOM */
    fputc(0xEF, out);
    fputc(0xBB, out);
    fputc(0xBF, out);

    fprintf(out, "순위,글자,빈도\n");
    for (int i = 0; i < used; i++) {
        int cp = result[i].codepoint;
        /* 코드포인트를 UTF-8로 변환 */
        unsigned char u[4];
        u[0] = 0xE0 | ((cp >> 12) & 0x0F);
        u[1] = 0x80 | ((cp >> 6) & 0x3F);
        u[2] = 0x80 | (cp & 0x3F);
        u[3] = 0;
        fprintf(out, "%d,%s,%d\n", i + 1, u, result[i].count);
    }
    fclose(out);

    printf("분석 완료: %d개 글자 사용됨\n", used);
    printf("상위 20개:\n");
    for (int i = 0; i < 20 && i < used; i++) {
        int cp = result[i].codepoint;
        unsigned char u[4];
        u[0] = 0xE0 | ((cp >> 12) & 0x0F);
        u[1] = 0x80 | ((cp >> 6) & 0x3F);
        u[2] = 0x80 | (cp & 0x3F);
        u[3] = 0;
        printf("  %2d. %s  %d\n", i + 1, u, result[i].count);
    }

    free(result);
    return 0;
}
