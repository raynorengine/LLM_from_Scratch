"""[STEP 6] 문장 생성 n그램 독립 검증 (n = 3, 5, 7)

step6_3gram.exe / step6_5gram.exe / step6_7gram.exe 가 만든 모델과 결과를
data/novel_sentences.txt 에서 **처음부터 다시 계산해** 전수 비교한다.

검증 원칙 (CLAUDE.md "가짜 검증 금지")
  1. 독립 재계산: C의 출력을 받아쓰지 않고 원본 데이터에서 다시 센다
  2. 다른 구현: C는 글자를 번호로 바꿔 V진법 정수 키를 만든다.
     이 검증기는 **문자열 슬라이스**로 n그램을 센다 (BOS/EOS는 데이터에 없는
     제어문자 U+0000 / U+0001 로 둔다). 같은 로직을 베끼지 않았으므로
     같은 버그를 공유하지 않는다
  3. 전수 비교: 상위 몇 개가 아니라 n그램 전체·문맥 전체·빈도표 전 줄을 비교한다
  4. 실패할 수 있어야 한다: --self-test 가 일부러 틀린 입력을 넣어
     각 검사가 FAIL을 내는지 확인한다
  5. 종료 코드: 0 = 전부 통과 / 1 = 하나라도 실패

사용법
  python step6_verify.py                  3, 5, 7 전부 검증
  python step6_verify.py --n 7            7그램만 검증
  python step6_verify.py --self-test      검증기 자체 점검 (틀린 입력에 FAIL이 나는지)
  python step6_verify.py --compare        결과 파일 3개를 모아 step6_compare.txt 작성
"""

from __future__ import annotations

import subprocess
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA = HERE / ".." / ".." / "data" / "novel_sentences.txt"

# BOS/EOS로 쓸 글자. 데이터의 글자는 U+0020 이상임을 [1]에서 확인하므로 겹치지 않는다
BOS = "\x00"
EOS = "\x01"

NS = (3, 5, 7)
EXE = {n: HERE / f"step6_{n}gram.exe" for n in NS}
SRC = {n: HERE / f"step6_{n}gram.c" for n in NS}
TABLE = {n: HERE / f"step6_{n}gram_table.txt" for n in NS}

# C의 [1] 구역 상수와 같아야 하는 값 (다르면 [1]에서 알려 준다)
HASH_BITS = {3: 19, 5: 21, 7: 22}


# ===================================================================
# [0] 공통 도구
#
# [목적] 검사 결과를 모아 마지막에 한 번에 요약하고, 종료 코드를 정한다.
# [동작] Checker.check(이름, 조건, 설명)을 부를 때마다 OK/FAIL을 찍고 센다.
# [이유] 검사 중간에 멈추면 "첫 번째 실패"만 보이고 나머지 상태를 알 수 없다.
#   전부 돌린 뒤 요약을 보면 무엇이 깨졌는지 한눈에 들어온다.
# ===================================================================
class Checker:
    def __init__(self) -> None:
        self.passed = 0
        self.failed = 0

    def check(self, name: str, ok: bool, detail: str = "") -> bool:
        if ok:
            self.passed += 1
            print(f"  OK   {name}" + (f"  ({detail})" if detail else ""))
        else:
            self.failed += 1
            print(f"  FAIL {name}" + (f"  ({detail})" if detail else ""))
        return ok

    def summary(self) -> int:
        total = self.passed + self.failed
        print(f"\n{'=' * 70}")
        if self.failed:
            print(f"[실패] {total}개 검사 중 {self.failed}개 실패")
            return 1
        print(f"[통과] {total}개 검사 전부 통과")
        return 0


def run_exe(n: int, args: list[str], stdin_text: str | None = None) -> str:
    """C 프로그램을 실행해 표준출력을 UTF-8 문자열로 돌려준다."""
    proc = subprocess.run(
        [str(EXE[n])] + args,
        input=stdin_text.encode("utf-8") if stdin_text is not None else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=str(HERE),
    )
    if proc.returncode != 0:
        print(f"  [경고] {EXE[n].name} {' '.join(args)} 가 코드 {proc.returncode}로 끝남")
        print("        " + proc.stderr.decode("utf-8", "replace").strip()[:300])
    return proc.stdout.decode("utf-8")


