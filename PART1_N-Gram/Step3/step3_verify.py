"""
step3_verify.py - [STEP 3] 바이그램 프로그램(step3_bigram.exe)의 세 구현을 독립적으로 검증한다.

===== 무엇을 검증하나 =====
step3_bigram.exe 안에는 구현이 세 개 있다 (array / hash / csr).
세 구현 각각에 대해 아래 [1]~[4]를 따로 돌리고, 일반 실행 결과로 [5][6]을 본다.
  [1] 쌍 빈도 전수 비교   : 171,425쌍 전부가 Python 재계산과 같은가 (--dump)
  [2] 행 합계            : 각 앞 글자의 합계가 "그 글자의 유니그램 빈도"와 같은가 (--rows)
  [3] r -> 글자 대응      : 모든 후보 구간의 시작·끝 r + 무작위 r에서 맞는 글자가 나오나 (--map)
  [4] 분포 (카이제곱)     : 몇 개 행에서 대량으로 뽑은 횟수가 실제 확률과 맞나 (--hist)
  [5] 빈도표 파일        : step3_bigram_table.txt 형식·순서·빈도·확률
  [6] 생성 결과 파일      : 탐욕적 결과, 샘플 단어의 모든 쌍이 실제로 있는가, 표시(*, ~), 세 구현 일치

===== 독립성 (CLAUDE.md "검증 규칙") =====
- 기준값은 전부 원본 words.txt에서 Python으로 처음부터 다시 센다. C 출력을 기준으로 쓰지 않는다.
- C는 배열 / 해시 / CSR + 이진 탐색으로 찾지만, 여기서는 Counter(파이썬 딕셔너리)로 세고
  r -> 글자는 "구간 [시작, 끝]을 직접 나열"해서 정답을 만든다. 탐색 코드를 복사하지 않는다.
- [2]의 기준은 쌍 세기가 아니라 글자 하나씩 센 유니그램 빈도다. 같은 쌍 세기 버그를
  기준값까지 공유하지 않도록 다른 방법으로 센다.
- C 프로그램은 subprocess로 실행하고 출력만 받는다. 결과 파일은 임시 폴더에 쓰게 해서
  Step3 폴더의 번호 붙은 결과 파일과 빈도표를 건드리지 않는다.
- 랜덤 샘플 단어 자체에는 정답 문자열이 없다. 그래서 "모든 쌍이 실제로 존재하는가"처럼
  성질을 검사하고, 분포는 [4] 카이제곱으로 따로 검사한다.

===== 사용법 =====
  python step3_verify.py               # 검증
  python step3_verify.py --self-test   # 망가진 출력을 흉내 낸 입력에 FAIL을 내는지 확인
"""

from __future__ import annotations

import bisect
import collections
import math
import pathlib
import random
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).parent
SOURCE = HERE.parent.parent / "data" / "korean_dictionary" / "words.txt"
TARGET = HERE / "step3_bigram.exe"
VERSIONS = ["array", "hash", "csr"]

BOS, EOS = "BOS", "EOS"
MAX_LEN = 20                      # C의 MAX_LEN과 같은 규칙 (단어 최대 길이)
MAP_RANDOM_COUNT = 10_000         # [3] 경계값 외에 넣는 무작위 질의 수
HIST_ROWS = [BOS, "사", "다", "리"]  # [4] 분포를 검사할 앞 글자 (큰 행 / 강의에 나온 행)
HIST_SAMPLES = 100_000            # [4] 행마다 뽑는 횟수
HIST_SEED = 12345
P_VALUE_THRESHOLD = 1e-3          # [4] p < 0.001 이면 "분포가 다르다"
GEN_COUNT, GEN_SEED = 20, 2026    # [5][6] 일반 실행 인자


