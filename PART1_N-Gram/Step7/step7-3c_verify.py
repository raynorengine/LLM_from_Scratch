"""[STEP 7 / Phase 3-c] 독립 검증 - 생성된 문장이 모델에서 나올 수 있는 것인가

검증 방식 (CLAUDE.md 검증 규칙):
  C의 출력을 받아쓰지 않는다. **step7-2b_tokens.txt 에서 6그램을 다시 세어**
  생성 결과의 모든 전이가 그 통계에 실재하는지 확인한다.

검사 항목:
  [1] 결과 파일 형식 (헤더에 seed / count / n 이 있는가)
  [2] 문장 수가 헤더의 count 와 같은가
  [3] 생성된 **모든 전이**가 빈도표에 실재하는가
      - 문맥 BOS 5개에서 시작해 한 토큰씩 민다. 각 (문맥 -> 다음) 이 학습에 있어야 한다
      - 없는 전이가 하나라도 있으면 모델이 만들 수 없는 토큰을 뱉은 것이다
  [4] 문장이 길이 상한에 걸리지 않았다면, 마지막 문맥에서 EOS 가 가능했는가
  [5] 어휘에 없는 토큰이 섞이지 않았는가
  [6] 재현성 - 같은 seed 로 다시 돌리면 같은 결과가 나오는가

사용법:
  python step7-3c_verify.py [결과파일]    검증 (생략하면 번호가 가장 큰 결과 파일)
  python step7-3c_verify.py --self-test   검증기가 망가진 입력에 FAIL을 내는지 확인

종료 코드: 0 = 성공 / 1 = 실패
"""

from __future__ import annotations

import re
import subprocess
import sys
import tempfile
import time
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
VOCAB_PATH = HERE / "step7-3a_vocab.txt"
TOKENS_PATH = HERE / "step7-2b_tokens.txt"
EXE_PATH = HERE / "step7-3c_6gram.exe"

N = 6
CTX = N - 1
W = "<W>"
BOS, EOS = "BOS", "EOS"


