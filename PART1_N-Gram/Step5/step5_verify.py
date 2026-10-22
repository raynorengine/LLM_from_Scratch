"""
step5_verify.py - [STEP 5] n = 2 ~ 6 다섯 프로그램(step5_bigram / _trigram / _4gram / _5gram / _6gram)을
독립 검증하고 사전 일치율을 n별로 비교한다.

===== 무엇을 검증하나 (다섯 프로그램 각각) =====
  [1] n그램 빈도 전수 비교 : 서로 다른 n그램 전부가 Python 재계산과 같은가 (--dump)
                             (171,425 / 654,039 / 976,877 / 1,105,860 / 1,141,426개)
  [2] 문맥 합계            : 문맥마다 합계가 "다른 방법으로 센 값"과 같은가 (--rows)
  [3] r -> 글자 대응        : 문맥 1,000개(+ [4]의 문맥)의 모든 후보 구간 시작·끝 r
                             + 무작위 r 1,000개에서 맞는 글자가 나오나 (--map)
  [4] 분포 (카이제곱)       : 몇 개 문맥에서 1,000번씩 뽑은 횟수가 실제 확률과 맞나 (--hist)
  [5] 생성 결과 파일        : 탐욕적 결과, 모든 단어의 n그램 실재, 표시(*, ~),
                             사전 일치율·길이별 수치를 단어 목록에서 다시 계산해 대조
                             (임시 폴더 실행 1회 + 폴더에 저장된 result_NN.txt 전부)
  [6] 빈도표 파일          : 형식·순서·빈도·확률 + 폴더의 빈도표가 프로그램 출력과 바이트 단위로 같은가
                             n=2·3은 Step4 빈도표(../Step4/step4_*_table.txt)와도 바이트 단위로 같아야 한다
                             (같은 모델이므로. Step5 파일이 Step4를 복사해 이름만 바꾼 것임을 확인)
  [7] 목차·주석            : C 파일 맨 위 목차의 줄 번호가 실제 구역 위치와 같은가,
                             모든 구역 머리 주석에 [목적] [동작] [이유]가 있는가
마지막에 같은 seed로 돌린 다섯 프로그램의 사전 일치율 비교표를 출력한다 (검증기가 다시 계산한 값).

===== 독립성 (CLAUDE.md "검증 규칙") =====
- 기준값은 전부 원본 words.txt에서 Python으로 처음부터 다시 센다. C 출력을 기준으로 쓰지 않는다.
- C는 64비트 키 해시(6그램은 문맥 + 다음 글자 두 칸 키)에 후보 2,452개를 차례로 물어 누적하지만,
  여기서는 튜플을 키로 쓰는 Counter로 세고 r -> 글자 정답은 "구간 [시작, 끝]을 직접 나열"해서 만든다.
  튜플 키라서 n이 커져도 넘치는 일이 없다 (C의 64비트 한계와 무관한 방법).
- [2]의 기준은 n그램을 더한 값이 아니라 "문맥 자체가 나온 횟수"를 따로 센 값이다.
- 일치율은 C가 쓴 숫자를 믿지 않고, 결과 파일의 단어 목록과 words.txt로 다시 센다.
- 강의 수치는 기준값으로 쓰지 않는다. --compare 문서에 "강의 보고값"으로 나란히 적을 뿐이다
  (사용자 지시 2026-10-22: 강의 결과는 목표가 아니다).
- C 프로그램은 subprocess로 실행하고 출력만 받는다. 결과는 임시 폴더에 쓰게 해서
  Step5 폴더의 번호 붙은 결과 파일과 빈도표를 건드리지 않는다.

===== 사용법 =====
  python step5_verify.py               # 검증
  python step5_verify.py --self-test   # 망가진 출력을 흉내 낸 입력에 FAIL을 내는지 확인 (6그램 대상)
  python step5_verify.py --compare     # 같은 seed의 다섯 결과 파일을 검증한 뒤
                                       # 사전 일치율 비교 문서 step5_compare.txt 를 만든다
"""

from __future__ import annotations

import bisect
import collections
import math
import pathlib
import random
import re
import subprocess
import sys
import tempfile
import unicodedata

HERE = pathlib.Path(__file__).parent
SOURCE = HERE.parent.parent / "data" / "korean_dictionary" / "words.txt"

BOS, EOS = "BOS", "EOS"
MAX_LEN = 20                      # C의 MAX_LEN과 같은 규칙 (단어 최대 길이)
LEN_BUCKETS = 10                  # 길이별 일치율: 1 ~ 9, 10 이상은 한 칸
# [3][4]의 표본 크기 (2026-10-22 사용자 지시: 1,000개)
# C가 CSR 없이 해시에 후보 2,452개를 차례로 물어 뽑으므로 질의 하나가 수천 번의 해시 조회다.
# 트라이그램의 모든 구간 경계(약 130만 질의)를 다 물으면 약 100초가 걸려 문맥을 표본으로 고른다.
MAP_CONTEXTS = 1_000              # [3] 구간 경계를 전부 물어볼 문맥 수 (무작위 + [4]의 문맥은 항상 포함)
MAP_RANDOM_COUNT = 1_000          # [3] 경계값 외에 넣는 무작위 질의 수
MAP_SEED = 7
HIST_SAMPLES = 1_000              # [4] 문맥마다 뽑는 횟수
HIST_SEED = 12345
P_VALUE_THRESHOLD = 1e-3          # [4] p < 0.001 이면 "분포가 다르다"
GEN_COUNT, GEN_SEED = 1000, 2026  # [5] 임시 실행 인자 (다섯 프로그램 같은 seed)
RATE_TOL = 0.05 + 1e-9            # 일치율은 소수 1자리로 쓰므로 반올림 오차 0.05까지 허용
SHARE_TOL = 0.005 + 1e-9          # 사전 길이 비율은 소수 2자리

# 프로그램별 설정. n = n그램의 n, 문맥 길이 = n - 1
# hist: [4]에서 분포를 볼 문맥. 단어 첫머리(BOS만) / '사'로 시작 / '사회'로 시작 (모두 실제로 있는 문맥)
STEP4 = HERE.parent / "Step4"
PROGRAMS = {
    "bigram": {
        "n": 2,
        "exe": HERE / "step5_bigram.exe",
        "src": HERE / "step5_bigram.c",
        "table": HERE / "step5_bigram_table.txt",
        "result_glob": "step5_bigram_result_*.txt",
        "table_keys": ("total_pairs", "unique_pairs"),
        "hist": [(BOS,), ("사",), ("다",)],
        "step4_table": STEP4 / "step4_bigram_table.txt",
    },
    "trigram": {
        "n": 3,
        "exe": HERE / "step5_trigram.exe",
        "src": HERE / "step5_trigram.c",
        "table": HERE / "step5_trigram_table.txt",
        "result_glob": "step5_trigram_result_*.txt",
        "table_keys": ("total_trigrams", "unique_trigrams"),
        "hist": [(BOS, BOS), (BOS, "사"), ("자", "동")],
        "step4_table": STEP4 / "step4_trigram_table.txt",
    },
}
for _n in (4, 5, 6):
    _name = f"{_n}gram"
    PROGRAMS[_name] = {
        "n": _n,
        "exe": HERE / f"step5_{_name}.exe",
        "src": HERE / f"step5_{_name}.c",
        "table": HERE / f"step5_{_name}_table.txt",
        "result_glob": f"step5_{_name}_result_*.txt",
        "table_keys": (f"total_{_n}grams", f"unique_{_n}grams"),
        "hist": [(BOS,) * (_n - 1), (BOS,) * (_n - 2) + ("사",), (BOS,) * (_n - 3) + ("사", "회")],
    }
