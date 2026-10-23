#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HANGUL_BASE 0xAC00
#define HANGUL_END  0xD7A3
#define BOS_CP 0
#define EOS_CP 1

/* 해시 테이블: (key1, key2) → 후보 목록 */
#define HASH_SIZE (1 << 18)
#define HASH_MASK (HASH_SIZE - 1)

typedef struct {
    int next_cp;
    int count;
} NextOption;

typedef struct PairEntry {
    int k1, k2;
    NextOption *opts;
    int opt_count;
    int opt_cap;
    int total;
    int *cum;
    struct PairEntry *next;
} PairEntry;

static PairEntry *table[HASH_SIZE];

static unsigned int hash2(int a, int b)
{
    return ((unsigned int)a * 73856093u ^ (unsigned int)b * 19349663u) & HASH_MASK;
}

static PairEntry *find_or_create(int k1, int k2)
{
    unsigned int h = hash2(k1, k2);
    PairEntry *e = table[h];
    while (e) {
        if (e->k1 == k1 && e->k2 == k2) return e;
        e = e->next;
    }
    e = calloc(1, sizeof(PairEntry));
    e->k1 = k1; e->k2 = k2;
    e->opt_cap = 16;
    e->opts = malloc(sizeof(NextOption) * e->opt_cap);
    e->next = table[h];
    table[h] = e;
    return e;
}

static void pair_add(int k1, int k2, int next_cp, int count)
{
    PairEntry *e = find_or_create(k1, k2);
    if (e->opt_count >= e->opt_cap) {
        e->opt_cap *= 2;
        e->opts = realloc(e->opts, sizeof(NextOption) * e->opt_cap);
    }
    e->opts[e->opt_count].next_cp = next_cp;
    e->opts[e->opt_count].count = count;
    e->opt_count++;
    e->total += count;
}

static PairEntry *find_pair(int k1, int k2)
{
    unsigned int h = hash2(k1, k2);
    PairEntry *e = table[h];
    while (e) {
        if (e->k1 == k1 && e->k2 == k2) return e;
        e = e->next;
    }
    return NULL;
}

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
    memset(table, 0, sizeof(table));

    /* step04a_result.csv 로드 */
    FILE *fp = fopen("step04a_result.csv", "r");
    if (!fp) {
        fprintf(stderr, "step04a_result.csv를 열 수 없습니다.\n");
        return 1;
    }

    char line[256];
    int c = fgetc(fp);
    if (c == 0xEF) { fgetc(fp); fgetc(fp); } else ungetc(c, fp);
    fgets(line, sizeof(line), fp); /* 헤더 */

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        int cp1 = parse_cp(&p);
        if (*p == ',') p++;
        int cp2 = parse_cp(&p);
        if (*p == ',') p++;
        int cp3 = parse_cp(&p);
        if (*p == ',') p++;
        int count = atoi(p);

        if (cp1 >= 0 && cp2 >= 0 && cp3 >= 0)
            pair_add(cp1, cp2, cp3, count);
    }
    fclose(fp);

    /* 누적 빈도 계산 */
    for (int i = 0; i < HASH_SIZE; i++) {
        for (PairEntry *e = table[i]; e; e = e->next) {
            e->cum = malloc(sizeof(int) * e->opt_count);
            e->cum[0] = e->opts[0].count;
            for (int j = 1; j < e->opt_count; j++)
                e->cum[j] = e->cum[j-1] + e->opts[j].count;
        }
    }

    /* 사전 로드 */
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

    /* 생성 */
    int gen_words = 20;
    char input[64];
    printf("생성할 단어 수 (기본 20): ");
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && atoi(input) > 0)
        gen_words = atoi(input);

    srand((unsigned)time(NULL));

    FILE *out = fopen("step04b_result.txt", "w");
    fputc(0xEF, out); fputc(0xBB, out); fputc(0xBF, out);

    int real_count = 0, fake_count = 0;

    for (int w = 0; w < gen_words; w++) {
        int prev2 = BOS_CP, prev1 = BOS_CP;
        char word_buf[512];
        int word_len = 0;

        for (int step = 0; step < 50; step++) {
            PairEntry *pe = find_pair(prev2, prev1);
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
            memcpy(word_buf + word_len, u, 3);
            word_len += 3;

            prev2 = prev1;
            prev1 = next_cp;
        }
        word_buf[word_len] = 0;

        int exists = dict_search(word_buf);
        const char *mark = exists ? "[O]" : "[X]";
        if (exists) real_count++; else fake_count++;

        fprintf(out, "%s %s\n", word_buf, mark);
        printf("%s %s\n", word_buf, mark);
    }

    fclose(out);
    printf("\n사전에 있는 단어: %d개, 새로 만들어진 단어: %d개\n", real_count, fake_count);
    printf("결과가 step04b_result.txt에 저장되었습니다.\n");

    /* 메모리 해제 */
    for (int i = 0; i < HASH_SIZE; i++) {
        PairEntry *e = table[i];
        while (e) {
            PairEntry *next = e->next;
            free(e->opts);
            free(e->cum);
            free(e);
            e = next;
        }
    }
    for (int i = 0; i < dict_count; i++) free(dict[i]);
    free(dict);
    return 0;
}