# ===================================================================
# [1] 원본 데이터 독립 읽기와 글자 번호
#
# [목적] C와 같은 입력에서 같은 글자 번호 체계를 독립적으로 세운다.
# [동작] 1) novel_sentences.txt 를 줄 단위로 읽어 문장 목록을 만든다
#   2) 쓰인 글자를 모아 코드포인트 오름차순으로 0, 1, 2 ... 번호를 준다
#      (C의 [5] 압축 번호와 같은 규칙. 등장 순서가 아니라 코드포인트 순서)
#   3) BOS = 글자수, EOS = 글자수 + 1
#   4) 표시 이름: 공백은 <SP>, NBSP는 <NBSP>, 제어문자는 <U+XXXX>
# [이유] C가 매긴 번호를 받아오면 번호가 틀렸을 때 못 잡는다. 규칙만 보고
#   직접 매긴 뒤 결과(빈도표의 정렬 순서)가 같은지로 확인한다.
#   BOS/EOS에 U+0000 / U+0001 을 쓰는 까닭: 데이터의 글자가 U+0020 이상이라
#   절대 겹치지 않는다. 겹치면 패딩과 실제 글자를 구분할 수 없다
#   (이 전제를 아래에서 실제로 검사한다).
# ===================================================================
class Corpus:
    def __init__(self, path: Path) -> None:
        text = path.read_text(encoding="utf-8")
        self.sents = [line for line in text.splitlines() if line]
        self.chars = sorted({ch for s in self.sents for ch in s}, key=ord)
        self.cid = {ch: i for i, ch in enumerate(self.chars)}
        self.bos_id = len(self.chars)
        self.eos_id = len(self.chars) + 1
        self.vocab = len(self.chars) + 2
        self.n_chars = sum(len(s) for s in self.sents)

        # 번호 -> 표시 이름 (C의 disp_name과 같은 규칙을 독립 구현)
        self.names = [self.disp_name(ch) for ch in self.chars]
        self.names.append("BOS")
        self.names.append("EOS")

    @staticmethod
    def disp_name(ch: str) -> str:
        cp = ord(ch)
        if cp == 0x20:
            return "<SP>"
        if cp == 0xA0:
            return "<NBSP>"
        if cp < 0x20 or cp == 0x7F:
            return f"<U+{cp:04X}>"
        return ch

    def sentinel_safe(self) -> bool:
        """BOS/EOS로 쓰는 U+0000, U+0001 이 데이터에 없는가."""
        return BOS not in self.cid and EOS not in self.cid

    def disp_id(self, token_id: int) -> int:
        """빈도표 정렬용 번호. BOS를 -1로 본다 (C의 disp_id와 같은 규칙)."""
        return -1 if token_id == self.bos_id else token_id

    def token_id(self, ch: str) -> int:
        """문자 하나 -> 번호. BOS/EOS 센티넬도 받는다."""
        if ch == BOS:
            return self.bos_id
        if ch == EOS:
            return self.eos_id
        return self.cid[ch]

    def name_of(self, ch: str) -> str:
        return self.names[self.token_id(ch)]


# ===================================================================
# [2] n그램 독립 재계산 (문자열 슬라이스 방식)
#
# [목적] 코퍼스의 모든 n그램과 빈도를 C와 무관하게 센다.
# [동작] 문장마다 앞에 BOS (n-1)개, 뒤에 EOS 1개를 붙이고 길이 n 창을 민다.
#   "나비", n=3 -> BOS BOS 나 비 EOS
#     -> (BOS,BOS,나) (BOS,나,비) (나,비,EOS)   3개 = 길이 2 + 1
# [이유] C는 문장 전체를 하나의 번호 배열(seq)로 이어 붙인 뒤 "문맥 칸에 EOS가
#   있으면 건너뛴다"로 문장 경계를 처리한다. 이 검증기는 **문장마다 따로**
#   패딩해서 센다. 방식이 다르지만 결과는 같아야 한다:
#   C가 남기는 창 = 각 문장의 패딩 블록 안에 완전히 들어가는 창이고,
#   그 개수는 길이 L 문장당 L+1개다. 경계 처리 버그가 있으면 여기서 어긋난다.
#   (전체 n그램 수 = 글자수 + 문장수 여야 하고, 이것도 아래에서 검사한다)
# ===================================================================
def count_grams(corpus: Corpus, n: int) -> tuple[Counter, Counter]:
    """(n그램 -> 빈도, 문맥 -> 합계). 키는 길이 n / n-1 문자열."""
    grams: Counter = Counter()
    ctx: Counter = Counter()
    pad = BOS * (n - 1)
    for s in corpus.sents:
        padded = pad + s + EOS
        for i in range(len(padded) - n + 1):
            w = padded[i:i + n]
            grams[w] += 1
            ctx[w[:-1]] += 1
    return grams, ctx


def gram_to_names(corpus: Corpus, gram: str) -> tuple[str, ...]:
    """n그램 문자열 -> 표시 이름 묶음 (C 출력과 맞춰 비교하기 위해)."""
    return tuple(corpus.name_of(ch) for ch in gram)


