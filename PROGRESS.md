# Progress

**마지막 갱신:** 2026-10-24 (Step7 형태소 토큰 N-그램 1~6단계 완료 + docs/09 + education 08·09)

## 현재 단계
- **STEP 1 완료**: 글자 빈도 계산 (C) + 독립 검증
- **STEP 2 완료**: 유니그램 생성 (C) — 탐욕적 디코딩 vs 확률적 샘플링 + 독립 검증
- **STEP 3 완료**: 바이그램 (C) — 같은 모델을 ① 압축+2차원 배열 ② 해시만 ③ 해시+CSR 세 구현으로 비교 + 독립 검증
- **STEP 4 완료 (2026-10-22 재작성)**: 바이그램·트라이그램 단일 파일 2개, **해시만** (CSR 제거, 사용자 지시)
  - 같은 seed면 옛 CSR 버전과 단어 1,000개·빈도표 전부 동일. 결과 seed 1792641056: 41.3% / 58.1%
- **STEP 5 완료 (2026-10-22)**: n=2~6 C 파일 5개, 해시만. **단어** 생성
  - seed 1792649450 사전 일치율: 42.8 / 57.6 / 94.4 / 99.7 / 100.0%
  - 발견: BOS n-1 패딩 때문에 **길이 n-2 이하 생성 단어는 반드시 사전 단어** (education/06 5.2)
- **STEP 6 완료 (2026-10-22)**: n=3/5/7 C 파일 3개, 해시만. **문장** 생성 (강의 2-8)
  - 전처리 `step6_prepare.py`가 원본을 `.?!`로 쪼개 `data/novel_sentences.txt` 생성 (66,961문장)
  - 원본 통째 일치: 3그램 0/15 · 5그램 1/15(6.7%) · 7그램 3/15(20.0%) — 7그램에서 암기가 드러남
  - **문장 데이터에서는 7그램이 가능**: 글자 종류가 줄어(2,450→1,596) V⁶ = 1.67×10¹⁹ < 2⁶⁴ (여유 1.108배)
  - 검증 88개 전부 통과 (약 41초)
- **STEP 7 진행 중 (2026-10-23~24)**: **형태소 토큰 단위** 6그램 (강의 2-9). Phase로 쪼개 진행
  - 1단계 전처리 / 2단계 토큰화 / 3단계 번호·세기·생성 / 4단계 역변환 / 5단계 원본 비교 / 6단계 프롬프트 **완료**
  - **7단계(백오프)는 Step8로 분리** — 아직 시작 안 함
  - 첫 외부 의존성: **Kiwi**(`kiwipiepy` 0.24.1). 토큰 = 형태소, 공백은 `<S>`, 토큰 구분은 `<W>`
  - 어휘 16,918종 / 토큰 1,460,593개 / 문장 64,376개
  - 생성 200문장 실측: 통째 일치 24.5%, **평균 복사 비율 71.0%**, 중복 0%
  - 프롬프트 `돈을` 200문장: 통째 14.0%, 복사 74.6%, **중복 39%** → 7단계 평가는 프롬프트 조건에서

## 완료
- `CLAUDE.md` 규칙 (워크플로우 / 장시간 작업 사전 승인 / 가짜 검증 금지 / 기록 트리거 / 세션 시작 절차 / 코딩 컨벤션)
  - 2026-10-21: 학습용 주석 규칙, 여러 파일 C 구현 규칙
  - 2026-10-22: 강의 결과는 목표 아님 / 예시 값은 실측만 / `.gitattributes`
- `docs/` 01~09 (`09_Morpheme-NGram.md` 신규 — 형태소 분석, `<W>`/`<S>` 표기, 백오프, 과적합)
- `education/` 01~09 — **2026-10-22 개번**: 옛 `07_N-Gram` → `06_N-Gram` (폐기된 `06_CSR` 자리를 메움),
  문장 생성 문서가 `07_Sentence-Generation` (본문 H1 `# 08.` 오타도 수정)
  - `08_Morpheme-Tokenization` (Step7 1·2단계) / `09_Token-NGram` (Step7 3~6단계, 복습 정리 1~32)
  - **단계가 늘어도 문서를 새로 만들지 않는다** — 같은 Step의 연속 단계는 한 문서에 이어 쓴다 (2026-10-24 사용자 지시)
