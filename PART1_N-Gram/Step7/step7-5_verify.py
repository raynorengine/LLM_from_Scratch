"""[STEP 7 / 5단계] 독립 검증 - 원본 비교 수치가 맞는가

검증 방식 (CLAUDE.md 검증 규칙):
  C의 출력을 받아쓰지 않는다. **step7-2b_tokens.txt 에서 처음부터 다시 계산**한다.
  그리고 C와 **다른 알고리즘**을 쓴다. 같은 방법을 옮겨 적으면 같은 버그를 함께 갖는다.

    C      : 토큰 번호 -> 등장 위치 역색인을 만들고, 시작점마다 따라가며 늘린다
    Python : 원본을 "|토큰|토큰|" 꼴의 **문자열 한 덩어리**로 만들고,
             생성 구간을 그 안에서 **부분 문자열로 찾는다**

  구분자 `|` 로 토큰을 감싸는 이유: `|말/NNG|` 로 찾으면 `|소말/NNG|` 에 걸리지 않는다.
  문장 사이는 줄바꿈으로 끊어, 한 구간이 두 문장에 걸치지 못하게 한다.

검사 항목:
  [1] 문장 수와 번호가 3-c 결과와 일치하는가
  [2] len 이 실제 생성 토큰 수와 같은가
  [3] 통째 일치(*) 판정 - 원본 문장 집합으로 독립 확인
  [4] 최장 복사 길이 - 양쪽으로 확인한다
        (a) C가 말한 길이의 구간이 원본에 **실제로 있는가**
        (b) 그보다 **1 긴 구간은 어디에도 없는가**   <- 이게 빠지면 과소평가를 놓친다
  [5] src 가 가리킨 원본 문장이 정말 그 구간을 담고 있는가
  [6] 중복(D) 판정
  [7] 요약 수치(통째 일치율 / 평균 복사 비율 / 전체 복사 / 중복)가 문장별 표와 맞는가

사용법:
  python step7-5_verify.py [번호]        생략하면 번호가 가장 큰 결과
  python step7-5_verify.py --self-test

종료 코드: 0 = 성공 / 1 = 실패
"""

from __future__ import annotations

import re
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOKENS_PATH = HERE / "step7-2b_tokens.txt"
GEN_FMT = "step7-3c_6gram_result_{}.txt"
OUT_FMT = "step7-5_compare_result_{}.txt"

W = "<W>"
SEP = "|"        # 토큰 경계 표시. 토큰 안에 나올 수 없는 글자여야 한다