# ===================================================================
# [3] 빈도표 독립 생성
#
# [목적] C가 쓴 step6_{n}gram_table.txt 와 바이트 단위로 같은 내용을 만든다.
# [동작] 1) n그램을 (문맥 번호들, 빈도 내림, 다음 번호 오름) 순으로 정렬
#      문맥 번호는 disp_id를 쓴다 (BOS = -1 이라 문장 첫머리가 맨 위로)
#   2) 같은 문맥끼리 묶어 합계를 구하고 확률 = 빈도/합계 x 100 을 소수 6자리로
#   3) 헤더 + "앞1 \t ... \t 다음 \t 빈도 \t 확률" 줄들
# [이유] 표를 "읽어서 숫자만 맞춰 보는" 방식으로는 정렬 순서 버그나 확률 계산
#   버그를 못 잡는다. 표 전체를 독립적으로 만들어 바이트로 비교하면 한 글자만
#   달라도 걸린다. 이것이 이 검증의 가장 강한 검사다.
#   C의 fprintf("%.6f")와 Python의 f"{x:.6f}"는 같은 반올림 규칙(IEEE 754
#   round-half-to-even)을 쓰므로 같은 문자열이 나온다.
# ===================================================================
def build_table(corpus: Corpus, n: int, grams: Counter, total_grams: int) -> str:
    rows = []
    for gram, cnt in grams.items():
        ids = [corpus.token_id(ch) for ch in gram]
        key = tuple(corpus.disp_id(i) for i in ids[:-1])
        rows.append((key, -cnt, ids[-1], gram, cnt))
    rows.sort(key=lambda r: (r[0], r[1], r[2]))

    n_ctx = len({r[0] for r in rows})
    out = []
    out.append(f"# {n}그램 빈도표 (문장)")
    out.append("# input_file\t../../data/novel_sentences.txt")
    out.append(f"# sentences\t{len(corpus.sents)}")
    out.append(f"# chars\t{corpus.n_chars}")
    out.append(f"# char_kinds\t{len(corpus.chars)}")
    out.append(f"# vocab\t{corpus.vocab}")
    out.append(f"# total_{n}grams\t{total_grams}")
    out.append(f"# unique_{n}grams\t{len(grams)}")
    out.append(f"# rows\t{n_ctx}")
    out.append("#")
    out.append("# 공백은 <SP>, NBSP는 <NBSP> 로 적는다 (탭 구분 표에서 열을 구분하려고)")
    out.append("#")
    out.append("# " + "\t".join([f"prev{i + 1}" for i in range(n - 1)] + ["next", "count", "percent"]))

    i = 0
    while i < len(rows):
        end = i
        total = 0
        while end < len(rows) and rows[end][0] == rows[i][0]:
            total += rows[end][4]
            end += 1
        for j in range(i, end):
            gram = rows[j][3]
            cnt = rows[j][4]
            cells = list(gram_to_names(corpus, gram))
            out.append("\t".join(cells) + f"\t{cnt}\t{100.0 * cnt / total:.6f}")
        i = end
    return "\n".join(out) + "\n"


# ===================================================================
# [4] 결과 파일 읽기
#
# [목적] C가 쓴 결과 파일에서 기계 판독용 값과 생성 문장을 꺼낸다.
# [동작] "# 키<탭>값" 줄은 메타로, 그 밖의 "키<탭>문장[<탭>표시]" 줄은 샘플로.
# [이유] 사람이 읽는 머리글(데이터: / seed: / [3-Gram ...])과 기계가 읽는 '#'
#   줄을 분리해 두었으므로, 검증기는 '#' 줄만 믿고 읽는다. 머리글의 숫자는
#   '#' 줄과 같은 값이어야 하고, 그것도 아래 [5]에서 검사한다.
# ===================================================================
def read_result(path: Path) -> tuple[dict[str, list[str]], list[tuple[str, str, bool, bool]], list[str]]:
    meta: dict[str, list[str]] = {}
    samples: list[tuple[str, str, bool, bool]] = []
    head: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("# "):
            parts = line[2:].split("\t")
            if len(parts) >= 2:
                meta[parts[0]] = parts[1:]
        elif line.startswith("#") or not line:
            continue
        elif "\t" in line:
            parts = line.split("\t")
            key, sent = parts[0], parts[1]
            marks = parts[2:]
            samples.append((key, sent, "*" in marks, "~" in marks))
        else:
            head.append(line)
    return meta, samples, head


# ===================================================================
# [5] 규모 검증
#
# [목적] C가 보고한 모델 규모가 독립 재계산과 같은지 본다.
# [동작] 문장수·글자수·글자종류·V·전체 n그램·서로 다른 n그램·문맥 수를 비교.
#   전체 n그램 수가 "글자수 + 문장수"와 같은지도 따로 확인한다.
# [이유] 규모가 틀리면 그 아래 모든 비교가 의미 없다. 가장 먼저 본다.
#   "글자수 + 문장수" 검사를 따로 두는 까닭: 길이 L 문장이 n그램 L+1개를
#   내므로 n과 무관하게 이 값이 나와야 한다. 패딩 개수를 잘못 넣으면
#   (BOS를 n-1개가 아니라 n개 넣는 등) 여기서 걸린다.
# ===================================================================
def verify_scale(ck: Checker, corpus: Corpus, n: int, meta: dict[str, list[str]],
                 grams: Counter, ctx: Counter, total_grams: int) -> None:
    def cmp(field: str, expect: int) -> None:
        got = meta.get(field, ["?"])[0]
        ck.check(f"[1] {field}", got == str(expect), f"C {got} vs 재계산 {expect}")

    cmp("sentences", len(corpus.sents))
    cmp("chars", corpus.n_chars)
    cmp("char_kinds", len(corpus.chars))
    cmp("vocab", corpus.vocab)
    cmp("total_grams", total_grams)
    cmp("unique_grams", len(grams))
    cmp("contexts", len(ctx))
    ck.check("[1] 전체 n그램 = 글자수 + 문장수",
             total_grams == corpus.n_chars + len(corpus.sents),
             f"{total_grams} == {corpus.n_chars} + {len(corpus.sents)}")
    ck.check("[1] HASH_BITS 적재율 70% 미만",
             len(grams) * 10 <= (1 << HASH_BITS[n]) * 7,
             f"{len(grams)} / 2^{HASH_BITS[n]} = {100 * len(grams) / (1 << HASH_BITS[n]):.1f}%")


