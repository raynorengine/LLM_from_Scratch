/*
 * step3_bigram_hash.c - [STEP 3] ② 해시테이블만 쓰는 바이그램
 *
 * ===== 이 구현의 위치 =====
 * 강의 문서가 말한 방식이다: "키(두 글자 조합) -> 값(횟수)"을 해시테이블에 저장.
 * 실제로 나온 쌍(약 17만 개)만 저장하므로 ① 배열처럼 0인 칸을 만들지 않는다.
 * 대신 뽑을 때 "prev 다음 후보 전부"를 바로 알 수 없어서, 후보가 될 수 있는
 * 번호 2,452개를 하나씩 해시에 물어본다 (Step2의 순차 탐색과 같은 구조).
 *
 * 이 파일은 두 부분으로 되어 있다.
 *   [A] 해시테이블 자체 (ht_*) : ③ CSR 구현도 "세기" 단계에서 그대로 빌려 쓴다
 *   [B] ② 바이그램 모델        : 해시테이블로 세고, 해시테이블로 뽑는다
 */

#include <stdio.h>
#include <stdlib.h>

#include "step3_bigram.h"

/* ================================================================== */
/* [A] 해시테이블                                                       */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* 해시 함수: 키 -> 칸 번호                                             */
/*                                                                     */
/* [목적] 키(정수)를 0 ~ (칸수-1) 사이의 칸 번호로 바꾼다.               */
/*   같은 키는 항상 같은 칸, 다른 키는 되도록 다른 칸으로 흩어져야 한다. */
/*                                                                     */
/* [동작] 피보나치 해싱 (곱셈 해시)                                     */
/*   1) 키에 2654435761을 곱한다 (unsigned 32비트라 넘치는 부분은 버려짐) */
/*   2) 결과의 "상위 bits 비트"를 칸 번호로 쓴다                        */
/*   예: bits=19 이면 >> 13 해서 위쪽 19비트만 남긴다 -> 0 ~ 524,287    */
/*                                                                     */
/* [이유] 키 = prev * 2452 + next 라서 키 값이 규칙적이다.              */
/*   (같은 행의 키는 연속, 행이 바뀌면 2452씩 건너뜀)                    */
/*   "키 % 칸수"처럼 아래 비트만 쓰면 이런 규칙이 칸 배치에 그대로 남아   */
/*   특정 구역에 몰릴 수 있다. 곱셈은 키의 모든 비트를 섞어 위쪽 비트로  */
/*   올려 보내므로 위쪽 비트를 쓰면 고르게 흩어진다.                     */
/*   2654435761 ≈ 2^32 / 황금비. 이 값이 비트를 잘 섞는다고 알려져 있어 */
/*   (Knuth) 관례적으로 쓴다.                                           */
/* ------------------------------------------------------------------ */
static unsigned ht_slot(const HashTable *ht, int key)
{
    return ((unsigned)key * 2654435761u) >> (32u - ht->bits);
}

