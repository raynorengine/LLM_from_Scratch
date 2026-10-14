# Progress

**마지막 갱신:** 2026-10-14

## 현재 단계
- **STEP 1 완료**: 글자 빈도 계산 (C) + 독립 검증
- **STEP 2 완료**: 유니그램 생성 (C) — 탐욕적 디코딩 vs 확률적 샘플링 + 독립 검증
- 다음: **STEP 3 바이그램** — 강의 자료(챕터 2-5) 수령 후 docs 작성 → 브리핑 → 승인 → 구현

## 완료
- `CLAUDE.md` 규칙 (워크플로우 / 장시간 작업 사전 승인 / 가짜 검증 금지 / 기록 트리거 / 세션 시작 절차 / 코딩 컨벤션)
  - 2026-10-14 추가: 학습 문서 흐름(raw→docs→실습→education), Step 폴더 구성, 결과 파일 번호 규칙, education 담는 범위
- `docs/` 01~04 (`04_Unigram.md` 신규). 01~04는 구 기준(압축형)으로 작성됨
- `education/` 01~03 (`03_Unigram.md` 신규 — C 핵심 로직 Q&A 중심, 2026-10-14 사용자 확정·종료)
- `PART1_N-Gram/Step1/`, `PART1_N-Gram/Step2/` 구현 + 검증

## 폴더 구성 규칙 (요약, 상세는 CLAUDE.md)
```
PART{N}_{주제}/Step{N}/
  step{N}_{이름}.c              C 구현 (유일한 구현)
  step{N}_verify.py             Python 독립 검증
  build.bat                     Step 폴더마다 복사, DEFAULT_SRC만 변경
  step{N}_{이름}_result_NN.txt  실행 결과 (01~99, 기존 최대+1)
```
- 데이터 경로: Step 폴더 기준 `../../data/`
- 다음 step의 입력이 되는 확정적 결과는 번호 없는 고정 이름 (예: `step1_char_freq_result.txt`)
- Python으로 C와 같은 기능을 다시 구현하지 않는다 (Step1의 `step1_char_freq.py`만 비교 학습 기록으로 남아 있음)

## 학습 데이터
- `data/korean_dictionary/words.txt` — 1줄 = 1단어, UTF-8(BOM 없음), LF 개행
  - 단어 **420,309개** / 글자 **1,551,565자**(개행 제외) / 고유 글자 **2,450종**
  - 코드포인트 전부 `0xAC00`~`0xD79D` (비한글 0종). '힣'은 없음
  - 강의 수치 "146만 자 / '다' 5.2%"는 실측과 다름 → 실측 기준 1,551,565 / 4.91%
- `data/korean_dictionary/data_all.txt` — 용도 미확인 (현재 미사용)
- `data/korean_free_novel/` — 근대문학 txt (문장 생성 단계에서 사용 예정)
- **`.gitignore`에 `data/` 포함 → Git 동기화 안 됨.** 다른 PC에서는 강의 자료를 다시 받아 `data/` 아래에 둘 것 (`data/data/`로 중첩되지 않게)

## 구현 현황
### `PART1_N-Gram/Step1/` — 글자 빈도
| 파일 | 역할 |
|---|---|
| `step1_char_freq.c` | 빈도 계산. 크기 11,172 배열(해시테이블 없음) |
| `step1_verify.py` | Python 독립 재계산 + `--self-test` |
| `step1_char_freq_result.txt` | 2,450종 빈도표. **Step2의 입력** (git 커밋됨) |
| `step1_char_freq.py` | C와 같은 기능의 Python 포팅 (비교 학습 기록, 정식 구현 아님) |

### `PART1_N-Gram/Step2/` — 유니그램
| 파일 | 역할 |
|---|---|
| `step2_unigram.c` | `../Step1/step1_char_freq_result.txt` 로드 → 누적 빈도 → `rand30`(rand 2개 → 30비트) → `rand_below`(모듈로 편향 재추첨) → `sample_index`(이진 탐색, upper_bound) |
| `step2_verify.py` | words.txt 재계산 기반 독립 검증 [1] r→글자 경계값 전수 [2] 100만 샘플 카이제곱 [3] greedy [4] 형식 + `--self-test` |
| `step2_unigram_result_01.txt` | seed 42, 5자 × 10개 (구 기본값) |
| `step2_unigram_result_02.txt` | seed 1791980925, 10자 × 30개 (현 기본값) |

```
build.bat                                  빌드 (Step 폴더에서)
step2_unigram.exe [글자수=10] [개수=30] [seed=현재시각] [빈도표] [출력]
step2_unigram.exe --hist <N> <seed>        검증용: 샘플 히스토그램
step2_unigram.exe --map                    검증용: stdin r → 글자
python step2_verify.py [--self-test]
```

## 다음 할 일
1. **STEP 3 바이그램** (강의 챕터 2-5 필요)
   - 실측: 서로 다른 글자 쌍 167,399개, 2,450² 배열 중 2.79%만 사용 → 해시테이블 필요
   - 뽑기는 앞 글자별 누적 빈도 + 이진 탐색으로 Step2 로직 재사용 가능
2. 미결 사항 (사용자 결정 대기)
   - `docs/04_Unigram.md`를 새 docs 기준(추가 설명·사례 보강)으로 다시 쓸지
   - `.gitignore`에 `~$*` 등 Word 임시파일 추가 여부 (`raw/~$...docx`, `raw/~WRL*.tmp`가 미추적 상태로 있음)
   - **커밋 안 됨**: Step1 폴더 이동, Step2 전체, 문서 갱신이 전부 미커밋

## 이슈/주의사항
- **git push 인증 함정**: 샌드박스의 `GIT_TERMINAL_PROMPT=0`, `GCM_INTERACTIVE=never` 해제 후 `git -c credential.guiPrompt=true`로 push (`devlog/2026-10-07.md`)
- **이 PC의 `core.autocrlf=true`**: 커밋된 결과 파일이 작업본에서 CRLF가 됨. 바이트 비교는 `git cat-file -p HEAD:<경로>`와 할 것. C에서 결과 파일을 읽을 땐 `\r` 제거
- **Claude 셸의 `NoDefaultCurrentDirectoryInExePath=1`**: cmd가 현재 폴더 exe를 못 찾음 → `build.bat`은 전체 경로로 호출, exe는 `./`로 직접 실행 (`build.bat ... run`의 실행 단계는 Claude 환경에서 실패)
- git 사용자 정보는 이 저장소 로컬에만 설정: **`raynorengine` / `raynorengine@gmail.com`** (사용자 확인 2026-10-14). GitHub 저장소는 public
  - ⚠ 2026-10-07 다른 PC에서 `dongwookraynor@gmail.com`(Claude 계정 이메일)으로 잘못 설정됨 → 그 PC에서 작업 시 `git config --local user.email raynorengine@gmail.com` 먼저 실행
- C 함정: 개행을 글자로 세지 않기 / UTF-8 선두 바이트로 길이 판단 / `SetConsoleOutputCP(CP_UTF8)` + `/utf-8` 둘 다 / `"rb"` 바이너리 모드
- 빌드 스크립트는 `.bat` (PowerShell 5.1은 BOM 없는 `.ps1`의 한글을 CP949로 읽음)
