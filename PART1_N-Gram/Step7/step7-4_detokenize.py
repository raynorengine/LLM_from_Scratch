"""[STEP 7 / 4단계] 역변환 - 토큰 열을 읽을 수 있는 문장으로

3-c 가 만든 토큰 열을 사람이 읽는 문장으로 바꾼다.

    침대/NNG<W><S><W>위/NNG<W>의/JKG<W><S><W>신음/NNG<W>은/JX ...
            ↓
    침대 위의 신음은 ...

복원 방식은 Phase 2-a 에서 1,000문장으로 비교해 고른 것을 그대로 쓴다.
  (1) <W>만 kiwi.join() 에 전부 맡김            66.3%
  (2) <S>로 어절 띄움 + 어절마다 join           68.8%
  (3) (2) + 어절 안의 공백 제거  <- 이것을 쓴다  84.4%

역할 분담:
  <S>          : 어절 경계 = 띄어쓰기
  kiwi.join()  : 어절 안의 활용형 복원 (넘어지 + 어 -> 넘어져)
  공백 제거     : join 이 어절 안에 넣은 공백 삭제

왜 어절 안의 공백을 지워도 되나:
  한 어절 안에는 원래 공백이 없다(그게 어절의 정의). 따라서 어절 내부에 나타난
  공백은 전부 join 이 만든 것이다. join 은 원문의 공백을 모르고 품사만 보고
  띄어쓰기 규칙을 새로 적용하기 때문이다.
    join([27/SN, 세기/NNG]) -> '27 세기'   (숫자 + 일반명사는 띄움)
    join([2/SN,  미터/NNB]) -> '2미터'     (숫자 + 의존명사는 붙임)

사용법:
  python step7-4_detokenize.py [결과파일] [출력파일]
    결과파일을 생략하면 번호가 가장 큰 3-c 결과.
    출력파일을 주면 그 경로에 쓴다 (6단계 결과를 복원할 때 쓴다. 토큰 열 형식이
    3-c 와 같으므로 이 프로그램을 고치지 않고 그대로 쓸 수 있다)
  python step7-4_detokenize.py --self-test

출력 파일은 입력의 번호를 따라간다 (..._result_01.txt -> ..._result_01.txt).
kiwi.join() 은 결정적이라 같은 입력이면 항상 같은 출력이므로 번호를 새로 매기지 않는다.

종료 코드: 0 = 성공 / 1 = 실패
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

from kiwipiepy import Kiwi

HERE = Path(__file__).resolve().parent
IN_GLOB = "step7-3c_6gram_result_[0-9][0-9].txt"
OUT_FMT = "step7-4_detokenize_result_{}.txt"

W = "<W>"
S = "<S>"


# ===================================================================
# [1] 입력 읽기
#
# [목적] 3-c 결과 파일에서 헤더와 토큰 열을 분리한다.
# [이유] 헤더(seed 등)를 출력에 그대로 옮겨 적는다. 어느 생성 결과를 복원한
#   것인지 추적할 수 있어야 하기 때문이다.
# ===================================================================
def read_result(path: Path) -> tuple[dict[str, str], list[tuple[int, list[str]]]]:
    head: dict[str, str] = {}
    sents: list[tuple[int, list[str]]] = []
    with path.open(encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n").rstrip("\r")
            if not line:
                continue
            if line.startswith("#"):
                parts = line[1:].strip().split("\t")
                if len(parts) >= 2:
                    head.setdefault(parts[0].strip(), parts[1].strip())
                continue
            m = re.match(r"^(\d+): (.*)$", line)
            if m:
                sents.append((int(m.group(1)), [p for p in m.group(2).split(W) if p]))
    return head, sents


# ===================================================================
# [2] 역변환 (Phase 2-a 의 C안)
#
# [동작] <S> 를 경계로 어절을 끊고, 어절마다 kiwi.join() 으로 활용형을 복원한 뒤
#   그 안의 공백을 지운다. 마지막에 어절들을 공백 하나로 잇는다.
# [이유] 토큰의 form 은 기본형이라(`넘어져` -> `넘어지`) 그냥 이어붙이면 글자가 틀린다.
#   활용형 복원은 kiwi.join() 만 할 수 있다. 반면 띄어쓰기는 join 이 모르므로
#   우리가 <S> 로 들고 있던 정보를 쓴다. 둘의 역할이 다르다.
# ===================================================================
def detokenize(kiwi: Kiwi, tokens: list[str]) -> str:
    groups: list[list[tuple[str, str]]] = []
    cur: list[tuple[str, str]] = []
    for piece in tokens:
        if piece == S:
            if cur:
                groups.append(cur)
                cur = []
            continue
        form, tag = piece.rsplit("/", 1)   # form 안에 '/'가 있을 수 있어 뒤에서 자른다
        cur.append((form, tag))
    if cur:
        groups.append(cur)
    return " ".join(kiwi.join(g).replace(" ", "") for g in groups)


# ===================================================================
# [3] 자가 시험
#
# [이유] 역변환은 Kiwi 에 의존해 "정답"을 따로 만들 수 없다. 대신 **구조가
#   지켜지는지**를 본다: <S> 가 어절을 끊는가, 어절 안에 공백이 남지 않는가.
#   이건 Kiwi 의 출력과 무관하게 항상 참이어야 한다.
# ===================================================================
def self_test() -> int:
    kiwi = Kiwi()
    cases = [
        # (토큰 열, 기대하는 성질)
        (["가/NNG", S, "나/NNG"], "어절 2개"),
        (["27/SN", "세기/NNG", "의/JKG"], "어절 1개 (안쪽 공백 없어야 함)"),
        ([S, "가/NNG"], "앞의 <S> 는 빈 어절을 만들지 않는다"),
    ]
    failed = 0
    for toks, desc in cases:
        out = detokenize(kiwi, toks)
        n_sp = toks.count(S)
        want_words = len([t for t in toks if t != S]) and (n_sp if n_sp else 0) + 1
        got_words = len(out.split(" "))
        ok = out and got_words <= want_words and not out.startswith(" ")
        print(f"  [{'OK' if ok else 'NG'}] {desc}: {out!r}")
        failed += 0 if ok else 1

    # 어절 안에 공백이 남으면 안 된다
    out = detokenize(kiwi, ["27/SN", "세기/NNG", "의/JKG"])
    ok = " " not in out
    print(f"  [{'OK' if ok else 'NG'}] 어절 안 공백 제거: {out!r} (공백 없어야 함)")
    failed += 0 if ok else 1

    # <S> 개수 + 1 = 어절 수
    toks = ["가/NNG", S, "나/NNG", S, "다/NNG"]
    out = detokenize(kiwi, toks)
    ok = len(out.split(" ")) == toks.count(S) + 1
    print(f"  [{'OK' if ok else 'NG'}] <S> 개수와 어절 수: {out!r}")
    failed += 0 if ok else 1

    print(f"\n자가 시험 {len(cases) + 2}건 중 {len(cases) + 2 - failed}건 통과")
    return 1 if failed else 0


# ===================================================================
# [4] main
# ===================================================================
def main(argv: list[str]) -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    if len(argv) > 1 and argv[1] == "--self-test":
        return self_test()

    if len(argv) > 1:
        in_path = Path(argv[1])
    else:
        found = sorted(HERE.glob(IN_GLOB))
        if not found:
            print("3-c 결과 파일이 없습니다. 먼저 step7-3c_6gram.exe 를 실행하세요.")
            return 1
        in_path = found[-1]
    if not in_path.exists():
        print(f"파일이 없습니다: {in_path}")
        return 1

    if len(argv) > 2:
        out_path = Path(argv[2])
    else:
        num = in_path.stem[-2:]
        out_path = HERE / OUT_FMT.format(num)

    head, sents = read_result(in_path)
    if not sents:
        print(f"복원할 문장이 없습니다: {in_path.name}")
        return 1

    kiwi = Kiwi()
    lines = [
        "=== STEP 7 4단계: 역변환 결과 (읽을 수 있는 문장) ===",
        "#",
        f"# 입력\t{in_path.name}",
        f"# seed\t{head.get('seed', '?')}",
        f"# count\t{len(sents)}",
        f"# n\t{head.get('n', '?')}",
        "#",
        "# 복원 방식: <S> 로 어절을 끊고, 어절마다 kiwi.join() 으로 활용형을 복원한 뒤",
        "#            어절 안의 공백을 지운다 (Phase 2-a 에서 고른 방식, 원본 왕복 85.9%).",
        "#",
        "# 원본에 있는 문장인지 여부는 여기서 보지 않는다. 그것은 5단계다.",
        "#",
    ]
    for no, toks in sents:
        lines.append(f"{no}: {detokenize(kiwi, toks)}")

    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="")

    print(f"입력: {in_path.name} (seed {head.get('seed', '?')})")
    print(f"문장 {len(sents)}개 복원")
    print(f"저장: {out_path.name}")
    print()
    for no, toks in sents[:5]:
        print(f"  {no}: {detokenize(kiwi, toks)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
