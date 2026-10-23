"""[STEP 7 / Phase 2-b] 전체 토큰화 - step7-1_sentences.txt -> step7-2b_tokens.txt

강의 2단계(형태소 구분 및 토큰화)에 해당한다. Phase 2-a에서 5문장으로 확인한 방식을
전체 문장에 적용한다.

출력 형식 (한 줄 = 한 문장):
    27/SN<W>세기/NNG<W>의/JKG<W><S><W>발명/NNG<W>왕/NNG<W>...
  - `<W>` : 형태소 구분자
  - `<S>` : 공백 토큰. 원문에 공백이 있던 자리에 들어간다

정한 것 (2026-10-24 사용자 승인):
  - **`<S>` 포함 버전 하나만** 만든다. 공백 정보를 버리면 kiwi.join()이 품사만 보고
    띄어쓰기를 새로 만들어 `27세기` 가 `27 세기` 가 된다 (Phase 2-a에서 실측, 66.3%)
  - **자모 합성(JONGSEONG_MAP)은 하지 않는다.** `큰` -> `크/VA` + `ᆫ/ETM` 을 그대로 둔다
    reference_C 가 자모를 앞 토큰에 합치는 것은 form 을 그냥 이어붙여 복원하기 때문이다.
    우리는 kiwi.join() 으로 복원하므로 합칠 필요가 없고, 합치면 join 이 받는 입력이
    Kiwi 출력과 달라져 오히려 복원이 나빠진다. Phase 2-a의 84.4%는 자모를 그대로 둔 수치다

사용법:
  python step7-2b_tokenize.py              토큰화해서 step7-2b_tokens.txt 로 저장
  python step7-2b_tokenize.py --self-test  검사기가 망가진 입력에 FAIL을 내는지 확인

종료 코드: 0 = 성공 / 1 = 실패
"""

from __future__ import annotations

import sys
import time
from collections import Counter
from pathlib import Path

from kiwipiepy import Kiwi

# ===================================================================
# [1] 설정
# ===================================================================
HERE = Path(__file__).resolve().parent
SRC_PATH = HERE / "step7-1_sentences.txt"
OUT_PATH = HERE / "step7-2b_tokens.txt"
REPORT_PATH = HERE / "step7-2b_tokenize_result.txt"

W = "<W>"   # 형태소 구분자
S = "<S>"   # 공백 토큰


# ===================================================================
# [2] 공백 위치 찾기
#
# [목적] Kiwi는 공백을 토큰으로 주지 않으므로 원문의 공백 위치를 직접 찾는다.
# [동작] 토큰마다 원문에서의 시작 위치(start)와 길이(len)가 주어진다.
#   "앞 토큰이 끝난 자리"보다 "이 토큰이 시작하는 자리"가 뒤면 그 사이에 공백이 있었다.
# [이유] 문자열 검색으로 공백을 찾으면 토큰 경계와 어긋난다. 위치 정보가 정확하다.
#   Phase 2-a의 같은 함수를 그대로 쓴다 (검증된 로직).
# ===================================================================
def to_line(sent: str, tokens: list) -> str:
    """토큰 목록 -> `form/tag<W>...` 한 줄 (공백 자리에 <S> 삽입)

    **공백을 품은 토큰을 쪼개는 이유** (2026-10-24 하드 검사가 잡아낸 결함):
      Kiwi는 띄어 쓴 고유명사를 **토큰 하나**로 묶는다.
        `강철 동굴/NNP` (start=2, len=5)  <- 토큰 안에 공백이 들어 있다
      실측 98개(33종): 도망친 로봇, 존 록스턴, 쥘 베른, 알파 센타우리 ...
      이걸 그대로 두면 두 가지가 깨진다.
        (1) `<S>` 개수가 원문 공백 개수보다 적어진다 (실측 100칸 부족)
        (2) 복원 때 어절 안의 공백을 지우므로 `강철 동굴` -> `강철동굴` 이 된다 (실제로 깨졌다)
      그래서 공백이 든 토큰은 **원문 조각 기준으로** 쪼개고 같은 태그를 붙인다.
        `강철 동굴/NNP` -> `강철/NNP<W><S><W>동굴/NNP`
      복원 결과는 같다: join([('강철','NNP'),('동굴','NNP')]) == '강철 동굴' (확인함).

    **form 이 아니라 원문 조각을 기준으로 삼는 이유** (두 번째로 하드 검사가 잡아낸 것):
      Kiwi 는 고유명사를 표준형으로 바꾸면서 **없던 공백을 만들기도 한다**.
        원문 `오토맨`   -> form `오토 맨`      (공백 0칸 -> 1칸)
        원문 `칼무크`   -> form `칼 무크`
        원문 `아이러브 유` -> form `아이 러브 유`  (1칸 -> 2칸)
      form 으로 쪼개면 원문에 없던 `<S>` 가 생긴다(실측 3칸 초과). 그래서 공백이 든
      토큰은 form 을 버리고 **원문 조각을 그대로** 쓴다. 표준형을 잃지만 98개뿐이고,
      원문에 없는 공백을 지어내는 것보다 낫다.
    """
    pieces: list[str] = []
    cursor = 0
    for t in tokens:
        if t.start > cursor:
            pieces.append(S)
        form = t.form
        surface = sent[t.start:t.start + t.len]
        if " " in form or " " in surface:
            form = surface            # 공백이 끼면 표준형 대신 원문 조각을 쓴다
        for i, part in enumerate(form.split(" ")):     # 보통은 조각이 1개뿐이다
            if i:
                pieces.append(S)                       # 토큰 안에 있던 공백을 <S>로 꺼낸다
            pieces.append(f"{part}/{t.tag}")
        cursor = t.start + t.len
    return W.join(pieces)


