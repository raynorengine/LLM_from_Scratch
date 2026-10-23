"""[STEP 7 / 4단계] 독립 검증 - 역변환이 정보를 잃지 않았는가

검증의 어려움:
  역변환은 kiwi.join() 에 의존한다. 같은 일을 하는 다른 구현이 없으므로
  CLAUDE.md 가 요구하는 "다른 구현으로 교차검증" 이 **불가능하다.**
  그래서 대신 **왕복(round-trip)** 으로 잰다.

    토큰 열  ->  [역변환]  ->  문장  ->  [다시 토큰화]  ->  토큰 열
       +------------------- 같은가? -------------------+

  복원한 문장을 다시 Kiwi 에 넣어 원래 토큰 열이 나오면, 복원이 정보를 잃지 않은 것이다.
  이건 kiwi.join() 과 kiwi.tokenize() 가 **서로 다른 함수**이므로 성립하는 검사다.

검사 항목:
  [1] 문장 수와 번호가 입력과 일치하는가
  [2] 빈 문장이 없는가
  [3] 왕복 일치율 - 복원 -> 재토큰화 결과가 원래 토큰 열과 같은가
  [4] 어절 수가 <S> 개수 + 1 과 같은가 (띄어쓰기가 보존됐는가)
  [5] 복원 문장에 표기 기호(<W>, <S>)가 남아 있지 않은가

사용법:
  python step7-4_verify.py [번호]        생략하면 번호가 가장 큰 결과
  python step7-4_verify.py --self-test

종료 코드: 0 = 성공 / 1 = 실패
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

from kiwipiepy import Kiwi

HERE = Path(__file__).resolve().parent
SRC_FMT = "step7-3c_6gram_result_{}.txt"
OUT_FMT = "step7-4_detokenize_result_{}.txt"

W = "<W>"
S = "<S>"

# [3] 왕복 일치율의 합격선.
# 100%를 요구할 수 없다. Kiwi 는 기본형으로 정규화하므로(대었 -> 댔) 복원과 재토큰화가
# 완전히 같아질 수 없고, 생성 문장은 원본에 없던 조합이라 더 어긋나기 쉽다.
# 원본 문장 왕복이 85.9%(Phase 2-b 실측)였으므로, 그보다 크게 낮으면 의심한다.
ROUNDTRIP_MIN = 50.0


def read_tokens(path: Path) -> list[tuple[int, list[str]]]:
    out = []
    with path.open(encoding="utf-8") as f:
        for line in f:
            m = re.match(r"^(\d+): (.*)$", line.rstrip("\n").rstrip("\r"))
            if m:
                out.append((int(m.group(1)), [p for p in m.group(2).split(W) if p]))
    return out


def read_sents(path: Path) -> list[tuple[int, str]]:
    out = []
    with path.open(encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n").rstrip("\r")
            if line.startswith("#") or line.startswith("==="):
                continue
            m = re.match(r"^(\d+): (.*)$", line)
            if m:
                out.append((int(m.group(1)), m.group(2)))
    return out


# ===================================================================
# [A] 다시 토큰화 - 2-b 와 같은 규칙으로
#
# [이유] 2-b 가 쓴 규칙(공백 자리에 <S>, 공백 든 토큰은 원문 조각으로 쪼갬)을
#   그대로 적용해야 같은 조건에서 비교된다. 규칙이 다르면 복원 잘못과
#   토큰화 규칙 차이를 구분할 수 없다.
# ===================================================================
def retokenize(kiwi: Kiwi, sent: str) -> list[str]:
    pieces: list[str] = []
    cursor = 0
    for t in kiwi.tokenize(sent):
        if t.start > cursor:
            pieces.append(S)
        form = t.form
        surface = sent[t.start:t.start + t.len]
        if " " in form or " " in surface:
            form = surface
        for i, part in enumerate(form.split(" ")):
            if i:
                pieces.append(S)
            pieces.append(f"{part}/{t.tag}")
        cursor = t.start + t.len
    return pieces


# ===================================================================
# [B] 검사
# ===================================================================
def check(kiwi: Kiwi, src: list[tuple[int, list[str]]],
          out: list[tuple[int, str]]) -> tuple[list[str], float, list]:
    errors: list[str] = []

    # [1] 문장 수와 번호
    if len(src) != len(out):
        errors.append(f"[1] 문장 수 불일치: 입력 {len(src)} vs 출력 {len(out)}")
        return errors, 0.0, []
    for (a, _), (b, _) in zip(src, out):
        if a != b:
            errors.append(f"[1] 번호 불일치: {a} vs {b}")
            break

    same = 0
    diffs: list[tuple[int, str, str]] = []
    for (no, toks), (_, sent) in zip(src, out):
        # [2] 빈 문장
        if not sent.strip():
            errors.append(f"[2] {no}번 문장이 비었습니다")
            continue

        # [5] 표기 기호가 남아 있으면 안 된다
        if W in sent or S in sent:
            errors.append(f"[5] {no}번 문장에 표기 기호가 남아 있습니다: {sent[:40]!r}")

        # [4] 어절 수 = <S> 개수 + 1
        want_words = toks.count(S) + 1
        got_words = len(sent.split(" "))
        if got_words != want_words:
            errors.append(f"[4] {no}번 어절 수 {got_words} != <S> {toks.count(S)} + 1")

        # [3] 왕복
        again = retokenize(kiwi, sent)
        if again == toks:
            same += 1
        elif len(diffs) < 5:
            diffs.append((no, W.join(toks), W.join(again)))

    rate = 100.0 * same / len(src) if src else 0.0
    if rate < ROUNDTRIP_MIN:
        errors.append(f"[3] 왕복 일치율 {rate:.1f}% < 합격선 {ROUNDTRIP_MIN}%")
    return errors, rate, diffs


# ===================================================================
# [C] 자가 시험
# ===================================================================
def self_test() -> int:
    kiwi = Kiwi()
    # 장난감 입력으로는 [3] 왕복 합격선을 통과할 수 없다. 실제 문장을 토큰화해서
    # 원래 토큰 열을 만들고, 그 문장을 그대로 복원 결과로 준다 (왕복이 성립해야 정상).
    sent = "곧 일행은 옆방으로 들어갔다."
    src = [(1, retokenize(kiwi, sent))]
    good = [(1, sent)]
    n_sp = src[0][1].count(S)

    cases: list[tuple[str, list, list, bool]] = [
        ("정상",              src, good,                                   False),
        ("문장 수 다름",       src, good + [(2, "다른 문장이다.")],          True),
        ("번호 다름",          src, [(9, sent)],                           True),
        ("빈 문장",            src, [(1, "   ")],                          True),
        ("어절 수 다름",       src, [(1, sent.replace(" ", "", 1))],        True),
        ("표기 기호 남음",     src, [(1, sent.replace(" ", " <S> ", 1))],   True),
        ("왕복 실패",          src, [(1, " ".join(["엉뚱한"] * (n_sp + 1)))], True),
    ]
    failed = 0
    for name, s, o, must_fail in cases:
        errs, _, _ = check(kiwi, s, o)
        got = bool(errs)
        ok = got == must_fail
        print(f"  [{'OK' if ok else 'NG'}] {name}: "
              f"기대 {'FAIL' if must_fail else 'PASS'} / 실제 {'FAIL' if got else 'PASS'}")
        failed += 0 if ok else 1
    print(f"\n자가 시험 {len(cases)}건 중 {len(cases) - failed}건 통과")
    return 1 if failed else 0


# ===================================================================
# [D] main
# ===================================================================
def main(argv: list[str]) -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    if len(argv) > 1 and argv[1] == "--self-test":
        return self_test()

    if len(argv) > 1:
        num = argv[1].zfill(2)
    else:
        found = sorted(HERE.glob("step7-4_detokenize_result_[0-9][0-9].txt"))
        if not found:
            print("4단계 결과가 없습니다. 먼저 step7-4_detokenize.py 를 실행하세요.")
            return 1
        num = found[-1].stem[-2:]

    src_path = HERE / SRC_FMT.format(num)
    out_path = HERE / OUT_FMT.format(num)
    for p in (src_path, out_path):
        if not p.exists():
            print(f"파일이 없습니다: {p.name}")
            return 1

    src = read_tokens(src_path)
    out = read_sents(out_path)
    kiwi = Kiwi()

    print(f"검증 대상: {out_path.name} ({len(out)}문장)")
    errors, rate, diffs = check(kiwi, src, out)
    print(f"  [3] 왕복 일치율: {rate:.1f}%  (원본 문장 왕복은 85.9%)")
    if diffs:
        print("      왕복이 어긋난 예:")
        for no, a, b in diffs[:2]:
            print(f"        {no}번 원래 : {a[:70]}")
            print(f"        {no}번 재토큰: {b[:70]}")
    print()

    if errors:
        print("검증 실패:")
        for e in errors[:20]:
            print("  " + e)
        return 1

    print("검증 통과")
    print("  [1] 문장 수·번호 / [2] 빈 문장 없음 / [4] 어절 수 = <S>+1 / [5] 표기 기호 없음")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
