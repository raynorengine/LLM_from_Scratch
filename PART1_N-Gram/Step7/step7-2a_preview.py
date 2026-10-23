"""[STEP 7 / Phase 2-a] Kiwi 미리보기 - 토큰화와 역토큰화를 눈으로 보고, 복원 방식 3가지를 비교한다

전체 과정 중 **2단계(형태소 토큰화)와 4단계(역토큰화)만** 맛본다. 3단계(N그램)는 하지 않는다.
모델이 아직 없으므로, 여기서 말하는 "복원"은 생성된 문장이 아니라
**원문을 토큰화했다가 그대로 되돌린 것**(왕복 시험)이다.

4단계를 미리 당겨 보는 이유:
  복원이 얼마나 부정확한지 알고 나서 Phase 2-b(토큰화 방식)를 정해야 하기 때문이다.
  공백을 토큰으로 넣을지(`<S>`)가 취향 문제가 아니라 정보 손실 문제라는 것이 여기서 드러난다.

비교하는 복원 방식 3가지:
  ① <W>만        : 공백 정보 없이 kiwi.join() 에 전부 맡김
  ② <W>+<S>      : <S>로 어절을 띄우고 어절마다 join + 어절 안의 공백 제거 (= C안)
  ③ <S>+실무방식 : 토큰을 **원문 조각 그대로**(표면형) 저장하고 그냥 이어붙임
                   SentencePiece가 공백을 `▁`로 어휘에 넣고 디코딩 때 치환하는 것과 같은 원리

사용법:
  python step7-2a_preview.py              결과를 화면과 step7-2a_preview_result.txt 에 출력

종료 코드: 0 = 성공 / 1 = 실패
"""

from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path

from kiwipiepy import Kiwi

# ===================================================================
# [1] 설정
#
# [목적] 입출력 경로, 강의 표기 기호, 표본 크기를 한곳에 모은다.
# [이유] 입력은 Phase 1 산출물을 쓴다. 원본 코퍼스를 다시 읽으면 정제되지 않은
#   글자(따옴표 등)가 섞여 들어와 Phase 2-b와 조건이 달라진다.
# ===================================================================
HERE = Path(__file__).resolve().parent
SRC_PATH = HERE / "step7-1_sentences.txt"
OUT_PATH = HERE / "step7-2a_preview_result.txt"

W = "<W>"   # 형태소 구분자 (강의 표기)
S = "<S>"   # 공백 토큰 (강의 표기). SentencePiece 의 `▁` 에 해당한다

SAMPLE = 1000   # 복원 정확도를 재는 표본 문장 수
DETAIL = 5      # 자세히 보여줄 문장 수


# ===================================================================
# [2] 볼 문장 고르기
#
# [목적] 성격이 다른 문장을 일부러 섞어 뽑는다.
# [동작] 숫자가 든 문장 1개, 영문이 든 문장 1개, 그리고 앞쪽 문장들.
# [이유] Phase 1에서 숫자·영문을 살리기로 했으므로(2026-10-24 결정) Kiwi가 그것들을
#   어떤 품사로 보는지 확인해야 한다. 고정된 규칙으로 고르므로 몇 번 돌려도 같다.
# ===================================================================
def pick_sentences(lines: list[str]) -> list[str]:
    picked: list[str] = []
    has_digit = next((s for s in lines if any(c.isdigit() for c in s)), None)
    has_latin = next((s for s in lines if any(("A" <= c <= "Z") or ("a" <= c <= "z") for c in s)), None)
    for s in (has_digit, has_latin):
        if s and s not in picked:
            picked.append(s)
    for s in lines:
        if len(picked) >= DETAIL:
            break
        if s not in picked and 15 <= len(s) <= 60:
            picked.append(s)
    return picked


# ===================================================================
# [3] 공백 위치 찾기 - `<S>` 를 어디에 넣을 것인가
#
# [목적] Kiwi는 공백을 토큰으로 주지 않으므로 원문의 공백 위치를 직접 찾아야 한다.
# [동작] 토큰마다 원문에서의 시작 위치(start)와 길이(len)를 알려준다.
#   "앞 토큰이 끝난 자리"와 "다음 토큰이 시작하는 자리"가 벌어져 있으면 그 사이에 공백이 있었다.
#   예) `나 는` -> `나`(start 0, len 1), `는`(start 2) -> 0+1 < 2 이므로 사이에 공백
# [이유] 문자열 검색(`" " in ...`)으로 찾으면 토큰 경계와 어긋난다. 위치 정보를 쓰는 것이 정확하다.
# ===================================================================
def space_before(tokens: list) -> list[bool]:
    """각 토큰 앞에 공백이 있었는지"""
    flags, cursor = [], 0
    for t in tokens:
        flags.append(t.start > cursor)
        cursor = t.start + t.len
    return flags


