#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HANGUL_BASE 0xAC00
#define HANGUL_END  0xD7A3
#define MAX_CHARS 2502

#define BOS_CP 0
#define EOS_CP 1
#define BOS_IDX 0
#define EOS_IDX 1

static void cp_to_utf8(int cp, unsigned char *u)
{
    u[0] = 0xE0 | ((cp >> 12) & 0x0F);
    u[1] = 0x80 | ((cp >> 6) & 0x3F);
    u[2] = 0x80 | (cp & 0x3F);
    u[3] = 0;
}

static int big_rand(int max)
{
    return (rand() * 32768 + rand()) % max;
}

/* 글자 목록: 0=BOS, 1=EOS, 2~=한글 */
static int char_list[MAX_CHARS];
static int char_count = 2;
static int cp_to_idx[0xD7A4 - 0xAC00];

static int find_or_add_hangul(int cp)
{
    int idx = cp - HANGUL_BASE;
    if (cp_to_idx[idx] >= 0)
        return cp_to_idx[idx];
    if (char_count >= MAX_CHARS)
        return -1;
    cp_to_idx[idx] = char_count;
    char_list[char_count] = cp;
    return char_count++;
}

static int find_idx(int cp)
{
    if (cp == BOS_CP) return BOS_IDX;
    if (cp == EOS_CP) return EOS_IDX;
    int idx = cp - HANGUL_BASE;
    if (idx >= 0 && idx < (0xD7A4 - 0xAC00) && cp_to_idx[idx] >= 0)
        return cp_to_idx[idx];
    return find_or_add_hangul(cp);
}

/* 바이그램: 각 앞글자별 다음 글자 후보 */
typedef struct {
    int next_idx;
    int count;
} NextChar;

static NextChar *bi_list[MAX_CHARS];
static int bi_count[MAX_CHARS];
static int bi_total[MAX_CHARS];
static int *bi_cum[MAX_CHARS];

/* 사전 (검증용) */
#define MAX_DICT 430000
static char **dict;
static int dict_count = 0;

