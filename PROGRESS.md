# Progress

**마지막 갱신:** 2026-10-07

## 현재 단계
- **STEP 1 완료**: 글자 빈도 계산 프로그램(C) + 독립 검증 통과
- 다음: **STEP 2 유니그램** — 빈도표로 글자 뽑기(샘플링), 탐욕적 vs 확률적 비교

## 완료
- `CLAUDE.md` 규칙 확정 (워크플로우 / 장시간 작업 사전 승인 / 가짜 검증 금지 / 기록 트리거 / 세션 시작 절차 / 코딩 컨벤션)
- `docs/` 이론 문서 3종 — `01_N-Gram.md`, `02_AI-Coding-Setup.md`, `03_Dictionary-Data.md`
- `education/` 학습 자료 2종 — `01_Harness-Engineering.md`, `02_Character-Frequency.md`
- `PART1_N-Gram/` STEP 1 구현 + 검증 (아래 "구현 현황")
- git / gh 설치 완료 (2026-10-07, winget)

## 학습 데이터
- `data/korean_dictionary/words.txt` — 1줄 = 1단어, UTF-8(BOM 없음), LF 개행
  - 단어 **420,309개** / 글자 **1,551,565자**(개행 제외) / 고유 글자 **2,450종**
  - 파일 문자수 1,971,874 = 글자 1,551,565 + 개행 420,309 (강의의 "197만 자"는 개행 포함 수치)
  - 코드포인트 전부 `0xAC00`~`0xD79D` (비한글 0종) → 배열 인덱싱 안전
  - 강의 제공 **전처리본** (품사 제거 / 특수기호 제거 / 미사용 글자 제거). GitHub 원본을 받지 않는다
- `data/korean_free_novel/` — 근대문학 txt (문장 생성 단계에서 사용 예정)
- **`.gitignore`에 `data/` 포함 → Git 동기화 안 됨.** 다른 PC에서는 강의 자료 재다운로드 필요

## 구현 현황 (`PART1_N-Gram/`)
| 파일 | 역할 |
|---|---|
| `step1_char_freq.c` | 글자 빈도 계산. 빈도표는 **해시테이블이 아니라 크기 11,172 배열** |
| `build.bat` | MSVC 빌드 (step 공용, 소스명을 인자로 받음) |
| `step1_verify.py` | Python 독립 재계산 교차검증 |
| `step1_char_freq_result.txt` | 전체 2,450종 빈도표 (탭 구분) |

```
build.bat                        빌드 (기본 step)
build.bat step1_char_freq run    빌드 후 실행
step1_char_freq.exe [입력] [상위N] [출력]
python step1_verify.py           전수 교차검증
python step1_verify.py --self-test   검증기가 FAIL을 낼 수 있는지 확인
```

- **검증 통과**: `docs/01_N-Gram.md:27` 대조 — `다 76,241 / 4.91%`, 상위 10위 순서까지 일치
- 2,450종 전수 비교 전항목 일치 / self-test 변조 4케이스 모두 FAIL 감지

## 다음 할 일
- **STEP 2 유니그램** 구현 (브리핑 → 승인 → 구현)
- (완료) GitHub 최초 push — 커밋 `286754c`, 2026-10-07

## 미결정
- 폴더명 `PART1_N-Gram` 유지 vs `Step1_N-Gram`으로 변경
  - 강의 문서(`챕터2-3`)는 `Step1_N-Gram`으로 표기. 변경 시 `CLAUDE.md`의 `PART{N}_{주제}` 규칙도 수정 필요

## 이슈/주의사항
- **git push 시 인증 함정**: 샌드박스에서 `GIT_TERMINAL_PROMPT=0`, `GCM_INTERACTIVE=never`로 묶여 있어 인증을 묻지 못하고 실패한다.
  두 값을 해제하고 `git -c credential.guiPrompt=true` 로 push할 것 (`devlog/2026-10-07.md` 참고)
- git 사용자 정보는 **이 저장소 로컬에만** 설정됨 (`raynorengine` / `dongwookraynor@gmail.com`)
- **GitHub 저장소는 public** (`raynorengine/LLM_from_Scratch`). `raw/`의 강의 정리 docx도 함께 공개됨
- C 구현 시 반복되는 함정 4개 (`step1_char_freq.c` 주석에 기록)
  1. 개행(`\n`)을 글자로 세면 확률이 4.91% → 3.87%로 틀어짐
  2. 한글 1음절 = UTF-8 3바이트 → 선두 바이트로 길이를 판단해 묶어야 함
  3. `SetConsoleOutputCP(CP_UTF8)` 없으면 Windows 콘솔에서 한글 깨짐
  4. `cl /utf-8` 없으면 소스의 한글 문자열 리터럴이 깨짐 (3번과 별개 문제)
- 빌드 스크립트는 `.ps1`이 아니라 `.bat` — PowerShell 5.1은 BOM 없는 `.ps1`의 한글을 CP949로 잘못 읽음
- 확률 합계가 99.9998% — double 누적 오차. 계산 오류는 아니며 검증기는 0.01% 허용 오차로 통과 처리