# ---------------------------------------------------------------------------
# 1. 원본에서 독립 재계산
#
# [목적] C와 비교할 기준 데이터를 words.txt에서 직접 만든다.
# [동작]
#   - 단어마다 [BOS, 글자..., EOS]로 감싸 인접 쌍을 Counter로 센다
#   - 글자 번호는 C와 같은 "규칙"(코드포인트 오름차순, 그 뒤 BOS, EOS)으로 정한다.
#     이건 구현을 베낀 게 아니라 프로그램이 지켜야 할 사양이다. 후보 순서가 이 규칙을
#     따라야 같은 r에 같은 글자가 나오므로 [3]의 정답을 만들려면 사양을 알아야 한다.
#   - 행(앞 글자)마다 후보를 번호 순으로 나열하고 각 후보의 r 구간 [시작, 끝]을 만든다
#     '가' 행: 가(3), 대(2) -> 가 = [0, 2], 대 = [3, 4]
# ---------------------------------------------------------------------------
class Reference:
    """words.txt에서 직접 만든 기준 데이터."""

    def __init__(self, source: pathlib.Path) -> None:
        text = source.read_bytes().decode("utf-8")
        self.words = [w.rstrip("\r") for w in text.split("\n") if w.rstrip("\r")]
        self.word_set = set(self.words)

        # 글자 번호: 코드포인트 오름차순 -> BOS -> EOS
        chars = sorted({c for w in self.words for c in w})
        self.order = {c: i for i, c in enumerate(chars)}
        self.order[BOS] = len(chars)
        self.order[EOS] = len(chars) + 1
        self.n_chars = len(chars)

        # 쌍 세기 (BOS/EOS 포함, 단어 안에서만)
        self.pairs: collections.Counter[tuple[str, str]] = collections.Counter()
        for w in self.words:
            tokens = [BOS, *w, EOS]
            self.pairs.update(zip(tokens, tokens[1:]))

        # [2]의 기준: 쌍이 아니라 글자를 하나씩 센 유니그램 빈도 (다른 방법으로 센 값)
        self.unigram = collections.Counter(c for w in self.words for c in w)

        # 행마다 후보(번호 순)와 r 구간
        rows: dict[str, list[tuple[str, int]]] = collections.defaultdict(list)
        for (a, b), n in self.pairs.items():
            rows[a].append((b, n))
        self.rows: dict[str, list[tuple[str, int]]] = {}
        self.row_total: dict[str, int] = {}
        self.intervals: dict[str, list[tuple[str, int, int]]] = {}   # (후보, 시작 r, 끝 r)
        for a, cands in rows.items():
            cands.sort(key=lambda bn: self.order[bn[0]])
            self.rows[a] = cands
            start = 0
            spans = []
            for b, n in cands:
                spans.append((b, start, start + n - 1))
                start += n
            self.intervals[a] = spans
            self.row_total[a] = start

    def greedy_word(self) -> tuple[str, bool]:
        """탐욕적 디코딩의 정답: 행마다 빈도 최대(동률이면 번호 작은 후보). (단어, 잘림 여부)"""
        prev, out = BOS, []
        while True:
            best = max(self.rows[prev], key=lambda bn: (bn[1], -self.order[bn[0]]))[0]
            if best == EOS:
                return "".join(out), False
            if len(out) == MAX_LEN:
                return "".join(out), True
            out.append(best)
            prev = best


# ---------------------------------------------------------------------------
# 2. 구현 실행 (출력만 받는다)
# ---------------------------------------------------------------------------
def run(args: list[str], stdin: str | None = None) -> str:
    proc = subprocess.run([str(TARGET), *args], input=stdin, capture_output=True,
                          text=True, encoding="utf-8", cwd=HERE)
    if proc.returncode != 0:
        raise RuntimeError(f"실행 실패 ({proc.returncode}): {' '.join(args)}\n{proc.stderr}")
    return proc.stdout


def get_dump(version: str) -> dict[tuple[str, str], int]:
    out: dict[tuple[str, str], int] = {}
    for line in run(["--dump", version]).splitlines():
        a, b, n = line.split("\t")
        out[(a, b)] = int(n)
    return out


def get_rows(version: str) -> dict[str, int]:
    return {a: int(n) for a, n in (line.split("\t") for line in run(["--rows", version]).splitlines())}


def build_map_queries(ref: Reference) -> list[tuple[str, int, str]]:
    """(앞 글자, r, 정답 글자). 모든 구간의 시작·끝 + 무작위 r.

    한 칸 어긋남('>' 대신 '>=')은 구간 경계에서만 드러나므로 경계를 전부 넣는다.
    무작위 r의 정답은 bisect로 찾는다 (C의 이진 탐색 코드와는 별개의 표준 라이브러리).
    """
    queries = [(a, r, b) for a, spans in ref.intervals.items()
               for b, lo, hi in spans for r in (lo, hi)]
    rng = random.Random(7)
    keys = sorted(ref.intervals)
    for _ in range(MAP_RANDOM_COUNT):
        a = rng.choice(keys)
        spans = ref.intervals[a]
        r = rng.randrange(ref.row_total[a])
        ends = [hi for _, _, hi in spans]
        queries.append((a, r, spans[bisect.bisect_left(ends, r)][0]))
    return queries