def tokens_ws(tokens: list) -> list[str]:
    """`<S>` 를 끼워 넣은 토큰 목록 (기본형 기준)"""
    out = []
    for t, sp in zip(tokens, space_before(tokens)):
        if sp:
            out.append(S)
        out.append(f"{t.form}/{t.tag}")
    return out


def tokens_surface(sent: str, tokens: list) -> list[str]:
    """실무 방식용 토큰 목록 - form 대신 **원문 조각**을 담는다

    Kiwi의 form 은 기본형이라 원문과 다를 수 있다 (`넘어져` -> `넘어지`).
    실무 토크나이저(SentencePiece 등)는 토큰이 항상 원문의 부분문자열이어서
    이어붙이면 원문이 된다. 그 성질을 흉내 내려면 원문 조각을 그대로 담아야 한다.

    **그런데 Kiwi의 토큰 구간은 서로 겹친다** (실측으로 확인, 2026-10-24).
      `큰` -> `크/VA`(start=0,len=1) + `ᆫ/ETM`(start=0,len=1)   <- 같은 글자를 둘이 공유
      `넘어져` -> `넘어지/VV`(start=6,len=3) + `어/EC`(start=8,len=1)  <- 1글자 겹침
    그냥 잘라 붙이면 글자가 중복된다(`큰큰`). 그래서 이미 쓴 자리는 건너뛰고
    **아직 안 쓴 부분만** 떼어 온다. 겹친 토큰은 빈 문자열이 된다.

    빈 문자열 토큰이 생긴다는 것 자체가 핵심이다. 실무 토크나이저는 원문을
    겹치지 않게 나눠 가지는 **분할(partition)** 이라 이런 일이 없다.
    형태소 분석은 분할이 아니므로 그 위에 실무 방식을 얹는 것이 구조적으로 불가능하다.
    """
    out = []
    cursor = 0
    for t, sp in zip(tokens, space_before(tokens)):
        if sp:
            out.append(S)
        begin = max(cursor, t.start)                 # 이미 쓴 자리는 건너뛴다
        end = max(cursor, t.start + t.len)
        out.append(f"{sent[begin:end]}/{t.tag}")     # 겹친 토큰이면 빈 조각이 된다
        cursor = end
    return out


# ===================================================================
# [4] 복원 방식 3가지
#
# [이유] 셋을 나란히 둬야 "무엇 때문에 정확도가 달라지는지"가 보인다.
#   ①→② 는 공백 정보를 넣은 효과, ②→③ 은 토큰을 기본형이 아니라 원문 조각으로 둔 효과다.
# ===================================================================
def restore_w_only(kiwi: Kiwi, tokens: list) -> str:
    """① <W>만 - 공백 정보 없이 join 에 전부 맡긴다"""
    return kiwi.join([(t.form, t.tag) for t in tokens])


def restore_w_s(kiwi: Kiwi, tokens: list) -> str:
    """② <W>+<S> (C안) - <S>로 어절을 띄우고, 어절마다 join 한 뒤 어절 안의 공백을 지운다

    어절 안의 공백을 지우는 까닭: 한 어절 안에는 원래 공백이 없다(그게 어절의 정의).
    따라서 어절 내부에 나타난 공백은 전부 join 이 만들어낸 것이므로 지워도 된다.
    이 한 줄이 68.8% -> 84.4% 를 만든다 (실측).
    """
    groups, cur = [], []
    for t, sp in zip(tokens, space_before(tokens)):
        if sp and cur:
            groups.append(cur)
            cur = []
        cur.append((t.form, t.tag))
    if cur:
        groups.append(cur)
    return " ".join(kiwi.join(g).replace(" ", "") for g in groups)


def restore_surface(sent: str, tokens: list) -> str:
    """③ <S>+실무방식 - 원문 조각을 그냥 이어붙이고 <S>는 공백으로 바꾼다

    SentencePiece 의 디코딩과 같다: "토큰을 전부 이어붙이고 `▁`를 공백으로 치환".
    복원 규칙이라 할 것이 없다. 치환 하나뿐이다.
    """
    out = []
    for piece in tokens_surface(sent, tokens):
        out.append(" " if piece == S else piece.rsplit("/", 1)[0])
    return "".join(out)


