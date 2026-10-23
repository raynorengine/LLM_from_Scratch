#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HANGUL_BASE 0xAC00
#define HANGUL_END  0xD7A3
#define BOS_CP 0
#define EOS_CP 1
#define MAX_CHARS 2502
#define GEN_COUNT 1000
#define MAX_WORD_LEN 50

/* ===== 사전 ===== */
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

/* ===== UTF-8 ===== */
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

/* 한글 글자 수 (바이트 길이 / 3) */
static int hangul_len(const char *s)
{
    return (int)strlen(s) / 3;
}

/* ===== 바이그램 구조 ===== */
static int bi_char_list[MAX_CHARS];
static int bi_char_count = 2;
static int bi_cp_to_idx[0xD7A4 - 0xAC00];

typedef struct { int next_idx; int count; } BiNext;
static BiNext *bi_list[MAX_CHARS];
static int bi_count[MAX_CHARS];
static int bi_total[MAX_CHARS];
static int *bi_cum[MAX_CHARS];

static int bi_find_idx(int cp)
{
    if (cp == BOS_CP) return 0;
    if (cp == EOS_CP) return 1;
    int idx = cp - HANGUL_BASE;
    if (idx >= 0 && idx < (0xD7A4 - 0xAC00) && bi_cp_to_idx[idx] >= 0)
        return bi_cp_to_idx[idx];
    if (bi_char_count >= MAX_CHARS) return -1;
    bi_cp_to_idx[idx] = bi_char_count;
    bi_char_list[bi_char_count] = cp;
    return bi_char_count++;
}

/* ===== 트라이그램 구조 ===== */
#define TRI_HASH_SIZE (1 << 18)
#define TRI_HASH_MASK (TRI_HASH_SIZE - 1)

typedef struct { int next_cp; int count; } TriNext;
typedef struct TriPair {
    int k1, k2;
    TriNext *opts;
    int opt_count, opt_cap, total;
    int *cum;
    struct TriPair *next;
} TriPair;

static TriPair *tri_table[TRI_HASH_SIZE];

static unsigned int tri_hash2(int a, int b)
{
    return ((unsigned int)a * 73856093u ^ (unsigned int)b * 19349663u) & TRI_HASH_MASK;
}

static TriPair *tri_find_or_create(int k1, int k2)
{
    unsigned int h = tri_hash2(k1, k2);
    TriPair *e = tri_table[h];
    while (e) {
        if (e->k1 == k1 && e->k2 == k2) return e;
        e = e->next;
    }
    e = calloc(1, sizeof(TriPair));
    e->k1 = k1; e->k2 = k2;
    e->opt_cap = 16;
    e->opts = malloc(sizeof(TriNext) * e->opt_cap);
    e->next = tri_table[h];
    tri_table[h] = e;
    return e;
}

static void tri_add(int k1, int k2, int next_cp, int count)
{
    TriPair *e = tri_find_or_create(k1, k2);
    if (e->opt_count >= e->opt_cap) {
        e->opt_cap *= 2;
        e->opts = realloc(e->opts, sizeof(TriNext) * e->opt_cap);
    }
    e->opts[e->opt_count].next_cp = next_cp;
    e->opts[e->opt_count].count = count;
    e->opt_count++;
    e->total += count;
}

static TriPair *tri_find(int k1, int k2)
{
    unsigned int h = tri_hash2(k1, k2);
    TriPair *e = tri_table[h];
    while (e) {
        if (e->k1 == k1 && e->k2 == k2) return e;
        e = e->next;
    }
    return NULL;
}

/* ===== CSV 파싱 ===== */
static int parse_cp(char **pp)
{
    char *p = *pp;
    if (strncmp(p, "<BOS>", 5) == 0) { *pp = p + 5; return BOS_CP; }
    if (strncmp(p, "<EOS>", 5) == 0) { *pp = p + 5; return EOS_CP; }
    unsigned char *u = (unsigned char *)p;
    if (u[0] >= 0xE0 && u[0] <= 0xEF && u[1] && u[2]) {
        int cp = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
        *pp = p + 3;
        return cp;
    }
    return -1;
}

/* ===== 바이그램으로 단어 생성 ===== */
static int gen_bigram(char *buf)
{
    int cur_idx = 0; /* BOS */
    int len = 0;
    for (int step = 0; step < MAX_WORD_LEN; step++) {
        if (bi_count[cur_idx] == 0) break;
        int r = big_rand(bi_total[cur_idx]);
        int lo = 0, hi = bi_count[cur_idx] - 1;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (bi_cum[cur_idx][mid] <= r) lo = mid + 1;
            else hi = mid;
        }
        int next_idx = bi_list[cur_idx][lo].next_idx;
        if (next_idx == 1) break; /* EOS */
        unsigned char u[4];
        cp_to_utf8(bi_char_list[next_idx], u);
        memcpy(buf + len, u, 3);
        len += 3;
        cur_idx = next_idx;
    }
    buf[len] = 0;
    return len;
}

