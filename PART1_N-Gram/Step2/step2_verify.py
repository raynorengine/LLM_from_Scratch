"""
step2_verify.py - [STEP 2] 유니그램 샘플러(step2_unigram.exe)를 독립적으로 검증한다.

랜덤 출력에는 "정답 문자열"이 없다. 그래서 검증 대상을 쪼갠다.
  [1] 난수 r -> 글자 대응 (확정적, 전수) : 2,450종 구간의 시작·끝 경계값 + 무작위 r 1만 개
  [2] 분포 (통계적)                      : 100만 번 샘플링 히스토그램을 카이제곱 검정
  [3] 탐욕적 디코딩                      : 최고 빈도 글자만 반복되는가
  [4] 생성 결과 형식                     : 줄 수 · 글자 수 · 모든 글자가 빈도표에 있는가

독립성 (CLAUDE.md "검증 규칙")
  - 빈도표를 STEP 1 결과 파일이 아니라 원본 words.txt에서 처음부터 다시 센다.
    구현이 빈도표를 잘못 읽으면 [1]에서 걸린다.
  - 구현은 누적 빈도 + 이진 탐색으로 글자를 찾지만, 여기서는 r -> 글자 대응표를
    155만 칸 그대로 펼쳐 놓고 바로 찾는다. 탐색 알고리즘 자체가 없다.
  - C 프로그램은 subprocess로 실행하고 출력만 받는다.
    생성 결과는 임시 폴더에 쓰게 하므로 Step2 폴더의 번호 붙은 결과 파일은 늘어나지 않는다.
  - 기대값을 코드에 박지 않는다. '다' 같은 글자도 재계산 결과에서 나온다.

사용법
  python step2_verify.py                 # 검증
  python step2_verify.py --self-test     # 망가진 구현을 흉내 낸 입력에 FAIL을 내는지 확인
"""

from __future__ import annotations

import collections
import math
import pathlib
import random
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).parent
SOURCE = HERE.parent.parent / "data" / "korean_dictionary" / "words.txt"
TARGET = [str(HERE / "step2_unigram.exe")]

HANGUL_FIRST = "가"
HANGUL_LAST = "힣"

MAP_RANDOM_COUNT = 10_000    # [1] 경계값 외에 추가로 넣는 무작위 r 개수
HIST_SAMPLES = 1_000_000     # [2] 분포 검사 샘플 수
HIST_SEED = 12345
P_VALUE_THRESHOLD = 1e-3     # [2] p < 0.001 이면 "분포가 다르다"고 판정
GEN_LENGTH, GEN_COUNT, GEN_SEED = 7, 30, 2026  # [3][4][5] 생성 검사 인자


# ---------------------------------------------------------------------------
# 1. 원본에서 독립 재계산
# ---------------------------------------------------------------------------
class Reference:
    """words.txt에서 직접 만든 기준 데이터."""

    def __init__(self, source: pathlib.Path) -> None:
        text = source.read_bytes().decode("utf-8")
        counter = collections.Counter(c for c in text if HANGUL_FIRST <= c <= HANGUL_LAST)

        # 구간 배정 순서는 STEP 1 빈도표의 규칙과 같다: 빈도 내림차순, 같으면 코드포인트 오름차순
        ordered = sorted(counter.items(), key=lambda kv: (-kv[1], ord(kv[0])))
        self.chars = [c for c, _ in ordered]
        self.counts = [n for _, n in ordered]
        self.total = sum(self.counts)
        self.prob = {c: n / self.total for c, n in ordered}

        # r -> 글자 대응표를 전부 펼친다. lookup[r] = 글자 번호. 이진 탐색이 필요 없다.
        self.lookup: list[int] = []
        for i, n in enumerate(self.counts):
            self.lookup.extend([i] * n)

        # 각 구간의 [시작, 끝] (경계값 검사용)
        self.bounds: list[tuple[int, int]] = []
        start = 0
        for n in self.counts:
            self.bounds.append((start, start + n - 1))
            start += n

        # 탐욕적 디코딩의 정답: 빈도 최대, 동률이면 코드포인트가 작은 글자
        self.argmax_char = max(counter, key=lambda c: (counter[c], -ord(c)))


