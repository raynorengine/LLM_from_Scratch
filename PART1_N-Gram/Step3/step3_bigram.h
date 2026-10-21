/*
 * step3_bigram.h - [STEP 3] 바이그램: 세 가지 구현이 함께 지키는 약속(인터페이스)
 *
 * ===== 이 파일이 왜 필요한가 =====
 * STEP 3은 같은 바이그램 모델을 자료구조만 바꿔 세 번 구현한다.
 *   ① step3_bigram_array.c : 압축 번호 + 2차원 배열 (강의 영상의 "2,450 x 2,450 = 24MB")
 *   ② step3_bigram_hash.c  : 해시테이블만 (강의 문서의 "해시테이블")
 *   ③ step3_bigram_csr.c   : 해시테이블로 세고 CSR로 옮겨 뽑기 (Claude 추가, 강의에 없음)
 * main(step3_bigram.c)은 세 구현을 똑같은 방식으로 불러서 시간을 재고 결과를 비교한다.
 * 그러려면 세 구현이 "같은 이름·같은 모양의 함수"를 내놓아야 한다. 그 약속이 이 파일이다.
 *
 * ===== 어떻게 약속하는가: 함수 포인터 묶음 =====
 * C에는 클래스가 없다. 대신 "함수 주소를 담는 변수(함수 포인터)"를 구조체에 모아 둔다.
 *   BigramModel m = model_array;   // ① 의 함수들이 들어 있는 묶음
 *   m.build(...);                  // 실제로는 array_build(...)가 실행된다
 * main은 m이 ①인지 ③인지 몰라도 된다. 그래서 시간 측정·생성 코드를 한 번만 쓰고
 * 세 구현에 똑같이 돌릴 수 있다 (= 공정한 비교).
 *
 * ===== 글자 번호(id) 규칙: 세 구현 공통 =====
 * main이 words.txt를 한 번 읽어 "실제로 나온 글자"에 코드포인트 순서대로 번호를 매긴다.
 *   0 ~ (글자수-1)  : 한글 글자 (가=0, 각=1, ... 코드포인트 오름차순, 실측 2,450종)
 *   글자수          : BOS (단어 시작 표시)
 *   글자수 + 1      : EOS (단어 끝 표시)
 *   vocab = 글자수 + 2 (실측 2,452)
 * BOS/EOS는 "<BOS>" 같은 문자열이 아니라 한글과 절대 겹치지 않는 번호 하나다.
 * 해시 구현은 원래 압축 번호가 필요 없지만, 세 구현이 같은 번호를 써야
 * 순수하게 자료구조 차이만 비교되므로 번호 매기기는 main이 공통으로 한다.
 *
 * ===== 세 구현의 결과가 "글자 하나까지" 같아야 하는 이유 =====
 * 다음 글자를 뽑는 방법은 세 구현 모두 같다:
 *   1) main이 r = 0 ~ (앞 글자 행의 합계 - 1) 범위 난수를 하나 뽑는다
 *   2) 구현이 "후보를 번호 오름차순으로 누적했을 때 누적 > r 이 되는 첫 후보"를 돌려준다
 * 난수는 main이 뽑고, 후보 순서(번호 오름차순)와 경계 규칙(누적 > r)이 같으므로
 * 같은 seed면 세 구현이 같은 단어를 만들어야 한다. 다르면 어딘가 버그다.
 */

#ifndef STEP3_BIGRAM_H
#define STEP3_BIGRAM_H

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* 모델 인터페이스                                                      */
/*                                                                     */
/* seq: main이 만든 번호 배열. 모든 단어를 BOS ... EOS로 감싸 이어 붙였다. */
/*   "나비", "사과" -> [BOS 나 비 EOS BOS 사 과 EOS]                     */
/* 쌍은 seq[i], seq[i+1]이다. 단 seq[i]가 EOS면 다음 칸은 다른 단어의 BOS */
/* 이므로 세지 않는다 (단어 사이에는 실제 문맥이 없다).                   */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *name;   /* 결과 파일에 쓰는 짧은 이름: array / hash / csr */
    const char *desc;   /* 사람이 읽는 설명 */

    /* 학습: seq에서 쌍을 세어 자기 자료구조를 만든다. 성공 1, 실패 0 */
    int (*build)(const int *seq, long long n, int vocab, int eos);

    /* prev 다음에 무언가 온 총 횟수 = 그 행의 합계. 난수 범위를 정하는 데 쓴다 */
    long long (*row_total)(int prev);

    /* 0 <= r < row_total(prev) 인 r을 받아, 누적 > r 이 되는 첫 후보 번호를 돌려준다 */
    int (*pick)(int prev, long long r);

    /* 탐욕적 디코딩: prev 다음 빈도 최대 후보. 동률이면 번호가 작은 쪽 */
    int (*greedy)(int prev);

    /* (prev, next) 쌍의 빈도. 없으면 0. 빈도표 출력·검증용 */
    long long (*count)(int prev, int next);

    /* 학습이 끝난 뒤 자료구조가 차지하는 바이트 수 (비교용) */
    size_t (*memory)(void);

    /* 할당한 메모리를 돌려준다. 다음 구현을 측정할 때 메모리를 깨끗이 하기 위함 */
    void (*release)(void);
} BigramModel;

extern const BigramModel model_array;   /* ① step3_bigram_array.c */
extern const BigramModel model_hash;    /* ② step3_bigram_hash.c  */
extern const BigramModel model_csr;     /* ③ step3_bigram_csr.c   */

/* ------------------------------------------------------------------ */
/* 해시테이블 (② 구현 파일에 있고, ③도 "세기" 단계에서 빌려 쓴다)       */
/*                                                                     */
/* ③은 "해시로 세고 CSR로 옮긴다"이므로 세는 부분은 ②와 완전히 같다.   */
/* 같은 코드를 두 번 쓰지 않도록 해시테이블 자체를 공개한다.             */
/* 키 = prev * vocab + next (쌍 하나를 정수 하나로). 빈 칸은 키 -1.      */
/* ------------------------------------------------------------------ */
typedef struct {
    int      *keys;    /* 칸마다 키. -1이면 빈 칸 */
    int      *counts;  /* 칸마다 빈도 */
    unsigned  bits;    /* 칸 수 = 2^bits */
    unsigned  mask;    /* 칸 수 - 1. "% 칸수" 대신 "& mask"로 칸 번호를 구한다 */
    long long used;    /* 채워진 칸 수 = 서로 다른 쌍의 수 */
} HashTable;

int       ht_init(HashTable *ht, unsigned bits);
int       ht_add(HashTable *ht, int key);          /* key의 빈도 +1. 표가 꽉 차면 0 */
long long ht_get(const HashTable *ht, int key);    /* 없으면 0 */
void      ht_free(HashTable *ht);

#endif