LABEL = {"bigram": "n=2", "trigram": "n=3", "4gram": "n=4", "5gram": "n=5", "6gram": "n=6"}

Ctx = tuple[str, ...]


# ---------------------------------------------------------------------------
# 1. 원본에서 독립 재계산
#
# [목적] C와 비교할 기준 데이터를 words.txt에서 직접 만든다.
# [동작]
#   - 단어마다 [BOS x (n-1), 글자..., EOS]로 감싸 길이 n 창을 Counter로 센다
#     트라이그램 "나비" -> [BOS BOS 나 비 EOS] -> (BOS,BOS,나) (BOS,나,비) (나,비,EOS)
#   - 글자 번호는 C와 같은 "규칙"(코드포인트 오름차순, 그 뒤 BOS, EOS)으로 정한다.
#     구현을 베낀 것이 아니라 프로그램이 지켜야 할 사양이다. 후보 순서가 이 규칙을
#     따라야 같은 r에 같은 글자가 나오므로 [3]의 정답을 만들려면 사양을 알아야 한다.
#   - 문맥마다 후보를 번호 순으로 나열하고 각 후보의 r 구간 [시작, 끝]을 만든다
#     문맥 (가,가): 대(3), 소(1) -> 대 = [0, 2], 소 = [3, 3]
# [이유] 단어 단위로 직접 감싸서 세므로 "단어 사이를 넘는 n그램"이 생길 수 없다.
#   C는 모든 단어를 이어 붙인 seq에서 경계를 건너뛰는 방식이라 세는 방법이 다르다.
# ---------------------------------------------------------------------------
class Reference:
    """words.txt에서 직접 만든 기준 데이터 (n그램 하나)."""

    def __init__(self, words: list[str], n: int) -> None:
        self.n = n
        self.words = words
        self.word_set = set(words)

        chars = sorted({c for w in words for c in w})
        self.order = {c: i for i, c in enumerate(chars)}
        self.order[BOS] = len(chars)
        self.order[EOS] = len(chars) + 1
        self.n_chars = len(chars)

        # n그램 세기 (단어 안에서만)
        self.grams: collections.Counter[tuple[str, ...]] = collections.Counter()
        for w in words:
            tokens = [BOS] * (n - 1) + list(w) + [EOS]
            self.grams.update(zip(*(tokens[i:] for i in range(n))))

        # [2]의 기준: 문맥(길이 n-1)이 "뒤에 무언가가 오는 자리"에 나온 횟수.
        # 마지막 EOS를 뺀 [BOS x (n-1), 글자...]에서 길이 n-1 창을 센다 (n그램 합계와 다른 방법)
        self.ctx_count: collections.Counter[Ctx] = collections.Counter()
        for w in words:
            tokens = [BOS] * (n - 1) + list(w)
            self.ctx_count.update(zip(*(tokens[i:] for i in range(n - 1))))

        # 문맥마다 후보(번호 순)와 r 구간
        rows: dict[Ctx, list[tuple[str, int]]] = collections.defaultdict(list)
        for g, c in self.grams.items():
            rows[g[:-1]].append((g[-1], c))
        self.rows: dict[Ctx, list[tuple[str, int]]] = {}
        self.row_total: dict[Ctx, int] = {}
        self.intervals: dict[Ctx, list[tuple[str, int, int]]] = {}   # (후보, 시작 r, 끝 r)
        for ctx, cands in rows.items():
            cands.sort(key=lambda bc: self.order[bc[0]])
            self.rows[ctx] = cands
            start, spans = 0, []
            for b, c in cands:
                spans.append((b, start, start + c - 1))
                start += c
            self.intervals[ctx] = spans
            self.row_total[ctx] = start

    def greedy_word(self) -> tuple[str, bool]:
        """탐욕적 디코딩의 정답: 문맥마다 빈도 최대(동률이면 번호 작은 후보). (단어, 잘림 여부)"""
        ctx, out = (BOS,) * (self.n - 1), []
        while True:
            best = max(self.rows[ctx], key=lambda bc: (bc[1], -self.order[bc[0]]))[0]
            if best == EOS:
                return "".join(out), False
            if len(out) == MAX_LEN:
                return "".join(out), True
            out.append(best)
            ctx = (*ctx[1:], best)   # 문맥 창을 한 칸 민다

    def word_grams_ok(self, word: str, truncated: bool) -> bool:
        """단어의 모든 n그램(BOS 패딩 포함, 잘리지 않았으면 EOS까지)이 사전에 실제로 있었나."""
        tokens = [BOS] * (self.n - 1) + list(word) + ([] if truncated else [EOS])
        return all(g in self.grams for g in zip(*(tokens[i:] for i in range(self.n))))

    def display_key(self, ctx: Ctx) -> tuple[int, ...]:
        """빈도표의 문맥 순서: BOS를 가장 작은 값(-1)으로 보고 번호 순."""
        return tuple(-1 if t == BOS else self.order[t] for t in ctx)


def load_words(source: pathlib.Path) -> list[str]:
    text = source.read_bytes().decode("utf-8")
    return [w.rstrip("\r") for w in text.split("\n") if w.rstrip("\r")]


# ---------------------------------------------------------------------------
# 2. 구현 실행 (출력만 받는다)
#
# [목적] C 프로그램을 실행해 표준출력을 받아 Python 자료형으로 바꾼다.
# [동작] 문맥 글자는 명령줄 인자가 아니라 표준입력으로 준다.
# [이유] Windows argv는 CP949라 UTF-8 한글이 깨진다 (Step3에서 겪은 문제).
# ---------------------------------------------------------------------------
def run(exe: pathlib.Path, args: list[str], stdin: str | None = None) -> str:
    proc = subprocess.run([str(exe), *args], input=stdin, capture_output=True,
                          text=True, encoding="utf-8", cwd=HERE)
    if proc.returncode != 0:
        raise RuntimeError(f"실행 실패 ({proc.returncode}): {exe.name} {' '.join(args)}\n{proc.stderr}")
    return proc.stdout


def get_dump(exe: pathlib.Path) -> dict[tuple[str, ...], int]:
    out = {}
    for line in run(exe, ["--dump"]).splitlines():
        *gram, c = line.split("\t")
        out[tuple(gram)] = int(c)
    return out


def get_rows(exe: pathlib.Path) -> dict[Ctx, int]:
    out = {}
    for line in run(exe, ["--rows"]).splitlines():
        *ctx, c = line.split("\t")
        out[tuple(ctx)] = int(c)
    return out


