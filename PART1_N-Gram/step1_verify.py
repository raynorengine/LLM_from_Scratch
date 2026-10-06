"""
step1_verify.py - [STEP 1] step1_char_freq.c(C 구현)의 결과를 독립적으로 검증한다.

검증 원칙 (CLAUDE.md "검증 규칙" 참고)
  1. 기대값을 코드에 박지 않는다. 원본 데이터에서 처음부터 다시 계산한다.
  2. C와 다른 방식으로 센다. C는 UTF-8 바이트를 직접 디코딩하지만
     여기서는 Python의 독립적인 UTF-8 디코더와 collections.Counter를 쓴다.
  3. 상위 몇 개가 아니라 전체 글자(2,450종)를 전수 비교한다.
  4. 틀린 입력에는 반드시 FAIL을 낸다. (--self-test 로 그 동작을 확인)
  5. 성공 0 / 실패 1 로 종료한다.

사용법
  python step1_verify.py                       # 기본 경로로 검증
  python step1_verify.py <결과파일> <원본파일>
  python step1_verify.py --self-test           # 검증기가 실제로 FAIL을 내는지 확인
"""

from __future__ import annotations

import collections
import pathlib
import shutil
import subprocess
import sys
import tempfile

DEFAULT_RESULT = pathlib.Path(__file__).parent / "step1_char_freq_result.txt"
DEFAULT_SOURCE = (
    pathlib.Path(__file__).parent.parent / "data" / "korean_dictionary" / "words.txt"
)

HANGUL_FIRST = "가"  # '가'
HANGUL_LAST = "힣"   # '힣'

# 확률(%) 비교 허용 오차. C는 double, Python은 float이라 끝자리가 다를 수 있다.
PERCENT_TOLERANCE = 1e-4


# ---------------------------------------------------------------------------
# 1. 원본 데이터에서 독립적으로 재계산
# ---------------------------------------------------------------------------
def count_from_source(path: pathlib.Path) -> dict:
    """words.txt를 직접 읽어 글자 빈도를 처음부터 다시 센다.

    C 구현을 참조하지 않는다. 바이트도 Python 표준 UTF-8 디코더로 처리한다.
    """
    raw_bytes = path.read_bytes()

    # 개행 수는 바이트 단위로 따로 센다 (C의 newlines 값과 대조용)
    newlines = raw_bytes.count(b"\n") + raw_bytes.count(b"\r")

    text = raw_bytes.decode("utf-8")  # 깨진 바이트가 있으면 여기서 예외가 난다

    # 개행을 제외한 글자만 남긴다
    body = [c for c in text if c not in ("\n", "\r")]

    counter = collections.Counter(body)

    in_range = {c: n for c, n in counter.items() if HANGUL_FIRST <= c <= HANGUL_LAST}
    out_of_range = {c: n for c, n in counter.items() if not (HANGUL_FIRST <= c <= HANGUL_LAST)}

    return {
        "counts": in_range,
        "total": sum(in_range.values()),
        "unique": len(in_range),
        "newlines": newlines,
        "out_of_range": sum(out_of_range.values()),
        "out_of_range_chars": out_of_range,
    }


# ---------------------------------------------------------------------------
# 2. C가 만든 결과 파일 파싱
# ---------------------------------------------------------------------------
def parse_result(path: pathlib.Path) -> dict:
    """step1_char_freq.exe가 쓴 결과 파일을 읽는다. 값은 신뢰하지 않고 그냥 가져온다."""
    header: dict[str, str] = {}
    rows: list[tuple[int, str, int, float]] = []

    with path.open("r", encoding="utf-8") as fp:
        for lineno, line in enumerate(fp, 1):
            line = line.rstrip("\n")
            if not line:
                continue
            if line.startswith("#"):
                parts = line.lstrip("#").strip().split("\t")
                if len(parts) == 2:
                    header[parts[0]] = parts[1]
                continue
            parts = line.split("\t")
            if len(parts) != 4:
                raise ValueError(f"{path}:{lineno} 열 개수가 4가 아님: {line!r}")
            rows.append((int(parts[0]), parts[1], int(parts[2]), float(parts[3])))

    return {"header": header, "rows": rows}


