"""[STEP 6] 문장 분할 전처리 - novel_test_data.txt -> data/novel_sentences.txt

원본 소설 데이터는 한 줄에 여러 문장이 뭉쳐 있다 (평균 54.5글자, 최대 4,104글자).
문장 생성 실습을 하려면 "한 줄 = 한 문장"이어야 하므로 종결부호로 쪼갠다.

이 분할을 C 안에 넣지 않고 전처리로 분리한 이유:
  C와 step6_verify.py가 각자 분할 규칙을 구현하면 같은 분할 버그를 함께 가지게 되어
  교차검증이 성립하지 않는다 (CLAUDE.md 검증 규칙 2번 "다른 구현으로 교차검증").
  분할을 한 번만 하고 양쪽이 같은 파일을 읽으면 그 문제가 사라진다.

사용법:
  python step6_prepare.py                 분할해서 ../../data/novel_sentences.txt 로 저장
  python step6_prepare.py --self-test     분할기가 망가진 입력에 FAIL을 내는지 확인

종료 코드: 0 = 성공 / 1 = 실패
"""

from __future__ import annotations

import sys
import unicodedata
from collections import Counter
from pathlib import Path

# ===================================================================
# [1] 설정
#
# [목적] 입출력 경로와 분할에 쓰는 글자 집합을 한곳에 모은다.
# [동작] 경로는 Step 폴더 기준 상대경로 (../../data/). 파트 폴더 -> 저장소 루트로
#   두 단계 올라간다 (CLAUDE.md "데이터 경로는 Step 폴더 기준 ../../data/").
# [이유] 스크립트를 어느 위치에서 실행해도 같은 파일을 보게 하려고 __file__ 기준으로
#   절대경로를 만든다. 현재 작업 디렉토리에 의존하면 Step 폴더 밖에서 실행했을 때
#   조용히 다른 파일을 읽거나 못 찾는다.
# ===================================================================
HERE = Path(__file__).resolve().parent
SRC_PATH = HERE / ".." / ".." / "data" / "korean_dictionary" / "novel_test_data.txt"
OUT_PATH = HERE / ".." / ".." / "data" / "novel_sentences.txt"

# 문장 종결부호. U+2026(말줄임표)도 넣는다: 이 데이터에 1,733개 있고 문장 끝에 쓰인다
TERMINATORS = ".!?…"

# 문장 경계에서 버려도 되는 공백. ASCII 스페이스와 탭만이다.
# NBSP(U+00A0)를 넣지 않는 까닭: 이 데이터는 NBSP를 문장 안의 단어 구분 공백으로 쓴다
# (실측 268개. 예: "베일리는 머릿속에서 우주인"). 글자 번호를 받는 실제 글자이므로
# 버리면 그냥 손실이다. Python의 str.isspace()는 NBSP도 공백으로 보기 때문에
# isspace()를 그대로 쓰면 NBSP가 조용히 버려진다 (실제로 31개가 그렇게 사라졌다).
SKIPPABLE_WS = " 	"

# 닫는 따옴표/괄호. 종결부호 "뒤"에 붙어 있으면 앞 문장의 일부로 본다.
# 순서대로: 아스키 큰따옴표, 아스키 작은따옴표, U+201D, U+2019, 닫는 소괄호,
#           닫는 대괄호, U+300D(모서리괄호), U+FF63(반각 모서리괄호)
CLOSERS = "\"'”’)]」｣"


def has_letter(text: str) -> bool:
    """한글 음절/영문/숫자가 하나라도 있는가.

    구두점만 남은 조각을 걸러내는 데 쓴다.
    """
    for ch in text:
        if "가" <= ch <= "힣":
            return True
        if ch.isascii() and ch.isalnum():
            return True
    return False


