/*
 * step3_bigram_csr.c - [STEP 3] ③ 해시테이블로 세고 CSR로 옮겨 뽑는 바이그램
 *
 * ===== 이 구현의 위치 =====
 * 강의에는 없고 Claude가 추가한 방식이다 (devlog/2026-10-21.md "Claude 단독 계획").
 *   - 세기는 ② 해시테이블과 똑같다 (step3_bigram_hash.c의 ht_* 함수를 빌려 쓴다)
 *   - 다 센 뒤 쌍들을 "앞 글자별로 모아 한 줄로 붙인 표"(CSR)로 옮긴다
 *   - 뽑기는 그 표에서 prev 행 구간만 이진 탐색한다
 *
 * ===== CSR (Compressed Sparse Row, 압축 희소 행) =====
 * 대부분이 0인 표에서 0이 아닌 칸만 행 순서대로 이어 붙여 저장하는 방식.
 * 강의 예시(가가 3, 가대 2, 대소 2, 소하 1, 하다 1)로 그리면
 *
 *   위치          0    1    2    3    4
 *   next_id   [  가,  대,  소,  하,  다 ]
 *   cum       [  3,   5,   2,   1,   1  ]   <- 행 안에서의 누적 빈도 (행마다 0부터 다시)
 *               └─가 행─┘ └대┘ └소┘ └하┘
 *   row_start    가=0  대=2  소=3  하=4  다=5  (끝=5)
 *
 * prev 행 = 위치 row_start[prev] ~ row_start[prev+1]-1. 그 안에는 실제 후보만 있다.
 * 국어사전 옆면의 ㄱ·ㄴ·ㄷ 색인처럼 row_start만 보면 구간을 바로 안다.
 *
 * ===== 현업에서 =====
 * SciPy(scipy.sparse.csr_matrix), scikit-learn의 문서-단어 행렬, cuSPARSE,
 * 그래프 라이브러리(노드 a와 연결된 노드 전부)에서 표준으로 쓰는 희소 행렬 형식이다.
 */

#include <stdio.h>
#include <stdlib.h>

#include "step3_bigram.h"

#define CSR_HASH_BITS 19   /* 세기 단계에서 쓰는 임시 해시테이블 크기 (② 와 같음) */

static int *g_row_start = NULL;  /* vocab + 1 칸. 행 a = [row_start[a], row_start[a+1]) */
static int *g_next_id   = NULL;  /* 쌍 수만큼. 각 칸의 next 번호 (행 안에서 오름차순) */
static int *g_cum       = NULL;  /* 쌍 수만큼. 행 안 누적 빈도 */
static int  g_vocab     = 0;
static int  g_pairs     = 0;     /* 서로 다른 쌍 수 */

/* 해시테이블에서 꺼낸 (키, 빈도). 정렬용 임시 구조 */
typedef struct {
    int key;
    int count;
} KeyCount;

/* 키 오름차순. 키 = prev * vocab + next 이므로 키 순서 = (prev, next) 순서 */
static int cmp_key(const void *x, const void *y)
{
    int a = ((const KeyCount *)x)->key;
    int b = ((const KeyCount *)y)->key;
    return (a > b) - (a < b);   /* a-b 를 그대로 돌려주면 int 넘침 위험이 있어 비교로 쓴다 */
}

