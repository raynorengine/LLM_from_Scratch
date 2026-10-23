#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_CHARS 3000

typedef struct {
    int codepoint;
    int count;
    int cumulative;
} CharEntry;

int main(void)
{
    FILE *fp = fopen("step01_result.csv", "r");
    if (!fp) {
        fprintf(stderr, "step01_result.csv 파일을 열 수 없습니다.\n");
        return 1;
    }

    CharEntry entries[MAX_CHARS];
    int n = 0;
    char line[256];

    /* BOM 건너뛰기 */
    int c = fgetc(fp);
    if (c == 0xEF) { fgetc(fp); fgetc(fp); }
    else ungetc(c, fp);

    /* 헤더 건너뛰기 */
    fgets(line, sizeof(line), fp);

    /* CSV 읽기: 순위,글자(UTF-8),빈도 */
    while (fgets(line, sizeof(line), fp) && n < MAX_CHARS) {
        /* 첫 번째 콤마 찾기 (순위 뒤) */
        char *p = strchr(line, ',');
        if (!p) continue;
        p++; /* 글자 시작 */

        /* UTF-8 3바이트로 코드포인트 추출 */
        unsigned char *u = (unsigned char *)p;
        if (u[0] < 0xE0) continue;
        int cp = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);

        /* 두 번째 콤마 뒤의 빈도 */
        char *p2 = strchr(p, ',');
        if (!p2) continue;
        int count = atoi(p2 + 1);

        entries[n].codepoint = cp;
        entries[n].count = count;
        n++;
    }
    fclose(fp);

    /* 누적 빈도 계산 */
    entries[0].cumulative = entries[0].count;
    for (int i = 1; i < n; i++)
        entries[i].cumulative = entries[i - 1].cumulative + entries[i].count;
    int total = entries[n - 1].cumulative;

    /* 생성할 글자 수 */
    int gen_len = 100;
    printf("생성할 글자 수 (기본 100): ");
    fflush(stdout);
    if (fgets(line, sizeof(line), stdin) && atoi(line) > 0)
        gen_len = atoi(line);

    /* 난수 기반 글자 생성 (MSVC rand()는 최대 32767이므로 조합하여 큰 난수 생성) */
    srand((unsigned)time(NULL));

    FILE *out = fopen("step02_result.txt", "w");
    /* UTF-8 BOM */
    fputc(0xEF, out);
    fputc(0xBB, out);
    fputc(0xBF, out);

    for (int i = 0; i < gen_len; i++) {
        int r = (rand() * 32768 + rand()) % total;

        /* 누적 빈도에서 해당 글자 찾기 */
        int lo = 0, hi = n - 1;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (entries[mid].cumulative <= r)
                lo = mid + 1;
            else
                hi = mid;
        }

        int cp = entries[lo].codepoint;
        unsigned char u[4];
        u[0] = 0xE0 | ((cp >> 12) & 0x0F);
        u[1] = 0x80 | ((cp >> 6) & 0x3F);
        u[2] = 0x80 | (cp & 0x3F);
        u[3] = 0;

        fprintf(out, "%s", u);
        printf("%s", u);

        /* 10글자마다 줄바꿈 */
        if ((i + 1) % 10 == 0) {
            fprintf(out, "\n");
            printf("\n");
        }
    }
    printf("\n");
    fprintf(out, "\n");
    fclose(out);

    printf("\n결과가 step02_result.txt에 저장되었습니다.\n");
    return 0;
}