# ===================================================================
# [2] 한 줄을 문장들로 쪼개기
#
# [목적] 종결부호를 기준으로 한 줄을 문장 여러 개로 나눈다.
# [동작] 왼쪽부터 한 글자씩 보며 종결부호를 만나면 거기서 끊는다.
#   1) 종결부호가 연속하면 한 덩어리로 삼는다. "뭐야?!" -> ?! 를 함께 소비
#   2) 숫자.숫자 는 소수점이므로 끊지 않는다. "2.5미터" -> 통째로 유지
#   3) 종결부호 뒤의 닫는 따옴표/괄호는 앞 문장에 붙인다 (글자 소실 방지)
#   4) 그 뒤의 공백은 문장 사이 구분이므로 버린다
#   5) 글자(한글/영문/숫자)가 없는 조각은 버리지 않고 앞 문장에 붙인다
# [이유] 1)~5)를 안 하면 실측에서 글자가 사라졌다.
#   - 3)을 안 했을 때: 닫는 따옴표가 구분자로 먹혀 12,942자가 결과에 안 담겼다
#     (내역: 아스키 큰따옴표 11,741 / U+201D 671 / 작은따옴표 379 / 닫는 괄호 135 / U+2019 16)
#   - 5)를 안 했을 때: 1,972글자 소실. 이 데이터는 U+201C(여는 큰따옴표)를 닫는
#     따옴표로도 써서 (U+201C 2,310개 vs U+201D 680개로 비대칭) 마침표+U+201C 로
#     끝나는 줄이 13,419건 있고, 거기서 외톨이 U+201C 조각 1,553개가 생긴다
#   조각을 "버리기"보다 "앞에 붙이기"로 정한 까닭: 버리면 글자 수가 원본과 달라져
#   무엇이 사라졌는지 추적해야 하는데, 붙이면 손실이 0이 되어 [4]의 전수 검사가
#   그냥 통과한다. 통계상으로도 닫는 따옴표는 앞 문장에 속하는 것이 맞다.
# ===================================================================
def split_line(line: str) -> list[str]:
    """한 줄을 문장 목록으로 나눈다. 공백을 뺀 글자는 하나도 잃지 않는다."""
    pieces: list[str] = []
    i = 0
    start = 0
    n = len(line)

    while i < n:
        if line[i] not in TERMINATORS:
            i += 1
            continue

        j = i
        while j < n and line[j] in TERMINATORS:   # (1) 연속 종결부호는 한 덩어리
            j += 1

        # (2) 소수점: 종결부호가 마침표 하나뿐이고 양옆이 숫자면 문장 끝이 아니다
        if (j - i == 1 and line[i] == "." and i > 0 and line[i - 1].isdigit()
                and j < n and line[j].isdigit()):
            i = j
            continue

        while j < n and line[j] in CLOSERS:       # (3) 닫는 따옴표는 앞 문장에
            j += 1

        pieces.append(line[start:j])

        k = j
        while k < n and line[k] in SKIPPABLE_WS:  # (4) 문장 사이 공백만 버린다
            k += 1                                #     NBSP는 버리지 않는다 ([1] SKIPPABLE_WS)
        i = start = k

    if start < n:
        pieces.append(line[start:])

    # (5) 글자 없는 조각은 앞 문장에 붙인다
    merged: list[str] = []
    for piece in pieces:
        if not has_letter(piece):
            if merged:
                merged[-1] += piece
            else:
                merged.append(piece)      # 줄 맨 앞이면 뒤 조각과 합쳐질 때까지 보류
        elif merged and not has_letter(merged[-1]):
            merged[-1] += piece           # 보류해 둔 조각에 이어 붙인다
        else:
            merged.append(piece)

    #  strip() 을 쓰면 양 끝의 NBSP까지 떼내므로 버려도 되는 공백만 지정해 떼낸다
    return [m.strip(SKIPPABLE_WS) for m in merged if m.strip(SKIPPABLE_WS)]


