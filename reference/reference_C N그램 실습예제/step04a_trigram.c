#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HANGUL_BASE 0xAC00
#define HANGUL_END  0xD7A3
#define BOS_CP 0
#define EOS_CP 1

/* 해시 테이블 */
#define HASH_SIZE (1 << 20) /* 약 100만 슬롯 */
#define HASH_MASK (HASH_SIZE - 1)

typedef struct HashEntry {
    int key1, key2, key3; /* 앞글자1, 앞글자2, 뒷글자 */
    int count;
    struct HashEntry *next;
} HashEntry;

static HashEntry *table[HASH_SIZE];

static unsigned int hash3(int a, int b, int c)
{
    unsigned int h = (unsigned int)a * 73856093u ^ (unsigned int)b * 19349663u ^ (unsigned int)c * 83492791u;
    return h & HASH_MASK;
}

static void hash_add(int k1, int k2, int k3)
{
    unsigned int h = hash3(k1, k2, k3);
    HashEntry *e = table[h];
    while (e) {
        if (e->key1 == k1 && e->key2 == k2 && e->key3 == k3) {
            e->count++;
            return;
        }
        e = e->next;
    }
    e = malloc(sizeof(HashEntry));
    e->key1 = k1; e->key2 = k2; e->key3 = k3;
    e->count = 1;
    e->next = table[h];
    table[h] = e;
}

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

static void write_cp(FILE *f, int cp)
{
    if (cp == BOS_CP) fprintf(f, "<BOS>");
    else if (cp == EOS_CP) fprintf(f, "<EOS>");
    else { unsigned char u[4]; cp_to_utf8(cp, u); fprintf(f, "%s", u); }
}

typedef struct {
    int k1, k2, k3;
    int count;
} TrigramEntry;

static int cmp_by_key(const void *a, const void *b)
{
    const TrigramEntry *ea = (const TrigramEntry *)a;
    const TrigramEntry *eb = (const TrigramEntry *)b;
    if (ea->k1 != eb->k1) return ea->k1 - eb->k1;
    if (ea->k2 != eb->k2) return ea->k2 - eb->k2;
    return eb->count - ea->count;
}

int main(void)
{
    memset(table, 0, sizeof(table));

    FILE *fp = fopen("korean-dict/words.txt", "r");
    if (!fp) {
        fprintf(stderr, "words.txt 파일을 열 수 없습니다.\n");
        return 1;
    }

    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        const unsigned char *p = (const unsigned char *)line;

        int prev2 = BOS_CP, prev1 = BOS_CP;
        int started = 0;

        while (*p) {
            int cp;
            int len = next_codepoint(p, &cp);
            if (cp >= HANGUL_BASE && cp <= HANGUL_END) {
                hash_add(prev2, prev1, cp);
                prev2 = prev1;
                prev1 = cp;
                started = 1;
            }
            p += len;
        }

        /* 단어 끝에 EOS */
        if (started)
            hash_add(prev2, prev1, EOS_CP);
    }
    fclose(fp);

    /* 해시 테이블에서 엔트리 수집 */
    int total = 0;
    for (int i = 0; i < HASH_SIZE; i++)
        for (HashEntry *e = table[i]; e; e = e->next)
            total++;

    TrigramEntry *entries = malloc(sizeof(TrigramEntry) * total);
    int n = 0;
    for (int i = 0; i < HASH_SIZE; i++)
        for (HashEntry *e = table[i]; e; e = e->next) {
            entries[n].k1 = e->key1;
            entries[n].k2 = e->key2;
            entries[n].k3 = e->key3;
            entries[n].count = e->count;
            n++;
        }

    qsort(entries, n, sizeof(TrigramEntry), cmp_by_key);

    /* 결과 저장 */
    FILE *out = fopen("step04a_result.csv", "w");
    fputc(0xEF, out); fputc(0xBB, out); fputc(0xBF, out);
    fprintf(out, "글자1,글자2,글자3,빈도\n");

    for (int i = 0; i < n; i++) {
        write_cp(out, entries[i].k1);
        fprintf(out, ",");
        write_cp(out, entries[i].k2);
        fprintf(out, ",");
        write_cp(out, entries[i].k3);
        fprintf(out, ",%d\n", entries[i].count);
    }
    fclose(out);

    printf("분석 완료: %d개 트라이그램\n", n);

    /* 상위 20개 (빈도 내림차순으로 재정렬) */
    TrigramEntry top[20];
    memset(top, 0, sizeof(top));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < 20; j++) {
            if (entries[i].count > top[j].count) {
                memmove(&top[j+1], &top[j], sizeof(TrigramEntry) * (19 - j));
                top[j] = entries[i];
                break;
            }
        }
    }

    printf("\n상위 20개 트라이그램:\n");
    for (int i = 0; i < 20; i++) {
        printf("  %2d. ", i + 1);
        write_cp(stdout, top[i].k1);
        printf(" ");
        write_cp(stdout, top[i].k2);
        printf(" ");
        write_cp(stdout, top[i].k3);
        printf("  %d\n", top[i].count);
    }

    /* 해시 메모리 해제 */
    for (int i = 0; i < HASH_SIZE; i++) {
        HashEntry *e = table[i];
        while (e) { HashEntry *next = e->next; free(e); e = next; }
    }
    free(entries);
    return 0;
}