# ===================================================================
# [A] 원본에서 6그램을 다시 센다
#
# [이유] C가 만든 빈도표 파일을 읽지 않는다. 그 파일이 틀렸다면 같은 오류를
#   그대로 받아들이게 된다. 원본 토큰 파일에서 처음부터 센다.
#   여기서는 빈도 값이 아니라 **"그 전이가 존재하는가"** 만 필요하므로
#   (문맥 -> 가능한 다음 토큰 집합) 형태로 모은다.
# ===================================================================
def build_model(path: Path) -> dict[tuple, set]:
    model: dict[tuple, set] = {}
    with path.open(encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n").rstrip("\r")
            if not line:
                continue
            seq = [BOS] * CTX + [p for p in line.split(W) if p] + [EOS]
            for i in range(CTX, len(seq)):
                model.setdefault(tuple(seq[i - CTX:i]), set()).add(seq[i])
    return model


# ===================================================================
# [B] 결과 파일 읽기
# ===================================================================
def read_result(path: Path) -> tuple[dict, list[list[str]]]:
    head: dict[str, str] = {}
    sents: list[list[str]] = []
    with path.open(encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n").rstrip("\r")
            if not line:
                continue
            if line.startswith("#"):
                parts = line[1:].strip().split("\t")
                if len(parts) >= 2:
                    head.setdefault(parts[0].strip(), parts[1].strip())
                continue
            m = re.match(r"^(\d+): (.*)$", line)
            if m:
                sents.append([p for p in m.group(2).split(W) if p])
    return head, sents


def latest_result() -> Path | None:
    files = sorted(HERE.glob("step7-3c_6gram_result_[0-9][0-9].txt"))
    return files[-1] if files else None


# ===================================================================
# [C] 검사
#
# [이유] 생성 결과는 난수에 따라 매번 달라지므로 "문장이 무엇인가" 로는 검증할 수 없다.
#   대신 **모델이 만들 수 있는 것만 나왔는가** 를 본다. 이것은 난수와 무관하게
#   항상 참이어야 하는 성질이다.
# ===================================================================
def check(head: dict, sents: list[list[str]], model: dict[tuple, set],
          vocab: set[str]) -> list[str]:
    errors: list[str] = []

    # [1] 헤더
    for key in ("seed", "count", "n"):
        if key not in head:
            errors.append(f"[1] 헤더에 {key} 가 없습니다")
    if head.get("n") and int(head["n"]) != N:
        errors.append(f"[1] n 이 {head['n']} 입니다 (검증기는 {N} 기준)")

    # [2] 문장 수
    if "count" in head and len(sents) != int(head["count"]):
        errors.append(f"[2] 문장 수 {len(sents)} != 헤더 count {head['count']}")

    bad_trans = bad_vocab = bad_end = 0
    sample: list[str] = []

    for si, toks in enumerate(sents, 1):
        # [5] 어휘 확인
        for t in toks:
            if t not in vocab:
                bad_vocab += 1
                if len(sample) < 5:
                    sample.append(f"      {si}번 문장: 어휘에 없는 토큰 {t!r}")
                break

        # [3] 전이 확인
        ctx = (BOS,) * CTX
        ok = True
        for t in toks:
            nexts = model.get(ctx)
            if nexts is None or t not in nexts:
                bad_trans += 1
                ok = False
                if len(sample) < 5:
                    sample.append(f"      {si}번 문장: 불가능한 전이 "
                                  f"{' '.join(ctx)} -> {t}")
                break
            ctx = ctx[1:] + (t,)

        # [4] 끝맺음 확인 (길이 상한에 걸린 문장은 제외)
        if ok and len(toks) < int(head.get("hit_limit_len", 200)):
            nexts = model.get(ctx)
            if nexts is None or EOS not in nexts:
                bad_end += 1
                if len(sample) < 5:
                    sample.append(f"      {si}번 문장: 마지막 문맥에서 EOS 가 불가능 "
                                  f"({' '.join(ctx)})")

    if bad_trans:
        errors.append(f"[3] 모델에 없는 전이를 쓴 문장 {bad_trans}개")
    if bad_vocab:
        errors.append(f"[5] 어휘에 없는 토큰을 쓴 문장 {bad_vocab}개")
    if bad_end:
        errors.append(f"[4] EOS 로 끝날 수 없는 문장 {bad_end}개")
    errors.extend(sample)
    return errors


# ===================================================================
# [D] 재현성 - 같은 seed 로 다시 돌린다
#
# [이유] seed 를 기록해 두는 것은 "같은 결과를 다시 만들 수 있다" 는 약속이다.
#   그 약속이 지켜지는지 실제로 돌려서 확인한다.
#   결과는 임시 폴더에 쓴다. 그래야 결과 파일 번호(01~99)를 소모하지 않는다.
# ===================================================================
def check_reproducible(head: dict, sents: list[list[str]]) -> list[str]:
    if not EXE_PATH.exists():
        return ["[6] 실행 파일이 없어 재현성을 확인하지 못했습니다"]
    if "seed" not in head or "count" not in head:
        return ["[6] 헤더에 seed/count 가 없어 재현성을 확인할 수 없습니다"]

    with tempfile.TemporaryDirectory() as td:
        out = Path(td) / "repro.txt"
        r = subprocess.run([str(EXE_PATH), head["count"], head["seed"], str(out)],
                           cwd=str(HERE), capture_output=True)
        if r.returncode != 0:
            return [f"[6] 재실행 실패 (종료 코드 {r.returncode})"]
        _, again = read_result(out)

    if len(again) != len(sents):
        return [f"[6] 재실행 문장 수가 다릅니다: {len(again)} vs {len(sents)}"]
    for i, (a, b) in enumerate(zip(sents, again), 1):
        if a != b:
            return [f"[6] 같은 seed 인데 {i}번 문장이 다릅니다",
                    f"      처음: {W.join(a)[:70]}",
                    f"      재실행: {W.join(b)[:70]}"]
    return []


# ===================================================================
# [E] 자가 시험
# ===================================================================
def self_test() -> int:
    # 아주 작은 모델: BOS*5 -> 가 -> 나 -> EOS
    model = {
        (BOS,) * CTX: {"가"},
        (BOS, BOS, BOS, BOS, "가"): {"나"},
        (BOS, BOS, BOS, "가", "나"): {EOS},
    }
    vocab = {"가", "나", BOS, EOS}
    head = {"seed": "1", "count": "1", "n": str(N)}
    good = [["가", "나"]]

    cases: list[tuple[str, dict, list, bool]] = [
        ("정상",                  head, good,                   False),
        ("불가능한 전이",          head, [["가", "가"]],          True),
        ("어휘에 없는 토큰",        head, [["가", "없는토큰"]],    True),
        ("EOS 로 못 끝남",         head, [["가"]],               True),
        ("문장 수가 헤더와 다름",   head, good + good,            True),
        ("헤더에 seed 없음",       {"count": "1", "n": str(N)}, good, True),
    ]

    failed = 0
    for name, h, s, must_fail in cases:
        got = bool(check(h, s, model, vocab))
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

    result_path = Path(argv[1]) if len(argv) > 1 else latest_result()
    if not result_path or not result_path.exists():
        print("결과 파일이 없습니다. 먼저 step7-3c_6gram.exe 를 실행하세요.")
        return 1
    for p in (VOCAB_PATH, TOKENS_PATH):
        if not p.exists():
            print(f"파일이 없습니다: {p.name}")
            return 1

    head, sents = read_result(result_path)
    print(f"검증 대상: {result_path.name} (seed {head.get('seed', '?')}, {len(sents)}문장)")

    t0 = time.time()
    model = build_model(TOKENS_PATH)
    vocab = {ln.split("\t")[1] for ln in VOCAB_PATH.read_text(encoding="utf-8").splitlines()
             if ln and not ln.startswith("#") and len(ln.split("\t")) == 3}
    print(f"  독립 재계산: 문맥 {len(model):,}개 ({time.time() - t0:.1f}초)")

    errors = check(head, sents, model, vocab)
    print("  [3][4][5] 전이·끝맺음·어휘 검사 완료")

    t0 = time.time()
    errors += check_reproducible(head, sents)
    print(f"  [6] 재현성 검사 완료 ({time.time() - t0:.1f}초)\n")

    if errors:
        print("검증 실패:")
        for e in errors[:20]:
            print("  " + e)
        return 1

    total_tok = sum(len(s) for s in sents)
    print(f"검증 통과 — {len(sents)}문장 / 토큰 {total_tok}개의 모든 전이가 모델에 실재")
    print(f"  평균 {total_tok / len(sents):.2f}토큰, 같은 seed 재실행 결과 일치")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