# ===================================================================
# [3] 전체 분할
#
# [목적] 원본 파일을 읽어 문장 목록을 만든다.
# [동작] BOM을 떼고(utf-8-sig) 줄 단위로 끊어 빈 줄을 버린 뒤 [2]를 적용한다.
# [이유] 원본은 CRLF이고 BOM은 없지만, 다른 PC에서 받은 파일이 BOM을 가질 수 있어
#   utf-8-sig로 읽는다 (BOM이 없으면 utf-8과 동일하게 동작한다).
#   splitlines()를 쓰는 까닭: CRLF와 LF를 모두 줄바꿈으로 처리해 준다.
# ===================================================================
def split_corpus(text: str) -> tuple[list[str], list[str]]:
    """원본 전체 -> (문장 목록, 원본의 비어 있지 않은 줄 목록)."""
    #  빈 줄 판정도 SKIPPABLE_WS 기준으로 한다 (NBSP만 있는 줄은 실측 0개이지만 기준을 맞춘다)
    lines = [line for line in text.splitlines() if line.strip(SKIPPABLE_WS)]
    sentences: list[str] = []
    for line in lines:
        sentences.extend(split_line(line))
    return sentences, lines


# ===================================================================
# [4] 글자 손실 전수 검사
#
# [목적] 분할이 글자를 잃거나 더하지 않았는지 확인한다.
# [동작] 원본과 분할 결과의 글자를 **공백까지 포함해** 종류별로 전수 집계해 비교한다.
#   글자를 두 부류로 나눠 서로 다른 기준을 적용한다.
#     - SKIPPABLE_WS(ASCII 스페이스·탭): [2]의 (4)가 문장 경계에서 버리는 대상이다.
#       **줄어드는 것은 허용**하되 몇 개가 줄었는지 보고한다. **늘어나면 실패**
#     - 그 밖의 글자 (NBSP 포함): 단 하나도 변하면 실패 (늘어도, 줄어도)
# [이유] 처음에는 공백류를 아예 집계에서 빼고 "공백 아닌 글자만" 비교했다.
#   그 설계에는 **맹점**이 있었다. Python의 isspace()는 NBSP(U+00A0)도 공백으로 보는데,
#   이 데이터는 NBSP를 문장 안의 단어 구분 공백으로 쓴다 (실측 268개). 그래서
#   경계에 있던 NBSP 31개가 버려졌는데도 검사는 "손실 0"이라고 보고했다.
#   고치는 과정에서 한 번 더 걸렸다. "공백류는 줄어도 된다"로 바꿨더니 그 규칙이
#   NBSP까지 덮어서 맹점이 그대로 남았다. 버려도 되는 공백을 isspace()가 아니라
#   **SKIPPABLE_WS로 한정**해야 비로소 검사가 NBSP를 본다 ([1] 참고).
#   공백류를 통째로 빼면 그 부류의 손실·증가를 **구조적으로 볼 수 없다.**
#   만약 어떤 버그가 공백을 잘못 끼워 넣거나 NBSP를 전부 날려도 검사가 조용히 통과한다.
#   그래서 "제외"가 아니라 "부류별로 다른 기준"으로 바꿨다. 버려도 되는 것은
#   버려도 된다고 명시하고, 그 수를 눈에 보이게 적는다.
#   상위 몇 개만 보지 않고 전체를 비교하는 까닭: 드문 글자(NBSP 268개, U+FF63 6개)에서
#   생기는 손실이 바로 이 분할기에서 실제로 났던 버그였다. 전수가 아니면 못 잡는다.
# ===================================================================
def check_no_loss(lines: list[str], sentences: list[str]) -> tuple[list[str], list[str]]:
    """(실패 내역, 의도된 공백 제거 내역)을 돌려준다. 실패 내역이 비면 통과."""
    before = Counter("".join(lines))
    after = Counter("".join(sentences))

    problems: list[str] = []
    dropped: list[str] = []
    for ch in sorted(set(before) | set(after), key=ord):
        a, b = before.get(ch, 0), after.get(ch, 0)
        if a == b:
            continue
        name = unicodedata.name(ch, "?")
        if ch in SKIPPABLE_WS and b < a:
            # 문장 경계에서 버린 공백. 의도된 동작이므로 수만 보고한다
            dropped.append(f"U+{ord(ch):04X} {name}: {a - b}자 제거 ({a} -> {b})")
        else:
            problems.append(f"U+{ord(ch):04X} {name}: 원본 {a} -> 분할 {b}")
    return problems, dropped


