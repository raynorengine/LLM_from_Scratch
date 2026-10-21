# Progress

**마지막 갱신:** 2026-10-21 (Step4 완료)

## 현재 단계
- **STEP 1 완료**: 글자 빈도 계산 (C) + 독립 검증
- **STEP 2 완료**: 유니그램 생성 (C) — 탐욕적 디코딩 vs 확률적 샘플링 + 독립 검증
- **STEP 3 완료**: 바이그램 (C) — 같은 모델을 ① 압축+2차원 배열 ② 해시만 ③ 해시+CSR 세 구현으로 비교 + 독립 검증
- **STEP 4 완료**: 트라이그램 + 바이그램 비교 (C 단일 파일 2개, 둘 다 해시+CSR, 64비트 키) + 독립 검증 + 비교 문서
  - 같은 seed 1,000개 사전 일치율: 바이그램 **41.7%** → 트라이그램 **57.3%** (1글자 제외 시 24.6% → 57.2%)
- 다음: **STEP 5** — 강의 자료 수령 후 결정 (사용자 지시 대기)

## 완료
- `CLAUDE.md` 규칙 (워크플로우 / 장시간 작업 사전 승인 / 가짜 검증 금지 / 기록 트리거 / 세션 시작 절차 / 코딩 컨벤션)
  - 2026-10-21 추가: **학습용 주석 규칙**(구역마다 목적·동작·이유, Step3부터), 여러 파일 C 구현 규칙
- `docs/` 01~06 (`06_Trigram.md` 신규 — raw 2-6 + 실측 대조)
- `education/` 01~05 (`05_Trigram.md` 신규 — 64비트 키 Q&A, 일치율 해석 Q&A, 과적합 실측, AI 교훈 10개)
- `PART1_N-Gram/Step1/` ~ `Step4/` 구현 + 검증

## 폴더 구성 규칙 (요약, 상세는 CLAUDE.md)
```
PART{N}_{주제}/Step{N}/
  step{N}_{이름}.c              C 구현 (여러 파일이면 + _*.c, .h)
  step{N}_verify.py             Python 독립 검증
  build.bat                     Step 폴더마다 복사, DEFAULT_SRC만 변경 (Step3 이후 것을 복사)
  step{N}_{이름}_result_NN.txt  실행 결과 (01~99, 기존 최대+1)
```
- 데이터 경로: Step 폴더 기준 `../../data/`
- 다음 step의 입력이 되는 확정적 결과는 번호 없는 고정 이름
- 코드 주석: 구역마다 [목적] / [동작] / [이유] (학습용, Step3부터)

## 학습 데이터
- `data/korean_dictionary/words.txt` — 1줄 = 1단어, UTF-8(BOM 없음), LF 개행
  - 단어 **420,309개** / 글자 **1,551,565자** / 고유 글자 **2,450종** (전부 한글, '힣' 없음)
  - 바이그램(BOS/EOS 포함): 쌍 총 1,971,874 / 서로 다른 쌍 **171,425** (BOS/EOS 제외 시 167,399)
- `data/korean_dictionary/data_all.txt` — 용도 미확인 (현재 미사용)
- `data/korean_free_novel/` — 근대문학 txt (문장 생성 단계에서 사용 예정)
- **`.gitignore`에 `data/` 포함 → Git 동기화 안 됨.** 다른 PC에서는 강의 자료를 다시 받아 `data/` 아래에 둘 것

## 구현 현황
### `Step1/` 글자 빈도 — `step1_char_freq.c`, 결과 `step1_char_freq_result.txt`(Step2 입력)
### `Step2/` 유니그램 — `step2_unigram.c`, Step1 빈도표 로드 → 누적 빈도 + 이진 탐색
### `Step3/` 바이그램
| 파일 | 역할 |
|---|---|
| `step3_bigram.h` | 세 구현 공통 인터페이스 `BigramModel`(함수 포인터) + 해시테이블 API |
| `step3_bigram.c` | main: words.txt → 압축 번호(가=0…, BOS=2450, EOS=2451) → seq, 세 구현 실행·시간 측정·일치 비교·출력, 검증 모드 |
| `step3_bigram_array.c` | ① 2,452² 배열, 행 순차 누적 |
| `step3_bigram_hash.c` | ② 해시(2^19칸, 피보나치 해싱, 선형 탐사), 후보 2,452개 순차 조회. ③도 ht_* 사용 |
| `step3_bigram_csr.c` | ③ 해시로 세기 → 키 qsort → CSR, 행 이진 탐색 |
| `step3_verify.py` | 3구현 x [1]쌍 전수 [2]행합=유니그램 [3]r→글자 경계 [4]카이제곱 + [5]빈도표 [6]결과 파일, `--self-test` |
| `step3_bigram_table.txt` | 전체 빈도표 171,425줄 (고정 이름, 확정적) |
| `step3_bigram_result_01~02.txt` | 생성 결과 + 시간 |

