"""[STEP 7 / Phase 1] 전처리 - novel_test_data.txt -> step7-1_sentences.txt

강의 1단계(데이터 전처리)에 해당한다. 하는 일은 두 가지다.
  (1) 정제: 한글 / 숫자 / 영문 / 공백 / `,` `.` `!` `?` 만 남기고 나머지 글자를 버린다
  (2) 분할: 한 줄에 여러 문장이 뭉쳐 있는 원본을 "한 줄 = 한 문장"으로 쪼갠다

왜 정제까지 하나 (Step6과 달라진 점):
  Step6은 글자를 하나도 버리지 않았다. 그래서 따옴표 짝이 맞지 않는 문제를 전부
  직접 처리해야 했다 (닫는 따옴표가 구분자에 먹혀 12,942자가 사라지고, 이 데이터는
  `“`를 닫는 따옴표로도 써서 외톨이 `“`가 1,553개 나오는 등).
  강의의 두 참고 구현은 모두 따옴표를 버린다:
    reference_python/03 : not_allowed = re.compile(r"[^가-힣 ,.!?]")
    reference_C/step08_tokenize.py : SKIP_TOKENS = {'"', "'", '(', ')', '“', '”', ...}
  따옴표가 사라지면 위 함정들이 원천적으로 없어진다. Step7은 강의를 따라 (b)안을 쓴다.
  (2026-10-24 사용자 승인. 대신 버리는 글자는 아래 [5]에서 전수 집계해 보고한다)

왜 Step6의 data/novel_sentences.txt 를 재활용하지 않나:
  사용자 지시 (2026-10-24): *"쪼개는 작업을 전처리 단계에서 다시 해. 공부용이라니까."*
  결과 파일을 가져다 쓰면 전처리가 무슨 일을 하는지 볼 기회가 사라진다.
  정제 기준도 Step6(손실 0)과 Step7(강의식 필터)이 서로 다르므로 어차피 다시 만들어야 한다.

사용법:
  python step7-1_prepare.py               정제·분할해서 step7-1_sentences.txt (이 폴더) 로 저장
  python step7-1_prepare.py --self-test   검사기가 망가진 입력에 FAIL을 내는지 확인

종료 코드: 0 = 성공 / 1 = 실패
"""

from __future__ import annotations

import re
import sys
from collections import Counter
from pathlib import Path

# ===================================================================
# [1] 설정
#
# [목적] 입출력 경로와 "어떤 글자를 남길지"를 한곳에 모은다.
# [동작] 경로는 Step 폴더 기준 ../../data/ (파트 폴더 -> 저장소 루트로 두 단계).
# [이유] 어느 디렉토리에서 실행해도 같은 파일을 보도록 __file__ 기준 절대경로를 만든다.
#   현재 작업 디렉토리에 의존하면 Step 폴더 밖에서 돌렸을 때 조용히 다른 파일을 읽는다.
# ===================================================================
HERE = Path(__file__).resolve().parent
SRC_PATH = HERE / ".." / ".." / "data" / "korean_dictionary" / "novel_test_data.txt"
# 출력은 Step7 폴더 안에 둔다. 입력(원본 코퍼스)만 ../../data/ 에서 읽는다.
# 사용자 지시 (2026-10-24): "지금부터 하는 모든 작업의 결과물은 각 작업 폴더 안에서만 추가한다."
# data/ 는 여러 step이 공유하는 원본 저장소이므로, 특정 step의 산출물을 섞지 않는다.
OUT_PATH = HERE / "step7-1_sentences.txt"
REPORT_PATH = HERE / "step7-1_prepare_result.txt"