# ===================================================================
# [5] 자기 검사
#
# [목적] 이 검사가 실제로 실패할 수 있는지 확인한다.
# [동작] 일부러 글자를 지우는 분할 결과를 만들어 [4]에 넣고, FAIL이 나오는지 본다.
#   FAIL이 나오면 검사가 살아 있는 것이므로 self-test는 성공(0).
#   FAIL이 안 나오면 검사가 무엇도 잡지 못하는 것이므로 self-test는 실패(1).
# [이유] CLAUDE.md 검증 규칙 4번 "검증 프로그램은 틀린 입력에 대해 반드시 FAIL을
#   내야 한다. 일부러 값을 틀리게 바꾼 입력으로 FAIL이 나오는 것까지 확인해야
#   검증 프로그램 자체가 검증된다."
# ===================================================================
def self_test() -> int:
    sample = [
        "급히 갖다대었습니다. 랄프는 문을 열었습니다.",
        "“아리스, 눈을 떠 보아요. 이게 어떻게 된 일이에요. 아리스!”",
        "거기에는 2.5미터가 넘는 몸집이 있었습니다.",
        "뭐야?! 정말…",
    ]

    print("[self-test] 1. 정상 분할은 손실 0이어야 한다")
    good: list[str] = []
    for line in sample:
        good.extend(split_line(line))
    for s in good:
        print(f"    {s}")
    problems, dropped = check_no_loss(sample, good)
    for d in dropped:
        print(f"    (경계 공백 제거) {d}")
    if problems:
        print("  FAIL: 정상 입력인데 손실이 보고됐다")
        for p in problems:
            print(f"    {p}")
        return 1
    print("  OK: 손실 0")

    print("[self-test] 2. 소수점을 끊지 않았는지")
    if not any("2.5미터" in s for s in good):
        print("  FAIL: 2.5 가 쪼개졌다")
        return 1
    print("  OK: 2.5미터 유지")

    print("[self-test] 3. 닫는 따옴표가 앞 문장에 붙었는지")
    if not any(s.endswith("아리스!”") for s in good):
        print("  FAIL: 닫는 따옴표가 사라졌거나 다음 문장으로 갔다")
        return 1
    print("  OK: 닫는 따옴표 유지")

    # 4a, 4b는 "공백류를 통째로 제외했을 때 생기던 맹점"을 겨냥한 검사다.
    # 예전 설계(공백 아닌 글자만 비교)에서는 둘 다 조용히 통과했다.
    print("[self-test] 4a. NBSP가 사라지면 FAIL이 나와야 한다 (공백류 맹점 점검)")
    nb_in = [x + " " for x in sample]   # 원본에는 NBSP가 있고
    nb_out = list(good)                      # 결과에는 없다 -> 손실이다
    problems, _ = check_no_loss(nb_in, nb_out)
    hit = [x for x in problems if "00A0" in x]
    if not hit:
        print("  FAIL: NBSP가 사라졌는데 검사가 통과했다 (공백류를 못 보고 있다)")
        return 1
    print(f"  OK: {hit[0]}")

    print("[self-test] 4b. 공백이 늘어나면 FAIL이 나와야 한다")
    extra = [x + " " for x in good]
    problems, _ = check_no_loss(sample, extra)
    hit = [x for x in problems if "0020" in x]
    if not hit:
        print("  FAIL: 공백이 늘었는데 검사가 통과했다")
        return 1
    print(f"  OK: {hit[0]}")

    print("[self-test] 5. 망가진 분할 결과에는 FAIL이 나와야 한다")
    broken = [s.replace("”", "") for s in good]   # 닫는 따옴표를 일부러 버린다
    problems, _ = check_no_loss(sample, broken)
    if not problems:
        print("  FAIL: 글자를 지웠는데도 검사가 통과했다 (검사가 무의미)")
        return 1
    print(f"  OK: 손실 {len(problems)}종 적발")
    for item in problems:
        print(f"    {item}")

    print("\n[self-test] 통과")
    return 0


