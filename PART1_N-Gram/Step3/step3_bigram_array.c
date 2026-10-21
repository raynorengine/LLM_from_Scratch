/*
 * step3_bigram_array.c - [STEP 3] ① 압축 번호 + 2차원 배열 바이그램
 *
 * ===== 이 구현의 위치 =====
 * 강의 영상이 "적절하다"고 한 방식이다.
 *   - 11,172 x 11,172 배열은 int 기준 약 500MB -> 너무 크다
 *   - 실제로 나온 글자(2,450종)에만 번호를 매기면 2,452 x 2,452 (BOS/EOS 포함) 약 24MB -> 적절
 * "안 쓰는 글자 8,722종"은 버렸지만, 쓰는 글자끼리라도 실제로 안 붙는 쌍(약 97%)은
 * 여전히 0인 칸으로 메모리를 차지한다. ②·③과 비교할 기준선 역할을 한다.
 *
 * ===== 자료구조 =====
 *   freq[prev * vocab + next] = (prev, next) 쌍의 빈도
 *   2차원 배열 freq[prev][next]를 1차원으로 펴서 쓴다 (아래 "학습" 구역 참고).
 *   row_sum[prev]             = prev 행의 합계 (뽑기의 난수 범위)
 */

#include <stdio.h>
#include <stdlib.h>

#include "step3_bigram.h"

static int       *g_freq    = NULL;  /* vocab * vocab 칸 */
static long long *g_row_sum = NULL;  /* vocab 칸 */
static int        g_vocab   = 0;

/* ------------------------------------------------------------------ */
/* 학습: 쌍 세기                                                        */
/*                                                                     */
/* [목적] seq의 모든 인접 쌍 (a, b)에 대해 freq[a][b]를 1씩 올린다.      */
/*                                                                     */
/* [동작] seq = [BOS 나 비 EOS BOS 사 과 EOS] 이면                       */
/*   i=0: (BOS,나)  i=1: (나,비)  i=2: (비,EOS)                          */
/*   i=3: EOS -> 건너뜀 (EOS 다음 BOS는 다른 단어라 쌍이 아니다)          */
/*   i=4: (BOS,사)  i=5: (사,과)  i=6: (과,EOS)                          */
/*                                                                     */
/* [이유] 2차원 배열을 freq[a][b] 대신 freq[a * vocab + b]로 쓴 까닭:   */
/*   vocab(2,452)은 실행해 봐야 아는 값이라 int freq[2452][2452]처럼     */
/*   크기를 고정해 선언할 수 없다. 그래서 칸 수만큼 한 덩어리로 할당하고  */
/*   "a행 b열 = a * vocab + b 번째 칸"으로 위치를 계산한다.               */
/*   (a행이 시작하는 칸 = a * vocab, 거기서 b칸 오른쪽)                  */
/*   이 크기(약 24MB)는 스택에 둘 수 없어서(보통 1MB) calloc으로 힙에    */
/*   할당한다. calloc은 0으로 채워 주므로 "아직 안 나온 쌍 = 0"이 된다.  */
/* ------------------------------------------------------------------ */
static int array_build(const int *seq, long long n, int vocab, int eos)
{
    long long i;

    g_vocab   = vocab;
    g_freq    = (int *)calloc((size_t)vocab * (size_t)vocab, sizeof(int));
    g_row_sum = (long long *)calloc((size_t)vocab, sizeof(long long));
    if (!g_freq || !g_row_sum) {
        fprintf(stderr, "[오류] array: 메모리 할당 실패\n");
        return 0;
    }

    for (i = 0; i + 1 < n; i++) {
        int a = seq[i];
        int b = seq[i + 1];
        if (a == eos) {
            continue;              /* 단어 경계: EOS -> 다음 단어의 BOS는 쌍이 아니다 */
        }
        g_freq[(size_t)a * (size_t)vocab + (size_t)b]++;
        g_row_sum[a]++;            /* 행 합계도 세면서 같이 만든다 (뽑기 때 다시 더하지 않게) */
    }
    return 1;
}

static long long array_row_total(int prev)
{
    return g_row_sum[prev];
}

/* ------------------------------------------------------------------ */
/* 뽑기: 행을 앞에서부터 누적                                           */
/*                                                                     */
/* [목적] r이 prev 행의 어느 후보 구간에 떨어지는지 찾는다.              */
/*                                                                     */
/* [동작] 강의 예시 '가' 행: 가=3, 대=2 (나머지 0), 합계 5               */
/*   b=가: 누적 3. r=0,1,2면 3 > r -> '가'                               */
/*   b=각, 간, ...: 빈도 0이라 누적이 그대로 3 (이 칸들도 전부 읽는다)    */
/*   b=대: 누적 5. r=3,4면 5 > r -> '대'                                 */
/*                                                                     */
/* [이유] 행마다 누적 배열을 미리 만들어 두면 이진 탐색을 쓸 수 있지만,  */
/*   그러면 24MB짜리 누적 배열이 하나 더 필요하다. 이 구현은 강의의      */
/*   "배열 하나" 방식을 그대로 보여주는 것이 목적이라 매번 행 전체       */
/*   (2,452칸)를 앞에서부터 더한다. 0인 칸(약 97%)도 전부 읽어야 한다는  */
/*   점이 ③ CSR과의 속도 차이로 나타난다.                               */
/*   '>'를 '>='로 쓰면 각 구간의 마지막 r이 다음 후보로 넘어가는 한 칸    */
/*   어긋남이 생긴다 (Step2와 같은 경계 조건).                           */
/* ------------------------------------------------------------------ */
static int array_pick(int prev, long long r)
{
    const int *row = g_freq + (size_t)prev * (size_t)g_vocab;   /* prev 행의 시작 칸 */
    long long cum = 0;
    int b;

    for (b = 0; b < g_vocab; b++) {
        cum += row[b];
        if (cum > r) {
            return b;
        }
    }
    return -1;   /* r이 범위를 벗어난 경우만 여기 온다 (main이 막고 있으므로 정상이면 안 옴) */
}

/* 탐욕적 디코딩: 행에서 빈도 최대. '>'로 비교하므로 동률이면 먼저 나온(번호 작은) 후보가 남는다 */
static int array_greedy(int prev)
{
    const int *row = g_freq + (size_t)prev * (size_t)g_vocab;
    int best = -1;
    int best_count = 0;
    int b;

    for (b = 0; b < g_vocab; b++) {
        if (row[b] > best_count) {
            best_count = row[b];
            best = b;
        }
    }
    return best;
}

static long long array_count(int prev, int next)
{
    return g_freq[(size_t)prev * (size_t)g_vocab + (size_t)next];
}

/* 쓰지 않는 0 칸까지 포함한 전체 크기. 이것이 "없는 조합도 메모리를 쓴다"의 실제 비용이다 */
static size_t array_memory(void)
{
    return (size_t)g_vocab * (size_t)g_vocab * sizeof(int) + (size_t)g_vocab * sizeof(long long);
}

static void array_release(void)
{
    free(g_freq);
    free(g_row_sum);
    g_freq = NULL;
    g_row_sum = NULL;
}

/* main이 부를 함수 묶음 (step3_bigram.h의 BigramModel 순서대로) */
const BigramModel model_array = {
    "array",
    "압축 번호 + 2차원 배열 (행 순차 누적)",
    array_build, array_row_total, array_pick, array_greedy,
    array_count, array_memory, array_release
};
