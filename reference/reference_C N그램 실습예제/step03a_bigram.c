#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HANGUL_BASE 0xAC00
#define HANGUL_END  0xD7A3
#define MAX_CHARS 2502  /* 2500 한글 + BOS(인덱스0) + EOS(인덱스1) */

#define BOS_IDX 0
#define EOS_IDX 1
#define BOS_CP  0
#define EOS_CP  1

static int next_codepoint(const unsigned char *s, int *cp)
{
    if (s[0] >= 0xE0 && s[0] <= 0xEF && s[1] && s[2]) {
        *cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    }
    *cp = 0;
    return 1;
}

static void cp_to_utf8(int cp, unsigned char *u)
{
    u[0] = 0xE0 | ((cp >> 12) & 0x0F);
    u[1] = 0x80 | ((cp >> 6) & 0x3F);
    u[2] = 0x80 | (cp & 0x3F);
    u[3] = 0;
}

/* 글자 목록: 인덱스 0=BOS, 1=EOS, 2~=한글 */
static int char_list[MAX_CHARS];
static int char_count = 2; /* BOS, EOS 미리 확보 */
static int char_index[0xD7A4 - 0xAC00];

static int find_or_add(int cp)
{
    int idx = cp - HANGUL_BASE;
    if (char_index[idx] >= 0)
        return char_index[idx];
    if (char_count >= MAX_CHARS)
        return -1;
    char_index[idx] = char_count;
    char_list[char_count] = cp;
    return char_count++;
}

typedef struct {
    int first, second;
    int count;
} BigramEntry;

typedef struct {
    int first, second;
    int count;
} BigramTop;

static int cmp_by_first(const void *a, const void *b)
{
    const BigramEntry *ea = (const BigramEntry *)a;
    const BigramEntry *eb = (const BigramEntry *)b;
    if (ea->first != eb->first)
        return ea->first - eb->first;
    return eb->count - ea->count; /* 같은 앞글자 내에서는 빈도 내림차순 */
}

static int cmp_by_count_desc(const void *a, const void *b)
{
    const BigramTop *ea = (const BigramTop *)a;
    const BigramTop *eb = (const BigramTop *)b;
    return eb->count - ea->count;
}

int main(void)
{
    memset(char_index, -1, sizeof(char_index));
    char_list[BOS_IDX] = BOS_CP;
    char_list[EOS_IDX] = EOS_CP;

    int *bigram = calloc(MAX_CHARS * MAX_CHARS, sizeof(int));
    if (!bigram) {
        fprintf(stderr, "메모리 할당 실패\n");
        return 1;
    }

    FILE *fp = fopen("korean-dict/words.txt", "r");
    if (!fp) {
        fprintf(stderr, "words.txt 파일을 열 수 없습니다.\n");
        free(bigram);
        return 1;
    }

    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        const unsigned char *p = (const unsigned char *)line;
        int prev_idx = BOS_IDX; /* 단어 시작은 BOS */
        int found_any = 0;

        while (*p) {
            int cp;
            int len = next_codepoint(p, &cp);
            if (cp >= HANGUL_BASE && cp <= HANGUL_END) {
                int cur_idx = find_or_add(cp);
                if (cur_idx >= 0) {
                    bigram[prev_idx * MAX_CHARS + cur_idx]++;
                    prev_idx = cur_idx;
                    found_any = 1;
                }
            }
            p += len;
        }

        /* 단어 끝에 EOS */
        if (found_any)
            bigram[prev_idx * MAX_CHARS + EOS_IDX]++;
    }
    fclose(fp);

    /* 0이 아닌 바이그램 수집 */
    int total_bigrams = 0;
    for (int i = 0; i < char_count; i++)
        for (int j = 0; j < char_count; j++)
            if (bigram[i * MAX_CHARS + j] > 0)
                total_bigrams++;

    BigramEntry *entries = malloc(sizeof(BigramEntry) * total_bigrams);
    int n = 0;
    for (int i = 0; i < char_count; i++)
        for (int j = 0; j < char_count; j++) {
            int cnt = bigram[i * MAX_CHARS + j];
            if (cnt > 0) {
                entries[n].first = char_list[i];
                entries[n].second = char_list[j];
                entries[n].count = cnt;
                n++;
            }
        }

    qsort(entries, n, sizeof(BigramEntry), cmp_by_first);

    /* 결과 저장 */
    FILE *out = fopen("step03a_result.csv", "w");
    fputc(0xEF, out); fputc(0xBB, out); fputc(0xBF, out);
    fprintf(out, "앞글자,뒷글자,빈도\n");

    for (int i = 0; i < n; i++) {
        /* 앞글자 출력 */
        if (entries[i].first == BOS_CP)
            fprintf(out, "<BOS>,");
        else if (entries[i].first == EOS_CP)
            fprintf(out, "<EOS>,");
        else {
            unsigned char u[4];
            cp_to_utf8(entries[i].first, u);
            fprintf(out, "%s,", u);
        }
        /* 뒷글자 출력 */
        if (entries[i].second == BOS_CP)
            fprintf(out, "<BOS>,");
        else if (entries[i].second == EOS_CP)
            fprintf(out, "<EOS>,");
        else {
            unsigned char u[4];
            cp_to_utf8(entries[i].second, u);
            fprintf(out, "%s,", u);
        }
        fprintf(out, "%d\n", entries[i].count);
    }
    fclose(out);

    printf("분석 완료: %d개 글자 (BOS/EOS 포함), %d개 바이그램\n", char_count, n);
    printf("\n상위 20개 바이그램:\n");
    BigramTop top[20];
    int top_count = 0;
    for (int i = 0; i < n; i++) {
        if (top_count < 20) {
            top[top_count].first = entries[i].first;
            top[top_count].second = entries[i].second;
            top[top_count].count = entries[i].count;
            top_count++;
            continue;
        }

        int min_idx = 0;
        for (int j = 1; j < 20; j++) {
            if (top[j].count < top[min_idx].count)
                min_idx = j;
        }
        if (entries[i].count > top[min_idx].count) {
            top[min_idx].first = entries[i].first;
            top[min_idx].second = entries[i].second;
            top[min_idx].count = entries[i].count;
        }
    }
    qsort(top, top_count, sizeof(BigramTop), cmp_by_count_desc);

    for (int i = 0; i < top_count; i++) {
        char s1[16], s2[16];
        if (top[i].first == BOS_CP) strcpy(s1, "<BOS>");
        else if (top[i].first == EOS_CP) strcpy(s1, "<EOS>");
        else { unsigned char u[4]; cp_to_utf8(top[i].first, u); strcpy(s1, (char*)u); }

        if (top[i].second == BOS_CP) strcpy(s2, "<BOS>");
        else if (top[i].second == EOS_CP) strcpy(s2, "<EOS>");
        else { unsigned char u[4]; cp_to_utf8(top[i].second, u); strcpy(s2, (char*)u); }

        printf("  %2d. %s %s  %d\n", i + 1, s1, s2, top[i].count);
    }

    free(entries);
    free(bigram);
    return 0;
}