def get_map(version: str, queries: list[tuple[str, int, str]]) -> list[str]:
    stdin = "".join(f"{a} {r}\n" for a, r, _ in queries)
    return run(["--map", version], stdin).split()


def get_hist(version: str, prev: str) -> dict[str, int]:
    # 앞 글자는 인자가 아니라 표준입력으로 준다 (Windows argv는 CP949라 한글이 깨진다)
    out = run(["--hist", version, str(HIST_SAMPLES), str(HIST_SEED)], prev + "\n")
    return {b: int(n) for b, n in (line.split("\t") for line in out.splitlines())}


def get_generated(tmp: pathlib.Path) -> tuple[str, str]:
    result, table = tmp / "result.txt", tmp / "table.txt"
    run([str(GEN_COUNT), str(GEN_SEED), str(result), str(table)])
    return result.read_bytes().decode("utf-8"), table.read_bytes().decode("utf-8")


# ---------------------------------------------------------------------------
# 3. 판정 함수 (실패 메시지 목록을 돌려준다. 비어 있으면 통과)
# ---------------------------------------------------------------------------
def check_dump(ref: Reference, dump: dict[tuple[str, str], int]) -> list[str]:
    """[1] 쌍 빈도 전수 비교: 빠진 쌍 / 없어야 할 쌍 / 빈도가 다른 쌍."""
    missing = [k for k in ref.pairs if k not in dump]
    extra = [k for k in dump if k not in ref.pairs]
    wrong = [k for k in ref.pairs if k in dump and dump[k] != ref.pairs[k]]
    fails = []
    if missing:
        fails.append(f"빠진 쌍 {len(missing)}개 (예: {missing[0]})")
    if extra:
        fails.append(f"없어야 할 쌍 {len(extra)}개 (예: {extra[0]} = {dump[extra[0]]})")
    if wrong:
        k = wrong[0]
        fails.append(f"빈도 불일치 {len(wrong)}개 (예: {k} {dump[k]} != {ref.pairs[k]})")
    return fails


def check_rows(ref: Reference, rows: dict[str, int]) -> list[str]:
    """[2] 행 합계 = 유니그램 빈도 (BOS 행 = 단어 수, EOS 행 = 0)."""
    expected = {c: ref.unigram[c] for c in ref.unigram}
    expected[BOS] = len(ref.words)
    expected[EOS] = 0
    fails = []
    if set(rows) != set(expected):
        fails.append(f"행 이름 집합이 다름 ({len(rows)}행, 기대 {len(expected)}행)")
    wrong = [a for a in expected if rows.get(a) != expected[a]]
    if wrong:
        a = wrong[0]
        fails.append(f"합계 불일치 {len(wrong)}행 (예: '{a}' {rows.get(a)} != {expected[a]})")
    return fails


def check_map(queries: list[tuple[str, int, str]], answers: list[str]) -> list[str]:
    """[3] r -> 글자 대응."""
    if len(answers) != len(queries):
        return [f"응답 수 {len(answers)} != 질의 수 {len(queries)}"]
    wrong = [(q, got) for q, got in zip(queries, answers) if got != q[2]]
    if wrong:
        (a, r, exp), got = wrong[0]
        return [f"{len(wrong)}건 불일치 (예: 앞 '{a}' r={r} -> '{got}', 정답 '{exp}')"]
    return []


def chi_square_p(stat: float, df: int) -> float:
    """카이제곱 분포의 위쪽 꼬리 확률. Wilson-Hilferty 근사 (자유도가 클 때 충분히 정확)."""
    if df <= 0:
        return 1.0
    z = ((stat / df) ** (1 / 3) - (1 - 2 / (9 * df))) / math.sqrt(2 / (9 * df))
    return 0.5 * math.erfc(z / math.sqrt(2))


def check_hist(ref: Reference, prev: str, hist: dict[str, int]) -> tuple[list[str], str]:
    """[4] 한 행의 분포. 기대 빈도 5 미만 후보는 하나로 묶는다 (카이제곱 근사의 전제)."""
    fails = []
    n = sum(hist.values())
    if n != HIST_SAMPLES:
        fails.append(f"샘플 합계 {n} != {HIST_SAMPLES}")
    cands = dict(ref.rows[prev])
    unknown = set(hist) - set(cands)
    if unknown:
        fails.append(f"이 행에 없는 후보가 뽑힘: {sorted(unknown)[:3]}")
    if n == 0:
        return fails + ["샘플 없음"], ""
    total = ref.row_total[prev]
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


