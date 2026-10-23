"""[STEP 7 / Phase 3-b] 독립 검증 - C가 센 6그램 빈도표가 맞는가

검증 방식 (CLAUDE.md 검증 규칙):
  C의 출력을 받아쓰지 않는다. **step7-2b_tokens.txt 에서 처음부터 다시 세어**
  C가 만든 step7-3b_6gram_table.txt 와 **전수 비교**한다.
  C는 해시 테이블 + 문맥별 후보 배열, 여기는 파이썬 dict 로 **다른 방식**이다.

검사 항목:
  [1] 헤더 수치 (문장/토큰/어휘)
  [2] 패딩 검산 - 전체 n그램 == 토큰 + 문장
  [3] 서로 다른 n그램 수 / 문맥 수
  [4] 모든 n그램의 빈도 전수 일치
  [5] 퍼센트 = 문맥 안에서의 비율
  [6] 행 순서가 토큰 번호 오름차순인가
  [7] 빈도 합 == 전체 n그램 수

사용법:
  python step7-3b_verify.py              검증
  python step7-3b_verify.py --self-test  검증기가 망가진 입력에 FAIL을 내는지 확인

종료 코드: 0 = 성공 / 1 = 실패
"""

from __future__ import annotations

import sys
import time
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
VOCAB_PATH = HERE / "step7-3a_vocab.txt"
TOKENS_PATH = HERE / "step7-2b_tokens.txt"
TABLE_PATH = HERE / "step7-3b_6gram_table.txt"

N = 6
CTX = N - 1
W = "<W>"
BOS, EOS = "BOS", "EOS"