- `PART1_N-Gram/Step1/` ~ `Step7/` 구현 + 검증
- `reference/` — 강의 실습 예제 코드(C·Python). 참고용, 필수 아님 (내부 `data/`는 gitignore)
- 루트 `.gitattributes` (`*.txt text eol=lf`), `.claude/settings.json` (attribution 끔)

## 폴더 구성 규칙 (요약, 상세는 CLAUDE.md)
```
PART{N}_{주제}/Step{N}/
  step{N}_{이름}.c              C 구현 (여러 파일이면 + _*.c, .h / 모델별 프로그램이면 step{N}_{모델}.c 여러 개)
  step{N}_verify.py             Python 독립 검증 (폴더의 프로그램 전부)
  build.bat                     Step 폴더마다 복사, DEFAULT_SRC만 변경. build.bat <소스이름> 으로 대상 지정
  step{N}_{이름}_result_NN.txt  실행 결과 (01~99, 기존 최대+1)
```
- 데이터 경로: Step 폴더 기준 `../../data/`
- 코드 주석: 구역마다 [목적] / [동작] / [이유] (학습용, Step3부터). 맨 위 목차 줄 번호는 verify [7]이 검사

## 학습 데이터
- `data/korean_dictionary/words.txt` — 1줄 = 1단어 (Step1~5)
  - 단어 **420,309개** / 글자 **1,551,565자** / 고유 글자 **2,450종** (전부 한글), 평균 3.691글자
  - n그램 총 개수는 n과 무관하게 **1,971,874**
  - 서로 다른 n그램 / 문맥: n=2 171,425 / 2,451 · n=3 654,039 / 169,589 · n=4 976,877 / 547,576 · n=5 1,105,860 / 684,407 · n=6 1,141,426 / 720,877
- `data/korean_dictionary/novel_test_data.txt` — 소설 코퍼스 **(Step6 원본)**. 옛 이름 `data_all.txt`, 사용자가 개명
  - 4,667,332바이트 / CRLF / BOM 없음 / **35,034줄**(빈 줄 제외) / 1,909,086글자 / **1,596종**
  - 코드포인트 범위 U+0020 ~ U+FF63. UTF-8 1바이트 80종 / 2바이트 1종(NBSP) / 3바이트 1,515종
  - 강의 주장(17만 문장·587만 글자·1,874종)과 어긋남 → **실측만 사용** (사용자 지시)
- `data/novel_sentences.txt` — 위를 문장 단위로 쪼갠 것 **(Step6 입력)**. `step6_prepare.py`가 생성
  - **66,961문장 / 1,877,291글자 / 1,596종** / 평균 28.04글자 / 최대 358
  - 전체 n그램은 n과 무관하게 **1,944,252** (= 글자 1,877,291 + 문장 66,961)
  - 서로 다른 n그램 / 문맥: n=3 171,051 / 38,032 · n=5 821,221 / 473,641 · n=7 1,319,934 / 1,092,883
- `data/korean_free_novel/` — 근대문학 txt 84개 (10.58MB). Step6은 쓰지 않음 (novel_test_data.txt 사용)
- **Step7은 `data/novel_sentences.txt`를 쓰지 않는다.** 공부용으로 `novel_test_data.txt`부터 다시 전처리했고,
  산출물은 전부 `PART1_N-Gram/Step7/` 안에 둔다 (2026-10-24 규칙)
  - `step7-1_sentences.txt` — **64,376문장 / 1,845,900글자 / 1,562종** (정제로 33,825자·34종 제거)
  - `step7-2b_tokens.txt` — 토큰 **1,460,593개**, `<S>` 408,173개(27.9%), 형태소 어휘 16,917종
- **`.gitignore`에 `data/` 포함 → Git 동기화 안 됨.** 다른 PC에서는 강의 자료를 다시 받아 `data/` 아래에 둘 것
  - `novel_sentences.txt`도 git에 없다 → 새 PC에서 `python step6_prepare.py` 를 한 번 실행해야 한다