def check_table(ref: Reference, text: str) -> list[str]:
    """[5] 빈도표 파일: 헤더 / 쌍 전수 / 행 순서(BOS 먼저, 번호 순) / 행 안 순서 / 확률."""
    fails = []
    header = parse_header(text)
    expect = {"words": str(len(ref.words)), "total_pairs": str(sum(ref.pairs.values())),
              "unique_pairs": str(len(ref.pairs)), "rows": str(len(ref.rows))}
    for k, v in expect.items():
        if header.get(k) != v:
            fails.append(f"헤더 {k}={header.get(k)} (기대 {v})")

    lines = [ln for ln in text.split("\n") if ln and not ln.startswith("#")]
    seen: dict[tuple[str, str], int] = {}
    row_order: list[str] = []
    bad_pct = bad_sort = 0
    last: tuple[str, int, int] | None = None
    for ln in lines:
        a, b, n_s, pct_s = ln.split("\t")
        n = int(n_s)
        seen[(a, b)] = n
        if not row_order or row_order[-1] != a:
            row_order.append(a)
            last = None
        # 확률: 행 합계로 나눈 % (소수 6자리 반올림이므로 0.5e-6까지 허용)
        if abs(float(pct_s) - 100 * n / ref.row_total.get(a, 1)) > 0.5e-6 + 1e-12:
            bad_pct += 1
        # 행 안 순서: 빈도 내림차순, 같으면 번호 오름차순
        key = (a, -n, ref.order.get(b, -1))
        if last is not None and key < last:
            bad_sort += 1
        last = key
    fails += [f"[쌍] {f}" for f in check_dump(ref, seen)]
    if len(seen) != len(lines):
        fails.append(f"중복 줄 {len(lines) - len(seen)}개")
    expected_rows = [BOS] + sorted((a for a in ref.rows if a != BOS), key=ref.order.__getitem__)
    if row_order != expected_rows:
        fails.append("행 순서가 BOS 먼저 + 번호(가나다) 순이 아님")
    if bad_pct:
        fails.append(f"확률이 틀린 줄 {bad_pct}개")
    if bad_sort:
        fails.append(f"행 안 정렬(빈도 내림차순)이 틀린 곳 {bad_sort}개")
    return fails


def parse_result(text: str) -> dict[str, list[tuple[str, str, set[str]]]]:
    """[이름] 구역별로 (키, 단어, 표시 집합) 목록."""
    sections: dict[str, list[tuple[str, str, set[str]]]] = {}
    current = None
    for line in text.split("\n"):
        if not line or line.startswith("#"):
            continue
        if line.startswith("["):
            current = line[1:line.index("]")]
            sections[current] = []
            continue
        key, word, *marks = line.split("\t")
        sections[current].append((key, word, set(marks)))
    return sections


def word_pairs_ok(ref: Reference, word: str, truncated: bool) -> bool:
    """단어의 모든 인접 쌍(BOS->첫 글자 ... 끝 글자->EOS)이 사전에 실제로 있었나."""
    tokens = [BOS, *word] + ([] if truncated else [EOS])
    return all(p in ref.pairs for p in zip(tokens, tokens[1:]))