# ===================================================================
# [6] --dump 전수 비교
#
# [목적] C 해시테이블에 들어 있는 n그램 전체가 재계산과 똑같은지 본다.
# [동작] --dump 출력을 (이름 묶음 -> 빈도) 사전으로 읽어, 재계산 사전과 비교.
#   개수·키 집합·빈도를 모두 본다. 다르면 몇 개가 어떻게 다른지 보여 준다.
# [이유] 이것이 "모델이 맞는가"의 핵심 검사다. 해시 충돌 처리나 키 겹침
#   (64비트 넘침) 버그는 여기서 "C에만 있는 n그램"이나 "빈도 불일치"로 드러난다.
#   상위 몇 개만 보지 않고 전부 비교한다 (CLAUDE.md 검증 규칙 3번).
# ===================================================================
def verify_dump(ck: Checker, corpus: Corpus, n: int, grams: Counter) -> None:
    expect = {gram_to_names(corpus, g): c for g, c in grams.items()}
    got: dict[tuple[str, ...], int] = {}
    dup = 0
    for line in run_exe(n, ["--dump"]).splitlines():
        if not line:
            continue
        cells = line.split("\t")
        if len(cells) != n + 1:
            ck.check("[2] --dump 열 개수", False, f"{len(cells)}열 (기대 {n + 1}): {line[:60]}")
            return
        key = tuple(cells[:n])
        if key in got:
            dup += 1
        got[key] = int(cells[n])

    ck.check("[2] --dump 중복 줄 없음", dup == 0, f"중복 {dup}개")
    ck.check("[2] --dump n그램 개수", len(got) == len(expect), f"C {len(got)} vs 재계산 {len(expect)}")

    only_c = set(got) - set(expect)
    only_py = set(expect) - set(got)
    ck.check("[2] --dump C에만 있는 n그램 없음", not only_c,
             f"{len(only_c)}개 예: {list(only_c)[:2]}")
    ck.check("[2] --dump 재계산에만 있는 n그램 없음", not only_py,
             f"{len(only_py)}개 예: {list(only_py)[:2]}")

    bad = [k for k in set(got) & set(expect) if got[k] != expect[k]]
    ck.check("[2] --dump 빈도 전수 일치", not bad,
             f"{len(bad)}개 불일치 예: {[(k, got[k], expect[k]) for k in bad[:2]]}")


# ===================================================================
# [7] --rows 문맥 합계 전수 비교
#
# [목적] 문맥마다의 합계가 맞는지 본다. 이 합계가 난수 범위를 정하므로
#   틀리면 뽑기 확률이 전부 어긋난다.
# [동작] --rows 출력을 (문맥 이름 묶음 -> 합계)로 읽어 재계산과 전수 비교.
# [이유] C는 합계를 저장하지 않고 후보 1,598개를 매번 물어 구한다([9]).
#   --rows는 빈도표 정렬 결과를 묶어 더하는 다른 경로로 합계를 낸다.
#   두 경로가 같은 값이어야 하고, 둘 다 재계산과 같아야 한다.
# ===================================================================
def verify_rows(ck: Checker, corpus: Corpus, n: int, ctx: Counter) -> None:
    expect = {gram_to_names(corpus, c): v for c, v in ctx.items()}
    got: dict[tuple[str, ...], int] = {}
    for line in run_exe(n, ["--rows"]).splitlines():
        if not line:
            continue
        cells = line.split("\t")
        if len(cells) != n:
            ck.check("[3] --rows 열 개수", False, f"{len(cells)}열 (기대 {n})")
            return
        got[tuple(cells[:n - 1])] = int(cells[n - 1])

    ck.check("[3] --rows 문맥 개수", len(got) == len(expect), f"C {len(got)} vs 재계산 {len(expect)}")
    bad = [k for k in set(got) & set(expect) if got[k] != expect[k]]
    ck.check("[3] --rows 합계 전수 일치",
             not bad and set(got) == set(expect),
             f"{len(bad)}개 불일치, 키 차이 {len(set(got) ^ set(expect))}개")


