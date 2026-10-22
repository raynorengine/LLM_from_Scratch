# Progress

**마지막 갱신:** 2026-10-22 (Step4 CSR 제거 재작성 + Step5 n=2~6 구현·검증 + education/07. 전부 미커밋)

## 현재 단계
- **STEP 1 완료**: 글자 빈도 계산 (C) + 독립 검증
- **STEP 2 완료**: 유니그램 생성 (C) — 탐욕적 디코딩 vs 확률적 샘플링 + 독립 검증
- **STEP 3 완료**: 바이그램 (C) — 같은 모델을 ① 압축+2차원 배열 ② 해시만 ③ 해시+CSR 세 구현으로 비교 + 독립 검증
- **STEP 4 완료 (2026-10-22 재작성)**: 바이그램·트라이그램 단일 파일 2개, **해시만** (CSR 제거, 사용자 지시)
  - 같은 seed면 옛 CSR 버전과 단어 1,000개·빈도표 전부 동일. 새 결과 seed 1792641056: 41.3% / 58.1%
- **STEP 5 완료 (2026-10-22)**: n=2~6 C 파일 5개 (`step5_bigram/_trigram/_4gram/_5gram/_6gram.c`), 해시만
  - seed 1792649450 사전 일치율: 42.8 / 57.6 / 94.4 / 99.7 / 100.0%, 사전에 없는 단어 572 / 424 / 56 / 3 / 0
  - 발견: BOS n-1 패딩 때문에 **길이 n-2 이하 생성 단어는 반드시 사전 단어** (education/07 5.2)
- 다음: 강의 예고는 **단어 → 문장** (소설 데이터 `data/korean_free_novel/`). 강의 자료 수령 대기

## 완료
- `CLAUDE.md` 규칙 (워크플로우 / 장시간 작업 사전 승인 / 가짜 검증 금지 / 기록 트리거 / 세션 시작 절차 / 코딩 컨벤션)
  - 2026-10-21: 학습용 주석 규칙, 여러 파일 C 구현 규칙
  - 2026-10-22: 강의 결과는 목표 아님 / 예시 값은 실측만 / `.gitattributes`
- `docs/` 01~07 (`07_N-Gram.md` 신규 — 강의 주장(미확인)과 실측 분리, 키 한계 V⁶ > 2⁶⁴)
- `education/` 01~05, 07 (`07_N-Gram.md` 신규 — 글자/단어/seq, 5개 파일 코드 차이, 인사이트 9개, Step2~5 코드 계보, AI 교훈 6개)
- `PART1_N-Gram/Step1/` ~ `Step5/` 구현 + 검증
- 루트 `.gitattributes` (`*.txt text eol=lf`)

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
- `data/korean_dictionary/words.txt` — 1줄 = 1단어, UTF-8(BOM 없음), LF 개행
  - 단어 **420,309개** / 글자 **1,551,565자** / 고유 글자 **2,450종** (전부 한글, '힣' 없음), 평균 3.691글자, 4글자 이하 79.4%
  - n그램 총 개수(BOS n-1개 + EOS 1개 패딩)는 n과 무관하게 **1,971,874**
  - 서로 다른 n그램 / 문맥: n=2 171,425 / 2,451 · n=3 654,039 / 169,589 · n=4 976,877 / 547,576 · n=5 1,105,860 / 684,407 · n=6 1,141,426 / 720,877
- `data/korean_dictionary/data_all.txt` — 용도 미확인 (현재 미사용)
- `data/korean_free_novel/` — 근대문학 txt (문장 생성 단계에서 사용 예정)
- **`.gitignore`에 `data/` 포함 → Git 동기화 안 됨.** 다른 PC에서는 강의 자료를 다시 받아 `data/` 아래에 둘 것

## 구현 현황
### `Step1/` 글자 빈도 — `step1_char_freq.c`, 결과 `step1_char_freq_result.txt`(Step2 입력)
### `Step2/` 유니그램 — `step2_unigram.c`, Step1 빈도표 로드 → 누적 빈도 + 이진 탐색
### `Step3/` 바이그램 — 세 구현 비교 (멀티 파일)
| 파일 | 역할 |
|---|---|
| `step3_bigram.h` / `.c` | 공통 인터페이스 `BigramModel`(함수 포인터), main·검증 모드 |
| `step3_bigram_array.c` / `_hash.c` / `_csr.c` | ① 2,452² 배열 ② 해시(int 키) + 후보 순차 조회 ③ 해시 → CSR + 이진 탐색 |
| `step3_verify.py`, `step3_bigram_table.txt`, `result_01~03` | 검증 / 빈도표 / 결과 |
- 실측(10,000개 생성): array 22~28ms / hash 약 485ms / csr 약 3.7ms