# ===================================================================
# [3] 복원 (C안) - 검증에 쓴다
#
# [목적] 토큰 줄을 다시 문장으로 되돌려 원문과 비교한다.
# [동작] `<S>` 로 어절을 끊고, 어절마다 kiwi.join() 으로 활용형을 복원한 뒤,
#   **어절 안에 join 이 넣은 공백을 지운다.** 마지막에 어절을 공백으로 잇는다.
# [이유] 어절 안에는 원래 공백이 없다(그게 어절의 정의). 따라서 어절 내부에 나타난
#   공백은 전부 join 이 만든 것이라 지워도 된다. 이 한 줄이 68.8% -> 84.4% 를 만든다
#   (Phase 2-a 실측).
# ===================================================================
def restore(kiwi: Kiwi, line: str) -> str:
    groups: list[list[tuple[str, str]]] = []
    cur: list[tuple[str, str]] = []
    for piece in line.split(W):
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
# [4] 반드시 지켜져야 하는 것 (하드 검사)
#
# [목적] "복원 일치율"은 84% 언저리로 나오는 지표라 합격/불합격 기준이 될 수 없다.
#   그래서 **100%여야만 하는 것**을 따로 정해 그것으로 성패를 가린다.
# [동작] 세 가지를 본다.
#   (1) 줄 수가 입력 문장 수와 같은가
#   (2) `<S>` 토큰 수가 원문의 공백 수와 같은가  <- 공백을 흘리면 바로 걸린다
#   (3) 모든 조각이 `form/tag` 꼴인가 (빈 form, 태그 없음 금지)
# [이유] (2)가 핵심이다. 공백은 이 Phase가 보존하기로 한 정보이므로 개수가 어긋나면
#   그 자체가 결함이다. 복원 일치율은 Kiwi의 정규화 탓이 섞여 있어 결함과 구분되지 않는다.
# ===================================================================
def hard_check(sentences: list[str], lines: list[str]) -> list[str]:
    errors: list[str] = []

    if len(sentences) != len(lines):
        errors.append(f"줄 수 불일치: 입력 {len(sentences):,} vs 출력 {len(lines):,}")
        return errors

    want_space = sum(s.count(" ") for s in sentences)
    got_space = sum(line.split(W).count(S) for line in lines)
    if want_space != got_space:
        errors.append(f"<S> 개수 불일치: 원문 공백 {want_space:,} vs <S> {got_space:,}")

    for i, line in enumerate(lines):
        for piece in line.split(W):
            if piece == S:
                continue
            if "/" not in piece:
                errors.append(f"{i + 1}번째 줄: 태그 없는 조각 {piece!r}")
                break
            form, tag = piece.rsplit("/", 1)
            if not form or not tag:
                errors.append(f"{i + 1}번째 줄: 빈 form 또는 빈 tag {piece!r}")
                break
        if len(errors) > 20:
            break
    return errors