/* ------------------------------------------------------------------ */
/* 학습 1단계: 해시테이블로 세기                                        */
/*                                                                     */
/* [이유] ② hash_build와 같은 일이다. 왜 바로 CSR에 세지 않는가:         */
/*   CSR은 "행마다 몇 칸인지"를 알아야 자리를 잡을 수 있는데, 그건 다    */
/*   세어 봐야 안다. 그래서 먼저 해시로 세고(크기를 몰라도 되는 구조),    */
/*   다 센 뒤 크기가 확정되면 CSR로 옮긴다.                             */
/* ------------------------------------------------------------------ */
static int count_pairs(HashTable *ht, const int *seq, long long n, int vocab, int eos)
{
    long long i;

    if (!ht_init(ht, CSR_HASH_BITS)) {
        return 0;
    }
    for (i = 0; i + 1 < n; i++) {
        if (seq[i] == eos) {
            continue;   /* 단어 경계 */
        }
        if (!ht_add(ht, seq[i] * vocab + seq[i + 1])) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* 학습 2단계: 해시테이블 -> CSR                                        */
/*                                                                     */
/* [동작]                                                              */
/*   1) 해시테이블의 채워진 칸만 꺼내 (키, 빈도) 배열을 만든다           */
/*      해시 칸 순서는 뒤죽박죽이다 (해시 함수가 흩어 놓았으므로)        */
/*   2) 키 오름차순으로 정렬한다                                        */
/*      키 = prev * vocab + next 이므로 정렬하면 prev별로 모이고,        */
/*      같은 prev 안에서는 next 오름차순이 된다                          */
/*   3) 행마다 칸 수를 세서 누적하면 row_start가 된다                    */
/*      예: 가 2칸, 대 1칸, 소 1칸 -> row_start = 0, 2, 3, 4 ...         */
/*   4) 행 안에서 빈도를 누적해 cum을 만든다 (행이 바뀌면 0부터)         */
/*                                                                     */
/* [이유] 2)에서 qsort를 쓰는 까닭: 계수 정렬로 prev별 자리만 잡으면     */
/*   O(n)이지만 행 안의 next 순서가 해시 칸 순서(뒤죽박죽)로 남는다.      */
/*   세 구현은 "후보를 번호 오름차순으로 누적"해야 같은 seed에서 같은    */
/*   단어를 만든다 (step3_bigram.h 참고). 키 정렬 한 번이면 행 묶기와    */
/*   행 안 정렬이 동시에 끝난다. 17만 개 정렬은 수십 ms 이내다.          */
/* ------------------------------------------------------------------ */
static int to_csr(const HashTable *ht, int vocab)
{
    KeyCount *items;
    size_t slots = (size_t)ht->mask + 1u;
    size_t s;
    int k = 0;
    int i;
    int a;

    g_pairs     = (int)ht->used;
    items       = (KeyCount *)malloc((size_t)g_pairs * sizeof(KeyCount));
    g_row_start = (int *)calloc((size_t)vocab + 1u, sizeof(int));
    g_next_id   = (int *)malloc((size_t)g_pairs * sizeof(int));
    g_cum       = (int *)malloc((size_t)g_pairs * sizeof(int));
    if (!items || !g_row_start || !g_next_id || !g_cum) {
        fprintf(stderr, "[오류] csr: 메모리 할당 실패\n");
        free(items);
        return 0;
    }

    /* 1) 채워진 칸만 꺼내기 */
    for (s = 0; s < slots; s++) {
        if (ht->keys[s] != -1) {
            items[k].key   = ht->keys[s];
            items[k].count = ht->counts[s];
            k++;
        }
    }

    /* 2) 키 오름차순 정렬 -> (prev, next) 순서 */
    qsort(items, (size_t)g_pairs, sizeof(KeyCount), cmp_key);

    /* 3) row_start: 먼저 행 a의 칸 수를 row_start[a + 1]에 세고, 앞에서부터 누적한다.  */
    /*    한 칸 밀어서 세는 이유: 누적이 끝나면 row_start[a] = "a보다 앞 행들의 칸 수 합" */
    /*    = 행 a가 시작하는 위치가 되기 때문이다. row_start[vocab] = 전체 쌍 수.        */
    for (i = 0; i < g_pairs; i++) {
        g_row_start[items[i].key / vocab + 1]++;
    }
    for (a = 0; a < vocab; a++) {
        g_row_start[a + 1] += g_row_start[a];
    }

    /* 4) next 번호와 행 안 누적 빈도. 정렬돼 있으므로 i 순서가 곧 CSR 위치다 */
    for (a = 0; a < vocab; a++) {
        int cum = 0;
        for (i = g_row_start[a]; i < g_row_start[a + 1]; i++) {
            cum += items[i].count;
            g_next_id[i] = items[i].key % vocab;
            g_cum[i]     = cum;
        }
    }

    free(items);
    return 1;
}

static int csr_build(const int *seq, long long n, int vocab, int eos)
{
    HashTable ht;
    int ok;

    g_vocab = vocab;
    ok = count_pairs(&ht, seq, n, vocab, eos) && to_csr(&ht, vocab);
    ht_free(&ht);   /* 세기용 해시는 CSR로 옮긴 뒤 필요 없다. 생성 단계엔 CSR만 남는다 */
    return ok;
}

/* 행 합계 = 행의 마지막 누적값. 빈 행(EOS 행)은 0 */
static long long csr_row_total(int prev)
{
    int lo = g_row_start[prev];
    int hi = g_row_start[prev + 1];
    return (hi > lo) ? g_cum[hi - 1] : 0;
}

/* ------------------------------------------------------------------ */
/* 뽑기: prev 행 구간 안에서 이진 탐색                                  */
/*                                                                     */
/* [동작] Step2 sample_index와 같은 upper_bound. 다른 점은 탐색 범위가  */
/*   전체(0 ~ 2449)가 아니라 prev 행 구간 [row_start[prev], row_start[prev+1]) */
/*   이라는 것뿐이다.                                                   */
/*   '가' 행: 위치 0~1, cum = 3, 5                                      */
/*     r=2: mid=0, cum 3 > 2 -> hi=0 -> 위치 0 = '가'                    */
/*     r=3: mid=0, cum 3 > 3 아님 -> lo=1 -> 위치 1 = '대'               */
/*                                                                     */
/* [이유] 행 안에는 실제 후보만 있고 cum은 오름차순이므로 이진 탐색이    */
/*   가능하다. '사' 행(592칸)이면 최대 10번 비교 (2^10 = 1024 >= 592).   */
/*   ①·②가 매번 2,452칸을 보는 것과의 차이가 속도 차이다.               */
/* ------------------------------------------------------------------ */
static int csr_pick(int prev, long long r)
{
    int lo = g_row_start[prev];
    int hi = g_row_start[prev + 1] - 1;   /* 행의 마지막 위치 */

    if (hi < lo) {
        return -1;   /* 빈 행 */
    }
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (g_cum[mid] > r) {   /* '>' 가 핵심. '>='면 구간 끝 r이 다음 후보로 밀린다 */
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return g_next_id[lo];
}

/* 행 안 위치 i의 빈도 = cum[i] - cum[i-1] (행의 첫 칸이면 cum[i] 그대로) */
static int count_at(int prev, int i)
{
    return (i == g_row_start[prev]) ? g_cum[i] : g_cum[i] - g_cum[i - 1];
}

static int csr_greedy(int prev)
{
    int best = -1;
    int best_count = 0;
    int i;

    /* 행이 next 오름차순이고 '>'로 비교하므로 동률이면 번호 작은 후보가 남는다 */
    for (i = g_row_start[prev]; i < g_row_start[prev + 1]; i++) {
        int c = count_at(prev, i);
        if (c > best_count) {
            best_count = c;
            best = g_next_id[i];
        }
    }
    return best;
}

/* (prev, next) 빈도: 행 안에서 next를 이진 탐색 (행이 next 오름차순이라 가능) */
static long long csr_count(int prev, int next)
{
    int lo = g_row_start[prev];
    int hi = g_row_start[prev + 1];   /* 반열린 구간 [lo, hi) */

    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (g_next_id[mid] < next) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < g_row_start[prev + 1] && g_next_id[lo] == next) {
        return count_at(prev, lo);
    }
    return 0;
}

/* 생성 단계에 남는 것만: row_start + next_id + cum (세기용 해시는 이미 해제) */
static size_t csr_memory(void)
{
    return ((size_t)g_vocab + 1u) * sizeof(int) + (size_t)g_pairs * sizeof(int) * 2;
}

static void csr_release(void)
{
    free(g_row_start);
    free(g_next_id);
    free(g_cum);
    g_row_start = NULL;
    g_next_id = NULL;
    g_cum = NULL;
}

const BigramModel model_csr = {
    "csr",
    "해시로 세기 + CSR로 뽑기 (행 이진 탐색)",
    csr_build, csr_row_total, csr_pick, csr_greedy,
    csr_count, csr_memory, csr_release
};