def build_map_queries(ref: Reference, must: list[Ctx]) -> list[tuple[Ctx, int, str]]:
    """(문맥, r, 정답 글자). 고른 문맥들의 모든 구간 시작·끝 + 무작위 r.

    한 칸 어긋남('>' 대신 '>=')은 구간 경계에서만 드러나므로 고른 문맥의 경계는 전부 넣는다.
    문맥은 MAP_CONTEXTS개를 무작위로 고르고 must(= [4]의 문맥, 후보가 많은 BOS 문맥 포함)를 더한다.
    무작위 r의 정답은 bisect로 찾는다 (C의 순차 누적 코드와는 별개의 표준 라이브러리).
    """
    rng = random.Random(MAP_SEED)
    keys = sorted(ref.intervals)
    chosen = set(rng.sample(keys, min(MAP_CONTEXTS, len(keys)))) | set(must)
    queries = [(ctx, r, b) for ctx in sorted(chosen)
               for b, lo, hi in ref.intervals[ctx] for r in (lo, hi)]
    for _ in range(MAP_RANDOM_COUNT):
        ctx = rng.choice(keys)
        spans = ref.intervals[ctx]
        r = rng.randrange(ref.row_total[ctx])
        ends = [hi for _, _, hi in spans]
        queries.append((ctx, r, spans[bisect.bisect_left(ends, r)][0]))
    return queries


def get_map(exe: pathlib.Path, queries: list[tuple[Ctx, int, str]]) -> list[str]:
    stdin = "".join(f"{' '.join(ctx)} {r}\n" for ctx, r, _ in queries)
    return run(exe, ["--map"], stdin).split()


def get_hist(exe: pathlib.Path, ctx: Ctx) -> dict[str, int]:
    out = run(exe, ["--hist", str(HIST_SAMPLES), str(HIST_SEED)], " ".join(ctx) + "\n")
    return {b: int(c) for b, c in (line.split("\t") for line in out.splitlines())}


def get_generated(exe: pathlib.Path, tmp: pathlib.Path) -> tuple[str, bytes]:
    """임시 폴더에 결과·빈도표를 쓰게 하고 읽어 온다. 빈도표는 바이트 비교용으로 bytes."""
    result, table = tmp / f"{exe.stem}_result.txt", tmp / f"{exe.stem}_table.txt"
    run(exe, [str(GEN_COUNT), str(GEN_SEED), str(result), str(table)])
    return result.read_bytes().decode("utf-8"), table.read_bytes()


# ---------------------------------------------------------------------------
# 3. 판정 함수 (실패 메시지 목록을 돌려준다. 비어 있으면 통과)
# ---------------------------------------------------------------------------
def check_dump(ref: Reference, dump: dict[tuple[str, ...], int]) -> list[str]:
    """[1] n그램 빈도 전수 비교: 빠진 것 / 없어야 할 것 / 빈도가 다른 것."""
    missing = [k for k in ref.grams if k not in dump]
    extra = [k for k in dump if k not in ref.grams]
    wrong = [k for k in ref.grams if k in dump and dump[k] != ref.grams[k]]
    fails = []
    if missing:
        fails.append(f"빠진 n그램 {len(missing)}개 (예: {missing[0]})")
    if extra:
        fails.append(f"없어야 할 n그램 {len(extra)}개 (예: {extra[0]} = {dump[extra[0]]})")
    if wrong:
        k = wrong[0]
        fails.append(f"빈도 불일치 {len(wrong)}개 (예: {k} {dump[k]} != {ref.grams[k]})")
    return fails


def check_rows(ref: Reference, rows: dict[Ctx, int]) -> list[str]:
    """[2] 문맥 합계 = 문맥이 나온 횟수 (다른 방법으로 센 값)."""
    fails = []
    if set(rows) != set(ref.ctx_count):
        fails.append(f"문맥 집합이 다름 ({len(rows):,}개, 기대 {len(ref.ctx_count):,}개)")
    wrong = [c for c in ref.ctx_count if rows.get(c) != ref.ctx_count[c]]
    if wrong:
        c = wrong[0]
        fails.append(f"합계 불일치 {len(wrong)}개 (예: {c} {rows.get(c)} != {ref.ctx_count[c]})")
    return fails


def check_map(queries: list[tuple[Ctx, int, str]], answers: list[str]) -> list[str]:
    """[3] r -> 글자 대응."""
    if len(answers) != len(queries):
        return [f"응답 수 {len(answers)} != 질의 수 {len(queries)}"]
    wrong = [(q, got) for q, got in zip(queries, answers) if got != q[2]]
    if wrong:
        (ctx, r, exp), got = wrong[0]
        return [f"{len(wrong)}건 불일치 (예: 문맥 {ctx} r={r} -> '{got}', 정답 '{exp}')"]
    return []


def chi_square_p(stat: float, df: int) -> float:
    """카이제곱 분포의 위쪽 꼬리 확률. Wilson-Hilferty 근사 (자유도가 클 때 충분히 정확)."""
    if df <= 0:
        return 1.0
    z = ((stat / df) ** (1 / 3) - (1 - 2 / (9 * df))) / math.sqrt(2 / (9 * df))
    return 0.5 * math.erfc(z / math.sqrt(2))


def check_hist(ref: Reference, ctx: Ctx, hist: dict[str, int]) -> tuple[list[str], str]:
    """[4] 한 문맥의 분포. 기대 빈도 5 미만 후보는 하나로 묶는다 (카이제곱 근사의 전제)."""
    fails = []
    n = sum(hist.values())
    if n != HIST_SAMPLES:
        fails.append(f"샘플 합계 {n} != {HIST_SAMPLES}")
    cands = dict(ref.rows[ctx])
    unknown = set(hist) - set(cands)
    if unknown:
        fails.append(f"이 문맥에 없는 후보가 뽑힘: {sorted(unknown)[:3]}")
    if n == 0:
        return fails + ["샘플 없음"], ""
    total = ref.row_total[ctx]
    stat, bins, pooled_obs, pooled_exp = 0.0, 0, 0, 0.0
    for b, c in cands.items():
        exp, obs = c / total * n, hist.get(b, 0)
        if exp < 5:
            pooled_obs += obs
            pooled_exp += exp
        else:
            stat += (obs - exp) ** 2 / exp
            bins += 1
    if pooled_exp > 0:
        stat += (pooled_obs - pooled_exp) ** 2 / pooled_exp
        bins += 1
    df = bins - 1
    p = chi_square_p(stat, df)
    detail = f"카이제곱={stat:,.1f} 자유도={df} p={p:.3g}"
    if p < P_VALUE_THRESHOLD:
        fails.append(f"분포가 실제 확률과 다름 ({detail})")
    return fails, detail


def parse_header(text: str) -> dict[str, str]:
    header = {}
    for line in text.split("\n"):
        if line.startswith("# ") and "\t" in line:
            k, _, v = line[2:].partition("\t")
            header.setdefault(k, v)
    return header