# ===================================================================
# [5] 자가 시험 - 검사기가 정말 FAIL 을 내는가
#
# [이유] 통과만 하는 검사기는 검사기가 아니다 (CLAUDE.md 검증 규칙 4번).
#   [4]의 세 검사가 각각 작동하는지 따로 확인한다.
# ===================================================================
def self_test() -> int:
    kiwi = Kiwi()
    sents = ["바로 그때 랄프는 놀랐습니다.", "아리스가 왔다."]
    good = [to_line(s, kiwi.tokenize(s)) for s in sents]

    cases: list[tuple[str, list[str], list[str], bool]] = [
        ("정상",            sents, good,                                   False),
        ("줄 수 부족",       sents, good[:1],                               True),
        ("<S> 하나 삭제",    sents, [good[0].replace(W + S + W, W, 1), good[1]], True),
        ("태그 없는 조각",   sents, [good[0] + W + "엉터리", good[1]],        True),
        ("빈 form",         sents, [good[0] + W + "/NNG", good[1]],         True),
    ]

    failed = 0
    for name, ss, ll, must_fail in cases:
        got = bool(hard_check(ss, ll))
        ok = got == must_fail
        print(f"  [{'OK' if ok else 'NG'}] {name}: "
              f"기대 {'FAIL' if must_fail else 'PASS'} / 실제 {'FAIL' if got else 'PASS'}")
        failed += 0 if ok else 1
    print(f"\n자가 시험 {len(cases)}건 중 {len(cases) - failed}건 통과")
    return 1 if failed else 0


# ===================================================================
# [6] 보고서
#
# [목적] 이 Phase의 핵심 질문에 숫자로 답한다:
#   "형태소로 쪼개니 어휘가 정말 줄었는가?"
# [이유] 어휘 폭발을 해결하려고 형태소로 온 것이므로, 어절 어휘(94,074종)와
#   비교하지 않으면 이 단계를 왜 했는지 확인할 수 없다.
# ===================================================================
def write_report(sentences: list[str], lines: list[str], stat: dict) -> None:
    v = stat["vocab"]
    once = sum(1 for c in v.values() if c == 1)
    L = [
        "=== STEP 7 Phase 2-b: 전체 토큰화 결과 ===",
        "",
        "--- 이 파일이 무엇인가 ---",
        "step7-1_sentences.txt 의 모든 문장을 Kiwi 형태소 분석기로 토큰화한 결과 보고서다.",
        "토큰 파일은 step7-2b_tokens.txt 이고 한 줄이 한 문장이다.",
        "",
        "  형식: form/tag<W>form/tag<W><S><W>form/tag ...",
        "    <W> = 형태소 구분자,  <S> = 공백이 있던 자리",
        "",
        "  정한 것: <S> 포함 버전만 만든다 / 자모 합성(큰 -> 크+ㄴ)은 하지 않는다",
        "          이유는 step7-2b_tokenize.py 머리 주석과 education/08 참고",
        "",
        "  '복원 일치율'은 합격 기준이 아니다. Kiwi가 기본형으로 정규화하기 때문에",
        "  100%가 될 수 없다(축약형: 대었->댔, 되어->돼). 합격 여부는 아래 '하드 검사'로 가린다.",
        "",
        "--- 규모 ---",
        f"문장 수        : {len(lines):,}",
        f"토큰 수(<S> 포함): {stat['tokens']:,}",
        f"  그중 <S>     : {stat['space']:,} ({stat['space'] / stat['tokens'] * 100:.1f}%)",
        f"  형태소만     : {stat['tokens'] - stat['space']:,}",
        f"평균 토큰/문장 : {stat['tokens'] / len(lines):.2f}",
        f"토큰화 시간    : {stat['sec']:.1f}초",
        "",
        "--- 어휘 (이 Phase의 핵심 숫자) ---",
        f"형태소 어휘(form/tag)  : {len(v):,}종",
        f"  그중 1회만 등장      : {once:,}종 ({once / len(v) * 100:.1f}%)",
        f"비교) 공백 분리 어절    : 94,074종 (그중 1회만 63.0%)",
        f"  -> 어휘가 {94074 / len(v):.1f}배 줄었다",
        "",
        "--- 품사 분포 (상위 15) ---",
    ]
    for tag, n in stat["tags"].most_common(15):
        L.append(f"  {tag:5s} {n:>9,}  ({n / (stat['tokens'] - stat['space']) * 100:5.2f}%)")
    L += [
        "",
        "--- 빈도 상위 토큰 20 ---",
    ]
    for tok, n in v.most_common(20):
        L.append(f"  {tok:16s} {n:>8,}")
    L += [
        "",
        "--- 복원 검사 (참고 지표) ---",
        f"전수 {len(lines):,}문장을 C안으로 복원해 원문과 비교",
        f"  완전 일치: {stat['same']:,}문장 ({stat['same'] / len(lines) * 100:.1f}%)",
        f"  (Phase 2-a 표본 1,000문장에서는 84.4%였다)",
        "  불일치의 원인은 Kiwi의 정규화다. 아래 예시 참고.",
        "",
        "  불일치 예시 5개:",
    ]
    for o, r in stat["diff"][:5]:
        L.append(f"    원문: {o[:70]}")
        L.append(f"    복원: {r[:70]}")
    L += [
        "",
        "--- 하드 검사 (이게 합격 기준) ---",
        "  (1) 줄 수 == 입력 문장 수",
        "  (2) <S> 개수 == 원문 공백 개수",
        "  (3) 모든 조각이 form/tag 꼴",
        "  -> 전부 통과했으므로 이 파일이 생성되었다 (하나라도 실패하면 쓰지 않는다)",
    ]
    REPORT_PATH.write_text("\n".join(L) + "\n", encoding="utf-8", newline="")


