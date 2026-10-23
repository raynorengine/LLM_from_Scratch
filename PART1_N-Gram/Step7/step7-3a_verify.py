"""[STEP 7 / Phase 3-a] 독립 검증 - C가 만든 어휘표가 맞는가

검증 방식 (CLAUDE.md 검증 규칙):
  C의 출력을 받아쓰지 않는다. **step7-2b_tokens.txt 에서 처음부터 다시 계산**해
  어휘·빈도·번호를 만들고, C가 만든 step7-3a_vocab.txt 와 **전수 비교**한다.
  C는 해시 테이블 + qsort, 여기는 파이썬 dict + sorted 로 **다른 방식**이다.

검사 항목:
  [1] 문장 수 / 토큰 총수
  [2] 어휘 종류 수
  [3] 번호 체계 (BOS=0, EOS=1, 실제 토큰은 2번부터 / 빠진 번호·중복 없음)
  [4] 정렬 순서 (UTF-8 바이트 사전순)
  [5] 토큰 목록 전수 일치
  [6] 빈도 전수 일치
  [7] 빈도 합 == 토큰 총수

사용법:
  python step7-3a_verify.py              검증
  python step7-3a_verify.py --self-test  검증기가 망가진 입력에 FAIL을 내는지 확인

종료 코드: 0 = 성공 / 1 = 실패
"""

from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOKENS_PATH = HERE / "step7-2b_tokens.txt"
VOCAB_PATH = HERE / "step7-3a_vocab.txt"

W = "<W>"
ID_BOS, ID_EOS, ID_BASE = 0, 1, 2