# ---------------------------------------------------------------------------
# [5] 생성 결과 파일
#
# [목적] 결과 파일에 적힌 단어와 수치가 모델·사전과 맞는지 본다.
# [동작] 1) 헤더 규모 수치를 기준값과 대조
#   2) 탐욕적 단어 = Python이 직접 따라간 경로
#   3) 단어 하나하나: 길이, 모든 n그램이 사전에 실재, 사전 표시(*) 정확
#   4) 사전 일치율: 단어 목록을 words.txt와 대조해 길이 칸별로 다시 센 값과
#      헤더의 match_all / len_k 줄을 비교. 사전 길이 비율도 words.txt에서 다시 계산
# [이유] 랜덤 단어 자체에는 정답 문자열이 없다. 그래서 "모든 n그램이 실재하는가" 같은
#   성질을 검사하고, 분포는 [4]에서 따로 본다. 일치율은 비교의 핵심 숫자라서 C가 쓴
#   값을 그대로 믿지 않고 반드시 다시 센다.
# ---------------------------------------------------------------------------
def len_bucket(length: int) -> int:
    return min(length, LEN_BUCKETS)


def bucket_label(b: int) -> str:
    return f"len_{b}+" if b == LEN_BUCKETS else f"len_{b}"


def parse_words(text: str) -> list[tuple[str, str, set[str]]]:
    """단어 줄: (키, 단어, 표시 집합)."""
    out = []
    for line in text.split("\n"):
        if not line or line.startswith("#"):
            continue
        key, word, *marks = line.split("\t")
        out.append((key, word, set(marks)))
    return out


def recompute_match(ref: Reference, words: list[str]) -> dict:
    """단어 목록과 words.txt만으로 사전 일치율을 센다 (C 출력의 숫자는 쓰지 않음)."""
    gen = collections.Counter(len_bucket(len(w)) for w in words)
    hit = collections.Counter(len_bucket(len(w)) for w in words if w in ref.word_set)
    dict_len = collections.Counter(len_bucket(len(w)) for w in ref.words)
    return {"total": len(words), "hits": sum(hit.values()), "gen": gen, "hit": hit,
            "share": {b: 100 * dict_len[b] / len(ref.words) for b in range(1, LEN_BUCKETS + 1)}}


def rate_ok(text: str, num: int, den: int, tol: float) -> bool:
    if den == 0:
        return text == "-"
    try:
        return abs(float(text) - 100 * num / den) <= tol
    except ValueError:
        return False


def check_result(ref: Reference, text: str, seed: str | None = None, count: int | None = None) -> list[str]:
    fails = []
    header = parse_header(text)
    count = int(header.get("count", "0")) if count is None else count
    expect = {"words": str(len(ref.words)), "chars": str(ref.n_chars), "vocab": str(ref.n_chars + 2),
              "total_grams": str(sum(ref.grams.values())), "unique_grams": str(len(ref.grams)),
              "contexts": str(len(ref.rows)), "count": str(count), "max_len": str(MAX_LEN)}
    if seed is not None:
        expect["seed"] = seed
    for k, v in expect.items():
        if header.get(k) != v:
            fails.append(f"헤더 {k}={header.get(k)} (기대 {v})")
    for k in ("build_ms", "gen_ms", "memory_bytes"):
        try:
            if float(header.get(k, "x")) < 0:
                raise ValueError
        except ValueError:
            fails.append(f"헤더 {k} 형식이 잘못됨: {header.get(k)}")

    rows = parse_words(text)
    keys = [k for k, _, _ in rows]
    if keys != ["greedy"] + [str(i) for i in range(1, count + 1)]:
        return fails + [f"단어 줄 구성이 greedy + 1..{count}이 아님 ({len(rows)}줄)"]

    g_word, g_trunc = ref.greedy_word()
    _, gw, gm = rows[0]
    if gw != g_word or ("~" in gm) != g_trunc:
        fails.append(f"탐욕적 '{gw}' != 기대 '{g_word}'")
    for key, word, marks in rows:
        trunc = "~" in marks
        if not word or len(word) > MAX_LEN or (trunc and len(word) != MAX_LEN):
            fails.append(f"{key}번 길이 이상: '{word}'")
        elif not ref.word_grams_ok(word, trunc):
            fails.append(f"{key}번 '{word}'에 사전에 없는 n그램이 있음")
        if ("*" in marks) != (word in ref.word_set):
            fails.append(f"{key}번 '{word}' 사전 표시(*)가 틀림")

    # 사전 일치율 재계산 (greedy 제외, 1..count 단어)
    m = recompute_match(ref, [w for _, w, _ in rows[1:]])
    hits, total, rate = (header.get("match_all", "") + "\t\t").split("\t")[:3]
    if hits != str(m["hits"]) or total != str(m["total"]) or not rate_ok(rate, m["hits"], m["total"], RATE_TOL):
        fails.append(f"match_all '{header.get('match_all')}' != 재계산 {m['hits']}/{m['total']}")
    for b in range(1, LEN_BUCKETS + 1):
        label = bucket_label(b)
        cols = (header.get(label, "") + "\t\t\t").split("\t")[:4]
        g, h = m["gen"][b], m["hit"][b]
        if cols[0] != str(g) or cols[1] != str(h) or not rate_ok(cols[2], h, g, RATE_TOL) \
                or abs(float(cols[3] or "nan") - m["share"][b]) > SHARE_TOL:
            fails.append(f"{label} '{header.get(label)}' != 재계산 생성 {g} / 일치 {h} / 사전비율 {m['share'][b]:.2f}")
    return fails


# ---------------------------------------------------------------------------
# [6] 빈도표 파일
#
# [목적] 표의 모든 줄이 기준값과 맞고, 정해진 순서로 쓰였는지 본다.
# [동작] 헤더 / n그램 전수 / 문맥 순서(BOS가 가장 앞) / 문맥 안 순서(빈도 내림차순,
#   같으면 번호 오름차순) / 확률(소수 6자리) 을 확인한다.
#   폴더의 고정 빈도표는 이 검사 + "프로그램이 지금 만드는 표와 바이트 단위로 같은가"를 본다.
# [이유] 빈도표는 프로그램을 인자 없이 실행할 때 만들어진다. 그 뒤 프로그램을 고쳤는데 표를
#   다시 만들지 않았다면 바이트 비교에서 드러난다. n=2·3은 Step4 표와도 비교한다.
# ---------------------------------------------------------------------------
def check_table(ref: Reference, text: str, keys: tuple[str, str]) -> list[str]:
    fails = []
    header = parse_header(text)
    expect = {"words": str(len(ref.words)), keys[0]: str(sum(ref.grams.values())),
              keys[1]: str(len(ref.grams)), "rows": str(len(ref.rows))}
    for k, v in expect.items():
        if header.get(k) != v:
            fails.append(f"헤더 {k}={header.get(k)} (기대 {v})")

    lines = [ln for ln in text.split("\n") if ln and not ln.startswith("#")]
    seen: dict[tuple[str, ...], int] = {}
    ctx_order: list[Ctx] = []
    bad_pct = bad_sort = 0
    last = None
    for ln in lines:
        *gram, c_s, pct_s = ln.split("\t")
        gram_t, c = tuple(gram), int(c_s)
        ctx = gram_t[:-1]
        seen[gram_t] = c
        if not ctx_order or ctx_order[-1] != ctx:
            ctx_order.append(ctx)
            last = None
        if abs(float(pct_s) - 100 * c / ref.row_total.get(ctx, 1)) > 0.5e-6 + 1e-12:
            bad_pct += 1
        key = (-c, ref.order.get(gram_t[-1], -1))
        if last is not None and key < last:
            bad_sort += 1
        last = key
    fails += [f"[n그램] {f}" for f in check_dump(ref, seen)]
    if len(seen) != len(lines):
        fails.append(f"중복 줄 {len(lines) - len(seen)}개")
    if ctx_order != sorted(ref.rows, key=ref.display_key):
        fails.append("문맥 순서가 'BOS 먼저 + 번호(가나다) 순'이 아님")
    if bad_pct:
        fails.append(f"확률이 틀린 줄 {bad_pct}개")
    if bad_sort:
        fails.append(f"문맥 안 정렬(빈도 내림차순)이 틀린 곳 {bad_sort}개")
    return fails