# ---------------------------------------------------------------------------
# 3. 비교
# ---------------------------------------------------------------------------
def verify(result_path: pathlib.Path, source_path: pathlib.Path) -> bool:
    print(f"검증 대상 (C 결과) : {result_path}")
    print(f"원본 데이터        : {source_path}")
    print()

    truth = count_from_source(source_path)
    got = parse_result(result_path)
    header, rows = got["header"], got["rows"]

    failures: list[str] = []

    def check(label: str, expected, actual) -> None:
        mark = "OK  " if expected == actual else "FAIL"
        print(f"  [{mark}] {label:16} 재계산={expected:<12} C결과={actual}")
        if expected != actual:
            failures.append(f"{label}: 재계산={expected}, C결과={actual}")

    # --- 3-1. 요약 값 비교 ---
    print("[1] 요약 값 비교 (왼쪽이 Python 독립 재계산 결과)")
    check("전체 글자수", truth["total"], int(header.get("total_chars", -1)))
    check("고유 글자 종류", truth["unique"], int(header.get("unique_chars", -1)))
    check("개행 수", truth["newlines"], int(header.get("newlines", -1)))
    check("범위 외 문자", truth["out_of_range"], int(header.get("out_of_range", -1)))
    print()

    # --- 3-2. 행 수가 고유 글자 종류와 맞는지 ---
    print("[2] 결과 파일 구조")
    if len(rows) == truth["unique"]:
        print(f"  [OK  ] 데이터 행 수      {len(rows)}행 = 고유 글자 종류")
    else:
        print(f"  [FAIL] 데이터 행 수      {len(rows)}행 != 재계산 {truth['unique']}종")
        failures.append(f"행 수: {len(rows)} != {truth['unique']}")
    print()

    # --- 3-3. 전체 글자 전수 비교 ---
    print(f"[3] 전체 글자 빈도 전수 비교 ({truth['unique']}종)")
    seen: set[str] = set()
    count_mismatch: list[str] = []
    percent_mismatch: list[str] = []
    dup: list[str] = []

    for rank, ch, cnt, pct in rows:
        if ch in seen:
            dup.append(ch)
        seen.add(ch)

        expected_cnt = truth["counts"].get(ch)
        if expected_cnt is None:
            count_mismatch.append(f"{ch!r}: 원본에 없는 글자인데 C결과에 있음(={cnt})")
            continue
        if expected_cnt != cnt:
            count_mismatch.append(f"{ch!r}: 재계산={expected_cnt}, C결과={cnt}")

        # 확률도 독립 재계산해서 비교
        expected_pct = expected_cnt * 100.0 / truth["total"]
        if abs(expected_pct - pct) > PERCENT_TOLERANCE:
            percent_mismatch.append(
                f"{ch!r}: 재계산={expected_pct:.6f}%, C결과={pct:.6f}%"
            )

    missing = sorted(set(truth["counts"]) - seen)

    for label, bad in (
        ("빈도 불일치", count_mismatch),
        ("확률 불일치", percent_mismatch),
        ("C결과에 누락된 글자", [repr(c) for c in missing]),
        ("중복 행", [repr(c) for c in dup]),
    ):
        if bad:
            print(f"  [FAIL] {label:20} {len(bad)}건")
            for item in bad[:10]:
                print(f"           - {item}")
            if len(bad) > 10:
                print(f"           ... 외 {len(bad) - 10}건")
            failures.append(f"{label} {len(bad)}건")
        else:
            print(f"  [OK  ] {label:20} 0건")
    print()

    # --- 3-4. 내부 정합성 (C 결과만으로 확인 가능한 것) ---
    print("[4] 내부 정합성")
    row_sum = sum(cnt for _, _, cnt, _ in rows)
    if row_sum == truth["total"]:
        print(f"  [OK  ] 빈도 합계           {row_sum} = 전체 글자수")
    else:
        print(f"  [FAIL] 빈도 합계           {row_sum} != {truth['total']}")
        failures.append(f"빈도 합계: {row_sum} != {truth['total']}")

    pct_sum = sum(pct for _, _, _, pct in rows)
    if abs(pct_sum - 100.0) < 0.01:
        print(f"  [OK  ] 확률 합계           {pct_sum:.4f}% (= 100%)")
    else:
        print(f"  [FAIL] 확률 합계           {pct_sum:.4f}% (!= 100%)")
        failures.append(f"확률 합계: {pct_sum:.4f}%")

    ranks = [r for r, _, _, _ in rows]
    if ranks == list(range(1, len(rows) + 1)):
        print("  [OK  ] 순위 연속성         1..N 빠짐없이 증가")
    else:
        print("  [FAIL] 순위 연속성         1..N 아님")
        failures.append("순위 연속성")

    counts_only = [cnt for _, _, cnt, _ in rows]
    if all(a >= b for a, b in zip(counts_only, counts_only[1:])):
        print("  [OK  ] 빈도 내림차순       정렬 정상")
    else:
        print("  [FAIL] 빈도 내림차순       정렬 깨짐")
        failures.append("정렬 순서")
    print()

    # --- 결과 ---
    if failures:
        print(f"=== 검증 실패: {len(failures)}개 항목 ===")
        for f in failures:
            print(f"  - {f}")
        return False

    print("=== 검증 통과: 모든 항목 일치 ===")
    print(f"    전체 글자 {truth['unique']}종 / {truth['total']}자를 전수 비교함")
    return True