/* ===== 트라이그램으로 단어 생성 ===== */
static int gen_trigram(char *buf)
{
    int prev2 = BOS_CP, prev1 = BOS_CP;
    int len = 0;
    for (int step = 0; step < MAX_WORD_LEN; step++) {
        TriPair *pe = tri_find(prev2, prev1);
        if (!pe || pe->opt_count == 0) break;
        int r = big_rand(pe->total);
        int lo = 0, hi = pe->opt_count - 1;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (pe->cum[mid] <= r) lo = mid + 1;
            else hi = mid;
        }
        int next_cp = pe->opts[lo].next_cp;
        if (next_cp == EOS_CP) break;
        unsigned char u[4];
        cp_to_utf8(next_cp, u);
        memcpy(buf + len, u, 3);
        len += 3;
        prev2 = prev1;
        prev1 = next_cp;
    }
    buf[len] = 0;
    return len;
}

#define MAX_LEN_BUCKET 16

int main(void)
{
    memset(bi_cp_to_idx, -1, sizeof(bi_cp_to_idx));
    memset(bi_count, 0, sizeof(bi_count));
    memset(tri_table, 0, sizeof(tri_table));
    bi_char_list[0] = BOS_CP;
    bi_char_list[1] = EOS_CP;

    /* ===== 바이그램 CSV 로드 ===== */
    FILE *fp = fopen("step03a_result.csv", "r");
    if (!fp) { fprintf(stderr, "step03a_result.csv를 열 수 없습니다.\n"); return 1; }
    {
        char line[256];
        int c = fgetc(fp); if (c == 0xEF) { fgetc(fp); fgetc(fp); } else ungetc(c, fp);
        fgets(line, sizeof(line), fp);

        /* 1차: 카운트 */
        int *temp = calloc(MAX_CHARS, sizeof(int));
        long start = ftell(fp);
        while (fgets(line, sizeof(line), fp)) {
            char *p = line;
            int cp1 = parse_cp(&p);
            if (*p == ',') p++;
            int idx1 = bi_find_idx(cp1);
            if (idx1 >= 0) temp[idx1]++;
        }
        for (int i = 0; i < bi_char_count; i++) {
            if (temp[i] > 0) {
                bi_list[i] = malloc(sizeof(BiNext) * temp[i]);
                bi_cum[i] = malloc(sizeof(int) * temp[i]);
            }
        }
        free(temp);

        /* 2차: 데이터 */
        fseek(fp, start, SEEK_SET);
        while (fgets(line, sizeof(line), fp)) {
            char *p = line;
            int cp1 = parse_cp(&p); if (*p == ',') p++;
            int cp2 = parse_cp(&p); if (*p == ',') p++;
            int count = atoi(p);
            int idx1 = bi_find_idx(cp1);
            int idx2 = bi_find_idx(cp2);
            if (idx1 >= 0 && idx2 >= 0) {
                int n = bi_count[idx1];
                bi_list[idx1][n].next_idx = idx2;
                bi_list[idx1][n].count = count;
                bi_count[idx1]++;
                bi_total[idx1] += count;
            }
        }
        for (int i = 0; i < bi_char_count; i++) {
            if (bi_count[i] > 0) {
                bi_cum[i][0] = bi_list[i][0].count;
                for (int j = 1; j < bi_count[i]; j++)
                    bi_cum[i][j] = bi_cum[i][j-1] + bi_list[i][j].count;
            }
        }
    }
    fclose(fp);

    /* ===== 트라이그램 CSV 로드 ===== */
    fp = fopen("step04a_result.csv", "r");
    if (!fp) { fprintf(stderr, "step04a_result.csv를 열 수 없습니다.\n"); return 1; }
    {
        char line[256];
        int c = fgetc(fp); if (c == 0xEF) { fgetc(fp); fgetc(fp); } else ungetc(c, fp);
        fgets(line, sizeof(line), fp);
        while (fgets(line, sizeof(line), fp)) {
            char *p = line;
            int cp1 = parse_cp(&p); if (*p == ',') p++;
            int cp2 = parse_cp(&p); if (*p == ',') p++;
            int cp3 = parse_cp(&p); if (*p == ',') p++;
            int count = atoi(p);
            if (cp1 >= 0 && cp2 >= 0 && cp3 >= 0)
                tri_add(cp1, cp2, cp3, count);
        }
    }
    fclose(fp);
    /* 누적 빈도 */
    for (int i = 0; i < TRI_HASH_SIZE; i++) {
        for (TriPair *e = tri_table[i]; e; e = e->next) {
            e->cum = malloc(sizeof(int) * e->opt_count);
            e->cum[0] = e->opts[0].count;
            for (int j = 1; j < e->opt_count; j++)
                e->cum[j] = e->cum[j-1] + e->opts[j].count;
        }
    }

    /* ===== 사전 로드 ===== */
    dict = malloc(sizeof(char *) * MAX_DICT);
    {
        FILE *df = fopen("korean-dict/words.txt", "r");
        if (df) {
            char dline[4096];
            while (fgets(dline, sizeof(dline), df) && dict_count < MAX_DICT) {
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

    /* ===== 생성 및 비교 ===== */
    srand((unsigned)time(NULL));

    int bi_match = 0, tri_match = 0;
    int bi_total_len = 0, tri_total_len = 0;
    int bi_len_total[MAX_LEN_BUCKET] = {0}, bi_len_match[MAX_LEN_BUCKET] = {0};
    int tri_len_total[MAX_LEN_BUCKET] = {0}, tri_len_match[MAX_LEN_BUCKET] = {0};

    char buf[512];
    for (int i = 0; i < GEN_COUNT; i++) {
        /* 바이그램 */
        gen_bigram(buf);
        int len = hangul_len(buf);
        bi_total_len += len;
        int bucket = len < MAX_LEN_BUCKET ? len : MAX_LEN_BUCKET - 1;
        bi_len_total[bucket]++;
        if (dict_search(buf)) { bi_match++; bi_len_match[bucket]++; }

        /* 트라이그램 */
        gen_trigram(buf);
        len = hangul_len(buf);
        tri_total_len += len;
        bucket = len < MAX_LEN_BUCKET ? len : MAX_LEN_BUCKET - 1;
        tri_len_total[bucket]++;
        if (dict_search(buf)) { tri_match++; tri_len_match[bucket]++; }
    }

    /* ===== 결과 출력 ===== */
    FILE *out = fopen("step05_result.txt", "w");
    fputc(0xEF, out); fputc(0xBB, out); fputc(0xBF, out);

    fprintf(out, "=== 바이그램 vs 트라이그램 비교 (%d개 단어 생성) ===\n\n", GEN_COUNT);
    printf("=== 바이그램 vs 트라이그램 비교 (%d개 단어 생성) ===\n\n", GEN_COUNT);

    fprintf(out, "             바이그램    트라이그램\n");
    printf("             바이그램    트라이그램\n");

    fprintf(out, "사전 일치    %3d/%-4d    %3d/%-4d\n", bi_match, GEN_COUNT, tri_match, GEN_COUNT);
    printf("사전 일치    %3d/%-4d    %3d/%-4d\n", bi_match, GEN_COUNT, tri_match, GEN_COUNT);

    fprintf(out, "일치율       %5.1f%%       %5.1f%%\n",
        100.0 * bi_match / GEN_COUNT, 100.0 * tri_match / GEN_COUNT);
    printf("일치율       %5.1f%%       %5.1f%%\n",
        100.0 * bi_match / GEN_COUNT, 100.0 * tri_match / GEN_COUNT);

    fprintf(out, "평균 길이    %5.1f자      %5.1f자\n\n",
        (double)bi_total_len / GEN_COUNT, (double)tri_total_len / GEN_COUNT);
    printf("평균 길이    %5.1f자      %5.1f자\n\n",
        (double)bi_total_len / GEN_COUNT, (double)tri_total_len / GEN_COUNT);

    fprintf(out, "글자수별 일치율:\n");
    printf("글자수별 일치율:\n");
    fprintf(out, "길이   바이그램          트라이그램\n");
    printf("길이   바이그램          트라이그램\n");

    for (int i = 1; i < MAX_LEN_BUCKET; i++) {
        if (bi_len_total[i] == 0 && tri_len_total[i] == 0) continue;
        char label[8];
        if (i < MAX_LEN_BUCKET - 1) sprintf(label, "%2d", i);
        else sprintf(label, "%d+", MAX_LEN_BUCKET - 1);

        double bi_pct = bi_len_total[i] > 0 ? 100.0 * bi_len_match[i] / bi_len_total[i] : 0;
        double tri_pct = tri_len_total[i] > 0 ? 100.0 * tri_len_match[i] / tri_len_total[i] : 0;

        fprintf(out, "%s자   %3d/%3d (%5.1f%%)   %3d/%3d (%5.1f%%)\n",
            label, bi_len_match[i], bi_len_total[i], bi_pct,
            tri_len_match[i], tri_len_total[i], tri_pct);
        printf("%s자   %3d/%3d (%5.1f%%)   %3d/%3d (%5.1f%%)\n",
            label, bi_len_match[i], bi_len_total[i], bi_pct,
            tri_len_match[i], tri_len_total[i], tri_pct);
    }

    fclose(out);
    printf("\n결과가 step05_result.txt에 저장되었습니다.\n");
    return 0;
}