# ===================================================================
# [A] 원본에서 독립 재계산
#
# [목적] C와 같은 결과를 **다른 경로로** 만든다.
# [동작] 토큰 파일을 파이썬으로 파싱해 빈도를 세고, UTF-8 바이트 사전순으로
#   정렬한 뒤 2번부터 번호를 매긴다.
# [이유] 정렬 기준을 `key=lambda s: s.encode("utf-8")` 로 두는 것이 핵심이다.
#   파이썬 문자열의 기본 정렬은 **코드포인트 순서**라 C의 strcmp(바이트 순서)와
#   다를 수 있다. 기준이 어긋나면 번호가 통째로 밀려 전부 불일치로 나온다.
# ===================================================================
def recompute(path: Path) -> tuple[int, int, list[tuple[int, str, int]]]:
    freq: Counter = Counter()
    sentences = 0
    total = 0

    with path.open(encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n").rstrip("\r")
            if not line:
                continue
            sentences += 1
            for piece in line.split(W):
                if not piece:
                    continue
                freq[piece] += 1
                total += 1

    ordered = sorted(freq, key=lambda s: s.encode("utf-8"))
    table = [(ID_BASE + i, tok, freq[tok]) for i, tok in enumerate(ordered)]
    return sentences, total, table


# ===================================================================
# [B] C가 만든 어휘표 읽기
#
# [이유] `#` 로 시작하는 줄은 설명이므로 건너뛴다. 토큰 자체에는 탭이 들어갈 수
#   없으므로(Phase 1 정제가 막았다) 탭 2개로 끊으면 안전하다.
# ===================================================================
def read_vocab(path: Path) -> list[tuple[int, str, int]]:
    rows: list[tuple[int, str, int]] = []
    with path.open(encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n").rstrip("\r")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) != 3:
                raise ValueError(f"어휘표 형식 오류: {line!r}")
            rows.append((int(parts[0]), parts[1], int(parts[2])))
    return rows


# ===================================================================
# [C] 비교 - 7개 항목
#
# [이유] 상위 몇 개만 맞춰 보지 않고 **전 항목**을 비교한다 (CLAUDE.md 검증 규칙 3번).
#   불일치가 나오면 앞의 몇 건만 보여 주되, 총 건수는 전부 센다.
# ===================================================================
def compare(sentences: int, total: int, mine: list, theirs: list) -> list[str]:
    errors: list[str] = []

    # C 표의 앞 두 줄은 BOS/EOS 여야 한다
    if len(theirs) < 2:
        return ["어휘표가 너무 짧습니다"]
    if theirs[0] != (ID_BOS, "BOS", 0):
        errors.append(f"[3] 0번이 BOS가 아님: {theirs[0]}")
    if theirs[1] != (ID_EOS, "EOS", 0):
        errors.append(f"[3] 1번이 EOS가 아님: {theirs[1]}")
    body = theirs[2:]

    # [2] 어휘 종류 수
    if len(body) != len(mine):
        errors.append(f"[2] 어휘 수 불일치: C {len(body):,} vs 재계산 {len(mine):,}")

    # [3] 번호가 2부터 빠짐없이 1씩 증가하는가
    for i, (tid, _, _) in enumerate(body):
        if tid != ID_BASE + i:
            errors.append(f"[3] 번호가 어긋남: {i}번째 줄의 번호가 {tid} (기대 {ID_BASE + i})")
            break

    # [4][5][6] 토큰 목록과 빈도 전수 비교
    bad_tok = bad_freq = 0
    first: list[str] = []
    for (a_id, a_tok, a_freq), (b_id, b_tok, b_freq) in zip(mine, body):
        if a_tok != b_tok:
            bad_tok += 1
            if len(first) < 5:
                first.append(f"      {a_id}번: 재계산 {a_tok!r} vs C {b_tok!r}")
        elif a_freq != b_freq:
            bad_freq += 1
            if len(first) < 5:
                first.append(f"      {a_tok!r}: 재계산 {a_freq:,} vs C {b_freq:,}")
    if bad_tok:
        errors.append(f"[4][5] 토큰/정렬 불일치 {bad_tok:,}건")
    if bad_freq:
        errors.append(f"[6] 빈도 불일치 {bad_freq:,}건")
    errors.extend(first)

    # [7] 빈도 합 == 토큰 총수
    s = sum(f for _, _, f in body)
    if s != total:
        errors.append(f"[7] 빈도 합 {s:,} != 토큰 총수 {total:,}")

    return errors


# ===================================================================
# [D] 자가 시험 - 검증기가 정말 FAIL 을 내는가
#
# [이유] 통과만 하는 검증기는 검증기가 아니다 (CLAUDE.md 검증 규칙 4번).
#   일부러 망가뜨린 표를 넣어 **반드시 FAIL 이 나오는지** 확인한다.
# ===================================================================
def self_test() -> int:
    mine = [(2, "가/NNG", 10), (3, "나/NNG", 5), (4, "다/NNG", 1)]
    total = 16
    good = [(ID_BOS, "BOS", 0), (ID_EOS, "EOS", 0)] + mine

    cases: list[tuple[str, list, bool]] = [
        ("정상",            good,                                              False),
        ("토큰 하나 다름",   good[:3] + [(3, "틀림/NNG", 5), good[4]],           True),
        ("빈도 하나 다름",   good[:2] + [(2, "가/NNG", 99)] + good[3:],          True),
        ("번호 건너뜀",      good[:3] + [(9, "나/NNG", 5), good[4]],             True),
        ("어휘 하나 빠짐",   good[:4],                                          True),
        ("BOS 자리 틀림",    [(0, "XXX", 0)] + good[1:],                        True),
    ]

    failed = 0
    for name, theirs, must_fail in cases:
        got = bool(compare(1, total, mine, theirs))
        ok = got == must_fail
        print(f"  [{'OK' if ok else 'NG'}] {name}: "
              f"기대 {'FAIL' if must_fail else 'PASS'} / 실제 {'FAIL' if got else 'PASS'}")
        failed += 0 if ok else 1
    print(f"\n자가 시험 {len(cases)}건 중 {len(cases) - failed}건 통과")
    return 1 if failed else 0


# ===================================================================
# [E] main
# ===================================================================
def main(argv: list[str]) -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    if len(argv) > 1 and argv[1] == "--self-test":
        return self_test()

    for p in (TOKENS_PATH, VOCAB_PATH):
        if not p.exists():
            print(f"파일이 없습니다: {p.name}")
            return 1

    sentences, total, mine = recompute(TOKENS_PATH)
    theirs = read_vocab(VOCAB_PATH)

    print("독립 재계산 (step7-2b_tokens.txt 에서 처음부터)")
    print(f"  [1] 문장 {sentences:,} / 토큰 {total:,}")
    print(f"  [2] 어휘 {len(mine):,}종 (BOS/EOS 제외)")
    sp = next((f for _, t, f in mine if t == "<S>"), 0)
    print(f"      그중 <S> {sp:,}회 — <S>도 토큰 하나이므로 어휘에 포함된다")
    print(f"      (Phase 2-b 보고서의 '형태소 어휘 16,917종'은 <S>를 뺀 수다)")

    errors = compare(sentences, total, mine, theirs)
    print()
    if errors:
        print("검증 실패:")
        for e in errors[:20]:
            print("  " + e)
        return 1

    print(f"검증 통과 — 어휘 {len(mine):,}종 전수 일치 (토큰·번호·정렬·빈도)")
    print(f"  [7] 빈도 합 {sum(f for _, _, f in mine):,} == 토큰 총수 {total:,}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