# 남길 글자: 한글 음절 + 숫자 + 영문 + 공백 + 쉼표 + 종결부호 3종.
#
# 강의 참고 구현 둘이 서로 다르다 (2026-10-24 확인):
#   reference_python/03     : not_allowed = r"[^가-힣 ,.!?]" -> 숫자·영문·따옴표 전부 제거
#   reference_C/clean_data.py: 줄 전체가 영문인 줄만 제거. **문장 속 영문과 숫자는 유지**.
#                              따옴표도 정제에서는 유지하고, step08_tokenize.py 가
#                              토큰화 시점에 SKIP_TOKENS 로 버린다
#
# 우리는 그 중간을 택했다 (사용자 결정 2026-10-24).
#   - 숫자(7,311개)·영문(6,734개)은 **살린다**. 자연스러운 문장을 만드는 것이 목적인데
#     `2미터가` -> `미터가`, `로봇은 SF에서는` -> `로봇은 에서는` 처럼 뜻이 깨지고 비문이 된다
#   - 따옴표류(33,825자)는 **버린다**. 어차피 참고 구현 양쪽 다 n그램에는 넣지 않는다
#     (노트북은 정제에서, C는 토큰화에서). 버리는 시점만 다르고 최종 결과는 같은데,
#     정제 때 버리면 문장 분할의 따옴표 함정까지 같이 사라져서 이득이다
ALLOWED_RE = re.compile(r"[가-힣0-9A-Za-z ,.!?]")

# 문장 종결부호. 정제 후에는 `…`(U+2026)가 이미 지워지므로 여기에 넣지 않는다.
# Step6은 `…`를 종결부호에 포함했지만, Step7은 정제 단계에서 사라져 들어올 수 없다.
TERMINATORS = ".!?"


def is_allowed(ch: str) -> bool:
    """정제 후에도 남는 글자인가."""
    return ALLOWED_RE.fullmatch(ch) is not None


# ===================================================================
# [2] 정제 - 허용하지 않은 글자를 버린다
#
# [목적] 따옴표·숫자·영문·말줄임표 등을 없애 뒤 단계(분할·토큰화)를 단순하게 만든다.
# [동작] 문단 안의 줄바꿈을 공백으로 바꾼 뒤(원본은 한 문장이 여러 줄에 걸치기도 한다),
#   허용 집합 밖의 글자를 지우고, 지우면서 생긴 연속 공백을 한 칸으로 합친다.
#   예) `그가 "안녕?" 하고 (웃었다)` -> `그가 안녕? 하고 웃었다`
# [이유] 연속 공백을 합치는 것은 강의 노트북에는 없는 처리다. 그래도 넣는 까닭은
#   Phase 2에서 공백을 토큰(`<S>`)으로 쓸 예정이기 때문이다. 공백 토큰이 연달아
#   생기면 n그램 문맥이 공백으로만 채워져 아무 정보도 못 본다.
#   (예: `(1)` 를 지우면 앞뒤 공백이 붙어 두 칸이 된다)
# ===================================================================
def clean_paragraph(para: str) -> str:
    joined = " ".join(para.split())      # 줄바꿈·탭·연속 공백을 한 칸으로 정리
    filtered = "".join(ch for ch in joined if is_allowed(ch))
    return re.sub(r" {2,}", " ", filtered)   # 글자를 지우면서 생긴 연속 공백을 다시 합친다


# ===================================================================
# [3] 문장 분할
#
# [목적] "한 줄 = 한 문장"을 만든다. 원본은 한 줄에 여러 문장이 뭉쳐 있어서
#   그대로 쓰면 BOS/EOS가 "문장의 시작·끝"이 아니라 "줄의 시작·끝"이 되고,
#   모델이 문장을 끝내는 법(EOS)을 배우지 못한다.
# [동작] 글자를 왼쪽부터 훑다가 종결부호를 만나면, 거기 이어 붙은 종결부호를
#   전부 삼킨 뒤 끊는다. 예) `가.. 나!` -> [`가..`, `나!`]
# [이유] 강의 노트북은 `re.findall(r"[^.!?]+[.!?]*", para)` 를 쓰지만 이 정규식은
#   **문단이 종결부호로 시작하면 그 부호들을 통째로 잃는다**. `[^.!?]+` 가 최소 한 글자를
#   요구하기 때문이다. 정제 후 `"...그래` -> `...그래` 처럼 점으로 시작하는 문단이
#   실제로 생길 수 있으므로, 손실이 없도록 직접 훑는 방식으로 썼다.
#   (강의와 다른 선택이며, [5]의 전수 검사로 손실 0임을 확인한다)
# ===================================================================
def split_sentences(text: str) -> list[str]:
    sentences: list[str] = []
    buf: list[str] = []
    i = 0
    while i < len(text):
        ch = text[i]
        buf.append(ch)
        if ch in TERMINATORS:
            # 연속된 종결부호(`...`, `?!`)는 한 문장의 끝으로 함께 삼킨다.
            # 하나씩 끊으면 `.` 만 들어 있는 빈 문장이 줄줄이 생긴다.
            i += 1
            while i < len(text) and text[i] in TERMINATORS:
                buf.append(text[i])
                i += 1
            sentences.append("".join(buf))
            buf = []
            continue
        i += 1
    if buf:                      # 종결부호 없이 끝난 꼬리도 한 문장으로 남긴다 (버리면 손실)
        sentences.append("".join(buf))
    return sentences