# ===================================================================
# [5] 표본으로 정확도와 어휘 크기를 잰다
#
# [목적] 세 방식의 복원 정확도와, 그 대가인 어휘 크기를 함께 본다.
# [이유] 정확도만 보면 ③이 압도적이지만, ③은 원문 조각을 그대로 담으므로
#   `넘어지`/`넘어져`/`넘어졌` 가 서로 다른 토큰이 되어 **어휘가 다시 늘어난다**.
#   형태소 분석을 하는 목적이 어휘를 줄이는 것이었으므로 이 대가를 같이 재야 공정하다.
# ===================================================================
def measure(kiwi: Kiwi, lines: list[str]) -> dict:
    ok = [0, 0, 0]
    vocab_form: Counter = Counter()
    vocab_surface: Counter = Counter()
    n_tokens = 0
    n_overlap = 0        # 앞 토큰과 원문 구간이 겹치는 토큰 수
    n_empty = 0          # 실무 방식에서 빈 조각이 되어 버리는 토큰 수

    for sent in lines:
        tokens = kiwi.tokenize(sent)
        n_tokens += len(tokens)
        cursor = 0
        for t in tokens:
            if t.start < cursor:
                n_overlap += 1
            if max(cursor, t.start) >= max(cursor, t.start + t.len):
                n_empty += 1
            cursor = max(cursor, t.start + t.len)
        if restore_w_only(kiwi, tokens) == sent:
            ok[0] += 1
        if restore_w_s(kiwi, tokens) == sent:
            ok[1] += 1
        if restore_surface(sent, tokens) == sent:
            ok[2] += 1
        for t in tokens:
            vocab_form[f"{t.form}/{t.tag}"] += 1
            vocab_surface[f"{sent[t.start:t.start + t.len]}/{t.tag}"] += 1

    n = len(lines)
    return {
        "n": n,
        "tokens": n_tokens,
        "acc": [c / n * 100 for c in ok],
        "hit": ok,
        "vocab_form": len(vocab_form),
        "vocab_surface": len(vocab_surface),
        "overlap": n_overlap,
        "empty": n_empty,
    }