## 구현 현황
### `Step1/` 글자 빈도 — `step1_char_freq.c`, 결과 `step1_char_freq_result.txt`(Step2 입력)
### `Step2/` 유니그램 — `step2_unigram.c`, Step1 빈도표 로드 → 누적 빈도 + 이진 탐색
### `Step3/` 바이그램 — 세 구현 비교 (멀티 파일)
| 파일 | 역할 |
|---|---|
| `step3_bigram.h` / `.c` | 공통 인터페이스 `BigramModel`(함수 포인터), main·검증 모드 |
| `step3_bigram_array.c` / `_hash.c` / `_csr.c` | ① 2,452² 배열 ② 해시(int 키) + 후보 순차 조회 ③ 해시 → CSR + 이진 탐색 |
- 실측(10,000개 생성): array 22~28ms / hash 약 485ms / csr 약 3.7ms

### `Step4/` 바이그램 + 트라이그램 (단일 파일 2개, 해시만)
- `step4_bigram.c` / `step4_trigram.c` — 15구역, 64비트 키, 후보 2,452개 순차 조회, 1,000개 + 사전 일치율
- `step4_verify.py` — [1]~[7], `--self-test` 18종, `--compare`. 표본 1,000개
- 생성 1,000개 226ms / 361ms, 메모리 6.29 / 25.17MB

### `Step5/` n = 2 ~ 6, 단어 생성 (단일 파일 5개, 해시만)
| 파일 | 역할 |
|---|---|
| `step5_bigram.c`, `step5_trigram.c` | Step4 복사 (이름만 변경). 빈도표가 Step4와 바이트 동일 |
| `step5_4gram.c`, `step5_5gram.c` | trigram 기반, 문맥 변수 `prev1..prev{n-1}`. `HASH_BITS` 22 |
| `step5_6gram.c` | **키를 `ctxs[]`(문맥 64비트) + `nexts[]`로 분리** (V⁶ ≈ 2.2×10²⁰ > 2⁶⁴) |
| `step5_verify.py` | 5개 각각 [1]~[7] + n=2·3 Step4 교차, `--self-test` 19종, `--compare` (verify 약 3분 23초) |

### `Step6/` n = 3 / 5 / 7, **문장 생성** (단일 파일 3개, 해시만)
| 파일 | 역할 |
|---|---|
| `step6_prepare.py` | 전처리. `novel_test_data.txt` → `novel_sentences.txt`. 종결부호 분할 + **글자 손실 전수 검사**, `--self-test` 5종 |
| `step6_3gram.c`, `step6_5gram.c` | 15구역. 단일 64비트 키 (V³ / V⁵). `HASH_BITS` 19 / 21 |
| `step6_7gram.c` | **키를 `ctxs[]` + `nexts[]`로 분리** (V⁷ ≈ 2.7×10²² > 2⁶⁴). `HASH_BITS` 22 |
| `step6_verify.py` | 3개 각각 [1]~[8], `--self-test` 4종, `--compare`. **88개 검사 약 41초** |
| `step6_compare.txt`, `step6_*_result_01.txt` | 비교 문서 / 결과 (seed 1792649450, 15문장) |
- Step5와 달라진 곳 5군데: `[4]` UTF-8 1~3바이트 / `[5]` `used[65536]` + V 상한 검사 + 표시 이름 /
  `[5]` 입력 파일 / `[10]` `MAX_LEN` 400 / `[11]` 평가 지표 (사전 일치율 → 원본 문장 통째 일치)
- 실측: 세기 10.9 / 41.6 / 76.1ms, 생성 15개 7.5 / 11.6 / 14.4ms, 메모리 6.3 / 25.2 / 67.1MB
- 빈도표 3.9 / 25.9 / 52.7MB (gitignore)
```
step6_prepare.py                                        <- 새 PC에서 1회 필수
step6_7gram.exe [개수=15] [seed=현재시각] [결과] [빈도표]   (3gram, 5gram 동일)
python step6_verify.py [--n 7 | --self-test | --compare]
```
- **공백·구두점도 글자**다. 특별 처리를 추가하지 않는 것이 핵심 (공백은 번호 0번, 전체의 21.6%)
- 빈도표·검증 입출력에서 공백은 `<SP>`, NBSP는 `<NBSP>` 로 적는다 (탭 구분 표에서 열을 구분하려고).
  데이터에 `<` `>` 가 없음을 전수 확인해 충돌 없음. **결과 파일의 문장은 글자 그대로**