# ===================================================================
# [4] 코퍼스 전체 처리
#
# [목적] 원문 전체를 문단 -> 정제 -> 문장 순서로 처리한다.
# [동작] 빈 줄을 문단 경계로 보고 쪼갠 뒤, 문단마다 [2]·[3]을 적용한다.
#   각 문장은 앞뒤 공백을 떼고(strip), 빈 문자열만 버린다.
# [이유] 문단 경계를 쓰는 것은 강의 노트북과 같다. 빈 줄은 장면 전환이라
#   그 너머로 문장을 이어 붙이면 없던 문맥이 생긴다.
#   내용으로 문장을 걸러내지는 않는다(예: `...` 만 남은 문장도 버리지 않는다).
#   버리기 시작하면 [5]의 "허용 글자는 개수가 똑같아야 한다"는 검사가 무너져서,
#   진짜 버그로 글자가 사라져도 알아챌 수 없게 되기 때문이다.
# ===================================================================
def build_sentences(raw: str) -> list[str]:
    text = raw.replace("\r\n", "\n").replace("\r", "\n")
    sentences: list[str] = []
    for para in re.split(r"\n\s*\n", text):
        cleaned = clean_paragraph(para)
        if not cleaned:
            continue
        for s in split_sentences(cleaned):
            s = s.strip()        # 문장 경계의 공백만 버린다
            if s:
                sentences.append(s)
    return sentences


# ===================================================================
# [5] 전수 검사 - 의도한 것만 사라졌는가
#
# [목적] "정제로 버린 글자"와 "버그로 사라진 글자"를 구분한다.
#   정제는 일부러 글자를 버리는 작업이라, 손실이 나도 눈에 띄지 않는다.
#   그래서 검사를 세 갈래로 나눈다.
# [동작] 원본과 결과를 글자 종류별로 전부 세어 비교한다.
#   (1) 허용 글자 중 공백이 아닌 것(한글, `,` `.` `!` `?`): 개수가 **완전히 같아야 한다**
#   (2) 허용하지 않은 글자: 결과에 **0개여야 한다**
#   (3) 공백: 검사하지 않고 개수만 보고한다
# [이유] (3)만 예외인 까닭은 공백이 양쪽으로 변하기 때문이다. 문단 안의 줄바꿈이
#   공백으로 바뀌어 늘기도 하고, 문장 경계에서 strip 되어 줄기도 한다.
#   늘고 주는 두 가지가 섞이므로 "줄어들기만 한다" 같은 규칙을 쓸 수 없다.
#   Step6에서 isspace()로 공백류를 통째로 검사에서 빼는 바람에 NBSP 31개가 조용히
#   사라진 적이 있다. 그래서 여기서는 **ASCII 공백 하나만** 예외로 두고, NBSP를 포함한
#   나머지는 전부 (2)에 걸리게 했다.
# ===================================================================
def check_loss(raw: str, sentences: list[str]) -> tuple[list[str], list[str]]:
    src = Counter(raw.replace("\r", "").replace("\n", ""))
    out = Counter("".join(sentences))
    errors: list[str] = []
    notes: list[str] = []

    # (1) 허용 글자(공백 제외)는 하나도 변하면 안 된다
    for ch in sorted(set(src) | set(out)):
        if ch == " " or not is_allowed(ch):
            continue
        if src[ch] != out[ch]:
            errors.append(f"허용 글자 U+{ord(ch):04X} {ch!r}: 원본 {src[ch]:,} -> 결과 {out[ch]:,}")

    # (2) 허용하지 않은 글자는 결과에 남아 있으면 안 된다
    for ch in sorted(out):
        if not is_allowed(ch):
            errors.append(f"제거 대상이 남음 U+{ord(ch):04X} {ch!r}: {out[ch]:,}개")

    # (3) 공백은 보고만 한다
    notes.append(f"공백: 원본 {src[' ']:,} -> 결과 {out[' ']:,} ({out[' '] - src[' ']:+,})")
    return errors, notes