# ===================================================================
# [6] 보고서
# ===================================================================
def build_report(kiwi: Kiwi, detail: list[str], m: dict) -> str:
    import kiwipiepy

    L: list[str] = [
        "=== STEP 7 Phase 2-a: Kiwi 미리보기 (토큰화 + 역토큰화) ===",
        "",
        "--- 이 파일이 무엇인가 ---",
        "전체 과정 중 **2단계(형태소 토큰화)와 4단계(역토큰화)만** 맛본 것이다.",
        "3단계(N그램)는 하지 않았다. 모델이 아직 없다.",
        "  강의 1단계 데이터 전처리  -> Phase 1 에서 끝냄 (step7-1_prepare.py)",
        "  강의 2단계 형태소 토큰화  -> 여기서 해 봄",
        "  강의 3단계 N그램 생성     -> 건너뜀",
        "  강의 4단계 문장 변환      -> 여기서 해 봄 (토큰화한 것을 바로 되돌림)",
        "  강의 5~7단계              -> 아직",
        "따라서 아래 [복원] 은 '모델이 생성한 문장'이 아니라",
        "'원문을 토큰화했다가 그대로 되돌린 것'이다. 왕복 시험이다.",
        "",
        f"Kiwi: kiwipiepy {getattr(kiwipiepy, '__version__', '?')}",
        f"표본: step7-1_sentences.txt 의 앞 {m['n']:,}문장 (토큰 {m['tokens']:,}개)",
        "",
        "",
        "=================================================================",
        " 복원 정확도 요약  (원문과 글자까지 똑같이 복원된 문장의 비율)",
        "=================================================================",
        "",
        "  방식                     복원 정확도      복원 방법",
        "  ----------------------   -----------      ------------------------------------",
        f"  (1) <W>만                {m['acc'][0]:5.1f}%          kiwi.join() 에 전부 맡김",
        f"  (2) <W>+<S>   (C안)      {m['acc'][1]:5.1f}%          <S>로 어절 띄움 + 어절마다 join",
        f"                                           + 어절 안의 공백 제거",
        f"  (3) <S>+실무방식         {m['acc'][2]:5.1f}%          원문 조각을 그냥 이어붙임",
        f"                                           (<S> -> 공백 치환)",
        "",
        f"  (3)의 함정: 원문 구간이 앞 토큰과 겹치는 토큰 {m['overlap']:,}개 "
        f"({m['overlap'] / m['tokens'] * 100:.1f}%)",
        f"             -> 그만큼이 **빈 토큰**이 된다 ({m['empty']:,}개). 토크나이저로 쓸 수 없다",
        "",
        f"  어휘 크기 (표본 기준):  기본형 토큰 {m['vocab_form']:,}종  /  "
        f"원문조각 토큰 {m['vocab_surface']:,}종",
        f"                          -> 실무 방식은 어휘가 {m['vocab_surface'] / m['vocab_form']:.2f}배로 늘어난다",
        "",
        "",
        "--- 이 숫자가 말하는 것 ---",
        "",
        "1) 공백 정보를 버리면 안 된다  ((1) -> (2))",
        "   kiwi.join() 은 원문의 공백을 모른다. 우리가 넘기는 것은 (형태, 품사) 쌍뿐이라",
        "   join 은 **품사만 보고 띄어쓰기 규칙을 새로 적용**한다.",
        "     join([27/SN, 세기/NNG])  -> '27 세기'   (숫자+일반명사는 띄움)",
        "     join([2/SN, 미터/NNB])   -> '2미터'     (숫자+의존명사는 붙임)",
        "   그래서 원문 `27세기`가 `27 세기`가 된다. 공백은 토큰화 시점에는 살아 있었고",
        "   (start/len 이 붙어 있음을 알려준다) join 단계에서 잃어버린 것이다.",
        "",
        "2) (2) C안은 땜질이다",
        "   <S>로 어절을 띄워도 join 이 **어절 안쪽**에 공백을 넣는다. 그래서 어절 내부의",
        "   공백을 지우는 보정을 덧붙여야 했다. 한 어절 안에는 원래 공백이 없으므로",
        "   이 보정 자체는 타당하지만, 복원 규칙이 두 단계로 늘어난 것은 분명한 땜질이다.",
        "   그러고도 100%가 되지 않는다. 남는 오차는 전부 **축약형**이다:",
        "     대었습니다 -> 댔습니다 / 되어 -> 돼 / 보아요 -> 봐요 / 이게 -> 이것이(거꾸로 풀어씀)",
        "   이건 띄어쓰기 문제가 아니라 Kiwi 가 표준형으로 정규화하는 것이라",
        "   토큰 (형태, 품사) 만으로는 복구할 방법이 없다.",
        "   (join(lm_search=False) 도 시험했으나 정확도 동일)",
        "",
        "3) 형태소 분석기는 원문 복원용 도구가 아니다",
        "   `넘어져`를 `넘어지 + 어`로 바꾸는 것이 형태소 분석의 본업이다. 이것을",
        "   **정규화(lemmatization)** 라고 하며, 정보를 일부러 버려서 '같은 말'을 하나로",
        "   모으는 작업이다. 즉 **정규화는 본질적으로 비가역이다.**",
        "   실무에서 형태소 분석기를 쓰는 곳은 원문을 되돌릴 필요가 없는 작업이다",
        "   (검색 색인, 키워드 추출, 통계 분석). 문장 생성에는 쓰지 않는다.",
        "   우리가 84%에서 막히는 이유는 **도구를 용도 밖으로 쓰고 있기 때문**이다.",
        "",
        "   더 근본적인 사실: **형태소 분석은 원문의 분할(partition)이 아니다.**",
        "   실무 토크나이저는 원문을 겹치지 않게 나눠 가지므로 이어붙이면 원문이 된다.",
        "   그런데 Kiwi는 한 글자를 두 토큰이 공유한다 (실측):",
        "     `큰`     -> `크/VA`(start=0,len=1) + `ᆫ/ETM`(start=0,len=1)   같은 글자를 둘이 가리킴",
        "     `넘어져` -> `넘어지/VV`(start=6,len=3) + `어/EC`(start=8,len=1)  1글자 겹침",
        "   축약·활용 때문에 '어디서 자른다'가 글자 경계와 맞아떨어지지 않는 것이다.",
        "   그래서 위 (3)은 겹친 부분을 건너뛰어야 100%가 되고, 그 대가로 토큰의 8.1%가",
        "   **빈 토큰**이 된다. 빈 토큰은 n그램에 넣을 수 없으므로 (3)은 복원만 되고",
        "   **토크나이저로는 쓸 수 없다**. 숫자가 100%라고 해서 쓸 수 있다는 뜻이 아니다.",
        "",
        "4) 실무(현대 LLM)는 어떻게 하나 - 애초에 가역이게 설계한다",
        "   HuggingFace 문서(강의 참고자료 [3])의 SentencePiece 설명:",
        "     \"treats the input text as a raw byte or character stream and includes the",
        "      space character, represented as '▁', in the vocabulary.\"",
        "     \"At decoding, SentencePiece concatenates all tokens and replaces '▁' with a space.\"",
        "   공백을 어휘에 넣고 전용 기호로 표시한다 -> **강의의 <S> 와 같은 발상이다.**",
        "   그리고 토큰이 항상 원문의 부분문자열이라 **이어붙이면 원문**이 된다.",
        "   복원 규칙이라 할 것이 없고 치환 하나뿐이다. 그래서 (3)이 100%가 나온다.",
        "   GPT-2 의 byte-level BPE 는 256개 바이트를 전부 어휘에 넣어 <unk> 자체가 없다.",
        "",
        "5) 그렇다면 공짜인가 - 아니다. 어휘가 늘어난다",
        f"   (3)은 `넘어지`/`넘어져`/`넘어졌`를 서로 다른 토큰으로 본다. 표본에서 어휘가",
        f"   {m['vocab_form']:,}종 -> {m['vocab_surface']:,}종으로 늘었다.",
        "   형태소 분석을 한 목적이 '어휘를 줄여 빈도를 모으는 것'이었음을 생각하면,",
        "   가역성과 어휘 효율이 **맞바꿈 관계**라는 뜻이다.",
        "   현대 서브워드 토크나이저는 이 둘을 동시에 잡으려고 나온 것이다:",
        "   언어학적 규칙을 버리고, 데이터에서 통계적으로 자주 쓰이는 조각을 찾아",
        "   어휘를 줄이면서도 조각이 원문의 부분문자열이라 가역성을 유지한다.",
        "",
        "   정리하면 이 실습의 위치는 여기다:",
        "     글자 단위(Step1~6)   가역 O / 어휘 작음 / 문맥 짧음",
        "     형태소 단위(Step7)   가역 X / 어휘 줄임 / 문맥 길어짐   <- 지금 여기",
        "     서브워드(현대 LLM)   가역 O / 어휘 줄임 / 문맥 길어짐",
        "",
        "",
        "=================================================================",
        f" 문장별 상세 ({len(detail)}개)",
        "=================================================================",
        "",
        "보는 법:",
        "  [토큰]          Kiwi가 쪼갠 결과. `표면형/품사태그` 형식",
        "  [<W>만]         형태소 사이를 <W> 로 이은 것. 공백 정보가 없다",
        "  [<W>+<S>]       공백이 있던 자리에 <S> 토큰을 넣은 것 (강의 표기)",
        "  [<S>+실무방식]  토큰을 **원문 조각**으로 담은 것. form 대신 원문 글자 그대로",
        "  [복원 1/2/3]    위 세 방식으로 되돌린 결과와 원문 일치 여부",
        "",
    ]

    for i, sent in enumerate(detail, 1):
        tokens = kiwi.tokenize(sent)
        r1 = restore_w_only(kiwi, tokens)
        r2 = restore_w_s(kiwi, tokens)
        r3 = restore_surface(sent, tokens)
        L += [
            f"[{i}] 원문          : {sent}",
            f"    토큰({len(tokens)}개)   : " + "  ".join(f"{t.form}/{t.tag}" for t in tokens),
            f"    <W>만         : " + W.join(f"{t.form}/{t.tag}" for t in tokens),
            f"    <W>+<S>       : " + W.join(tokens_ws(tokens)),
            f"    <S>+실무방식  : " + W.join(tokens_surface(sent, tokens)),
            f"    복원 1 <W>만  : {r1}   [{'일치' if r1 == sent else '불일치'}]",
            f"    복원 2 <W>+<S>: {r2}   [{'일치' if r2 == sent else '불일치'}]",
            f"    복원 3 실무   : {r3}   [{'일치' if r3 == sent else '불일치'}]",
            "",
        ]
    return "\n".join(L) + "\n"


# ===================================================================
# [7] main
# ===================================================================
def main() -> int:
    if not SRC_PATH.exists():
        print(f"입력 파일이 없습니다: {SRC_PATH}  (먼저 step7-1_prepare.py 를 실행하세요)")
        return 1

    lines = SRC_PATH.read_text(encoding="utf-8").splitlines()
    kiwi = Kiwi()
    m = measure(kiwi, lines[:SAMPLE])
    text = build_report(kiwi, pick_sentences(lines), m)

    OUT_PATH.write_text(text, encoding="utf-8", newline="")
    sys.stdout.reconfigure(encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