static int dict_search(const char *word)
{
    int lo = 0, hi = dict_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = strcmp(dict[mid], word);
        if (cmp == 0) return 1;
        if (cmp < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return 0;
}

int main(void)
{
    memset(cp_to_idx, -1, sizeof(cp_to_idx));
    memset(bi_count, 0, sizeof(bi_count));
    memset(bi_total, 0, sizeof(bi_total));
    char_list[BOS_IDX] = BOS_CP;
    char_list[EOS_IDX] = EOS_CP;

    /* step03a_result.csv 로드 */
    FILE *fp = fopen("step03a_result.csv", "r");
    if (!fp) {
        fprintf(stderr, "step03a_result.csv를 열 수 없습니다.\n");
        return 1;
    }

    char line[256];
    int c = fgetc(fp);
    if (c == 0xEF) { fgetc(fp); fgetc(fp); } else ungetc(c, fp);
    fgets(line, sizeof(line), fp); /* 헤더 */

    /* 1차: 각 앞글자별 후보 수 세기 */
    int *temp_count = calloc(MAX_CHARS, sizeof(int));
    long data_start = ftell(fp);

    while (fgets(line, sizeof(line), fp)) {
        int cp1;
        if (strncmp(line, "<BOS>,", 6) == 0)
            cp1 = BOS_CP;
        else if (strncmp(line, "<EOS>,", 6) == 0)
            cp1 = EOS_CP;
        else {
            unsigned char *u = (unsigned char *)line;
            if (u[0] < 0xE0) continue;
            cp1 = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
        }
        int idx1 = find_idx(cp1);
        if (idx1 >= 0)
            temp_count[idx1]++;
    }

    for (int i = 0; i < char_count; i++) {
        if (temp_count[i] > 0) {
            bi_list[i] = malloc(sizeof(NextChar) * temp_count[i]);
            bi_cum[i] = malloc(sizeof(int) * temp_count[i]);
        }
    }
    free(temp_count);

    /* 2차: 데이터 채우기 */
    fseek(fp, data_start, SEEK_SET);
    while (fgets(line, sizeof(line), fp)) {
        int cp1, cp2;
        char *p2;

        /* 앞글자 파싱 */
        if (strncmp(line, "<BOS>,", 6) == 0) {
            cp1 = BOS_CP;
            p2 = line + 6;
        } else if (strncmp(line, "<EOS>,", 6) == 0) {
            cp1 = EOS_CP;
            p2 = line + 6;
        } else {
            unsigned char *u = (unsigned char *)line;
            if (u[0] < 0xE0) continue;
            cp1 = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
            p2 = (char *)(u + 4); /* 3바이트 + 콤마 */
        }

        /* 뒷글자 파싱 */
        if (strncmp(p2, "<BOS>,", 6) == 0) {
            cp2 = BOS_CP;
            p2 += 6;
        } else if (strncmp(p2, "<EOS>,", 6) == 0) {
            cp2 = EOS_CP;
            p2 += 6;
        } else {
            unsigned char *u2 = (unsigned char *)p2;
            if (u2[0] < 0xE0) continue;
            cp2 = ((u2[0] & 0x0F) << 12) | ((u2[1] & 0x3F) << 6) | (u2[2] & 0x3F);
            p2 = (char *)(u2 + 4);
        }

        int count = atoi(p2);

        int idx1 = find_idx(cp1);
        int idx2 = find_idx(cp2);
        if (idx1 >= 0 && idx2 >= 0) {
            int n = bi_count[idx1];
            bi_list[idx1][n].next_idx = idx2;
            bi_list[idx1][n].count = count;
            bi_count[idx1]++;
            bi_total[idx1] += count;
        }
    }
    fclose(fp);

    /* 누적 빈도 계산 */
    for (int i = 0; i < char_count; i++) {
        if (bi_count[i] > 0) {
            bi_cum[i][0] = bi_list[i][0].count;
            for (int j = 1; j < bi_count[i]; j++)
                bi_cum[i][j] = bi_cum[i][j - 1] + bi_list[i][j].count;
        }
    }

    /* 사전 로드 (검증용) */
    dict = malloc(sizeof(char *) * MAX_DICT);
    {
        FILE *df = fopen("korean-dict/words.txt", "r");
        if (df) {
            char dline[4096];
            while (fgets(dline, sizeof(dline), df) && dict_count < MAX_DICT) {
                /* 줄바꿈 제거 */
                int len = (int)strlen(dline);
                while (len > 0 && (dline[len-1] == '\n' || dline[len-1] == '\r'))
                    dline[--len] = 0;
                dict[dict_count] = malloc(len + 1);
                strcpy(dict[dict_count], dline);
                dict_count++;
            }
            fclose(df);
        }
    }

    /* 생성할 단어 수 */
    int gen_words = 20;
    char input[64];
    printf("생성할 단어 수 (기본 20): ");
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && atoi(input) > 0)
        gen_words = atoi(input);

    srand((unsigned)time(NULL));

    FILE *out = fopen("step03b_result.txt", "w");
    fputc(0xEF, out); fputc(0xBB, out); fputc(0xBF, out);

    int real_count = 0, fake_count = 0;

    for (int w = 0; w < gen_words; w++) {
        int cur_idx = BOS_IDX;
        char word_buf[512];
        int word_len = 0;

        /* BOS에서 다음 글자 선택 → EOS 나올 때까지 반복 */
        for (int step = 0; step < 50; step++) {
            if (bi_count[cur_idx] == 0) break;

            int r = big_rand(bi_total[cur_idx]);
            int lo = 0, hi = bi_count[cur_idx] - 1;
            while (lo < hi) {
                int mid = (lo + hi) / 2;
                if (bi_cum[cur_idx][mid] <= r) lo = mid + 1;
                else hi = mid;
            }
            int next_idx = bi_list[cur_idx][lo].next_idx;

            if (next_idx == EOS_IDX) break;

            unsigned char u[4];
            cp_to_utf8(char_list[next_idx], u);
            memcpy(word_buf + word_len, u, 3);
            word_len += 3;

            cur_idx = next_idx;
        }
        word_buf[word_len] = 0;

        /* 사전 검증 */
        int exists = dict_search(word_buf);
        const char *mark = exists ? "[O]" : "[X]";
        if (exists) real_count++; else fake_count++;

        fprintf(out, "%s %s\n", word_buf, mark);
        printf("%s %s\n", word_buf, mark);
    }

    fclose(out);
    printf("\n사전에 있는 단어: %d개, 새로 만들어진 단어: %d개\n", real_count, fake_count);
    printf("결과가 step03b_result.txt에 저장되었습니다.\n");

    for (int i = 0; i < char_count; i++) {
        free(bi_list[i]);
        free(bi_cum[i]);
    }
    return 0;
}