# ---------------------------------------------------------------------------
# 2. 구현 실행 (출력만 받는다)
# ---------------------------------------------------------------------------
def run(cmd: list[str], stdin: str | None = None) -> str:
    proc = subprocess.run(cmd, input=stdin, capture_output=True, text=True,
                          encoding="utf-8", cwd=HERE)
    if proc.returncode != 0:
        raise RuntimeError(f"실행 실패 ({proc.returncode}): {' '.join(cmd)}\n{proc.stderr}")
    return proc.stdout


def build_map_queries(ref: Reference) -> list[int]:
    """모든 구간의 시작·끝 + 무작위 r. 한 칸 어긋남은 경계에서만 드러난다."""
    queries = [r for lo, hi in ref.bounds for r in (lo, hi)]
    rng = random.Random(7)
    queries += [rng.randrange(ref.total) for _ in range(MAP_RANDOM_COUNT)]
    return queries


def get_map(cmd: list[str], queries: list[int]) -> list[str]:
    return run(cmd + ["--map"], "\n".join(map(str, queries)) + "\n").split()


def get_hist(cmd: list[str]) -> dict[str, int]:
    out = run(cmd + ["--hist", str(HIST_SAMPLES), str(HIST_SEED)])
    hist: dict[str, int] = {}
    for line in out.splitlines():
        ch, n = line.split("\t")
        hist[ch] = int(n)
    return hist


def get_generated(cmd: list[str], out_file: pathlib.Path) -> bytes:
    run(cmd + [str(GEN_LENGTH), str(GEN_COUNT), str(GEN_SEED),
               "../Step1/step1_char_freq_result.txt", str(out_file)])
    return out_file.read_bytes()


# ---------------------------------------------------------------------------
# 3. 판정 함수 (실패 메시지 목록을 돌려준다. 비어 있으면 통과)
# ---------------------------------------------------------------------------
def check_map(ref: Reference, queries: list[int], answers: list[str]) -> list[str]:
    if len(answers) != len(queries):
        return [f"응답 수 {len(answers)} != 질의 수 {len(queries)}"]
    wrong = [(r, a, ref.chars[ref.lookup[r]])
             for r, a in zip(queries, answers) if a != ref.chars[ref.lookup[r]]]
    if wrong:
        r, got, exp = wrong[0]
        return [f"{len(wrong)}건 불일치 (예: r={r} -> '{got}', 정답 '{exp}')"]
    return []


def chi_square_p(stat: float, df: int) -> float:
    """카이제곱 분포의 위쪽 꼬리 확률. Wilson-Hilferty 근사 (자유도가 클 때 충분히 정확)."""
    z = ((stat / df) ** (1 / 3) - (1 - 2 / (9 * df))) / math.sqrt(2 / (9 * df))
    return 0.5 * math.erfc(z / math.sqrt(2))


def check_hist(ref: Reference, hist: dict[str, int]) -> tuple[list[str], str]:
    fails: list[str] = []
    n = sum(hist.values())
    if n != HIST_SAMPLES:
        fails.append(f"샘플 합계 {n} != {HIST_SAMPLES}")
    unknown = set(hist) - set(ref.prob)
    if unknown:
        fails.append(f"빈도표에 없는 글자 {len(unknown)}종")
    if n == 0:
        return fails + ["샘플 없음"], ""

    # 기대 빈도가 5 미만인 희귀 글자는 하나로 묶는다 (카이제곱 근사의 전제 조건)
    stat, df_bins = 0.0, 0
    pooled_obs, pooled_exp = 0, 0.0
    for c, p in ref.prob.items():
        exp, obs = p * n, hist.get(c, 0)
        if exp < 5:
            pooled_obs += obs
            pooled_exp += exp
        else:
            stat += (obs - exp) ** 2 / exp
            df_bins += 1
    if pooled_exp > 0:
        stat += (pooled_obs - pooled_exp) ** 2 / pooled_exp
        df_bins += 1
    df = df_bins - 1
    p_value = chi_square_p(stat, df)
    detail = f"카이제곱={stat:,.1f}  자유도={df}  p={p_value:.4g}"
    if p_value < P_VALUE_THRESHOLD:
        fails.append(f"분포가 실제 확률과 다름 ({detail})")
    return fails, detail