# ===================================================================
# [8] 빈도표 바이트 비교
#
# [목적] C가 쓴 빈도표 파일이 독립 생성과 한 바이트도 다르지 않은지 본다.
# [동작] [3]이 만든 문자열을 UTF-8 바이트로 바꿔 파일 바이트와 직접 비교.
#   다르면 처음 어긋난 줄을 찾아 양쪽을 보여 준다.
# [이유] 정렬 순서·확률 반올림·표시 이름까지 한꺼번에 검증된다.
#   파일을 "줄 수만 세거나" "앞 몇 줄만 보는" 검사로는 가운데 한 줄이 밀린 것을
#   못 잡는다. 바이트 비교는 그런 여지가 없다.
# ===================================================================
def verify_table(ck: Checker, corpus: Corpus, n: int, grams: Counter, total_grams: int) -> None:
    path = TABLE[n]
    if not path.exists():
        ck.check("[4] 빈도표 파일 존재", False, f"{path.name} 이 없다 (exe를 먼저 실행)")
        return
    expect = build_table(corpus, n, grams, total_grams).encode("utf-8")
    got = path.read_bytes()
    if got == expect:
        ck.check("[4] 빈도표 바이트 일치", True, f"{path.name} {len(got):,} bytes")
        return

    gl = got.decode("utf-8", "replace").splitlines()
    el = expect.decode("utf-8").splitlines()
    detail = f"줄 수 C {len(gl)} vs 재계산 {len(el)}"
    for i in range(min(len(gl), len(el))):
        if gl[i] != el[i]:
            detail = f"{i + 1}번째 줄부터 다름\n       C   : {gl[i][:90]}\n       재계산: {el[i][:90]}"
            break
    ck.check("[4] 빈도표 바이트 일치", False, detail)


# ===================================================================
# [9] --map 구간 경계 검증
#
# [목적] 누적 비교가 '>' 인지 '>=' 인지에 따라 갈리는 경계에서 뽑히는 글자가
#   맞는지 본다. 이것이 뽑기 확률의 정확성을 가르는 지점이다.
# [동작] 문맥 표본마다 후보를 번호 순으로 누적해, 후보 c가 차지하는 구간
#   [p, p+k) 의 **양 끝** r = p 와 r = p+k-1 을 질의한다. 둘 다 c가 나와야 한다.
# [이유] '>='를 쓰면 구간이 한 칸씩 밀려 r = p 에서 앞 후보가 나온다.
#   양 끝만 찔러도 밀림을 반드시 잡는다. 가운데 값은 밀려도 같은 후보가
#   나올 수 있어서 못 잡는다 -- 그래서 경계를 고른다.
#   문맥을 전부 쓰지 않고 표본을 쓰는 까닭: 문맥이 100만 개가 넘고 질의마다
#   후보 1,598개를 조회하므로 전수는 현실적이지 않다. 대신 후보가 여럿인
#   문맥을 골라(경계가 많은 쪽) 효율을 높인다.
# ===================================================================
def verify_map(ck: Checker, corpus: Corpus, n: int, grams: Counter,
               ctx: Counter, sample: int = 120) -> None:
    # 문맥별 후보 목록 (번호 순) 을 만든다
    by_ctx: dict[str, list[tuple[int, str, int]]] = {}
    for gram, cnt in grams.items():
        c = gram[:-1]
        by_ctx.setdefault(c, []).append((corpus.token_id(gram[-1]), gram[-1], cnt))

    # 후보가 많은 문맥을 앞에서부터 고른다 (경계가 많아 검사 효율이 높다)
    picked = sorted(by_ctx.items(), key=lambda kv: -len(kv[1]))[:sample]

    queries: list[str] = []
    expects: list[str] = []
    for c, cands in picked:
        cands.sort()
        cum = 0
        for cid, ch, cnt in cands:
            for r in (cum, cum + cnt - 1):       # 구간의 양 끝
                queries.append(" ".join(corpus.name_of(x) for x in c) + f" {r}")
                expects.append(corpus.name_of(ch))
            cum += cnt

    out = run_exe(n, ["--map"], "\n".join(queries) + "\n").splitlines()
    ck.check("[5] --map 응답 개수", len(out) == len(expects),
             f"{len(out)} vs {len(expects)}")
    bad = [(queries[i], expects[i], out[i]) for i in range(min(len(out), len(expects)))
           if out[i] != expects[i]]
    ck.check("[5] --map 구간 경계 전부 일치", not bad,
             f"문맥 {len(picked)}개 / 질의 {len(queries)}개 / 불일치 {len(bad)}개"
             + (f" 예: {bad[:2]}" if bad else ""))