# ===================================================================
# [6] 자가 시험 - 검사기가 정말 FAIL을 내는가
#
# [목적] 검사기 자신을 검증한다. 통과만 하는 검사기는 검사기가 아니다
#   (CLAUDE.md 검증 규칙 4번 "실패할 수 있어야 한다").
# [동작] 일부러 망가뜨린 결과를 [5]에 넣어 **반드시 오류가 나오는지** 확인한다.
#   정상 입력 하나는 오류가 **없어야** 통과로 친다.
# [이유] 망가뜨리는 방식을 네 가지로 나눈 것은 [5]의 세 갈래가 각각 작동하는지
#   따로 확인하기 위해서다. 한 가지만 시험하면 나머지 갈래가 죽어 있어도 모른다.
# ===================================================================
def self_test() -> int:
    raw = '그가 "안녕?" 하고 말했다. 나는 웃었다!\n\n다음 문단 (1) 입니다.'
    good = build_sentences(raw)

    cases: list[tuple[str, list[str], bool]] = [
        ("정상 분할", good, False),                                  # 오류가 없어야 함
        ("한글 1자 삭제", [s.replace("웃", "", 1) for s in good], True),
        ("한글 1자 추가", good + ["덧붙인문장."], True),
        ("제거 대상이 남음", good + ['"'], True),
        ("종결부호 개수 변경", [s.replace(".", "", 1) for s in good], True),
    ]

    failed = 0
    for name, sents, must_fail in cases:
        errors, _ = check_loss(raw, sents)
        got_fail = bool(errors)
        ok = got_fail == must_fail
        print(f"  [{'OK' if ok else 'NG'}] {name}: "
              f"기대 {'FAIL' if must_fail else 'PASS'} / 실제 {'FAIL' if got_fail else 'PASS'}")
        if not ok:
            failed += 1
    print(f"\n자가 시험 {len(cases)}건 중 {len(cases) - failed}건 통과")
    return 1 if failed else 0


# ===================================================================
# [7] 통계와 보고서
#
# [목적] 정제로 무엇을 얼마나 버렸는지 사람이 확인할 수 있게 남긴다.
# [동작] 버려진 글자를 종류별로 전부 세어 빈도순으로 적는다. 공백은 `<SP>`,
#   NBSP는 `<NBSP>`로 표기한다 (Step6과 같은 표기. 데이터에 `<` `>`가 없음을 확인함).
# [이유] "2.51%를 버렸다"는 한 줄로는 무엇을 잃었는지 알 수 없다. 전 종류를 적어야
#   나중에 결과가 이상할 때 정제 탓인지 아닌지 되짚을 수 있다.
# ===================================================================
def display_name(ch: str) -> str:
    if ch == " ":
        return "<SP>"
    if ch == " ":
        return "<NBSP>"
    if ord(ch) < 0x20:
        return f"<U+{ord(ch):04X}>"
    return ch