### `Step4/` 바이그램 + 트라이그램 (단일 파일 2개, 해시만)
| 파일 | 역할 |
|---|---|
| `step4_bigram.c` / `step4_trigram.c` | 15구역. words.txt → 압축 번호 → seq → 해시(64비트 키)로 세기 → 후보 2,452개 순차 조회로 뽑기 → 1,000개 + 사전 일치율 |
| `step4_verify.py` | [1]~[7] 검증, `--self-test` 18종, `--compare`. [3][4] 표본 1,000개 |
| `step4_compare.txt`, `step4_*_table.txt`, `step4_*_result_01.txt` | 비교 문서 / 빈도표 / 결과 (seed 1792641056) |
- 생성 1,000개 226ms / 361ms, 메모리 6.29 / 25.17MB (CSR 시절 1.1·1.7ms, 1.40·7.27MB)

### `Step5/` n = 2 ~ 6 (단일 파일 5개, 해시만)
| 파일 | 역할 |
|---|---|
| `step5_bigram.c`, `step5_trigram.c` | Step4 복사 (이름만 변경). 빈도표가 Step4와 바이트 동일 |
| `step5_4gram.c`, `step5_5gram.c` | trigram 기반, 문맥 변수 `prev1..prev{n-1}`. `HASH_BITS` 22 |
| `step5_6gram.c` | **키를 `ctxs[]`(문맥 64비트) + `nexts[]`로 분리** (V⁶ ≈ 2.2×10²⁰ > 2⁶⁴) |
| `step5_verify.py` | 5개 각각 [1]~[7] + n=2·3 Step4 빈도표 교차, `--self-test` 19종(6gram 대상, 키 넘침 흉내 포함), `--compare` |
| `step5_compare.txt` | n별 일치율·길이별·길이 분포·사전에 없는 단어·강의 보고값 나란히 (해석 문장 없음) |
| `step5_*_table.txt`, `step5_*_result_01.txt` | 빈도표(3.3 / 15.5 / 27.6 / 35.7 / 41.5MB) / 결과 (seed 1792649450) |
```
step5_6gram.exe [개수=1000] [seed=현재시각] [결과] [빈도표]      (다른 n도 같은 형식)
python step5_verify.py [--self-test | --compare]                 (verify 약 3분 23초, compare 약 1분 20초)
```

## 다음 할 일
1. 다음 강의(문장 생성) 자료 수령 → docs → 브리핑 → 승인 → 구현
2. 사용자 결정 대기
   - Step5 trigram ↔ 4~6gram **작성 방식 통일** 여부 (n과 무관한 차이 5곳, education/07 3.4)
   - 4~6그램 빈도표(최대 41MB) 커밋 여부
   - `education/05`·`docs/06`의 CSR 서술 정리 (사용자가 추후 지시)
   - `docs/07`에 Step5 실측 반영 여부
   - `docs/04_Unigram.md` 재작성, Step1·2 주석 소급 적용 (이전부터 미결)
3. 미반영: `.gitignore`에 `~$*` 추가 / devlog에 작성했다고 적힌 `education/06_CSR.md`가 이 PC에 없음
4. 미확인: 강의 트라이그램 일치율 61.7% vs 실측 57~58%의 원인
5. 커밋 안 된 작업: Step4 재작성, Step5, docs/07, education/07, CLAUDE.md, `.gitattributes`, devlog, `raw/챕터2-7` (사용자가 꺼낼 때 진행)

## 이슈/주의사항
- **빈도표(`*_table.txt`)는 git에 없다** (2026-10-22부터 `.gitignore`). 새 PC에서는 각 step 프로그램을 인자 없이 한 번 실행해야 생기고, 그 전에는 verify [6]이 FAIL
- **커밋 attribution 끔**: `.claude/settings.json`의 `attribution` (Co-Authored-By 안 붙음)
- **Windows에서 C의 argv는 CP949**: 한글을 명령줄 인자로 넘기면 깨진다 → 검증 모드는 한글을 표준입력으로 받는다
- **git push 인증 함정**: 샌드박스의 `GIT_TERMINAL_PROMPT=0`, `GCM_INTERACTIVE=never` 해제 후 `git -c credential.guiPrompt=true`로 push (`devlog/2026-10-07.md`)
- **이 PC의 `core.autocrlf=true`**: `.gitattributes`(2026-10-22) 이전에 받은 txt는 CRLF일 수 있음 → 바이트 비교 실패의 원인. C에서 결과 파일을 읽을 땐 `\r` 제거
- **Claude 셸의 `NoDefaultCurrentDirectoryInExePath=1`**: `build.bat`은 전체 경로로 호출, exe는 `./`로 직접 실행
- **`.pyc` 캐시**: 검증기를 import로 시험할 때는 `python -B`
- git 사용자: **`raynorengine` / `raynorengine@gmail.com`** (로컬 설정). GitHub 저장소는 public
- C 함정: 개행을 글자로 세지 않기 / UTF-8 선두 바이트로 길이 판단 / `SetConsoleOutputCP(CP_UTF8)` + `/utf-8` 둘 다 / `"rb"` 바이너리 모드 / 곱하기 전에 64비트로 변환
- 빌드 스크립트는 `.bat` (PowerShell 5.1은 BOM 없는 `.ps1`의 한글을 CP949로 읽음)