# ---------------------------------------------------------------------------
# 4. 자기 검증 - 검증기가 정말 FAIL을 낼 수 있는지 확인
# ---------------------------------------------------------------------------
def self_test(result_path: pathlib.Path, source_path: pathlib.Path) -> bool:
    """결과 파일을 일부러 틀리게 고쳐서, 검증기가 그걸 잡아내는지 본다.

    검증기가 무조건 통과만 낸다면 검증이 아니라 장식이다. 그걸 배제한다.
    """
    original = result_path.read_text(encoding="utf-8")
    lines = original.split("\n")

    cases: list[tuple[str, list[str]]] = []

    # (a) 1위 글자의 빈도를 1 늘린다
    mutated = list(lines)
    for i, ln in enumerate(mutated):
        if ln and not ln.startswith("#"):
            p = ln.split("\t")
            p[2] = str(int(p[2]) + 1)
            mutated[i] = "\t".join(p)
            cases.append((f"1위 글자({p[1]}) 빈도를 +1", mutated))
            break

    # (b) 헤더의 전체 글자수를 틀리게 바꾼다
    mutated = list(lines)
    for i, ln in enumerate(mutated):
        if ln.startswith("# total_chars"):
            mutated[i] = "# total_chars\t9999999"
            cases.append(("헤더 total_chars를 9999999로", mutated))
            break

    # (c) 마지막 데이터 행을 삭제한다
    mutated = [ln for ln in lines if ln.strip()]
    removed = mutated.pop()
    cases.append((f"마지막 행 삭제 ({removed.split(chr(9))[1]})", mutated))

    # (d) 두 행의 순서를 바꿔 정렬을 깬다
    data_idx = [i for i, ln in enumerate(lines) if ln and not ln.startswith("#")]
    mutated = list(lines)
    a, b = data_idx[0], data_idx[5]
    mutated[a], mutated[b] = mutated[b], mutated[a]
    cases.append(("1위와 6위 행 교체 (정렬 깨기)", mutated))

    tmpdir = pathlib.Path(tempfile.mkdtemp(prefix="charfreq_selftest_"))
    all_caught = True
    try:
        print("=" * 70)
        print("자기 검증: 일부러 틀린 결과를 넣고 FAIL이 나오는지 확인")
        print("=" * 70)
        print()

        for idx, (desc, content) in enumerate(cases, 1):
            bad = tmpdir / f"case{idx}.txt"
            bad.write_text("\n".join(content), encoding="utf-8")

            print(f"--- 케이스 {idx}: {desc} ---")
            proc = subprocess.run(
                [sys.executable, __file__, str(bad), str(source_path)],
                capture_output=True,
                text=True,
                encoding="utf-8",
            )
            caught = proc.returncode != 0
            print(f"    종료코드 {proc.returncode} -> " + ("FAIL 감지 (정상)" if caught else "통과해버림 (문제!)"))
            if not caught:
                all_caught = False
                print(proc.stdout)
            print()

        # 정상 파일은 통과해야 한다
        print("--- 케이스 0: 원본 결과 파일 (변조 없음) ---")
        proc = subprocess.run(
            [sys.executable, __file__, str(result_path), str(source_path)],
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        ok = proc.returncode == 0
        print(f"    종료코드 {proc.returncode} -> " + ("통과 (정상)" if ok else "실패해버림 (문제!)"))
        if not ok:
            all_caught = False
            print(proc.stdout)
        print()

        print("=" * 70)
        if all_caught:
            print("자기 검증 통과: 변조된 입력 4건 모두 FAIL, 정상 입력은 PASS")
            print("  -> 이 검증기는 무조건 통과시키는 가짜 검증이 아니다")
        else:
            print("자기 검증 실패: 검증기가 틀린 입력을 잡아내지 못했다")
        print("=" * 70)
        return all_caught
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


# ---------------------------------------------------------------------------
def main() -> int:
    args = [a for a in sys.argv[1:] if a != "--self-test"]
    do_self_test = "--self-test" in sys.argv

    result_path = pathlib.Path(args[0]) if len(args) > 0 else DEFAULT_RESULT
    source_path = pathlib.Path(args[1]) if len(args) > 1 else DEFAULT_SOURCE

    for p, label in ((result_path, "결과 파일"), (source_path, "원본 데이터")):
        if not p.exists():
            print(f"[오류] {label}을 찾을 수 없습니다: {p}")
            return 1

    if do_self_test:
        return 0 if self_test(result_path, source_path) else 1

    return 0 if verify(result_path, source_path) else 1


if __name__ == "__main__":
    sys.exit(main())