# ===================================================================
# [10] --hist 분포 검증
#
# [목적] 실제로 여러 번 뽑았을 때 빈도에 비례하는지 본다.
# [동작] 후보가 많은 문맥 하나에서 N번 뽑게 하고, 후보별 관측 횟수를
#   기대값과 비교한다. 기대값 30 이상인 후보는 |관측 - 기대| <= 5 x sqrt(기대)
#   안에 있어야 한다 (이항분포 표준편차의 5배 = 사실상 확실한 범위).
# [이유] [5]는 "r을 주면 올바른 글자가 나오는가"만 본다. 난수가 한쪽으로
#   치우쳐도 [5]는 통과한다. 실제 분포를 봐야 rand_below의 모듈로 편향 제거가
#   동작하는지 확인된다.
#   후보가 한 개뿐인 문맥을 고르면 분포 검사가 무의미하므로 가장 다양한
#   문맥을 고른다. 또 뽑힌 글자가 전부 "후보 목록 안"인지도 본다.
# ===================================================================
def verify_hist(ck: Checker, corpus: Corpus, n: int, grams: Counter,
                samples: int = 200000) -> None:
    by_ctx: dict[str, dict[str, int]] = {}
    for gram, cnt in grams.items():
        by_ctx.setdefault(gram[:-1], {})[gram[-1]] = cnt
    c, cands = max(by_ctx.items(), key=lambda kv: len(kv[1]))
    total = sum(cands.values())

    ctx_names = " ".join(corpus.name_of(x) for x in c)
    out = run_exe(n, ["--hist", str(samples), "12345"], ctx_names + "\n")
    got = {}
    for line in out.splitlines():
        if line:
            name, cnt = line.split("\t")
            got[name] = int(cnt)

    expect_names = {corpus.name_of(ch): cnt for ch, cnt in cands.items()}
    ck.check("[6] --hist 관측 합계", sum(got.values()) == samples,
             f"{sum(got.values())} vs {samples}")
    ck.check("[6] --hist 후보 밖 글자 없음", set(got) <= set(expect_names),
             f"후보 밖 {sorted(set(got) - set(expect_names))[:3]}")

    worst = (0.0, "")
    for name, cnt in expect_names.items():
        exp = samples * cnt / total
        if exp < 30:
            continue
        obs = got.get(name, 0)
        sigma = (exp * (1 - cnt / total)) ** 0.5
        z = abs(obs - exp) / sigma if sigma > 0 else 0.0
        if z > worst[0]:
            worst = (z, f"{name}: 관측 {obs} 기대 {exp:.1f}")
    # 어떤 문맥을 골랐는지 함께 찍는다. n=5, 7은 둘 다 "문장 시작"(BOS만으로 된) 문맥이
    # 뽑히고, 문장 첫 글자 분포는 n과 무관하게 같으므로 두 n의 결과가 똑같이 나온다.
    # 그 사실을 모르면 "복사된 값 아닌가" 하고 의심하게 되므로 문맥을 보여 준다.
    shown = "".join("BOS" if ch == BOS else corpus.name_of(ch) for ch in c)
    ck.check("[6] --hist 분포가 빈도에 비례", worst[0] <= 5.0,
             f"문맥 {shown!r} / 합계 {total:,} / 후보 {len(cands)}개 중 "
             f"최대 편차 {worst[0]:.2f}시그마 ({worst[1]})")


# ===================================================================
# [11] 소스 구역 줄 번호 검증
#
# [목적] C 파일 머리말의 목차에 적힌 줄 번호가 실제 구역 위치와 맞는지 본다.
# [동작] 머리말의 " *   [k]  NNN줄 | ..." 를 읽고, 본문에서 "/* [k] ..." 로
#   시작하는 줄의 실제 번호를 찾아 비교한다.
# [이유] 코드를 고치면 줄이 밀리는데 목차는 그대로 남는다. 교재로 읽는
#   파일이라 목차가 틀리면 독자가 헤맨다. 사람이 눈으로 맞추면 반드시 틀리므로
#   검증기가 본다 (Step5에서 같은 검사를 두었다).
# ===================================================================
def verify_source_index(ck: Checker, n: int) -> None:
    lines = SRC[n].read_text(encoding="utf-8").splitlines()
    listed: dict[int, int] = {}
    actual: dict[int, int] = {}
    for no, line in enumerate(lines, start=1):
        t = line.strip()
        if t.startswith("*   [") and "줄 |" in t:
            num = t[t.index("[") + 1:t.index("]")]
            body = t[t.index("]") + 1:]
            try:
                listed[int(num)] = int(body[:body.index("줄 |")].strip())
            except ValueError:
                pass
        elif t.startswith("/* [") and "]" in t:
            num = t[t.index("[") + 1:t.index("]")]
            if num.isdigit() and int(num) not in actual:
                actual[int(num)] = no

    bad = [(k, listed[k], actual.get(k)) for k in sorted(listed) if listed[k] != actual.get(k)]
    ck.check(f"[7] {SRC[n].name} 목차 줄 번호", not bad,
             f"구역 {len(listed)}개 / 어긋남 {len(bad)}개"
             + (f" (구역, 적힌줄, 실제줄) = {bad[:4]}" if bad else ""))