## 다음 할 일
1. **Step8 = 7단계 백오프** — 브리핑 → 승인 → 구현
   - 고칠 것 둘: ① 막힌 프롬프트(`로봇이 돈을` 후보 0개) ② 프롬프트 조건 중복 39%
   - 강의 설정: `MIN_BACKOFF 2`, `BACKOFF_THRESHOLD 1`(후보 1개면 백오프), `MAX_NGRAM 10`
   - 평가는 **프롬프트 고정 조건**에서 중복률 + 평균 복사 비율
2. Step7 미착수: n을 2~10으로 확장(강의 범위). 단일 n 파이프라인이 끝난 뒤로 미룸
3. 사용자 결정 대기
   - Step5 trigram ↔ 4~6gram **작성 방식 통일** 여부 (n과 무관한 차이 5곳, education/06 3.4)
   - `education/05`·`docs/06`의 CSR 서술 정리 (사용자가 추후 지시)
   - `docs/07`에 Step5 실측 반영 여부
   - `docs/04_Unigram.md` 재작성, Step1·2 주석 소급 적용 (이전부터 미결)
   - Step6에 **최장 연속 일치 길이** 지표 추가 여부 (브리핑에서 제안했으나 비용 때문에 보류)
4. 미반영: `.gitignore`에 `~$*` 추가
5. 미확인: 강의 트라이그램 일치율 61.7% vs 실측 57~58%의 원인
6. 해소됨: `education/06_CSR.md` 결번 → 2026-10-22 사용자가 `07_N-Gram` → `06_N-Gram` 으로 개번

## 이슈/주의사항
- **빈도표(`*_table.txt`)는 git에 없다** (`.gitignore`). 새 PC에서는 각 step 프로그램을 인자 없이 한 번 실행해야 생기고, 그 전에는 verify의 빈도표 검사가 FAIL
- **Step6은 `step6_prepare.py`를 먼저 돌려야 한다.** `data/novel_sentences.txt`가 없으면 C가 입력을 못 찾는다
- **`isspace()`에는 NBSP가 들어 있다** (2026-10-22 실제 결함): 손실 검사에서 공백류를 통째로 제외했더니
  NBSP 31개가 사라져도 "손실 0"으로 보고했다. **검사에서 제외하는 범위는 직접 열거해서 최소로** 잡을 것
  (`SKIPPABLE_WS = " \t"`). 남이 정한 분류를 검사 범위로 쓰면 구멍이 난다
- **커밋 attribution 끔**: `.claude/settings.json`의 `attribution` (Co-Authored-By 안 붙음)
- **Windows에서 C의 argv는 CP949**: 한글을 명령줄 인자로 넘기면 깨진다
  - Step6까지: 검증 모드가 한글을 **표준입력**으로 받음
  - Step7-6부터: `CommandLineToArgvW` + `WideCharToMultiByte(CP_UTF8)` 로 명령줄을 다시 받는 `utf8_argv()` 사용
- **git push 인증 함정**: 샌드박스의 `GIT_TERMINAL_PROMPT=0`, `GCM_INTERACTIVE=never` 해제 후 `git -c credential.guiPrompt=true`로 push (`devlog/2026-10-07.md`)
- **이 PC의 `core.autocrlf=true`**: LF로 커밋된 파일이 `git status`에 `M`으로 떠도 `git diff`가 비어 있으면 줄바꿈 표시 차이일 뿐이다 (내용 변경 아님)
- **Claude 셸의 `NoDefaultCurrentDirectoryInExePath=1`**: `build.bat`은 `.\build.bat` 처럼 경로를 명시해 호출, exe는 `./`로 직접 실행
- **`.pyc` 캐시**: 검증기를 import로 시험할 때는 `python -B`
- git 사용자: **`raynorengine` / `raynorengine@gmail.com`** (로컬 설정). GitHub 저장소는 public
- C 함정: 개행을 글자로 세지 않기 / UTF-8 선두 바이트로 길이 판단 / `SetConsoleOutputCP(CP_UTF8)` + `/utf-8` 둘 다 / `"rb"` 바이너리 모드 / 곱하기 전에 64비트로 변환 / **오버플로는 곱하기 전에 나눗셈으로 검사**
- 빌드 스크립트는 `.bat` (PowerShell 5.1은 BOM 없는 `.ps1`의 한글을 CP949로 읽음)