def check_result(ref: Reference, text: str) -> list[str]:
    """[6] 생성 결과: 헤더 / 시간 표 / 탐욕적 / 샘플 성질 / 표시 / 세 구현 일치."""
    fails = []
    header = parse_header(text)
    expect = {"words": str(len(ref.words)), "chars": str(ref.n_chars), "vocab": str(ref.n_chars + 2),
              "total_pairs": str(sum(ref.pairs.values())), "unique_pairs": str(len(ref.pairs)),
              "seed": str(GEN_SEED), "count": str(GEN_COUNT), "max_len": str(MAX_LEN),
              "same_output": "YES"}
    for k, v in expect.items():
        if header.get(k) != v:
            fails.append(f"헤더 {k}={header.get(k)} (기대 {v})")
    for v in VERSIONS:
        cols = header.get(v, "").split("\t")
        if len(cols) != 4 or not all(float(c) >= 0 for c in cols):
            fails.append(f"시간 표의 {v} 줄 형식이 잘못됨: {header.get(v)}")

    sections = parse_result(text)
    if list(sections) != VERSIONS:
        return fails + [f"구역이 {VERSIONS}가 아님: {list(sections)}"]

    g_word, g_trunc = ref.greedy_word()
    for v, rows in sections.items():
        keys = [k for k, _, _ in rows]
        if keys != ["greedy"] + [str(i) for i in range(1, GEN_COUNT + 1)]:
            fails.append(f"[{v}] 줄 구성이 greedy + 1..{GEN_COUNT}이 아님")
            continue
        _, gw, gm = rows[0]
        if gw != g_word or ("~" in gm) != g_trunc:
            fails.append(f"[{v}] 탐욕적 '{gw}' != 기대 '{g_word}'")
        for key, word, marks in rows:
            trunc = "~" in marks
            if not word or len(word) > MAX_LEN or (trunc and len(word) != MAX_LEN):
                fails.append(f"[{v}] {key}번 길이 이상: '{word}'")
            elif not word_pairs_ok(ref, word, trunc):
                fails.append(f"[{v}] {key}번 '{word}'에 사전에 없는 쌍이 있음")
            if ("*" in marks) != (word in ref.word_set):
                fails.append(f"[{v}] {key}번 '{word}' 사전 표시(*)가 틀림")
    base = sections[VERSIONS[0]]
    for v in VERSIONS[1:]:
        if sections[v] != base:
            fails.append(f"[{v}] 단어 목록이 [{VERSIONS[0]}]와 다름")
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
            for f in fails:
                print(f"  [FAIL] {label:<24} {f}")
        else:
            print(f"  [OK  ] {label:<24} {detail}")


def verify() -> bool:
    ref = Reference(SOURCE)
    queries = build_map_queries(ref)
    print(f"기준 데이터 (words.txt 재계산): 단어 {len(ref.words):,} / 글자 {ref.n_chars:,}종 / "
          f"서로 다른 쌍 {len(ref.pairs):,} / 쌍 총수 {sum(ref.pairs.values()):,}")
    report = Report()

    for v in VERSIONS:
        print(f"\n=== 구현: {v} ===")
        report.item("[1] 쌍 빈도 전수", check_dump(ref, get_dump(v)), f"{len(ref.pairs):,}쌍 전부 일치")
        report.item("[2] 행 합계=유니그램", check_rows(ref, get_rows(v)),
                    f"{len(ref.rows) + 1:,}행 (BOS=단어 수, EOS=0)")
        report.item("[3] r->글자 대응", check_map(queries, get_map(v, queries)),
                    f"경계 {len(queries) - MAP_RANDOM_COUNT:,}개 + 무작위 {MAP_RANDOM_COUNT:,}개")
        for prev in HIST_ROWS:
            fails, detail = check_hist(ref, prev, get_hist(v, prev))
            report.item(f"[4] 분포 '{prev}' 행", fails, detail)

    print("\n=== 일반 실행 (임시 폴더에 결과·빈도표) ===")
    with tempfile.TemporaryDirectory() as d:
        result, table = get_generated(pathlib.Path(d))
    report.item("[5] 빈도표 파일", check_table(ref, table), f"{len(ref.pairs):,}줄, 순서·확률 포함")
    g_word, _ = ref.greedy_word()
    report.item("[6] 생성 결과 파일", check_result(ref, result),
                f"탐욕적 '{g_word}', 샘플 {GEN_COUNT}개 x 3구현 일치, 모든 쌍 실재")

    print("\n=== 검증 통과 ===" if report.ok else "\n=== 검증 실패 ===")
    return report.ok