def check_table_file(ref: Reference, saved: bytes | None, generated: bytes, keys: tuple[str, str]) -> list[str]:
    if saved is None:
        return ["폴더에 고정 빈도표가 없음 (프로그램을 인자 없이 한 번 실행해야 만들어짐)"]
    fails = check_table(ref, saved.decode("utf-8"), keys)
    if saved != generated:
        fails.append("폴더의 빈도표가 프로그램이 지금 만드는 표와 바이트 단위로 다름")
    return fails


# ---------------------------------------------------------------------------
# [7] 목차·주석
#
# [목적] 단일 파일을 읽기 쉽게 하려고 맨 위에 둔 목차가 실제와 맞는지,
#   학습용 주석 규칙(구역마다 [목적] [동작] [이유])이 지켜졌는지 본다.
# [동작] 목차 줄 " *   [k]  N줄 | 이름 | 함수" 를 읽어
#   - N번째 줄이 "/* [k] 이름" 으로 시작하는가
#   - 파일 안 구역 머리(/* [k] ...)가 목차에 빠짐없이 있는가
#   - 구역 머리 상자(구분선 ~ 구분선) 안에 [목적] [동작] [이유]가 모두 있는가
# [이유] 줄 번호는 코드를 고치면 쉽게 어긋난다. 사람이 매번 확인하지 않아도
#   검증기가 잡게 했다 (사용자 지시로 단일 파일 + 목차 방식을 쓰기로 한 2026-10-21 결정).
# ---------------------------------------------------------------------------
INDEX_RE = re.compile(r"^ \*\s+\[(\d+)\]\s+(\d+)줄 \|\s*(.+?)\s*\|")
HEAD_RE = re.compile(r"^/\* \[(\d+)\] ")
BAR = "/* " + "=" * 66 + " */"


def check_index(src: str) -> list[str]:
    lines = src.split("\n")
    fails = []
    index = [(int(m[1]), int(m[2]), m[3]) for ln in lines if (m := INDEX_RE.match(ln))]
    heads = {int(m[1]): i + 1 for i, ln in enumerate(lines) if (m := HEAD_RE.match(ln))}
    if not index:
        return ["목차를 찾을 수 없음"]
    if sorted(k for k, _, _ in index) != sorted(heads):
        fails.append(f"목차 구역 {sorted(k for k, _, _ in index)} != 파일 구역 {sorted(heads)}")
    for k, no, title in index:
        if not (1 <= no <= len(lines)) or not lines[no - 1].startswith(f"/* [{k}] {title}"):
            fails.append(f"[{k}] {title}: 목차 {no}줄이 구역 머리가 아님 (실제 {heads.get(k)}줄)")
    for k, no in heads.items():
        end = next((j for j in range(no, len(lines)) if lines[j] == BAR), len(lines))
        block = "\n".join(lines[no - 1:end])
        missing = [t for t in ("[목적]", "[동작]", "[이유]") if t not in block]
        if missing:
            fails.append(f"[{k}] 구역 머리 주석에 {' '.join(missing)} 없음")
    return fails


# ---------------------------------------------------------------------------
# 4. 검증 실행
# ---------------------------------------------------------------------------
class Report:
    def __init__(self) -> None:
        self.ok = True

    def item(self, label: str, fails: list[str], detail: str = "") -> None:
        if fails:
            self.ok = False
            for f in fails[:5]:
                print(f"  [FAIL] {label:<26} {f}")
            if len(fails) > 5:
                print(f"  [FAIL] {label:<26} ... 외 {len(fails) - 5}건")
        else:
            print(f"  [OK  ] {label:<26} {detail}")


def print_comparison(stats: dict[str, dict]) -> None:
    """같은 seed로 만든 다섯 프로그램의 단어를 검증기가 다시 센 사전 일치율."""
    names = [k for k in PROGRAMS if k in stats]

    def cell(m: dict, b: int | None) -> str:
        h, g = (m["hits"], m["total"]) if b is None else (m["hit"][b], m["gen"][b])
        return f"{h:>4}/{g:<4} {100 * h / g:5.1f}%" if g else f"{'-':>9} {'-':>6}"

    print(f"\n=== 사전 일치율 비교 (seed {GEN_SEED}, 각 {GEN_COUNT}개, 검증기 재계산) ===")
    print("  " + f"{'길이':<5}" + "".join(f"{LABEL[k]:>18}" for k in names) + f"{'사전 비율':>11}")
    print("  " + f"{'전체':<5}" + "".join(f"{cell(stats[k], None):>18}" for k in names))
    for b in range(1, LEN_BUCKETS + 1):
        name = f"{b}+" if b == LEN_BUCKETS else str(b)
        print("  " + f"{name:<6}" + "".join(f"{cell(stats[k], b):>18}" for k in names)
              + f"{stats[names[0]]['share'][b]:>10.2f}%")
    print("  ('일치/생성 일치율'. 생성 길이 분포는 각 칸의 생성 수)")


def verify() -> bool:
    words = load_words(SOURCE)
    report = Report()
    stats: dict[str, dict] = {}

    for name, cfg in PROGRAMS.items():
        exe = cfg["exe"]
        ref = Reference(words, cfg["n"])
        queries = build_map_queries(ref, cfg["hist"])
        print(f"\n=== {name} (n={cfg['n']}) — 기준: words.txt 재계산 "
              f"n그램 {len(ref.grams):,} / 총 {sum(ref.grams.values()):,} / 문맥 {len(ref.rows):,} ===")
        if not exe.exists():
            report.item("실행 파일", [f"{exe.name} 없음 (build.bat {exe.stem} 먼저)"])
            continue
        report.item("[1] n그램 빈도 전수", check_dump(ref, get_dump(exe)), f"{len(ref.grams):,}개 전부 일치")
        report.item("[2] 문맥 합계", check_rows(ref, get_rows(exe)), f"{len(ref.ctx_count):,}개 문맥")
        report.item("[3] r->글자 대응", check_map(queries, get_map(exe, queries)),
                    f"문맥 {len({q[0] for q in queries[:-MAP_RANDOM_COUNT]}):,}개의 경계 {len(queries) - MAP_RANDOM_COUNT:,}개 + 무작위 {MAP_RANDOM_COUNT:,}개")
        for ctx in cfg["hist"]:
            fails, detail = check_hist(ref, ctx, get_hist(exe, ctx))
            report.item(f"[4] 분포 {' '.join(ctx)}", fails, detail)

        with tempfile.TemporaryDirectory() as d:
            result, table = get_generated(exe, pathlib.Path(d))
        g_word, _ = ref.greedy_word()
        report.item("[5] 결과 (임시 실행)", check_result(ref, result, str(GEN_SEED), GEN_COUNT),
                    f"탐욕적 '{g_word}', {GEN_COUNT}개 n그램 실재·표시·일치율 재계산 일치")
        for path in sorted(HERE.glob(cfg["result_glob"])):
            text = path.read_bytes().decode("utf-8")
            report.item(f"[5] {path.name}", check_result(ref, text),
                        f"seed {parse_header(text).get('seed')}, 일치율 {parse_header(text).get('match_all', '').split(chr(9))[-1]}%")
        stats[name] = recompute_match(ref, [w for _, w, _ in parse_words(result)[1:]])

        saved = cfg["table"].read_bytes() if cfg["table"].exists() else None
        report.item(f"[6] {cfg['table'].name}", check_table_file(ref, saved, table, cfg["table_keys"]),
                    "전수·순서·확률 + 프로그램 출력과 바이트 일치")
        if "step4_table" in cfg:
            s4 = cfg["step4_table"]
            same = s4.exists() and s4.read_bytes() == table
            report.item(f"[6] = ../Step4/{s4.name}", [] if same else [f"Step4 빈도표와 다름 (또는 없음): {s4}"],
                        "Step4와 같은 모델: 빈도표 바이트 일치")
        report.item(f"[7] {cfg['src'].name} 목차·주석", check_index(cfg["src"].read_text(encoding="utf-8")),
                    "목차 줄 번호 = 구역 머리, 모든 구역에 [목적][동작][이유]")

    if stats:
        print_comparison(stats)
    print("\n=== 검증 통과 ===" if report.ok else "\n=== 검증 실패 ===")
    return report.ok