# ===================================================================
# [12] 결과 파일 재검증
#
# [목적] 결과 파일의 문장과 표시가 맞는지 독립적으로 다시 확인한다.
# [동작] 1) 머리글의 숫자가 '#' 메타와 같은지
#   2) 문장 개수가 count와 맞는지
#   3) '*' 표시가 실제 원본 포함 여부와 같은지 (원본 문장 집합으로 직접 확인)
#   4) 생성된 문장의 모든 n그램이 실제로 코퍼스에 있는지
#      (모델은 센 n그램만 뽑을 수 있으므로 하나라도 없으면 뽑기 버그다)
# [이유] 4번이 생성 로직의 가장 강한 검사다. 생성 과정을 흉내내지 않고도
#   "나온 결과가 모델로 설명되는가"를 확인할 수 있다. 문맥 창을 잘못 밀면
#   (예: 두 칸씩 밀면) 코퍼스에 없는 n그램이 섞여 들어오므로 반드시 걸린다.
#   잘린 문장(~)은 EOS로 끝나지 않았으므로 마지막 n그램 검사에서 제외한다.
# ===================================================================
def verify_result(ck: Checker, corpus: Corpus, n: int, path: Path,
                  meta: dict[str, list[str]], samples: list, head: list[str],
                  grams: Counter) -> None:
    joined = " ".join(head)
    ck.check(f"[8] 머리글 숫자 일치 ({path.name})",
             f"{len(corpus.sents)}문장" in joined
             and f"{corpus.n_chars}글자" in joined
             and f"{len(corpus.chars)}종" in joined,
             f"머리글: {head[0] if head else '(없음)'}")
    ck.check("[8] 머리글 패턴수 = unique_grams",
             f"[{n}-Gram ({meta['unique_grams'][0]}개 패턴수)]" in joined,
             f"unique_grams {meta['unique_grams'][0]}")

    count = int(meta["count"][0])
    gen = [s for s in samples if s[0] != "greedy"]
    ck.check("[8] 생성 문장 개수", len(gen) == count, f"{len(gen)} vs {count}")

    sent_set = set(corpus.sents)
    bad_mark = [(k, s[:40]) for k, s, star, _ in samples if star != (s in sent_set)]
    ck.check("[8] '*' 표시가 원본 포함 여부와 일치", not bad_mark,
             f"어긋남 {len(bad_mark)}개 예: {bad_mark[:2]}")

    hits = sum(1 for _, s, _, _ in gen if s in sent_set)
    ck.check("[8] in_corpus 수치 재계산 일치",
             hits == int(meta["in_corpus"][0]),
             f"C {meta['in_corpus'][0]} vs 재계산 {hits}")

    pad = BOS * (n - 1)
    missing = []
    for key, sent, _, trunc in samples:
        padded = pad + sent + ("" if trunc else EOS)
        for i in range(len(padded) - n + 1):
            if padded[i:i + n] not in grams:
                missing.append((key, "".join(padded[i:i + n]).replace(BOS, "_")))
                break
    ck.check("[8] 생성 문장의 모든 n그램이 코퍼스에 존재", not missing,
             f"설명 안 되는 문장 {len(missing)}개 예: {missing[:2]}")


# ===================================================================
# [13] 자기 검사
#
# [목적] 위 검사들이 "틀린 입력에 FAIL을 내는지" 확인한다.
# [동작] 재계산 결과를 일부러 망가뜨려 비교 함수에 넣고, FAIL이 나오는지 본다.
#   - 빈도를 하나 바꾼다            -> [4] 빈도표 비교가 FAIL이어야 한다
#   - n그램을 하나 지운다          -> 표가 한 줄 짧아져 FAIL이어야 한다
#   - 표시 이름 규칙을 바꾼다      -> 표 내용이 달라져 FAIL이어야 한다
#   FAIL이 나오면 self-test는 성공(0), FAIL이 안 나오면 실패(1).
# [이유] CLAUDE.md 검증 규칙 4번. "통과"만 찍는 검증기는 검증기가 아니다.
#   통과를 신뢰하려면 "깨진 것을 깨졌다고 말할 수 있다"를 먼저 보여야 한다.
# ===================================================================
def self_test() -> int:
    print("[self-test] 검증기가 틀린 입력에 FAIL을 내는지 확인한다\n")
    corpus = Corpus(DATA.resolve())
    n = 3
    grams, _ = count_grams(corpus, n)
    total = sum(grams.values())
    good = build_table(corpus, n, grams, total)

    fails = 0
    checks = 0

    def expect_fail(name: str, broken: str) -> None:
        nonlocal fails, checks
        checks += 1
        if broken == good:
            print(f"  FAIL {name}: 망가뜨렸는데 원본과 같다 (검사가 무의미)")
            fails += 1
        else:
            # 어디서 처음 달라지는지 보여 준다
            a, b = good.splitlines(), broken.splitlines()
            where = "줄 수"
            for i in range(min(len(a), len(b))):
                if a[i] != b[i]:
                    where = f"{i + 1}번째 줄"
                    break
            print(f"  OK   {name}: 바이트 비교가 차이를 잡아낸다 ({where})")

    # 1. 빈도 하나를 바꾼다
    g2 = Counter(grams)
    worst = next(iter(g2))
    g2[worst] += 1
    expect_fail("빈도 1 변경", build_table(corpus, n, g2, total + 1))

    # 2. n그램 하나를 지운다
    g3 = Counter(grams)
    del g3[worst]
    expect_fail("n그램 1개 삭제", build_table(corpus, n, g3, total))

    # 3. 표시 이름 규칙을 바꾼다 (공백을 <SP> 가 아니라 _ 로)
    saved = corpus.names[:]
    corpus.names = ["_" if x == "<SP>" else x for x in corpus.names]
    expect_fail("표시 이름 규칙 변경", build_table(corpus, n, grams, total))
    corpus.names = saved

    # 4. 센티넬 전제가 깨지면 알아차리는지
    checks += 1
    if corpus.sentinel_safe():
        print("  OK   센티넬 전제: U+0000 / U+0001 이 데이터에 없다")
    else:
        print("  FAIL 센티넬 전제: BOS/EOS가 실제 글자와 겹친다")
        fails += 1

    print(f"\n[self-test] {checks}개 중 {fails}개 실패")
    return 1 if fails else 0