# ---------------------------------------------------------------------------
# 5. 자기 검증: 망가진 구현의 출력을 흉내 내서 넣으면 반드시 FAIL이 나와야 한다
#
# [이유] 검증기가 언제나 OK만 낸다면 검증이 아니다. 실제로 생길 법한 버그를
#   하나씩 흉내 낸 출력을 판정 함수에 넣고 FAIL이 나는지 확인한다.
#   C 출력은 csr 구현 하나만 받아 와서 조작한다 (판정 함수는 구현과 무관하므로).
# ---------------------------------------------------------------------------
def self_test() -> bool:
    ref = Reference(SOURCE)
    queries = build_map_queries(ref)
    dump, rows = get_dump("csr"), get_rows("csr")
    answers = get_map("csr", queries)
    hist = get_hist("csr", "사")
    with tempfile.TemporaryDirectory() as d:
        result, table = get_generated(pathlib.Path(d))

    some_pair = next(iter(ref.pairs))
    # '>=' 버그: 구간 끝 r이 다음 후보로 넘어간다 -> 끝 r 질의의 답을 다음 후보로 바꿔 흉내
    next_of = {(a, b): spans[i + 1][0] for a, spans in ref.intervals.items()
               for i, (b, _, _) in enumerate(spans) if i + 1 < len(spans)}
    end_rs = {(a, hi) for a, spans in ref.intervals.items() for _, _, hi in spans}
    off_by_one = [next_of.get((a, exp), exp) if (a, r) in end_rs else got
                  for (a, r, exp), got in zip(queries, answers)]
    cands = [b for b, _ in ref.rows["사"]]
    uniform = {b: HIST_SAMPLES // len(cands) for b in cands}
    uniform[cands[0]] += HIST_SAMPLES - sum(uniform.values())

    # 결과 파일 조작용: 첫 구역(array)의 1번 단어 앞에 "단어 첫 글자로 한 번도 안 나온 글자"를 붙인다
    lines = result.split("\n")
    first = next(i for i, ln in enumerate(lines) if ln.startswith("1\t"))
    word = lines[first].split("\t")[1]
    never_first = next(c for c in ref.order if c not in (BOS, EOS) and (BOS, c) not in ref.pairs)
    bad_word = lines[first].replace(word, never_first + word, 1)
    star_flip = (lines[first][:-2] if lines[first].endswith("\t*") else lines[first] + "\t*")
    table_lines = table.split("\n")
    data_i = next(i for i, ln in enumerate(table_lines) if ln and not ln.startswith("#"))

    def swapped(i: int, j: int) -> str:
        t = table_lines[:]
        t[i], t[j] = t[j], t[i]
        return "\n".join(t)

    cases = [
        ("정상 출력", lambda: check_dump(ref, dump) + check_rows(ref, rows)
            + check_map(queries, answers) + check_hist(ref, "사", hist)[0]
            + check_table(ref, table) + check_result(ref, result), False),
        ("쌍 하나의 빈도 +1", lambda: check_dump(ref, dump | {some_pair: dump[some_pair] + 1}), True),
        ("쌍 하나 빠짐", lambda: check_dump(ref, {k: n for k, n in dump.items() if k != some_pair}), True),
        ("단어 경계를 넘어 셈 (EOS->BOS)", lambda: check_dump(ref, dump | {(EOS, BOS): 420308}), True),
        ("행 합계 하나 틀림 (BOS -1)", lambda: check_rows(ref, rows | {BOS: rows[BOS] - 1}), True),
        ("이진 탐색 한 칸 어긋남 ('>=')", lambda: check_map(queries, off_by_one), True),
        ("'사' 행 후보를 균등하게 선택", lambda: check_hist(ref, "사", uniform)[0], True),
        ("빈도표 행 안 순서 뒤바뀜", lambda: check_table(ref, swapped(data_i, data_i + 1)), True),
        ("빈도표 확률 틀림", lambda: check_table(ref, table.replace("\t1.097764\n", "\t1.100000\n", 1)), True),
        ("생성 단어에 없는 쌍", lambda: check_result(ref, "\n".join(
            lines[:first] + [bad_word] + lines[first + 1:])), True),
        ("사전 표시(*) 틀림", lambda: check_result(ref, "\n".join(
            lines[:first] + [star_flip] + lines[first + 1:])), True),
        ("세 구현 결과 불일치 표시", lambda: check_result(ref, result.replace(
            "same_output\tYES", "same_output\tNO")), True),
    ]

    all_ok = True
    print("자기 검증 (C 출력은 csr 구현에서 받아 조작)")
    for label, fn, should_fail in cases:
        fails = fn()
        failed = bool(fails)
        good = failed == should_fail
        all_ok &= good
        verdict = "FAIL 감지" if failed else "PASS"
        mark = "OK  " if good else "XX  "
        print(f"  [{mark}] {label:<30} -> {verdict}" + (f"  ({fails[0]})" if failed else ""))

    print("\n자기 검증 통과: 망가진 입력은 모두 FAIL, 정상 입력은 PASS" if all_ok
          else "\n자기 검증 실패: 검증기가 문제를 놓친다")
    return all_ok


# ---------------------------------------------------------------------------
def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    if not SOURCE.exists():
        print(f"[오류] 원본 데이터를 찾을 수 없습니다: {SOURCE}")
        return 1
    if not TARGET.exists():
        print(f"[오류] 검증 대상이 없습니다: {TARGET} (build.bat으로 먼저 빌드)")
        return 1
    if "--self-test" in sys.argv[1:]:
        return 0 if self_test() else 1
    return 0 if verify() else 1


if __name__ == "__main__":
    sys.exit(main())