# ===================================================================
# [6] main
#
# [목적] 분할해서 파일로 쓰고, 통계를 화면에 보여 준다.
# [동작] 읽기 -> 분할 -> 손실 검사 -> (통과하면) 쓰기.
#   검사가 실패하면 파일을 쓰지 않고 1로 끝낸다.
# [이유] 검사를 통과하지 못한 파일을 남기면 그걸 입력으로 쓴 C 결과가 전부
#   의심스러워진다. 그래서 "검사 통과"를 쓰기의 전제 조건으로 둔다.
#   줄바꿈을 LF로 고정하는 까닭: C는 파일을 "rb"로 읽으므로 CRLF면 캐리지리턴이
#   글자로 섞여 들어가 글자 종류가 늘어난다. 7그램 키 여유가 27종뿐이라
#   늘어나면 안 된다. .gitattributes도 *.txt를 LF로 고정하고 있다.
# ===================================================================
def main(argv: list[str]) -> int:
    if len(argv) > 1 and argv[1] == "--self-test":
        return self_test()

    src = SRC_PATH.resolve()
    out = OUT_PATH.resolve()
    if not src.exists():
        print(f"[오류] 원본을 찾을 수 없습니다: {src}")
        return 1

    text = src.read_text(encoding="utf-8-sig")
    sentences, lines = split_corpus(text)

    problems, dropped = check_no_loss(lines, sentences)
    if problems:
        print(f"[FAIL] 글자 손실 {len(problems)}종 - 파일을 쓰지 않았습니다")
        for p in problems[:20]:
            print(f"  {p}")
        return 1

    with open(out, "w", encoding="utf-8", newline="\n") as fp:
        fp.write("\n".join(sentences) + "\n")

    chars = sum(len(s) for s in sentences)
    kinds = len({ch for s in sentences for ch in s})
    lengths = sorted(len(s) for s in sentences)
    print(f"원본   : {src}")
    print(f"         {len(lines):,}줄 (빈 줄 제외)")
    print(f"분할   : {len(sentences):,}문장 | {chars:,}글자 | {kinds:,}종 글자")
    print(f"길이   : 최소 {lengths[0]} / 중앙 {lengths[len(lengths) // 2]} / "
          f"평균 {chars / len(sentences):.2f} / 최대 {lengths[-1]}")
    print("손실   : 없음 (공백까지 포함한 전수 비교. 아래 경계 공백 제거만 허용)")
    for d in dropped:
        print(f"         경계 공백 제거: {d}")
    print(f"출력   : {out}")

    # 7그램 키 한계: 문맥 V^6 이 2^64 안에 들어가야 한다 (V = 글자 종류 + BOS + EOS)
    vocab = kinds + 2
    limit = 1 << 64
    print(f"\nV = {vocab:,} (글자 {kinds:,} + BOS + EOS)")
    print(f"V^6 = {vocab ** 6:.4e}  vs  2^64 = {limit:.4e}", end="  ")
    if vocab ** 6 < limit:
        print(f"-> 7그램 가능 (여유 {limit / vocab ** 6:.3f}배, 글자 종류 상한 1,623종)")
    else:
        print("-> 7그램 키 초과! step6_7gram 은 쓸 수 없다")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