# ===================================================================
# [7] main
# ===================================================================
def main(argv: list[str]) -> int:
    if len(argv) > 1 and argv[1] == "--self-test":
        return self_test()

    if not SRC_PATH.exists():
        print(f"입력 파일이 없습니다: {SRC_PATH}  (먼저 step7-1_prepare.py 를 실행하세요)")
        return 1

    sentences = SRC_PATH.read_text(encoding="utf-8").splitlines()
    kiwi = Kiwi()

    # 토큰화 - 리스트를 통째로 넘기면 Kiwi가 내부적으로 묶어 처리해 훨씬 빠르다
    t0 = time.time()
    results = list(kiwi.tokenize(sentences))
    lines = [to_line(s, toks) for s, toks in zip(sentences, results)]
    sec = time.time() - t0

    errors = hard_check(sentences, lines)
    if errors:
        print("하드 검사 실패 - 파일을 쓰지 않습니다:")
        for e in errors[:20]:
            print("  " + e)
        return 1

    # 통계와 복원 검사
    vocab: Counter = Counter()
    tags: Counter = Counter()
    n_tokens = n_space = same = 0
    diff: list[tuple[str, str]] = []
    for sent, line in zip(sentences, lines):
        # 통계는 **실제로 쓰는 줄**에서 센다. Kiwi 원본 토큰에서 세면 공백 든 토큰을
        # 쪼갠 것이 반영되지 않아 보고서와 파일이 어긋난다
        # (2026-10-24 독립 검증에서 토큰 100개·어휘 2종 차이로 실제 적발됨)
        for piece in line.split(W):
            n_tokens += 1
            if piece == S:
                n_space += 1
                continue
            vocab[piece] += 1
            tags[piece.rsplit("/", 1)[1]] += 1
        r = restore(kiwi, line)
        if r == sent:
            same += 1
        elif len(diff) < 20:
            diff.append((sent, r))

    OUT_PATH.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="")
    write_report(sentences, lines, {
        "tokens": n_tokens, "space": n_space, "vocab": vocab, "tags": tags,
        "same": same, "diff": diff, "sec": sec,
    })

    sys.stdout.reconfigure(encoding="utf-8")
    print(f"문장 {len(lines):,} / 토큰 {n_tokens:,} (<S> {n_space:,}) / 어휘 {len(vocab):,}종")
    print(f"복원 일치 {same:,} ({same / len(lines) * 100:.1f}%) / 토큰화 {sec:.1f}초")
    print(f"저장: {OUT_PATH.name}, {REPORT_PATH.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