### `Step4/` 트라이그램 + 바이그램 비교 (각각 단일 파일, 맨 위에 구역 목차)
| 파일 | 역할 |
|---|---|
| `step4_bigram.c` / `step4_trigram.c` | 같은 15구역 구성. words.txt → 압축 번호 → seq → 해시(64비트 키)로 세기 → CSR → 문맥 이진 탐색 → 생성 1,000개 + 사전 일치율 |
| `step4_verify.py` | 두 프로그램 각각 [1]n그램 전수 [2]문맥 합계 [3]r→글자 [4]카이제곱 [5]결과·일치율 재계산 [6]빈도표 바이트 일치 [7]목차·주석, `--self-test` 18종, `--compare` |
| `step4_compare.txt` | `--compare`가 만드는 비교 문서 (표 3개 + 해석 + 질문과 답) |
| `step4_bigram_table.txt` | Step3 빈도표 복사본 (3.3MB) / `step4_trigram_table.txt` 새로 생성 (15.5MB) |
| `step4_*_result_01.txt` | 같은 seed 1792592635 결과 |

```
step4_trigram.exe [개수=1000] [seed=현재시각] [결과] [빈도표]      step4_bigram.exe 도 같은 형식
python step4_verify.py [--self-test | --compare]
```
- 실측: 학습 36ms / 133ms, 생성 메모리 1.40 / 7.27MB, 서로 다른 n그램 171,425 / 654,039, 문맥 2,451 / 169,589
- 탐욕적: 바이그램 "사" / 트라이그램 "사회"

### Step3 빌드·실행 (참고)
```
build.bat                                            빌드 (step3_bigram.c + step3_bigram_*.c)
step3_bigram.exe [개수=20] [seed=현재시각] [결과] [빈도표]
step3_bigram.exe --dump|--rows|--map <array|hash|csr>
step3_bigram.exe --hist <구현> <샘플수> <seed>        (앞 글자는 표준입력)
python step3_verify.py [--self-test]
```
- 실측(10,000개 생성): array 22~28ms / hash 약 485ms / csr 약 3.7ms. 메모리 24.07 / 4.21 / 1.38MB. 탐욕적 = "사"

## 다음 할 일
1. **STEP 5** — 다음 강의 자료(raw) 수령 후 docs 작성 → 브리핑 → 승인 → 구현
2. 미결 (사용자 결정 대기)
   - `docs/04_Unigram.md`를 새 docs 기준으로 다시 쓸지
   - Step1·Step2 코드에 학습용 주석 소급 적용 여부 (Step3은 주석 규칙을 절반만 지킴 → 2026-10-21 "Step3은 그대로, Step4부터 지킨다"로 결정)
3. 미확인: 강의 트라이그램 일치율 61.7% vs 실측 57.3~57.5% 차이의 원인

## 이슈/주의사항
- **Windows에서 C의 argv는 CP949**: 한글을 명령줄 인자로 넘기면 깨진다 → 검증 모드는 한글을 표준입력으로 받는다
- **git push 인증 함정**: 샌드박스의 `GIT_TERMINAL_PROMPT=0`, `GCM_INTERACTIVE=never` 해제 후 `git -c credential.guiPrompt=true`로 push (`devlog/2026-10-07.md`)
- **이 PC의 `core.autocrlf=true`**: 커밋된 결과 파일이 작업본에서 CRLF가 될 수 있음. C에서 결과 파일을 읽을 땐 `\r` 제거
- **Claude 셸의 `NoDefaultCurrentDirectoryInExePath=1`**: `build.bat`은 전체 경로로 호출, exe는 `./`로 직접 실행
- **`.pyc` 캐시**: 검증기를 import로 시험할 때는 `python -B` (불필요한 `__pycache__` 방지)
- git 사용자: **`raynorengine` / `raynorengine@gmail.com`** (로컬 설정). GitHub 저장소는 public
- C 함정: 개행을 글자로 세지 않기 / UTF-8 선두 바이트로 길이 판단 / `SetConsoleOutputCP(CP_UTF8)` + `/utf-8` 둘 다 / `"rb"` 바이너리 모드
- 빌드 스크립트는 `.bat` (PowerShell 5.1은 BOM 없는 `.ps1`의 한글을 CP949로 읽음)