def parse_generated(data: bytes) -> tuple[dict[str, str], list[tuple[str, str]]]:
    header: dict[str, str] = {}
    rows: list[tuple[str, str]] = []
    for line in data.decode("utf-8").split("\n"):
        if not line:
            continue
        if line.startswith("#"):
            parts = line[2:].split("\t")
            if len(parts) == 2:
                header[parts[0]] = parts[1]
            continue
        key, _, text = line.partition("\t")
        rows.append((key, text))
    return header, rows


def check_greedy(ref: Reference, rows: list[tuple[str, str]]) -> list[str]:
    greedy = [text for key, text in rows if key == "greedy"]
    if len(greedy) != 1:
        return [f"greedy 줄이 {len(greedy)}개"]
    expected = ref.argmax_char * GEN_LENGTH
    if greedy[0] != expected:
        return [f"'{greedy[0]}' != 기대값 '{expected}'"]
    return []


def check_samples(ref: Reference, header: dict[str, str],
                  rows: list[tuple[str, str]]) -> list[str]:
    fails: list[str] = []
    samples = [(key, text) for key, text in rows if key != "greedy"]
    if [k for k, _ in samples] != [str(i) for i in range(1, GEN_COUNT + 1)]:
        fails.append(f"샘플 줄 번호가 1..{GEN_COUNT}이 아님 ({len(samples)}줄)")
    bad_len = [t for _, t in samples if len(t) != GEN_LENGTH]
    if bad_len:
        fails.append(f"글자수가 {GEN_LENGTH}이 아닌 줄 {len(bad_len)}개 (예: '{bad_len[0]}')")
    unknown = {c for _, t in samples for c in t} - set(ref.prob)
    if unknown:
        fails.append(f"빈도표에 없는 글자: {sorted(unknown)[:5]}")
    expect_header = {"table_kinds": str(len(ref.chars)), "table_total": str(ref.total),
                     "seed": str(GEN_SEED), "length": str(GEN_LENGTH), "count": str(GEN_COUNT)}
    for k, v in expect_header.items():
        if header.get(k) != v:
            fails.append(f"헤더 {k}={header.get(k)} (기대 {v})")
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
                print(f"  [FAIL] {label:<22} {f}")
        else:
            print(f"  [OK  ] {label:<22} {detail}")


def collect(cmd: list[str], ref: Reference, queries: list[int], tmp: pathlib.Path,
            name: str) -> dict:
    return {
        "map": get_map(cmd, queries),
        "hist": get_hist(cmd),
        "gen": get_generated(cmd, tmp / f"{name}_gen.txt"),
    }


def verify_outputs(ref: Reference, queries: list[int], out: dict, report: Report) -> None:
    report.item("[1] r->글자 대응", check_map(ref, queries, out["map"]),
                f"경계 {len(ref.bounds) * 2:,}개 + 무작위 {MAP_RANDOM_COUNT:,}개 전부 일치")
    fails, detail = check_hist(ref, out["hist"])
    report.item("[2] 분포 (카이제곱)", fails, detail)
    header, rows = parse_generated(out["gen"])
    report.item("[3] 탐욕적 디코딩", check_greedy(ref, rows),
                f"'{ref.argmax_char * GEN_LENGTH}'")
    report.item("[4] 생성 결과 형식", check_samples(ref, header, rows),
                f"{GEN_COUNT}줄 x {GEN_LENGTH}자, 전부 빈도표 글자")