# ---------------------------------------------------------------------------
# 5. 자기 검증: 망가진 구현의 출력을 흉내 내서 넣으면 반드시 FAIL이 나와야 한다
#
# [목적] 검증기가 언제나 OK만 낸다면 검증이 아니다.
# [동작] 실제로 생길 법한 버그를 하나씩 흉내 낸 출력을 판정 함수에 넣고 FAIL이 나는지 본다.
#   C 출력은 6그램에서 받아 와서 조작한다 (판정 함수는 n과 무관하게 같은 코드).
#   빈도표 바이트 비교만 바이그램 표로 따로 확인한다.
# [이유] Step5에서 새로 생긴 위험은 6그램의 키다. 키를 Step4처럼 숫자 하나로 만들면 2^64를
#   넘어 잘린다. 그 상황을 Python으로 그대로 흉내 낸 출력(키를 2^64로 나눈 나머지를 다시 글자로
#   푼 것)을 넣어 [1]이 잡아내는지 확인한다. 나머지 사례는 Step4와 같다.
# ---------------------------------------------------------------------------
SELF_TEST_TARGET = "6gram"


def overflowed_dump(ref: Reference) -> tuple[dict[tuple[str, ...], int], int]:
    """6그램 키를 숫자 하나(V진법 6자리)로 만들어 64비트에 넣었다면 C가 출력했을 --dump.

    키 = 각 글자 번호를 V진법 자리로 붙인 값 mod 2^64. 그 키를 다시 V진법으로 풀어 글자로 바꾼다.
    맨 앞 자리는 V를 넘을 수 있어 그런 경우 '?'로 적는다. (바뀐 n그램 수도 돌려준다)
    """
    names = {i: c for c, i in ref.order.items()}
    v = len(ref.order)
    out: collections.Counter[tuple[str, ...]] = collections.Counter()
    changed = 0
    for g, c in ref.grams.items():
        key = 0
        for t in g:
            key = (key * v + ref.order[t]) % (1 << 64)
        digits = []
        for _ in range(ref.n - 1):
            digits.append(names[key % v])
            key //= v
        digits.append(names.get(key, "?"))
        back = tuple(reversed(digits))
        changed += back != g
        out[back] += c
    return dict(out), changed