/* ------------------------------------------------------------------ */
/* 생성: 칸 2^bits개를 만들고 전부 빈 칸(키 -1)으로 표시                */
/*                                                                     */
/* [이유] 칸 수를 2의 거듭제곱으로 하는 까닭: 다음 칸으로 넘어갈 때      */
/*   (i + 1) % 칸수 대신 (i + 1) & mask 로 끝에서 0번 칸으로 돌아간다.   */
/*   예: 칸수 8 -> mask 7(0b111). 7+1=8(0b1000) & 0b111 = 0              */
/*   빈 칸 표시를 0이 아니라 -1로 하는 까닭: 키 0 = (가, 가) 쌍이라      */
/*   0은 실제로 쓰이는 키다.                                            */
/* ------------------------------------------------------------------ */
int ht_init(HashTable *ht, unsigned bits)
{
    size_t size = (size_t)1 << bits;
    size_t i;

    ht->bits   = bits;
    ht->mask   = (unsigned)(size - 1);
    ht->used   = 0;
    ht->keys   = (int *)malloc(size * sizeof(int));
    ht->counts = (int *)calloc(size, sizeof(int));
    if (!ht->keys || !ht->counts) {
        fprintf(stderr, "[오류] 해시테이블 메모리 할당 실패 (2^%u칸)\n", bits);
        return 0;
    }
    for (i = 0; i < size; i++) {
        ht->keys[i] = -1;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* 추가: key의 빈도를 1 올린다 (없으면 새로 넣는다)                     */
/*                                                                     */
/* [동작] 오픈 어드레싱 + 선형 탐사                                     */
/*   해시 함수가 준 칸부터 보면서                                       */
/*     - 같은 키가 있으면  -> 빈도 +1, 끝                               */
/*     - 빈 칸(-1)이면     -> 키를 넣고 빈도 1, 끝                      */
/*     - 다른 키가 있으면  -> 충돌. 바로 옆 칸으로 가서 다시 확인         */
/*   예: (사,회)가 100번 칸으로 계산됐는데 100번에 (가,나)가 있으면       */
/*       101번 칸을 본다. 비어 있으면 거기에 넣는다.                    */
/*                                                                     */
/* [이유] 충돌 처리 방법은 크게 둘: 칸마다 연결 리스트를 다는 방식      */
/*   (체이닝)과 빈 칸을 찾아 옆으로 가는 방식(오픈 어드레싱).            */
/*   오픈 어드레싱은 배열 두 개(keys, counts)로 끝나서 malloc을 쌍마다    */
/*   하지 않아도 되고, 옆 칸은 메모리상 바로 옆이라 빠르다.              */
/*   대신 칸이 많이 차면 빈 칸까지 멀리 가야 해서 느려지므로 70%를 넘으면 */
/*   실패로 처리한다. 2^19칸에 쌍 약 17만 개면 33%만 찬다.               */
/* ------------------------------------------------------------------ */
int ht_add(HashTable *ht, int key)
{
    unsigned i = ht_slot(ht, key);

    for (;;) {
        if (ht->keys[i] == key) {
            ht->counts[i]++;
            return 1;
        }
        if (ht->keys[i] == -1) {
            /* 새 키. 적재율 70%를 넘기기 직전이면 거부 (칸수 * 0.7 을 정수로 계산) */
            if ((ht->used + 1) * 10 > (long long)(ht->mask + 1u) * 7) {
                fprintf(stderr, "[오류] 해시테이블이 70%% 넘게 찼습니다. bits를 늘리세요\n");
                return 0;
            }
            ht->keys[i]   = key;
            ht->counts[i] = 1;
            ht->used++;
            return 1;
        }
        i = (i + 1u) & ht->mask;   /* 충돌: 옆 칸으로. 끝 칸 다음은 0번 칸 */
    }
}

/* ------------------------------------------------------------------ */
/* 조회: key의 빈도. 없으면 0                                           */
/*                                                                     */
/* [동작] 추가와 같은 순서로 칸을 따라간다.                              */
/*   같은 키를 만나면 그 빈도, 빈 칸을 만나면 "없음"(0).                 */
/* [이유] 빈 칸에서 멈춰도 되는 까닭: 그 키가 있었다면 추가할 때 이 빈 칸 */
/*   이전 어딘가에 들어갔어야 한다. 이 구현은 삭제를 하지 않으므로       */
/*   중간에 빈 칸이 새로 생기는 일이 없어 이 규칙이 항상 성립한다.       */
/*   없는 쌍(약 97%)도 빈 칸을 만날 때까지 몇 칸은 확인해야 한다.        */
/*   ②의 뽑기가 느린 이유가 바로 이 "없는 쌍 조회"를 2,452번 하는 것이다. */
/* ------------------------------------------------------------------ */
long long ht_get(const HashTable *ht, int key)
{
    unsigned i = ht_slot(ht, key);

    for (;;) {
        if (ht->keys[i] == key) {
            return ht->counts[i];
        }
        if (ht->keys[i] == -1) {
            return 0;
        }
        i = (i + 1u) & ht->mask;
    }
}

void ht_free(HashTable *ht)
{
    free(ht->keys);
    free(ht->counts);
    ht->keys = NULL;
    ht->counts = NULL;
}

/* ================================================================== */
/* [B] ② 바이그램 모델: 해시로 세고, 해시로 뽑는다                      */
/* ================================================================== */

#define HASH_BITS 19   /* 2^19 = 524,288칸. 쌍 약 17만 개 -> 적재율 약 33% */

static HashTable  g_ht;
static long long *g_row_sum = NULL;
static int        g_vocab   = 0;

/* ------------------------------------------------------------------ */
/* 학습: ①과 같은 순서로 쌍을 훑되, 배열 칸 대신 해시테이블에 센다      */
/*                                                                     */
/* [동작] (a, b) -> 키 a * vocab + b -> ht_add                          */
/*   예: vocab=2452, '사'의 번호가 1000, '회'가 2000이라고 하면          */
/*       (설명용 가짜 번호) 키 = 1000 * 2452 + 2000 = 2,454,000          */
/* [이유] 쌍을 정수 하나로 만들어야 해시 함수 하나로 처리할 수 있다.     */
/*   a = 키 / vocab, b = 키 % vocab 으로 되돌릴 수 있으므로 서로 다른    */
/*   쌍이 같은 키가 되는 일은 없다. 최대 키 = 2452^2 - 1 ≈ 600만으로    */
/*   int 범위(약 21억) 안이다.                                          */
/* ------------------------------------------------------------------ */
static int hash_build(const int *seq, long long n, int vocab, int eos)
{
    long long i;

    g_vocab   = vocab;
    g_row_sum = (long long *)calloc((size_t)vocab, sizeof(long long));
    if (!g_row_sum || !ht_init(&g_ht, HASH_BITS)) {
        return 0;
    }

    for (i = 0; i + 1 < n; i++) {
        int a = seq[i];
        if (a == eos) {
            continue;   /* 단어 경계 (①과 같은 이유) */
        }
        if (!ht_add(&g_ht, a * vocab + seq[i + 1])) {
            return 0;
        }
        g_row_sum[a]++;
    }
    return 1;
}

static long long hash_row_total(int prev)
{
    return g_row_sum[prev];
}

/* ------------------------------------------------------------------ */
/* 뽑기: 후보 2,452개를 번호 순서대로 해시에 물어보며 누적              */
/*                                                                     */
/* [동작] next = 0, 1, 2, ... 마다 ht_get(prev, next)로 빈도를 받아 누적.*/
/*   누적 > r 이 되는 순간의 next가 답. 구조는 ① 배열과 똑같고,          */
/*   "배열 칸 읽기"가 "해시 조회"로 바뀌었을 뿐이다.                     */
/*                                                                     */
/* [이유] 해시테이블은 "이 쌍이 몇 번?"에는 바로 답하지만               */
/*   "prev 다음 후보 전부"는 모른다 (키가 칸 전체에 흩어져 있다).        */
/*   그래서 가능한 next를 전부 물어보는 수밖에 없다.                     */
/*   '사' 행은 실제 후보가 592개인데 2,452번을 물으므로 약 76%가 헛수고.  */
/*   이 낭비를 없애는 것이 ③ CSR이다.                                   */
/* ------------------------------------------------------------------ */
static int hash_pick(int prev, long long r)
{
    int base = prev * g_vocab;   /* prev 행의 첫 키. next를 더하면 (prev, next)의 키 */
    long long cum = 0;
    int b;

    for (b = 0; b < g_vocab; b++) {
        cum += ht_get(&g_ht, base + b);
        if (cum > r) {
            return b;
        }
    }
    return -1;
}

static int hash_greedy(int prev)
{
    int base = prev * g_vocab;
    int best = -1;
    long long best_count = 0;
    int b;

    for (b = 0; b < g_vocab; b++) {
        long long c = ht_get(&g_ht, base + b);
        if (c > best_count) {      /* '>' 이므로 동률이면 번호 작은 후보 유지 */
            best_count = c;
            best = b;
        }
    }
    return best;
}

static long long hash_count(int prev, int next)
{
    return ht_get(&g_ht, prev * g_vocab + next);
}

/* 칸 전체(빈 칸 포함) x (키 4바이트 + 빈도 4바이트) + 행 합계 */
static size_t hash_memory(void)
{
    return ((size_t)g_ht.mask + 1u) * (sizeof(int) * 2) + (size_t)g_vocab * sizeof(long long);
}

static void hash_release(void)
{
    ht_free(&g_ht);
    free(g_row_sum);
    g_row_sum = NULL;
}

const BigramModel model_hash = {
    "hash",
    "해시테이블만 (후보 순차 조회)",
    hash_build, hash_row_total, hash_pick, hash_greedy,
    hash_count, hash_memory, hash_release
};