# ===================================================================
# [A] 원본 읽기 - 두 가지 모양으로 들고 있는다
#
# [이유] 검사마다 필요한 모양이 다르다.
#   - 통째 일치: 문장 하나를 통으로 비교하므로 **집합**이 빠르다
#   - 최장 복사: 문장 경계를 넘지 않는 연속 구간을 찾으므로 **문자열**이 편하다
# ===================================================================
def load_corpus() -> tuple[list[list[str]], set[str], str]:
    sents: list[list[str]] = []
    with TOKENS_PATH.open(encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n").rstrip("\r")
            if not line:
                continue
            sents.append([t for t in line.split(W) if t])

    # 문장 하나를 "|a|b|c|" 로 적는다. 양끝에도 구분자를 둬야 부분 일치가 안 걸린다
    bodies = [SEP + SEP.join(s) + SEP for s in sents]
    whole = set(bodies)                 # 통째 일치용
    big = "\n".join(bodies)             # 최장 복사용. 줄바꿈이 문장 경계 역할을 한다
    return sents, whole, big


def span(tokens: list[str]) -> str:
    """토큰 구간을 원본 문자열에서 찾을 수 있는 모양으로"""
    return SEP + SEP.join(tokens) + SEP


# ===================================================================
# [B] 입력 읽기
# ===================================================================
def read_generated(path: Path) -> list[list[str]]:
    out: list[list[str]] = []
    with path.open(encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n").rstrip("\r")
            m = re.match(r"^(\d+): (.*)$", line)
            if m:
                out.append([t for t in m.group(2).split(W) if t])
    return out


def read_report(path: Path) -> tuple[dict[str, str], list[dict]]:
    """요약 줄(이름 <탭> 값 ...)과 문장별 표를 읽는다"""
    summary: dict[str, str] = {}
    rows: list[dict] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#") or line.startswith("=") or line.startswith("["):
            continue
        parts = line.split("\t")
        if len(parts) == 7 and parts[0].isdigit():
            rows.append({
                "no": int(parts[0]), "mark": parts[1], "len": int(parts[2]),
                "copy": int(parts[3]), "ratio": parts[4],
                "src": int(parts[5]), "gen_at": int(parts[6]),
            })
        elif len(parts) >= 2 and not parts[0].startswith(" "):
            summary.setdefault(parts[0], "\t".join(parts[1:]))
    return summary, rows


# ===================================================================
# [C] 검사
# ===================================================================
def check(gen: list[list[str]], rows: list[dict], summary: dict[str, str],
          sents: list[list[str]], whole: set[str], big: str) -> list[str]:
    errors: list[str] = []

    # --- [1] 문장 수와 번호 ---
    if len(rows) != len(gen):
        return [f"[1] 표의 줄 수 {len(rows)} != 생성 문장 수 {len(gen)}"]
    for i, r in enumerate(rows, 1):
        if r["no"] != i:
            errors.append(f"[1] 번호가 어긋납니다: {i}번째 줄의 no={r['no']}")
            break

    n_whole = n_full = n_dup = 0
    ratio_sum = 0.0
    seen: dict[tuple, int] = {}

    for r, toks in zip(rows, gen):
        no, m = r["no"], len(toks)

        # --- [2] 길이 ---
        if r["len"] != m:
            errors.append(f"[2] {no}번 len={r['len']} 인데 실제 토큰 수는 {m}")
            continue

        # --- [3] 통째 일치 ---
        is_whole = span(toks) in whole
        said_whole = "*" in r["mark"]
        if is_whole != said_whole:
            errors.append(f"[3] {no}번 통째 일치 판정이 다릅니다: "
                          f"C={'*' if said_whole else '-'} / 실제={'*' if is_whole else '-'}")
        if is_whole:
            n_whole += 1

        # --- [4] 최장 복사 ---
        best = r["copy"]
        if not 0 < best <= m:
            errors.append(f"[4] {no}번 copy={best} 가 범위를 벗어납니다 (길이 {m})")
            continue

        # (a) 그 길이의 구간이 실제로 원본에 있는가
        if not any(span(toks[s:s + best]) in big for s in range(m - best + 1)):
            errors.append(f"[4a] {no}번 길이 {best} 짜리 구간이 원본에 없습니다")

        # (b) 1 더 긴 구간은 없는가 — 과소평가를 잡는 검사
        if best < m:
            longer = [s for s in range(m - best) if span(toks[s:s + best + 1]) in big]
            if longer:
                errors.append(f"[4b] {no}번 copy={best} 인데 길이 {best + 1} 구간이 "
                              f"원본에 있습니다 (생성 {longer[0]}번 자리부터)")

        if best == m:
            n_full += 1
        ratio_sum += best / m

        # --- [5] src 가 가리킨 원본 문장 ---
        si = r["src"] - 1
        if not 0 <= si < len(sents):
            errors.append(f"[5] {no}번 src={r['src']} 가 범위 밖입니다")
        else:
            piece = span(toks[r["gen_at"]:r["gen_at"] + best])
            if piece not in span(sents[si]):
                errors.append(f"[5] {no}번 src={r['src']} 문장에 그 구간이 없습니다")

        # --- [6] 중복 ---
        key = tuple(toks)
        said_dup = "D" in r["mark"]
        is_dup = key in seen
        if is_dup != said_dup:
            errors.append(f"[6] {no}번 중복 판정이 다릅니다: "
                          f"C={'D' if said_dup else '-'} / 실제={'D' if is_dup else '-'}")
        if is_dup:
            n_dup += 1
        else:
            seen[key] = no

        # 전체 복사(=) 표시도 확인
        said_full = "=" in r["mark"]
        if said_full and not (best == m and not is_whole):
            errors.append(f"[6] {no}번 '=' 표시가 잘못됐습니다")

    # --- [7] 요약과 표가 맞는가 ---
    n = len(rows)
    want = {
        "통째 일치": f"{n_whole} / {n}",
        "전체 복사": f"{n_full} / {n}",
    }
    for name, val in want.items():
        got = summary.get(name, "").split("\t")[0]
        if got != val:
            errors.append(f"[7] 요약 '{name}' = {got!r} 인데 표에서 세면 {val!r}")

    got_ratio = summary.get("평균 복사 비율", "").split("\t")[0]
    want_ratio = f"{100.0 * ratio_sum / n:.1f}%"
    if got_ratio != want_ratio:
        errors.append(f"[7] 요약 '평균 복사 비율' = {got_ratio!r} 인데 표에서 구하면 {want_ratio!r}")

    got_dup = summary.get("중복 문장", "").split("\t")[0]
    if got_dup != f"{n_dup} / {n}":
        errors.append(f"[7] 요약 '중복 문장' = {got_dup!r} 인데 표에서 세면 {n_dup} / {n}")

    return errors


# ===================================================================
# [D] 자가 시험
#
# [이유] 검증기가 틀린 입력에 FAIL 을 내는지 확인해야 검증기 자체가 검증된다.
#   원본에서 실제 문장을 가져다 쓴다. 지어낸 토큰으로는 [4] 검사가 성립하지 않는다.
# ===================================================================
def self_test() -> int:
    sents, whole, big = load_corpus()

    real = sents[100]                      # 원본 문장 하나를 그대로 "생성한 셈" 친다
    m = len(real)
    gen = [real]
    base = {"no": 1, "mark": "*", "len": m, "copy": m,
            "ratio": "100%", "src": 101, "gen_at": 0}
    summary = {"통째 일치": "1 / 1", "전체 복사": "1 / 1",
               "평균 복사 비율": "100.0%", "중복 문장": "0 / 1"}

    def row(**kw):
        r = dict(base); r.update(kw); return [r]

    cases: list[tuple[str, list, list, dict, bool]] = [
        ("정상",              gen, row(),                       summary, False),
        ("len 이 틀림",        gen, row(len=m + 1),              summary, True),
        ("통째 일치 누락",      gen, row(mark="-"),               summary, True),
        ("copy 를 줄여 적음",   gen, row(copy=m - 2, mark="-"),   summary, True),
        ("copy 를 늘려 적음",   gen, row(copy=m + 1),             summary, True),
        ("src 가 엉뚱함",       gen, row(src=1),                  summary, True),
        ("중복 아닌데 D",       gen, row(mark="*D"),              summary, True),
        ("요약이 표와 다름",    gen, row(),
         {**summary, "통째 일치": "0 / 1"},                       True),
    ]

    failed = 0
    for name, g, rows, summ, must_fail in cases:
        errs = check(g, rows, summ, sents, whole, big)
        got = bool(errs)
        ok = got == must_fail
        print(f"  [{'OK' if ok else 'NG'}] {name}: "
              f"기대 {'FAIL' if must_fail else 'PASS'} / 실제 {'FAIL' if got else 'PASS'}"
              + ("" if ok else f"   -> {errs[:1]}"))
        failed += 0 if ok else 1
    print(f"\n자가 시험 {len(cases)}건 중 {len(cases) - failed}건 통과")
    return 1 if failed else 0


# ===================================================================
# [E] main
# ===================================================================
def main(argv: list[str]) -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    if len(argv) > 1 and argv[1] == "--self-test":
        return self_test()

    if len(argv) > 1:
        num = argv[1].zfill(2)
    else:
        found = sorted(HERE.glob("step7-5_compare_result_[0-9][0-9].txt"))
        if not found:
            print("5단계 결과가 없습니다. 먼저 step7-5_compare.exe 를 실행하세요.")
            return 1
        num = found[-1].stem[-2:]

    gen_path = HERE / GEN_FMT.format(num)
    out_path = HERE / OUT_FMT.format(num)
    for p in (gen_path, out_path, TOKENS_PATH):
        if not p.exists():
            print(f"파일이 없습니다: {p.name}")
            return 1

    gen = read_generated(gen_path)
    summary, rows = read_report(out_path)
    print(f"검증 대상: {out_path.name} ({len(rows)}문장)")

    t0 = time.time()
    sents, whole, big = load_corpus()
    print(f"  독립 재계산: 원본 {len(sents):,}문장 / {len(big):,}글자 ({time.time() - t0:.1f}초)")

    t0 = time.time()
    errors = check(gen, rows, summary, sents, whole, big)
    print(f"  [1]~[7] 검사 완료 ({time.time() - t0:.1f}초)\n")

    if errors:
        print("검증 실패:")
        for e in errors[:20]:
            print("  " + e)
        return 1

    print(f"검증 통과 — {len(rows)}문장의 통째 일치·최장 복사·중복 판정이 모두 재계산과 일치")
    print(f"  통째 일치 {summary.get('통째 일치', '?')}")
    print(f"  평균 복사 비율 {summary.get('평균 복사 비율', '?')}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