def verify() -> bool:
    ref = Reference(SOURCE)
    queries = build_map_queries(ref)
    print(f"기준 데이터 (words.txt 재계산): {len(ref.chars):,}종 / {ref.total:,}자 / "
          f"최다 글자 '{ref.argmax_char}'")
    report = Report()

    with tempfile.TemporaryDirectory() as d:
        print(f"\n=== 대상: {pathlib.Path(TARGET[-1]).name} ===")
        outputs = collect(TARGET, ref, queries, pathlib.Path(d), "c")
        verify_outputs(ref, queries, outputs, report)

    print("\n=== 검증 통과 ===" if report.ok else "\n=== 검증 실패 ===")
    return report.ok


# ---------------------------------------------------------------------------
# 5. 자기 검증: 망가진 구현의 출력을 흉내 내서 넣으면 반드시 FAIL이 나와야 한다
# ---------------------------------------------------------------------------
def fake_hist(ref: Reference, draw) -> dict[str, int]:
    hist = collections.Counter(ref.chars[ref.lookup[draw()]] for _ in range(HIST_SAMPLES))
    return {c: hist.get(c, 0) for c in ref.chars}


def self_test() -> bool:
    ref = Reference(SOURCE)
    queries = build_map_queries(ref)
    with tempfile.TemporaryDirectory() as d:
        real = collect(TARGET, ref, queries, pathlib.Path(d), "c")
    header, rows = parse_generated(real["gen"])
    second = ref.chars[1]
    rng = random.Random(99)

    cases = [
        # (설명, 판정 함수 실행 결과, 실패해야 하는가)
        ("정상 출력", lambda: check_map(ref, queries, real["map"])
                            + check_hist(ref, real["hist"])[0]
                            + check_greedy(ref, rows)
                            + check_samples(ref, header, rows), False),
        # '>' 대신 '>='를 쓰면 "누적 >= r 인 첫 글자"를 찾게 되어,
        # 각 구간의 시작값 r(= 앞 글자의 누적값)이 앞 글자로 잘못 간다 -> lookup[r - 1]과 같다
        ("이진 탐색 한 칸 어긋남 ('>' 대신 '>=')",
         lambda: check_map(ref, queries,
                           [ref.chars[ref.lookup[max(r - 1, 0)]] for r in queries]),
         True),
        ("rand() 하나만 사용 (r < 32768)",
         lambda: check_hist(ref, fake_hist(ref, lambda: rng.randrange(32768) % ref.total))[0],
         True),
        ("모든 글자를 균등하게 선택",
         lambda: check_hist(ref, {c: HIST_SAMPLES // len(ref.chars) for c in ref.chars}
                            | {ref.chars[0]: HIST_SAMPLES // len(ref.chars)
                               + HIST_SAMPLES % len(ref.chars)})[0],
         True),
        ("탐욕적 디코딩이 2위 글자를 반복",
         lambda: check_greedy(ref, [("greedy", second * GEN_LENGTH)]), True),
        ("생성 글자수가 하나 모자람",
         lambda: check_samples(ref, header,
                               [(k, t[:-1] if k == "1" else t) for k, t in rows]), True),
    ]

    all_ok = True
    print(f"자기 검증 (대상: {pathlib.Path(TARGET[-1]).name})")
    for label, fn, should_fail in cases:
        fails = fn()
        failed = bool(fails)
        good = failed == should_fail
        all_ok &= good
        verdict = "FAIL 감지" if failed else "PASS"
        mark = "OK  " if good else "XX  "
        print(f"  [{mark}] {label:<38} -> {verdict}" + (f"  ({fails[0]})" if failed else ""))

    print("\n자기 검증 통과: 망가진 입력은 모두 FAIL, 정상 입력은 PASS" if all_ok
          else "\n자기 검증 실패: 검증기가 문제를 놓친다")
    return all_ok


# ---------------------------------------------------------------------------
def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    if not SOURCE.exists():
        print(f"[오류] 원본 데이터를 찾을 수 없습니다: {SOURCE}")
        return 1
    if not pathlib.Path(TARGET[-1]).exists():
        print(f"[오류] 검증 대상이 없습니다: {TARGET[-1]} (build.bat으로 먼저 빌드)")
        return 1

    if "--self-test" in sys.argv[1:]:
        return 0 if self_test() else 1
    return 0 if verify() else 1


if __name__ == "__main__":
    sys.exit(main())