def self_test() -> bool:
    words = load_words(SOURCE)
    cfg = PROGRAMS[SELF_TEST_TARGET]
    n = cfg["n"]
    ref = Reference(words, n)
    exe = cfg["exe"]
    keys = cfg["table_keys"]
    start = (BOS,) * (n - 1)
    queries = build_map_queries(ref, cfg["hist"])
    dump, rows = get_dump(exe), get_rows(exe)
    answers = get_map(exe, queries)
    hist_ctx = cfg["hist"][1]
    hist = get_hist(exe, hist_ctx)
    with tempfile.TemporaryDirectory() as d:
        result, table_b = get_generated(exe, pathlib.Path(d))
    table = table_b.decode("utf-8")
    src = cfg["src"].read_text(encoding="utf-8")
    bi_ref = Reference(words, 2)
    bi_saved = PROGRAMS["bigram"]["table"].read_bytes()

    some = next(iter(ref.grams))
    # 해시 충돌 처리 버그 흉내: 서로 다른 n그램 두 개가 하나로 합쳐짐
    a, b = list(ref.grams)[:2]
    merged = {k: c for k, c in dump.items() if k != b} | {a: dump[a] + dump[b]}
    overflow, n_changed = overflowed_dump(ref)
    # '>=' 버그: 구간 끝 r이 다음 후보로 넘어간다
    next_of = {(ctx, c): spans[i + 1][0] for ctx, spans in ref.intervals.items()
               for i, (c, _, _) in enumerate(spans) if i + 1 < len(spans)}
    end_rs = {(ctx, hi) for ctx, spans in ref.intervals.items() for _, _, hi in spans}
    off_by_one = [next_of.get((ctx, exp), exp) if (ctx, r) in end_rs else got
                  for (ctx, r, exp), got in zip(queries, answers)]
    cands = [c for c, _ in ref.rows[hist_ctx]]
    uniform = {c: HIST_SAMPLES // len(cands) for c in cands}
    uniform[cands[0]] += HIST_SAMPLES - sum(uniform.values())

    # 결과 파일 조작
    lines = result.split("\n")
    first = next(i for i, ln in enumerate(lines) if ln.startswith("1\t"))
    word = lines[first].split("\t")[1]
    never_first = next(c for c in ref.order if c not in (BOS, EOS) and start + (c,) not in ref.grams)
    bad_word = lines[first].replace(word, never_first + word, 1)
    star_flip = lines[first][:-2] if lines[first].endswith("\t*") else lines[first] + "\t*"
    header = parse_header(result)
    h, t, r = header["match_all"].split("\t")
    l3 = header["len_3"].split("\t")
    greedy_i = next(i for i, ln in enumerate(lines) if ln.startswith("greedy\t"))
    greedy_w = lines[greedy_i].split("\t")[1]
    fake_greedy = "가" if greedy_w != "가" else "사"
    table_lines = table.split("\n")
    data_i = next(i for i, ln in enumerate(table_lines) if ln and not ln.startswith("#"))
    pct_cols = table_lines[data_i].split("\t")
    pct_cols[-1] = f"{float(pct_cols[-1]) + 0.001:.6f}"

    def with_line(i: int, new: str) -> str:
        return "\n".join(lines[:i] + [new] + lines[i + 1:])

    def table_with(i: int, new: str) -> str:
        return "\n".join(table_lines[:i] + [new] + table_lines[i + 1:])

    def swapped(i: int, j: int) -> str:
        t2 = table_lines[:]
        t2[i], t2[j] = t2[j], t2[i]
        return "\n".join(t2)

    # 목차 조작: [8] 줄 번호 +1 / [9] 구역 머리 주석의 [이유] 삭제
    src_lines = src.split("\n")
    idx8 = next(i for i, ln in enumerate(src_lines) if (m := INDEX_RE.match(ln)) and m[1] == "8")
    shifted = src_lines[:]
    shifted[idx8] = re.sub(r"(\d+)줄", lambda m: f"{int(m[1]) + 1}줄", shifted[idx8], count=1)
    head9 = next(i for i, ln in enumerate(src_lines) if ln.startswith("/* [9] "))
    reason9 = next(i for i in range(head9, len(src_lines)) if "[이유]" in src_lines[i])
    no_reason = src_lines[:]
    no_reason[reason9] = no_reason[reason9].replace("[이유]", "      ")

    cases = [
        ("정상 출력", lambda: check_dump(ref, dump) + check_rows(ref, rows)
            + check_map(queries, answers) + check_hist(ref, hist_ctx, hist)[0]
            + check_table(ref, table, keys) + check_result(ref, result, str(GEN_SEED), GEN_COUNT)
            + check_index(src) + check_table_file(bi_ref, bi_saved, bi_saved, PROGRAMS["bigram"]["table_keys"]),
            False),
        (f"{n}그램 하나의 빈도 +1", lambda: check_dump(ref, dump | {some: dump[some] + 1}), True),
        (f"{n}그램 하나 빠짐", lambda: check_dump(ref, {k: c for k, c in dump.items() if k != some}), True),
        (f"서로 다른 {n}그램 두 개가 합쳐짐", lambda: check_dump(ref, merged), True),
        (f"{n}그램 키를 숫자 하나로 (2^64 넘침, {n_changed:,}개 변형)", lambda: check_dump(ref, overflow), True),
        ("단어 경계를 넘어 셈 (EOS,BOS,...)", lambda: check_dump(ref, dump | {(EOS,) + start: 420308}), True),
        ("문맥 합계 하나 틀림", lambda: check_rows(ref, rows | {start: rows[start] - 1}), True),
        ("누적 비교 한 칸 어긋남 ('>=')", lambda: check_map(queries, off_by_one), True),
        (f"({','.join(hist_ctx)}) 후보를 균등하게 선택", lambda: check_hist(ref, hist_ctx, uniform)[0], True),
        ("빈도표 문맥 안 순서 뒤바뀜", lambda: check_table(ref, swapped(data_i, data_i + 1), keys), True),
        ("빈도표 확률 틀림", lambda: check_table(ref, table_with(data_i, "\t".join(pct_cols)), keys), True),
        (f"생성 단어에 없는 {n}그램", lambda: check_result(ref, with_line(first, bad_word)), True),
        ("사전 표시(*) 틀림", lambda: check_result(ref, with_line(first, star_flip)), True),
        ("탐욕적 단어 틀림", lambda: check_result(ref, with_line(greedy_i, f"greedy\t{fake_greedy}")), True),
        ("일치 수 +1 (match_all)", lambda: check_result(ref, result.replace(
            f"# match_all\t{h}\t{t}\t{r}", f"# match_all\t{int(h) + 1}\t{t}\t{r}")), True),
        ("3글자 일치율만 조작", lambda: check_result(ref, result.replace(
            "# len_3\t" + "\t".join(l3), "# len_3\t" + "\t".join(l3[:2] + [f"{float(l3[2]) + 1:.1f}", l3[3]]))), True),
        ("목차 줄 번호 1 어긋남", lambda: check_index("\n".join(shifted)), True),
        ("구역 머리 주석에 [이유] 없음", lambda: check_index("\n".join(no_reason)), True),
        ("바이그램 빈도표 1바이트 변경", lambda: check_table_file(
            bi_ref, bi_saved.replace(b"\t4614\t", b"\t4615\t", 1), bi_saved, PROGRAMS["bigram"]["table_keys"]), True),
    ]

    all_ok = True
    print(f"자기 검증 (C 출력은 {SELF_TEST_TARGET}에서 받아 조작)")
    for label, fn, should_fail in cases:
        fails = fn()
        failed = bool(fails)
        good = failed == should_fail
        all_ok &= good
        verdict = "FAIL 감지" if failed else "PASS"
        mark = "OK  " if good else "XX  "
        print(f"  [{mark}] {label:<40} -> {verdict}" + (f"  ({fails[0]})" if failed else ""))

    print("\n자기 검증 통과: 망가진 입력은 모두 FAIL, 정상 입력은 PASS" if all_ok
          else "\n자기 검증 실패: 검증기가 문제를 놓친다")
    return all_ok


# ---------------------------------------------------------------------------
# 6. 비교 문서 (--compare): step5_compare.txt
#
# [목적] n = 2 ~ 6의 사전 일치율과 생성 단어를 결과 파일 다섯 개를 오가지 않고 한 문서로 본다.
# [동작] 1) 폴더의 result_NN.txt에서 seed와 단어 수가 같은 다섯 파일 묶음을 찾는다
#   2) 다섯 파일을 [5] check_result로 먼저 검증한다. 하나라도 실패하면 문서를 만들지 않는다
#   3) 단어 목록과 words.txt로 일치율을 다시 세서(recompute_match) 표를 만든다
#   4) 사전에 없는 단어(= 모델이 새로 만든 단어)를 n마다 앞에서부터 보여 준다
#   5) 강의 보고값(raw 2-6 표, raw 2-7 본문)을 실측 옆에 나란히 적는다
# [이유] 표의 숫자는 C가 헤더에 쓴 값이 아니라 검증기가 다시 센 값이다.
#   해석 문장은 자동으로 쓰지 않는다. "그럴듯한 새 단어인가"는 숫자로 잴 수 없어서 사람이
#   단어 목록을 보고 판단해야 하고, 강의 결과에 맞춘 해석을 미리 써 두지 않기 위해서다
#   (사용자 지시 2026-10-22). 한글은 2칸을 차지하므로 표 칸 맞춤은 표시 폭으로 한다.
# ---------------------------------------------------------------------------
COMPARE_PATH = HERE / "step5_compare.txt"
NEW_WORDS_SHOWN = 20   # n마다 보여 줄 "사전에 없는 단어" 수 (결과 파일 순서대로)

# 강의 보고값 (기준값이 아니라 참고용으로 나란히 적는다)
#   raw/챕터2-6: 사전 일치율 표 - 전체 40% / 61.7%, 2글자 79% / 98%, 3글자 17% / 65%, 5글자 0% / 27%
#   raw/챕터2-7: 6-그램 "생성된 단어가 100% 사전에 존재하는 단어와 일치"
#   강의는 생성 개수 외에 seed·패딩 등 조건을 밝히지 않았다
LECTURE = {
    "bigram": {"all": "40%", 2: "79%", 3: "17%", 5: "0%"},
    "trigram": {"all": "61.7%", 2: "98%", 3: "65%", 5: "27%"},
    "6gram": {"all": "100%"},
}


def disp_width(s: str) -> int:
    """화면 표시 폭: 한글 등 전각 문자는 2칸."""
    return sum(2 if unicodedata.east_asian_width(c) in "WF" else 1 for c in s)


def render_table(head: list[str], rows: list[list[str]], left: int = 1) -> list[str]:
    """| 칸 | 칸 | 형식의 표. 앞 left개 칸은 왼쪽 정렬, 나머지(숫자)는 오른쪽 정렬."""
    widths = [max(disp_width(r[i]) for r in [head, *rows]) for i in range(len(head))]

    def line(cells: list[str]) -> str:
        out = []
        for i, c in enumerate(cells):
            gap = " " * (widths[i] - disp_width(c))
            out.append(c + gap if i < left else gap + c)
        return "| " + " | ".join(out) + " |"

    sep = "|" + "|".join("-" * (w + 2) for w in widths) + "|"
    return [line(head), sep, *(line(r) for r in rows)]


def pct(num: int, den: int) -> str:
    return f"{100 * num / den:.1f}%" if den else "-"


def find_groups() -> list[tuple[str, dict[str, pathlib.Path]]]:
    """seed와 단어 수가 같은 다섯 결과 파일 묶음. 같은 seed가 여럿이면 번호가 큰 파일."""
    found: dict[str, dict[tuple[str, str], pathlib.Path]] = {}
    for name, cfg in PROGRAMS.items():
        found[name] = {}
        for path in sorted(HERE.glob(cfg["result_glob"])):   # 번호 순이라 나중 파일이 덮어씀
            h = parse_header(path.read_bytes().decode("utf-8"))
            found[name][(h.get("seed", ""), h.get("count", ""))] = path
    common = set.intersection(*(set(f) for f in found.values()))
    common_sorted = sorted(common, key=lambda k: found["6gram"][k].name)
    return [(seed, {name: found[name][(seed, cnt)] for name in PROGRAMS}) for seed, cnt in common_sorted]


def compare() -> bool:
    words = load_words(SOURCE)
    refs = {name: Reference(words, cfg["n"]) for name, cfg in PROGRAMS.items()}
    groups = find_groups()
    if not groups:
        print("[오류] seed와 단어 수가 같은 결과 파일 다섯 개(n=2~6) 묶음이 없습니다")
        print("       예: step5_6gram.exe 실행 후, 그 결과 파일의 seed로 나머지 네 프로그램을 1000 <seed>로 실행")
        return False

    names = list(PROGRAMS)
    out = ["# n = 2 ~ 6 사전 일치율 비교",
           "# 만든 프로그램: step5_verify.py --compare",
           "# 사전 일치율 = 생성 단어 중 words.txt에 있는 단어의 비율",
           "# 숫자 출처: 결과 파일의 단어 목록을 words.txt와 대조해 검증기가 다시 센 값",
           "#            (결과 파일이 [5] 검증을 통과한 경우에만 문서를 만든다)",
           "# 해석은 적지 않는다. 사전에 없는 단어가 그럴듯한지는 [4]의 목록을 직접 읽고 판단한다",
           ""]
    for seed, paths in groups:
        texts = {name: paths[name].read_bytes().decode("utf-8") for name in names}
        for name, text in texts.items():
            fails = check_result(refs[name], text)
            if fails:
                print(f"[오류] {name} 결과 파일이 검증을 통과하지 못해 비교 문서를 만들지 않습니다: {fails[0]}")
                return False
        rows = {name: parse_words(texts[name]) for name in names}
        m = {name: recompute_match(refs[name], [w for _, w, _ in rows[name][1:]]) for name in names}
        heads = [LABEL[k] for k in names]

        out += [f"## seed {seed} / 각 {m['bigram']['total']:,}개 생성",
                *[f"   {LABEL[k]}: {paths[k].name}" for k in names],
                "",
                "[1] 전체 사전 일치율과 탐욕적 디코딩",
                *render_table(["", *heads], [
                    ["일치 / 생성", *[f"{m[k]['hits']} / {m[k]['total']}" for k in names]],
                    ["사전 일치율", *[pct(m[k]["hits"], m[k]["total"]) for k in names]],
                    ["사전에 없는 단어 수", *[str(m[k]["total"] - m[k]["hits"]) for k in names]],
                    ["탐욕적 디코딩", *[rows[k][0][1] for k in names]],
                ]),
                "",
                "[2] 길이별 사전 일치율 (일치 수 / 생성 수, 일치율)",
                *render_table(["길이", *heads], [
                    [f"{b}+" if b == LEN_BUCKETS else str(b),
                     *[f"{m[k]['hit'][b]}/{m[k]['gen'][b]} {pct(m[k]['hit'][b], m[k]['gen'][b])}" for k in names]]
                    for b in range(1, LEN_BUCKETS + 1)
                ]),
                "",
                "[3] 생성 단어 길이 분포 vs 사전 (각 길이가 차지하는 비율)",
                *render_table(["길이", "사전", *heads], [
                    [f"{b}+" if b == LEN_BUCKETS else str(b), f"{m['bigram']['share'][b]:.2f}%",
                     *[pct(m[k]["gen"][b], m[k]["total"]) for k in names]]
                    for b in range(1, LEN_BUCKETS + 1)
                ]),
                "",
                f"[4] 사전에 없는 단어 (결과 파일 순서대로 앞 {NEW_WORDS_SHOWN}개, ~ = 최대 길이에서 잘림)"]
        for k in names:
            new = [w + ("~" if "~" in marks else "") for _, w, marks in rows[k][1:] if w not in refs[k].word_set]
            shown = ", ".join(new[:NEW_WORDS_SHOWN]) if new else "(없음)"
            out.append(f"   {LABEL[k]} ({len(new)}개): {shown}")
        out += ["",
                "[5] 강의 보고값과 나란히 (강의 값은 목표가 아니다. 조건이 달라 직접 비교에 한계가 있다)",
                "    강의 출처: raw/챕터2-6 사전 일치율 표 (n=2, 3), raw/챕터2-7 본문 (n=6 '100% 일치')",
                *render_table(["", *[f"{LABEL[k]} 강의" if j % 2 == 0 else f"{LABEL[k]} 실측"
                                     for k in ("bigram", "trigram", "6gram") for j in (0, 1)]], [
                    [label, *[cell for k in ("bigram", "trigram", "6gram")
                              for cell in (LECTURE[k].get(key, "-"),
                                           pct(m[k]["hits"], m[k]["total"]) if key == "all"
                                           else pct(m[k]["hit"][key], m[k]["gen"][key]))]]
                    for label, key in (("전체", "all"), ("2글자", 2), ("3글자", 3), ("5글자", 5))
                ]),
                ""]

    COMPARE_PATH.write_bytes("\n".join(out).encode("utf-8"))   # UTF-8, LF
    print("\n".join(out))
    print(f"비교 문서: {COMPARE_PATH.name} (묶음 {len(groups)}개)")
    return True


# ---------------------------------------------------------------------------
def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    if not SOURCE.exists():
        print(f"[오류] 원본 데이터를 찾을 수 없습니다: {SOURCE}")
        return 1
    if "--self-test" in sys.argv[1:]:
        return 0 if self_test() else 1
    if "--compare" in sys.argv[1:]:
        return 0 if compare() else 1
    return 0 if verify() else 1


if __name__ == "__main__":
    sys.exit(main())
