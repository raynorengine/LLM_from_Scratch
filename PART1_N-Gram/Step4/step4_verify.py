"""
step4_verify.py - [STEP 4] 바이그램(step4_bigram.exe)과 트라이그램(step4_trigram.exe)을 독립 검증하고
사전 일치율을 비교한다.

===== 무엇을 검증하나 (두 프로그램 각각) =====
  [1] n그램 빈도 전수 비교 : 바이그램 171,425 / 트라이그램 654,039개 전부가 Python 재계산과 같은가 (--dump)
  [2] 문맥 합계            : 문맥마다 합계가 "다른 방법으로 센 값"과 같은가 (--rows)
  [3] r -> 글자 대응        : 모든 후보 구간의 시작·끝 r + 무작위 r에서 맞는 글자가 나오나 (--map)
  [4] 분포 (카이제곱)       : 몇 개 문맥에서 대량으로 뽑은 횟수가 실제 확률과 맞나 (--hist)
  [5] 생성 결과 파일        : 탐욕적 결과, 모든 단어의 n그램 실재, 표시(*, ~),
                             사전 일치율·길이별 수치를 단어 목록에서 다시 계산해 대조
                             (임시 폴더 실행 1회 + 폴더에 저장된 result_NN.txt 전부)
  [6] 빈도표 파일          : 형식·순서·빈도·확률 + 폴더의 고정 빈도표가 프로그램 출력과 바이트 단위로 같은가
                             (step4_bigram_table.txt는 Step3 파일을 복사한 것이라 이 확인이 특히 중요)
  [7] 목차·주석            : C 파일 맨 위 목차의 줄 번호가 실제 구역 위치와 같은가,
                             모든 구역 머리 주석에 [목적] [동작] [이유]가 있는가
마지막에 같은 seed로 돌린 두 프로그램의 사전 일치율 비교표를 출력한다 (검증기가 다시 계산한 값).

===== 독립성 (CLAUDE.md "검증 규칙") =====
- 기준값은 전부 원본 words.txt에서 Python으로 처음부터 다시 센다. C 출력을 기준으로 쓰지 않는다.
- C는 64비트 키 해시 + CSR + 이진 탐색이지만, 여기서는 튜플을 키로 쓰는 Counter로 세고
  r -> 글자 정답은 "구간 [시작, 끝]을 직접 나열"해서 만든다. 탐색 코드를 복사하지 않는다.
- [2]의 기준은 n그램을 더한 값이 아니라 "문맥 자체가 나온 횟수"를 따로 센 값이다.
  (바이그램: 글자 하나씩 센 유니그램 / 트라이그램: [BOS, BOS, 단어]에서 센 인접 쌍)
- 일치율은 C가 쓴 숫자를 믿지 않고, 결과 파일의 단어 목록과 words.txt로 다시 센다.
- C 프로그램은 subprocess로 실행하고 출력만 받는다. 결과는 임시 폴더에 쓰게 해서
  Step4 폴더의 번호 붙은 결과 파일과 빈도표를 건드리지 않는다.

===== 사용법 =====
  python step4_verify.py               # 검증 (약 15초)
  python step4_verify.py --self-test   # 망가진 출력을 흉내 낸 입력에 FAIL을 내는지 확인
  python step4_verify.py --compare     # 같은 seed의 바이그램·트라이그램 결과 파일을 검증한 뒤
                                       # 사전 일치율 비교표 step4_compare.txt 를 만든다
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
MAP_RANDOM_COUNT = 10_000         # [3] 경계값 외에 넣는 무작위 질의 수
HIST_SAMPLES = 100_000            # [4] 문맥마다 뽑는 횟수
HIST_SEED = 12345
P_VALUE_THRESHOLD = 1e-3          # [4] p < 0.001 이면 "분포가 다르다"
GEN_COUNT, GEN_SEED = 1000, 2026  # [5] 임시 실행 인자 (두 프로그램 같은 seed)
RATE_TOL = 0.05 + 1e-9            # 일치율은 소수 1자리로 쓰므로 반올림 오차 0.05까지 허용
SHARE_TOL = 0.005 + 1e-9          # 사전 길이 비율은 소수 2자리

# 프로그램별 설정. n = n그램의 n, 문맥 길이 = n - 1
PROGRAMS = {
    "bigram": {
        "n": 2,
        "exe": HERE / "step4_bigram.exe",
        "src": HERE / "step4_bigram.c",
        "table": HERE / "step4_bigram_table.txt",
        "result_glob": "step4_bigram_result_*.txt",
        "table_keys": ("total_pairs", "unique_pairs"),
        "hist": [(BOS,), ("사",), ("다",)],
    },
    "trigram": {
        "n": 3,
        "exe": HERE / "step4_trigram.exe",
        "src": HERE / "step4_trigram.c",
        "table": HERE / "step4_trigram_table.txt",
        "result_glob": "step4_trigram_result_*.txt",
        "table_keys": ("total_trigrams", "unique_trigrams"),
        "hist": [(BOS, BOS), (BOS, "사"), ("자", "동")],
    },
}

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


def build_map_queries(ref: Reference) -> list[tuple[Ctx, int, str]]:
    """(문맥, r, 정답 글자). 모든 구간의 시작·끝 + 무작위 r.

    한 칸 어긋남('>' 대신 '>=')은 구간 경계에서만 드러나므로 경계를 전부 넣는다.
    무작위 r의 정답은 bisect로 찾는다 (C의 이진 탐색 코드와는 별개의 표준 라이브러리).
    """
    queries = [(ctx, r, b) for ctx, spans in ref.intervals.items()
               for b, lo, hi in spans for r in (lo, hi)]
    rng = random.Random(7)
    keys = sorted(ref.intervals)
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
# [이유] step4_bigram_table.txt는 Step3 파일을 복사한 것이다. 복사 과정에서 내용이 바뀌거나
#   Step4 프로그램의 형식이 Step3과 달라졌다면 바이트 비교에서 드러난다.
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
    """같은 seed로 만든 두 프로그램의 단어를 검증기가 다시 센 사전 일치율."""
    bi, tri = stats["bigram"], stats["trigram"]

    def cell(m: dict, b: int | None) -> str:
        h, g = (m["hits"], m["total"]) if b is None else (m["hit"][b], m["gen"][b])
        return f"{h:>4}/{g:<4} {100 * h / g:5.1f}%" if g else f"{'-':>9} {'-':>6}"

    print(f"\n=== 사전 일치율 비교 (seed {GEN_SEED}, 각 {GEN_COUNT}개, 검증기 재계산) ===")
    print(f"  {'길이':<6} {'바이그램':>17} {'트라이그램':>17} {'사전 길이 비율':>14}")
    print(f"  {'전체':<6} {cell(bi, None):>18} {cell(tri, None):>18}")
    for b in range(1, LEN_BUCKETS + 1):
        name = f"{b}+" if b == LEN_BUCKETS else str(b)
        print(f"  {name:<6} {cell(bi, b):>18} {cell(tri, b):>18} {bi['share'][b]:>13.2f}%")
    print(f"  (생성 수 / 일치 수는 '일치/생성'. 생성 길이 분포는 각 칸의 생성 수)")


def verify() -> bool:
    words = load_words(SOURCE)
    report = Report()
    stats: dict[str, dict] = {}

    for name, cfg in PROGRAMS.items():
        exe = cfg["exe"]
        ref = Reference(words, cfg["n"])
        queries = build_map_queries(ref)
        print(f"\n=== {name} (n={cfg['n']}) — 기준: words.txt 재계산 "
              f"n그램 {len(ref.grams):,} / 총 {sum(ref.grams.values()):,} / 문맥 {len(ref.rows):,} ===")
        if not exe.exists():
            report.item("실행 파일", [f"{exe.name} 없음 (build.bat {exe.stem} 먼저)"])
            continue
        report.item("[1] n그램 빈도 전수", check_dump(ref, get_dump(exe)), f"{len(ref.grams):,}개 전부 일치")
        report.item("[2] 문맥 합계", check_rows(ref, get_rows(exe)), f"{len(ref.ctx_count):,}개 문맥")
        report.item("[3] r->글자 대응", check_map(queries, get_map(exe, queries)),
                    f"경계 {len(queries) - MAP_RANDOM_COUNT:,}개 + 무작위 {MAP_RANDOM_COUNT:,}개")
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
        report.item(f"[7] {cfg['src'].name} 목차·주석", check_index(cfg["src"].read_text(encoding="utf-8")),
                    "목차 줄 번호 = 구역 머리, 모든 구역에 [목적][동작][이유]")

    if len(stats) == 2:
        print_comparison(stats)
    print("\n=== 검증 통과 ===" if report.ok else "\n=== 검증 실패 ===")
    return report.ok


# ---------------------------------------------------------------------------
# 5. 자기 검증: 망가진 구현의 출력을 흉내 내서 넣으면 반드시 FAIL이 나와야 한다
#
# [목적] 검증기가 언제나 OK만 낸다면 검증이 아니다.
# [동작] 실제로 생길 법한 버그를 하나씩 흉내 낸 출력을 판정 함수에 넣고 FAIL이 나는지 본다.
#   C 출력은 트라이그램에서 받아 와서 조작한다 (판정 함수는 n과 무관하게 같은 코드).
#   빈도표 바이트 비교만 바이그램 복사본으로 따로 확인한다.
# [이유] Step4에서 새로 생긴 위험(64비트 키 넘침 = 서로 다른 3그램 합쳐짐, 단어 경계 3그램,
#   일치율 숫자 조작, 목차 어긋남, 주석 누락)을 각각 하나 이상의 사례로 넣었다.
# ---------------------------------------------------------------------------
def self_test() -> bool:
    words = load_words(SOURCE)
    ref = Reference(words, 3)
    exe = PROGRAMS["trigram"]["exe"]
    keys = PROGRAMS["trigram"]["table_keys"]
    queries = build_map_queries(ref)
    dump, rows = get_dump(exe), get_rows(exe)
    answers = get_map(exe, queries)
    hist_ctx = (BOS, "사")
    hist = get_hist(exe, hist_ctx)
    with tempfile.TemporaryDirectory() as d:
        result, table_b = get_generated(exe, pathlib.Path(d))
    table = table_b.decode("utf-8")
    src = PROGRAMS["trigram"]["src"].read_text(encoding="utf-8")
    bi_ref = Reference(words, 2)
    bi_saved = PROGRAMS["bigram"]["table"].read_bytes()

    some = next(iter(ref.grams))
    # 64비트 키 넘침 흉내: 서로 다른 3그램 두 개가 같은 키로 합쳐져 하나는 사라지고 하나는 합이 됨
    a, b = list(ref.grams)[:2]
    merged = {k: c for k, c in dump.items() if k != b} | {a: dump[a] + dump[b]}
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
    never_first = next(c for c in ref.order if c not in (BOS, EOS) and (BOS, BOS, c) not in ref.grams)
    bad_word = lines[first].replace(word, never_first + word, 1)
    star_flip = lines[first][:-2] if lines[first].endswith("\t*") else lines[first] + "\t*"
    header = parse_header(result)
    h, t, r = header["match_all"].split("\t")
    l3 = header["len_3"].split("\t")
    greedy_i = next(i for i, ln in enumerate(lines) if ln.startswith("greedy\t"))
    table_lines = table.split("\n")
    data_i = next(i for i, ln in enumerate(table_lines) if ln and not ln.startswith("#"))

    def with_line(i: int, new: str) -> str:
        return "\n".join(lines[:i] + [new] + lines[i + 1:])

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
        ("3그램 하나의 빈도 +1", lambda: check_dump(ref, dump | {some: dump[some] + 1}), True),
        ("3그램 하나 빠짐", lambda: check_dump(ref, {k: c for k, c in dump.items() if k != some}), True),
        ("키 넘침으로 두 3그램이 합쳐짐", lambda: check_dump(ref, merged), True),
        ("단어 경계를 넘어 셈 (EOS,BOS,BOS)", lambda: check_dump(ref, dump | {(EOS, BOS, BOS): 420308}), True),
        ("문맥 합계 하나 틀림", lambda: check_rows(ref, rows | {(BOS, BOS): rows[(BOS, BOS)] - 1}), True),
        ("이진 탐색 한 칸 어긋남 ('>=')", lambda: check_map(queries, off_by_one), True),
        ("(BOS,사) 후보를 균등하게 선택", lambda: check_hist(ref, hist_ctx, uniform)[0], True),
        ("빈도표 문맥 안 순서 뒤바뀜", lambda: check_table(ref, swapped(data_i, data_i + 1), keys), True),
        ("빈도표 확률 틀림", lambda: check_table(ref, table.replace("\t1.097764\n", "\t1.100000\n", 1), keys), True),
        ("생성 단어에 없는 3그램", lambda: check_result(ref, with_line(first, bad_word)), True),
        ("사전 표시(*) 틀림", lambda: check_result(ref, with_line(first, star_flip)), True),
        ("탐욕적 단어 틀림", lambda: check_result(ref, with_line(greedy_i, "greedy\t사")), True),
        ("일치 수 +1 (match_all)", lambda: check_result(ref, result.replace(
            f"# match_all\t{h}\t{t}\t{r}", f"# match_all\t{int(h) + 1}\t{t}\t{r}")), True),
        ("3글자 일치율만 조작", lambda: check_result(ref, result.replace(
            "# len_3\t" + "\t".join(l3), "# len_3\t" + "\t".join(l3[:2] + [f"{float(l3[2]) + 1:.1f}", l3[3]]))), True),
        ("목차 줄 번호 1 어긋남", lambda: check_index("\n".join(shifted)), True),
        ("구역 머리 주석에 [이유] 없음", lambda: check_index("\n".join(no_reason)), True),
        ("바이그램 빈도표 복사본 1바이트 변경", lambda: check_table_file(
            bi_ref, bi_saved.replace(b"\t4614\t", b"\t4615\t", 1), bi_saved, PROGRAMS["bigram"]["table_keys"]), True),
    ]

    all_ok = True
    print("자기 검증 (C 출력은 트라이그램에서 받아 조작)")
    for label, fn, should_fail in cases:
        fails = fn()
        failed = bool(fails)
        good = failed == should_fail
        all_ok &= good
        verdict = "FAIL 감지" if failed else "PASS"
        mark = "OK  " if good else "XX  "
        print(f"  [{mark}] {label:<34} -> {verdict}" + (f"  ({fails[0]})" if failed else ""))

    print("\n자기 검증 통과: 망가진 입력은 모두 FAIL, 정상 입력은 PASS" if all_ok
          else "\n자기 검증 실패: 검증기가 문제를 놓친다")
    return all_ok


# ---------------------------------------------------------------------------
# 6. 비교 문서 (--compare): step4_compare.txt
#
# [목적] 바이그램·트라이그램의 사전 일치율을 결과 파일 두 개를 오가며 보지 않고
#   표 하나로 나란히 비교한다 (사용자 요청).
# [동작] 1) 폴더의 result_NN.txt에서 seed와 단어 수가 같은 바이그램·트라이그램 쌍을 찾는다
#   2) 두 파일을 [5] check_result로 먼저 검증한다. 하나라도 실패하면 문서를 만들지 않는다
#   3) 단어 목록과 words.txt로 일치율을 다시 세서(recompute_match) 표를 만든다
#   4) 쌍이 여럿이면 seed마다 표를 하나씩 쓴다. 파일은 매번 새로 덮어쓴다
# [이유] 표의 숫자는 C가 헤더에 쓴 값이 아니라 검증기가 다시 센 값이다. 검증을 통과한
#   파일의 숫자이므로 둘은 같지만, "비교의 근거 숫자"를 독립 재계산 쪽에 두기 위해서다.
#   한글은 터미널·편집기에서 2칸을 차지하므로 표 칸 맞춤은 글자 수가 아니라 표시 폭으로 한다.
# ---------------------------------------------------------------------------
COMPARE_PATH = HERE / "step4_compare.txt"


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


def find_pairs() -> list[tuple[str, pathlib.Path, pathlib.Path]]:
    """seed와 단어 수가 같은 (seed, 바이그램 파일, 트라이그램 파일). 같은 seed가 여럿이면 번호가 큰 파일."""
    found: dict[str, dict[tuple[str, str], pathlib.Path]] = {}
    for name, cfg in PROGRAMS.items():
        found[name] = {}
        for path in sorted(HERE.glob(cfg["result_glob"])):   # 번호 순이라 나중 파일이 덮어씀
            h = parse_header(path.read_bytes().decode("utf-8"))
            found[name][(h.get("seed", ""), h.get("count", ""))] = path
    common = sorted(set(found["bigram"]) & set(found["trigram"]), key=lambda k: found["trigram"][k].name)
    return [(seed, found["bigram"][(seed, cnt)], found["trigram"][(seed, cnt)]) for seed, cnt in common]


def interpret(refs: dict[str, Reference], bi: dict, tri: dict,
              bi_rows: list[tuple[str, str, set[str]]], tri_rows: list[tuple[str, str, set[str]]]) -> list[str]:
    """표 [1][2][3]의 해석. 문장 속 숫자는 전부 여기서 계산한다 (seed가 바뀌어도 맞게).

    [이유] 해석을 손으로 txt에 쓰면 --compare를 다시 돌릴 때 사라지고, 다른 seed에서는
    숫자가 틀린다. 그래서 확률·비율을 words.txt와 결과 파일에서 계산해 문장에 넣는다.
    """
    rb, rt = refs["bigram"], refs["trigram"]
    bi_words = [w for _, w, _ in bi_rows[1:]]
    tri_words = [w for _, w, _ in tri_rows[1:]]
    bi_rate, tri_rate = 100 * bi["hits"] / bi["total"], 100 * tri["hits"] / tri["total"]

    # 생성 단어 속 3글자 조각이 사전 어떤 단어 안에 실제로 있는 비율 (BOS/EOS 없는 순수 3글자)
    real3 = {g for g in rt.grams if BOS not in g and EOS not in g}

    def window_rate(ws: list[str]) -> tuple[int, int]:
        wins = [tuple(w[i:i + 3]) for w in ws for i in range(len(w) - 2)]
        return sum(t in real3 for t in wins), len(wins)

    bw_ok, bw_all = window_rate(bi_words)
    tw_ok, tw_all = window_rate(tri_words)

    # 탐욕적 첫 글자 c에서 단어가 바로 끝날 확률: 바이그램 P(EOS|c) vs 트라이그램 P(EOS|BOS,c)
    c = bi_rows[0][1][0]
    p_bi = 100 * rb.grams[(c, EOS)] / rb.row_total[(c,)]
    p_tri = 100 * rt.grams[(BOS, c, EOS)] / rt.row_total[(BOS, c)]
    tri_next = max(rt.rows[(BOS, c)], key=lambda bc: (bc[1], -rt.order[bc[0]]))
    starts_c = rt.row_total[(BOS, c)]

    diffs = {b: 100 * (tri["hit"][b] / tri["gen"][b] - bi["hit"][b] / bi["gen"][b])
             for b in range(1, LEN_BUCKETS + 1) if bi["gen"][b] and tri["gen"][b]}
    best = max(diffs, key=diffs.get)
    zero_from = next((b for b in range(1, LEN_BUCKETS + 1)
                      if all(tri["hit"][k] == 0 for k in range(b, LEN_BUCKETS + 1))), None)

    def dist(m: dict) -> float:   # 사전 길이 분포와의 차이 = ½ Σ |생성 비율 - 사전 비율|
        return sum(abs(100 * m["gen"][b] / m["total"] - m["share"][b]) for b in range(1, LEN_BUCKETS + 1)) / 2

    def share(m: dict, b: int) -> float:
        return 100 * m["gen"][b] / m["total"]

    L = LEN_BUCKETS
    more_short = share(tri, 2) > bi["share"][2]
    lines = [
        "=" * 72,
        "해석",
        "=" * 72,
        "",
        "[1] 전체 사전 일치율",
        f"- 1,000개 중 사전에 있는 단어가 {bi['hits']}개 -> {tri['hits']}개로 늘었다 "
        f"({bi_rate:.1f}% -> {tri_rate:.1f}%, {tri_rate - bi_rate:+.1f}%p).",
        "- 왜 늘었나: 생성 단어를 3글자씩 잘라 보면, 그 조각이 사전 어떤 단어 안에 실제로 있는 비율이",
        f"  바이그램 {100 * bw_ok / bw_all:.1f}% ({bw_ok}/{bw_all}) / 트라이그램 {100 * tw_ok / tw_all:.1f}% ({tw_ok}/{tw_all}) 이다.",
        "  바이그램은 2글자 조각만 사전에서 왔다는 것이 보장된다. 그래서 서로 다른 단어의 2글자 조각을",
        "  겹치는 글자에서 이어 붙인 '그럴듯하지만 없는 단어'가 많다.",
        "  트라이그램은 앞 2글자를 보고 고르므로 3글자 조각이 항상 사전에 있던 조각이다 (구조상 100%).",
        "  같은 이어 붙이기라도 겹치는 부분이 2글자로 길어져서 실제 단어와 맞을 가능성이 커진다.",
        f"- 탐욕적 디코딩: 바이그램 '{bi_rows[0][1]}' / 트라이그램 '{tri_rows[0][1]}'.",
        f"  둘 다 첫 글자는 사전에서 가장 많은 첫 글자 '{c}'이다. 바이그램은 '{c}' 다음에 EOS가 1위라",
        f"  P(EOS|{c}) = {p_bi:.1f}% 로 바로 끝난다 ('{c}'로 끝나는 단어가 많기 때문).",
        f"  트라이그램은 '단어 첫 글자인 {c}'를 따로 보므로 P(EOS|BOS,{c}) = {p_tri:.2f}% 이다.",
        f"  '{c}'로 시작하는 단어 {starts_c:,}개 중 바로 끝나는 단어가 거의 없어서 1위인 '{tri_next[0]}'({tri_next[1]:,}회)로 이어진다.",
        "- 강의(raw 챕터2-6)는 바이그램 40%, 트라이그램 61.7%였다. 생성 1,000개라 seed에 따라",
        "  몇 %p씩 흔들린다 (docs/06의 10만 개 실측: 41.6% / 57.5%).",
        "",
        "[2] 길이별 사전 일치율",
        f"- 1글자: 둘 다 거의 100%라 일치율 차이는 없다. 차이는 생성 수다 ({bi['gen'][1]}개 vs {tri['gen'][1]}개, [3] 참고).",
        f"- 2글자: {pct(bi['hit'][2], bi['gen'][2])} -> {pct(tri['hit'][2], tri['gen'][2])}. 트라이그램에서 2글자 단어 'ab'가 나오려면",
        "  (BOS,BOS,a) (BOS,a,b) (a,b,EOS)가 모두 사전에 있어야 한다. 즉 'ab로 시작하는 단어'와",
        "  'ab로 끝나는 단어'가 둘 다 있어야 해서 사실상 사전 단어만 나온다.",
        "  바이그램은 'a로 시작', 'ab', 'b로 끝남'이 각각 따로 있기만 하면 되므로 없는 단어가 섞인다.",
        f"- 차이가 가장 큰 길이는 {best}글자다 ({diffs[best]:+.1f}%p). 3~4글자는 문맥 2글자가 단어의 대부분을",
        "  덮는 길이라 트라이그램의 효과가 가장 크게 나타난다.",
        f"- 길수록 둘 다 떨어진다. 트라이그램도 "
        + (f"{zero_from}글자 이상은 0%다." if zero_from else "긴 단어에서 일치율이 크게 낮다.")
        + " 문맥이 2글자뿐이라",
        "  앞쪽 글자를 잊어버린 채 이어 붙이므로, 단어가 길어질수록 전체가 한 단어와 맞을 확률이 줄어든다.",
        "- 일치율이 0%인 긴 단어가 모두 나쁜 것은 아니다. 강의 말처럼 사전에 없지만 그럴듯한 단어를",
        "  만드는 것도 언어 모델의 능력이다. 일치율은 '사전을 얼마나 닮았나'만 보여 주는 지표다.",
        "",
        "[3] 생성 단어 길이 분포 vs 사전",
        f"- 사전 분포와의 차이(각 길이 비율 차이의 절반 합, 0이면 사전과 같은 분포):",
        f"  바이그램 {dist(bi):.1f}%p / 트라이그램 {dist(tri):.1f}%p -> 트라이그램이 사전의 길이 분포에 더 가깝다.",
        f"- 1글자: 사전 {bi['share'][1]:.2f}% / 바이그램 {share(bi, 1):.1f}% / 트라이그램 {share(tri, 1):.1f}%.",
        f"  바이그램의 1글자 과다는 [1]의 P(EOS|{c}) = {p_bi:.1f}% 같은 값이 단어 첫 글자에도 그대로 쓰여서 생긴다.",
        "  바이그램은 글자가 단어의 몇 번째인지 모른다. 트라이그램은 (BOS, 글자) 문맥이 '첫 글자'임을 알려 준다.",
        f"- {L}글자 이상: 사전 {bi['share'][L]:.2f}% / 바이그램 {share(bi, L):.1f}% / 트라이그램 {share(tri, L):.1f}%.",
        "  끝낼지 말지를 앞 1~2글자만 보고 정하므로 둘 다 사전보다 긴 단어가 많다. 트라이그램이 덜하다.",
        f"- 2글자: 트라이그램 {share(tri, 2):.1f}%로 사전 {bi['share'][2]:.2f}%보다 "
        + ("많다. 짧은 단어 쪽으로 약간 치우쳐 있다." if more_short else "적다."),
        f"  4글자(사전에서 가장 많은 길이 {bi['share'][4]:.2f}%)는 바이그램 {share(bi, 4):.1f}% / 트라이그램 {share(tri, 4):.1f}%로 둘 다 모자란다.",
        "",
        "[정리]",
        "- 문맥을 1글자에서 2글자로 늘리자 '단어 안 위치'와 '3글자 조각'이 맞아서 일치율과 길이 분포가 모두 사전에 가까워졌다.",
        "- 다만 문맥 2글자로는 긴 단어 전체를 맞추지 못한다. n을 더 키우면 일치율은 더 오르지만,",
        f"  평균 단어 길이({sum(map(len, rb.words)) / len(rb.words):.2f}글자)에 가까워질수록 사전을 외워 그대로 내놓게 된다"
        " (과적합, docs/06 참고).",
        "",
    ]
    return lines


def questions(refs: dict[str, Reference], bi: dict, tri: dict) -> list[str]:
    """사용자가 표를 보고 물은 질문과 답 (2026-10-21). 숫자는 interpret()처럼 계산해서 넣는다."""
    rb = refs["bigram"]
    n_words = len(rb.words)
    dict_len = collections.Counter(len_bucket(len(w)) for w in rb.words)

    def without_1(m: dict) -> tuple[int, int]:
        return m["hits"] - m["hit"][1], m["total"] - m["gen"][1]

    bh, bt = without_1(bi)
    th, tt = without_1(tri)
    bi_short = bi["hit"][1] + bi["hit"][2]
    tri_short = tri["hit"][1] + tri["hit"][2]
    rows = [[f"{b}+" if b == LEN_BUCKETS else str(b), f"{dict_len[b]:,}", f"{100 * dict_len[b] / n_words:.2f}%",
             f"{1000 * dict_len[b] / n_words:.1f}"] for b in range(1, LEN_BUCKETS + 1)]
    return [
        "=" * 72,
        "질문과 답 (표를 보고 이해가 안 됐던 것)",
        "=" * 72,
        "",
        "Q1. 길이별 사전 일치율은 왜 필요한가? 전체 일치율 하나면 되지 않나?",
        "A1. 전체 일치율만 보면 '짧은 단어를 많이 만든 모델'이 점수를 쉽게 딴다.",
        "    1글자 단어('가', '나', '사' ...)는 거의 항상 사전에 있어서, 1글자를 많이 만들면 일치율이 저절로 오른다.",
        "",
        *render_table(["", "바이그램", "트라이그램"], [
            ["전체 일치 수", f"{bi['hits']}개", f"{tri['hits']}개"],
            ["그중 1글자 단어", f"{bi['hit'][1]}개 ({pct(bi['hit'][1], bi['hits'])})",
             f"{tri['hit'][1]}개 ({pct(tri['hit'][1], tri['hits'])})"],
            ["그중 1~2글자 단어", f"{bi_short}개 ({pct(bi_short, bi['hits'])})",
             f"{tri_short}개 ({pct(tri_short, tri['hits'])})"],
            ["전체 일치율", pct(bi["hits"], bi["total"]), pct(tri["hits"], tri["total"])],
            ["1글자를 빼고 다시 계산", f"{bh} / {bt} = {pct(bh, bt)}", f"{th} / {tt} = {pct(th, tt)}"],
        ]),
        "",
        f"    - 바이그램 일치의 {pct(bi['hit'][1], bi['hits'])}가 1글자 단어에서 나왔다. 1글자를 빼면 {pct(bh, bt)}로 떨어진다.",
        f"    - 트라이그램은 1글자를 거의 안 만들어서 빼도 {pct(th, tt)}로 거의 그대로다.",
        f"    - 그래서 실력 차이는 전체 숫자의 {100 * (tri['hits'] / tri['total'] - bi['hits'] / bi['total']):+.1f}%p가 아니라"
        f" 1글자를 뺀 {100 * (th / tt - bh / bt):+.1f}%p에 가깝다.",
        "      전체 숫자는 이 차이를 가린다. 길이별 표는 그걸 드러내려고 본다.",
        "    - 비유: 시험에서 쉬운 문제(1글자)와 어려운 문제(3~4글자)의 정답률을 따로 보는 것.",
        "",
        "Q2. '생성 단어 길이 분포 vs 사전' 표는 무엇을 보는 표인가?",
        "A2. '모델이 사전 단어와 비슷한 길이로 단어를 만드는가'를 본다.",
        "    - 길이별 일치율 표 = 내용이 사전과 맞나 / 길이 분포 표 = 길이가 사전과 닮았나",
        f"    - 사전 단어는 3~4글자가 대부분({100 * (dict_len[3] + dict_len[4]) / n_words:.1f}%)이고 1글자는"
        f" {100 * dict_len[1] / n_words:.2f}%뿐이다.",
        f"      그런데 바이그램은 생성 1,000개 중 1글자가 {bi['gen'][1]}개, 트라이그램은 {tri['gen'][1]}개다.",
        "    - 두 표는 이어져 있다. 바이그램은 길이 분포가 1글자 쪽으로 치우쳤고(분포 표),",
        "      그 1글자가 거의 다 사전 단어라서 전체 일치율이 부풀려졌다(Q1).",
        "",
        "Q3. 모델이 1,000개를 확률 샘플링한 것처럼, 사전도 1,000개를 샘플링해서 비교한 것인가?",
        f"A3. 아니다. '사전' 칸은 뽑은 것이 아니라 words.txt 단어 {n_words:,}개 전부를 길이별로 센 값이다 (무작위성 없음).",
        "    '1,000개로 치면 몇 개'는 비율을 감 잡기 쉽게 환산한 것일 뿐, 실제로 뽑아 본 것이 아니다.",
        "",
        *render_table(["길이", "사전 단어 수(전체)", f"비율 = 개수 / {n_words:,}", "1,000개로 환산"], rows),
        "",
        "    - 개수가 달라도 비교할 수 있는 이유: 모델 칸 = 생성 1,000개 중 비율, 사전 칸 = 전체 중 비율. 둘 다 %다.",
        "    - 사전은 전체를 셀 수 있으니 전체를 센다. 1,000개만 뽑으면 뽑을 때마다 값이 흔들린다",
        "      (1글자가 2개일 수도 5개일 수도 있다). 전체를 세면 흔들리지 않는 기준값이 된다.",
        "    - 모델은 만들 수 있는 단어가 끝없이 많아 전체를 셀 수 없다. 그래서 1,000개로 추정하고 seed마다 조금 흔들린다.",
        "    - 모델이 사전을 완벽하게 흉내 낸다면, 아주 많이 생성했을 때 길이 비율이 이 사전 비율에 가까워져야 한다.",
        "      즉 사전 칸은 '목표 분포'다.",
        "",
    ]


def compare() -> bool:
    words = load_words(SOURCE)
    refs = {name: Reference(words, cfg["n"]) for name, cfg in PROGRAMS.items()}
    pairs = find_pairs()
    if not pairs:
        print("[오류] seed와 단어 수가 같은 바이그램·트라이그램 결과 파일 쌍이 없습니다")
        print("       예: step4_trigram.exe 실행 후, 그 결과 파일의 seed로 step4_bigram.exe 1000 <seed>")
        return False

    out = ["# 바이그램 vs 트라이그램 사전 일치율 비교",
           "# 만든 프로그램: step4_verify.py --compare",
           "# 사전 일치율 = 생성 단어 중 words.txt에 있는 단어의 비율",
           "# 숫자 출처: 결과 파일의 단어 목록을 words.txt와 대조해 검증기가 다시 센 값",
           "#            (결과 파일이 [5] 검증을 통과한 경우에만 문서를 만든다)",
           ""]
    for seed, bi_path, tri_path in pairs:
        texts = {"bigram": bi_path.read_bytes().decode("utf-8"),
                 "trigram": tri_path.read_bytes().decode("utf-8")}
        for name, text in texts.items():
            fails = check_result(refs[name], text)
            if fails:
                print(f"[오류] {name} 결과 파일이 검증을 통과하지 못해 비교 문서를 만들지 않습니다: {fails[0]}")
                return False
        bi_rows = parse_words(texts["bigram"])
        tri_rows = parse_words(texts["trigram"])
        bi = recompute_match(refs["bigram"], [w for _, w, _ in bi_rows[1:]])
        tri = recompute_match(refs["trigram"], [w for _, w, _ in tri_rows[1:]])

        out += [f"## seed {seed} / 각 {bi['total']:,}개 생성",
                f"   바이그램  : {bi_path.name}",
                f"   트라이그램: {tri_path.name}",
                "",
                "[1] 전체 사전 일치율",
                *render_table(["", "바이그램", "트라이그램", "차이"], [
                    ["일치 / 생성", f"{bi['hits']} / {bi['total']}", f"{tri['hits']} / {tri['total']}",
                     f"{tri['hits'] - bi['hits']:+d}개"],
                    ["사전 일치율", pct(bi["hits"], bi["total"]), pct(tri["hits"], tri["total"]),
                     f"{100 * (tri['hits'] / tri['total'] - bi['hits'] / bi['total']):+.1f}%p"],
                    ["탐욕적 디코딩", bi_rows[0][1], tri_rows[0][1], ""],
                ]),
                "",
                "[2] 길이별 사전 일치율 (일치 수 / 생성 수, 일치율)",
                *render_table(["길이", "바이그램", "일치율", "트라이그램", "일치율", "차이"], [
                    [f"{b}+" if b == LEN_BUCKETS else str(b),
                     f"{bi['hit'][b]} / {bi['gen'][b]}", pct(bi["hit"][b], bi["gen"][b]),
                     f"{tri['hit'][b]} / {tri['gen'][b]}", pct(tri["hit"][b], tri["gen"][b]),
                     f"{100 * (tri['hit'][b] / tri['gen'][b] - bi['hit'][b] / bi['gen'][b]):+.1f}%p"
                     if bi["gen"][b] and tri["gen"][b] else "-"]
                    for b in range(1, LEN_BUCKETS + 1)
                ]),
                "",
                "[3] 생성 단어 길이 분포 vs 사전 (각 길이가 차지하는 비율)",
                *render_table(["길이", "사전", "바이그램", "트라이그램"], [
                    [f"{b}+" if b == LEN_BUCKETS else str(b), f"{bi['share'][b]:.2f}%",
                     pct(bi["gen"][b], bi["total"]), pct(tri["gen"][b], tri["total"])]
                    for b in range(1, LEN_BUCKETS + 1)
                ]),
                "",
                *interpret(refs, bi, tri, bi_rows, tri_rows),
                *questions(refs, bi, tri)]

    COMPARE_PATH.write_bytes("\n".join(out).encode("utf-8"))   # UTF-8, LF
    print("\n".join(out))
    print(f"비교 문서: {COMPARE_PATH.name} (쌍 {len(pairs)}개)")
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