def write_report(raw: str, sentences: list[str], notes: list[str]) -> None:
    src = Counter(raw.replace("\r", "").replace("\n", ""))
    removed = Counter({ch: n for ch, n in src.items() if not is_allowed(ch)})
    body = "".join(sentences)
    lengths = [len(s) for s in sentences]

    removed_types = len(removed)
    removed_total = sum(removed.values())
    lines = [
        "=== STEP 7 Phase 1: 전처리 결과 (정제 + 문장 분할) ===",
        "",
        "--- 이 파일이 무엇인가 ---",
        "step7-1_prepare.py (Step7의 첫 단계) 를 실행하고 남긴 보고서다. 원본 소설 코퍼스를 두 가지로 손봤다.",
        "",
        f"  (1) 정제: 한글 / 숫자 / 영문 / 공백 / , . ! ? 만 남기고 나머지를 버렸다.",
        f"      따옴표·괄호·말줄임표가 여기서 사라진다 (총 {removed_total:,}자, {removed_types}종).",
        "      숫자와 영문은 **남긴다**. 버리면 '2미터가' -> '미터가',",
        "      '로봇은 SF에서는' -> '로봇은 에서는' 처럼 뜻이 깨지고 비문이 되기 때문이다.",
        "      따옴표를 버리면 '닫는 따옴표가 어디까지인지' 같은 골치 아픈 문제가 통째로 없어진다.",
        "      (강의 참고 구현도 따옴표는 n그램에 넣지 않는다. 노트북은 정제에서, C는 토큰화에서 버린다)",
        "",
        "  (2) 분할: 한 줄에 여러 문장이 뭉쳐 있던 것을 '한 줄 = 한 문장'으로 쪼갰다.",
        "      안 쪼개면 모델이 문장의 끝(EOS)을 배우지 못하고 줄의 끝을 배운다.",
        "",
        "아래 숫자를 읽는 법:",
        "  - '허용 글자'(한글·숫자·영문과 , . ! ?)는 원본과 결과의 개수가 완전히 같아야 한다.",
        "    하나라도 다르면 프로그램이 파일을 쓰지 않고 멈춘다. 즉 이 파일이 있다는 것은",
        "    '의도한 것 말고는 아무것도 잃지 않았다'는 검사를 통과했다는 뜻이다.",
        "  - 공백만 예외다. 문단 안의 줄바꿈이 공백으로 바뀌어 늘기도 하고,",
        "    문장 경계에서 떼어내 줄기도 해서 개수가 양쪽으로 변한다. 그래서 보고만 한다.",
        "  - 맨 아래 목록은 정제로 버린 글자 전 종류다. 나중에 생성 결과가 이상하면",
        "    여기서 무엇을 잃었는지 되짚어 보면 된다.",
        "",
        "--- 설정 ---",
        f"입력 : {SRC_PATH.name}  (원본 코퍼스. ../../data/korean_dictionary/ 에서 읽는다)",
        f"출력 : {OUT_PATH.name}  (Step7 폴더 안에 생성)",
        "정제 기준: 한글(가-힣) / 숫자 / 영문 / 공백 / , . ! ? 만 남김",
        "",
        "--- 규모 ---",
        f"원본 글자수(개행 제외) : {sum(src.values()):,}",
        f"결과 글자수            : {len(body):,}",
        f"원본 글자 종류         : {len(src):,}",
        f"결과 글자 종류         : {len(set(body)):,}",
        f"문장 수                : {len(sentences):,}",
        f"평균 문장 길이         : {sum(lengths) / len(lengths):.2f} 글자",
        f"최대 / 최소            : {max(lengths):,} / {min(lengths)}",
        "",
        "--- 공백 ---",
        *[f"{n}" for n in notes],
        "",
        f"--- 정제로 버린 글자: {sum(removed.values()):,}개 / {len(removed)}종 (전 종류) ---",
    ]
    for ch, n in removed.most_common():
        lines.append(f"  {display_name(ch)}\tU+{ord(ch):04X}\t{n:,}")
    REPORT_PATH.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="")


# ===================================================================
# [8] main
#
# [동작] 읽기 -> 정제·분할 -> 전수 검사 -> (통과해야만) 파일 쓰기 -> 보고서.
# [이유] 검사를 통과하지 못하면 **파일을 쓰지 않는다**. 망가진 데이터를 저장하면
#   뒤 Phase가 그걸 정상으로 믿고 진행해 원인을 찾기 어려워진다.
#   출력은 LF 고정이다. CRLF면 C가 "rb"로 읽을 때 `\r`이 글자로 섞여 어휘가 늘어난다.
# ===================================================================
def main(argv: list[str]) -> int:
    if len(argv) > 1 and argv[1] == "--self-test":
        return self_test()

    if not SRC_PATH.exists():
        print(f"입력 파일이 없습니다: {SRC_PATH}")
        return 1

    raw = SRC_PATH.read_text(encoding="utf-8")
    sentences = build_sentences(raw)
    errors, notes = check_loss(raw, sentences)

    if errors:
        print("전수 검사 실패 - 파일을 쓰지 않습니다:")
        for e in errors[:20]:
            print("  " + e)
        if len(errors) > 20:
            print(f"  ... 외 {len(errors) - 20}건")
        return 1

    OUT_PATH.write_text("\n".join(sentences) + "\n", encoding="utf-8", newline="")
    write_report(raw, sentences, notes)

    body = "".join(sentences)
    print(f"문장 {len(sentences):,}개 / 글자 {len(body):,}자 / 글자 종류 {len(set(body)):,}종")
    for n in notes:
        print("  " + n)
    print(f"저장: {OUT_PATH}")
    print(f"보고서: {REPORT_PATH.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