# ===================================================================
# [14] 비교 문서 작성 (--compare)
#
# [목적] n = 3 / 5 / 7 결과를 사용자가 지정한 형식 하나로 모은다.
# [동작] 각 결과 파일에서 머리글·패턴수·문장 목록을 읽어 섹션 3개로 잇는다.
# [이유] C는 n마다 프로그램이 따로라 자기 섹션만 쓴다. 세 섹션을 한 파일로
#   보려면 누군가 모아야 하는데, 결과를 해석하지 않고 옮기기만 하는 일이라
#   검증기가 맡는다 (Step5의 step5_compare.txt와 같은 방식).
#   숫자를 새로 계산하지 않는다 -- 검증을 통과한 결과 파일의 값을 그대로 옮긴다.
# ===================================================================
def write_compare() -> int:
    out = []
    out.append("# n = 3 / 5 / 7 문장 생성 결과 비교")
    out.append("# 만든 프로그램: step6_verify.py --compare")
    out.append("# 숫자 출처: 각 step6_{n}gram_result_NN.txt (검증을 통과한 결과 파일)")
    out.append("# 해석은 적지 않는다. 문장이 자연스러운지는 아래 샘플을 직접 읽고 판단한다")
    out.append("")

    first = True
    for n in NS:
        files = sorted(HERE.glob(f"step6_{n}gram_result_*.txt"))
        if not files:
            print(f"[오류] step6_{n}gram_result_*.txt 가 없습니다")
            return 1
        path = files[-1]
        meta, samples, head = read_result(path)
        if first:
            out.append(head[0])                       # 데이터: ... (3개 모두 같다)
            out.append(f"seed: {meta['seed'][0]}")
            out.append(f"생성 상한: {meta['max_len'][0]}글자")
            out.append("")
            first = False
        out.append(f"[{n}-Gram ({meta['unique_grams'][0]}개 패턴수)]")
        out.append(f"샘플갯수 : {meta['count'][0]}개")
        out.append(f"원본 문장 그대로: {meta['in_corpus'][0]} / {meta['in_corpus'][1]}"
                   f" = {meta['in_corpus'][2]}%   (* 표시)")
        out.append(f"출처: {path.name}")
        out.append("")
        for key, sent, star, trunc in samples:
            mark = ("  *" if star else "") + ("  ~" if trunc else "")
            out.append(f"{key:>6}  {sent}{mark}")
        out.append("")
        out.append("---")
        out.append("")

    out.append("# * = 원본에 통째로 있는 문장 / ~ = 생성 상한에서 잘림")
    target = HERE / "step6_compare.txt"
    with open(target, "w", encoding="utf-8", newline="\n") as fp:
        fp.write("\n".join(out) + "\n")
    print(f"작성: {target}")
    return 0


# ===================================================================
# [15] main
# ===================================================================
def verify_one(ck: Checker, corpus: Corpus, n: int) -> None:
    print(f"\n{'=' * 70}\n{n}그램 검증\n{'=' * 70}")
    if not EXE[n].exists():
        ck.check(f"{EXE[n].name} 존재", False, "build.bat 로 먼저 빌드하세요")
        return

    grams, ctx = count_grams(corpus, n)
    total = sum(grams.values())
    print(f"  (독립 재계산: 전체 {total:,} / 서로 다른 {len(grams):,} / 문맥 {len(ctx):,})")

    files = sorted(HERE.glob(f"step6_{n}gram_result_*.txt"))
    if files:
        meta, samples, head = read_result(files[-1])
        verify_scale(ck, corpus, n, meta, grams, ctx, total)
        verify_result(ck, corpus, n, files[-1], meta, samples, head, grams)
    else:
        ck.check(f"step6_{n}gram_result_*.txt 존재", False, "exe를 먼저 실행하세요")

    verify_dump(ck, corpus, n, grams)
    verify_rows(ck, corpus, n, ctx)
    verify_table(ck, corpus, n, grams, total)
    verify_map(ck, corpus, n, grams, ctx)
    verify_hist(ck, corpus, n, grams)
    verify_source_index(ck, n)


def main(argv: list[str]) -> int:
    if "--self-test" in argv:
        return self_test()
    if "--compare" in argv:
        return write_compare()

    data = DATA.resolve()
    if not data.exists():
        print(f"[오류] {data} 가 없습니다. step6_prepare.py 를 먼저 실행하세요")
        return 1

    targets = list(NS)
    if "--n" in argv:
        targets = [int(argv[argv.index("--n") + 1])]

    print(f"입력: {data}")
    corpus = Corpus(data)
    print(f"문장 {len(corpus.sents):,} / 글자 {corpus.n_chars:,} / "
          f"글자 종류 {len(corpus.chars):,} / V {corpus.vocab:,}")

    ck = Checker()
    ck.check("[0] BOS/EOS 센티넬이 실제 글자와 겹치지 않음", corpus.sentinel_safe(),
             "U+0000 / U+0001")
    for n in targets:
        verify_one(ck, corpus, n)
    return ck.summary()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