# ===================================================================
# [A] 어휘표 읽기
#
# [이유] 번호 자체는 Phase 3-a 에서 이미 전수 검증했다. 여기서는 "행 순서가
#   번호 오름차순인가"([6])를 보기 위해서만 쓴다.
# ===================================================================
def load_vocab(path: Path) -> dict[str, int]:
    tok2id: dict[str, int] = {}
    with path.open(encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.rstrip("\n").rstrip("\r").split("\t")
            if len(parts) == 3:
                tok2id[parts[1]] = int(parts[0])
    return tok2id


# ===================================================================
# [B] 원본에서 독립 재계산
#
# [목적] C와 같은 결과를 다른 경로로 만든다.
# [동작] 문장마다 앞에 BOS 5개, 뒤에 EOS 1개를 붙이고 창을 한 칸씩 밀며 센다.
# [이유] 패딩을 C와 똑같이 **직접** 붙인다. C가 만든 번호열을 가져다 쓰면
#   패딩 버그가 양쪽에 똑같이 생겨 검증이 성립하지 않는다.
#   n그램 키를 토큰 문자열 튜플로 두는 것은, 번호 체계가 틀려도 빈도 비교는
#   독립적으로 성립하게 하려는 것이다.
# ===================================================================
def recount(path: Path) -> tuple[Counter, Counter, int, int]:
    grams: Counter = Counter()
    ctx_total: Counter = Counter()
    sents = 0
    tokens = 0

    with path.open(encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n").rstrip("\r")
            if not line:
                continue
            sents += 1
            seq = [BOS] * CTX + [p for p in line.split(W) if p] + [EOS]
            tokens += len(seq) - CTX - 1

            for i in range(CTX, len(seq)):
                key = tuple(seq[i - CTX:i + 1])
                grams[key] += 1
                ctx_total[key[:CTX]] += 1

    return grams, ctx_total, sents, tokens


# ===================================================================
# [C] C의 빈도표 읽기
#
# [이유] `#` 줄은 설명이지만 `# sentences<탭>64376` 처럼 수치도 들어 있으므로
#   따로 모아 [1] 검사에 쓴다.
# ===================================================================
def read_table(path: Path) -> tuple[dict, list, dict]:
    head: dict[str, str] = {}
    rows: list[tuple[tuple, int, float]] = []
    with path.open(encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n").rstrip("\r")
            if not line:
                continue
            if line.startswith("#"):
                parts = line[1:].strip().split("\t")
                if len(parts) == 2:
                    head[parts[0].strip()] = parts[1].strip()
                continue
            parts = line.split("\t")
            if len(parts) != N + 2:
                raise ValueError(f"빈도표 형식 오류 ({len(parts)}칸): {line[:80]!r}")
            rows.append((tuple(parts[:N]), int(parts[N]), float(parts[N + 1])))
    table = {k: c for k, c, _ in rows}
    return head, rows, table


# ===================================================================
# [D] 비교
#
# [이유] 상위 몇 개만 맞춰 보지 않고 전 항목을 비교한다 (CLAUDE.md 검증 규칙 3번).
#   한쪽에만 있는 n그램도 양방향으로 찾는다. C 가 빠뜨린 것과 C 가 지어낸 것은
#   서로 다른 버그이므로 따로 센다.
# ===================================================================
def compare(grams: Counter, ctx_total: Counter, sents: int, tokens: int,
            head: dict, rows: list, table: dict,
            tok2id: dict[str, int]) -> list[str]:
    errors: list[str] = []

    # [1] 헤더 수치
    for name, want in (("sentences", sents), ("tokens", tokens)):
        if name in head and int(head[name]) != want:
            errors.append(f"[1] 헤더 {name}: 표 {head[name]} vs 재계산 {want:,}")

    # [2] 패딩 검산
    total = sum(grams.values())
    if total != tokens + sents:
        errors.append(f"[2] 패딩 검산 실패: 전체 {total:,} != 토큰 {tokens:,} + 문장 {sents:,}")
    if "total_6grams" in head and int(head["total_6grams"]) != total:
        errors.append(f"[2] 헤더 total_6grams {head['total_6grams']} vs 재계산 {total:,}")

    # [3] 종류 수
    if len(table) != len(grams):
        errors.append(f"[3] 서로 다른 6그램 수: 표 {len(table):,} vs 재계산 {len(grams):,}")
    if "contexts" in head and int(head["contexts"]) != len(ctx_total):
        errors.append(f"[3] 문맥 수: 표 {head['contexts']} vs 재계산 {len(ctx_total):,}")

    # [4] 빈도 전수 비교 (양방향)
    missing = wrong = extra = 0
    sample: list[str] = []
    for key, want in grams.items():
        got = table.get(key)
        if got is None:
            missing += 1
            if len(sample) < 5:
                sample.append(f"      표에 없음: {' '.join(key)} (재계산 {want})")
        elif got != want:
            wrong += 1
            if len(sample) < 5:
                sample.append(f"      빈도 다름: {' '.join(key)} 표 {got} vs 재계산 {want}")
    for key in table:
        if key not in grams:
            extra += 1
            if len(sample) < 5:
                sample.append(f"      표에만 있음: {' '.join(key)}")
    if missing:
        errors.append(f"[4] 표에 빠진 6그램 {missing:,}건")
    if wrong:
        errors.append(f"[4] 빈도가 다른 6그램 {wrong:,}건")
    if extra:
        errors.append(f"[4] 표에만 있는 6그램 {extra:,}건")
    errors.extend(sample)

    # [5] 퍼센트
    bad_pct = 0
    for key, count, pct in rows:
        denom = ctx_total.get(key[:CTX])
        if not denom:
            continue
        want = round(100.0 * count / denom, 6)
        if abs(want - pct) > 1e-6:
            bad_pct += 1
            if bad_pct == 1:
                errors.append(f"[5] 퍼센트 다름: {' '.join(key)} 표 {pct} vs 재계산 {want}")
    if bad_pct:
        errors.append(f"[5] 퍼센트가 다른 행 {bad_pct:,}건")

    # [6] 행 순서가 토큰 번호 오름차순인가
    prev = None
    for key, _, _ in rows:
        try:
            cur = tuple(tok2id[t] for t in key)
        except KeyError as e:
            errors.append(f"[6] 어휘표에 없는 토큰: {e}")
            break
        if prev is not None and cur < prev:
            errors.append(f"[6] 행 순서가 번호 오름차순이 아님: {' '.join(key)}")
            break
        prev = cur

    # [7] 빈도 합
    s = sum(c for _, c, _ in rows)
    if s != total:
        errors.append(f"[7] 표의 빈도 합 {s:,} != 전체 6그램 {total:,}")

    return errors


# ===================================================================
# [E] 자가 시험
#
# [이유] 통과만 하는 검증기는 검증기가 아니다 (CLAUDE.md 검증 규칙 4번).
#   [2][3][4][5][6][7] 이 각각 작동하는지 따로 확인한다.
# ===================================================================
def self_test() -> int:
    # 문장 2개짜리 작은 코퍼스를 손으로 만든다
    sents_txt = ["가/NNG<W>나/NNG", "가/NNG<W>다/NNG"]
    grams: Counter = Counter()
    ctx_total: Counter = Counter()
    tokens = 0
    for line in sents_txt:
        seq = [BOS] * CTX + line.split(W) + [EOS]
        tokens += len(seq) - CTX - 1
        for i in range(CTX, len(seq)):
            k = tuple(seq[i - CTX:i + 1])
            grams[k] += 1
            ctx_total[k[:CTX]] += 1
    sents = len(sents_txt)

    tok2id = {BOS: 0, EOS: 1, "가/NNG": 2, "나/NNG": 3, "다/NNG": 4}
    rows = [(k, c, round(100.0 * c / ctx_total[k[:CTX]], 6)) for k, c in grams.items()]
    rows.sort(key=lambda r: tuple(tok2id[t] for t in r[0]))
    head = {"sentences": str(sents), "tokens": str(tokens),
            "total_6grams": str(sum(grams.values())), "contexts": str(len(ctx_total))}

    def tbl(rs):
        return {k: c for k, c, _ in rs}

    cases: list[tuple[str, tuple, bool]] = [
        ("정상", (head, rows, tbl(rows)), False),
        ("빈도 하나 틀림",
         (head, [(rows[0][0], rows[0][1] + 1, rows[0][2])] + rows[1:],
          tbl([(rows[0][0], rows[0][1] + 1, rows[0][2])] + rows[1:])), True),
        ("행 하나 빠짐", (head, rows[1:], tbl(rows[1:])), True),
        ("없는 행 추가",
         (head, rows + [(("가/NNG",) * N, 1, 100.0)], tbl(rows + [(("가/NNG",) * N, 1, 100.0)])), True),
        ("퍼센트 틀림", (head, [(rows[0][0], rows[0][1], 1.0)] + rows[1:], tbl(rows)), True),
        ("행 순서 뒤집힘", (head, rows[::-1], tbl(rows)), True),
        ("헤더 문장수 틀림", ({**head, "sentences": "999"}, rows, tbl(rows)), True),
    ]

    failed = 0
    for name, (h, r, t), must_fail in cases:
        got = bool(compare(grams, ctx_total, sents, tokens, h, r, t, tok2id))
        ok = got == must_fail
        print(f"  [{'OK' if ok else 'NG'}] {name}: "
              f"기대 {'FAIL' if must_fail else 'PASS'} / 실제 {'FAIL' if got else 'PASS'}")
        failed += 0 if ok else 1
    print(f"\n자가 시험 {len(cases)}건 중 {len(cases) - failed}건 통과")
    return 1 if failed else 0


# ===================================================================
# [F] main
# ===================================================================
def main(argv: list[str]) -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    if len(argv) > 1 and argv[1] == "--self-test":
        return self_test()

    for p in (VOCAB_PATH, TOKENS_PATH, TABLE_PATH):
        if not p.exists():
            print(f"파일이 없습니다: {p.name}")
            return 1

    t0 = time.time()
    tok2id = load_vocab(VOCAB_PATH)
    grams, ctx_total, sents, tokens = recount(TOKENS_PATH)
    t_recount = time.time() - t0

    t0 = time.time()
    head, rows, table = read_table(TABLE_PATH)
    t_read = time.time() - t0

    print("독립 재계산 (step7-2b_tokens.txt 에서 처음부터)")
    print(f"  문장 {sents:,} / 토큰 {tokens:,}")
    print(f"  전체 6그램 {sum(grams.values()):,} (= 토큰 + 문장)")
    print(f"  서로 다른 6그램 {len(grams):,} / 문맥 {len(ctx_total):,}")
    print(f"  (재계산 {t_recount:.1f}초 / 표 읽기 {t_read:.1f}초)")

    t0 = time.time()
    errors = compare(grams, ctx_total, sents, tokens, head, rows, table, tok2id)
    print(f"  (비교 {time.time() - t0:.1f}초)\n")

    if errors:
        print("검증 실패:")
        for e in errors[:20]:
            print("  " + e)
        return 1

    print(f"검증 통과 — 6그램 {len(grams):,}종 전수 일치")
    print("  [1] 헤더 수치 / [2] 패딩 검산 / [3] 종류 수 / [4] 빈도 전수")
    print("  [5] 퍼센트 / [6] 행 순서 / [7] 빈도 합")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
