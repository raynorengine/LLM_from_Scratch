"""[STEP 7 / 6단계] 독립 검증 - 프롬프트 생성이 약속을 지켰는가

이 단계에서 지켜야 할 약속은 네 가지다.
  (1) 모든 문장이 **프롬프트로 시작**한다
  (2) 프롬프트 뒤에 이어진 토큰이 전부 **학습에 실재하는 전이**다
      (프롬프트 자체는 우리가 준 것이므로 전이 검사 대상이 아니다.
       검사는 '프롬프트 다음에 무엇이 왔는가' 부터 시작한다)
  (3) 머리말의 진단값(context / bos_left / candidates / corpus_starts)이 맞다
  (4) 같은 seed 로 다시 돌리면 같은 결과가 나온다

검증 방식 (CLAUDE.md 검증 규칙):
  C의 출력을 받아쓰지 않는다. **step7-2b_tokens.txt 에서 6그램을 다시 세어**
  (문맥 -> 가능한 다음 토큰 집합) 을 만들고 그것으로 확인한다.

사용법:
  python step7-6_verify.py [번호]              생략하면 번호가 가장 큰 결과
  python step7-6_verify.py --self-test
  python step7-6_verify.py --tokenize "돈을"   한글 -> 프롬프트 토큰 문자열

종료 코드: 0 = 성공 / 1 = 실패
"""

from __future__ import annotations

import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
VOCAB_PATH = HERE / "step7-3a_vocab.txt"
TOKENS_PATH = HERE / "step7-2b_tokens.txt"
EXE_PATH = HERE / "step7-6_prompt.exe"

N = 6
CTX = N - 1
W = "<W>"
S = "<S>"
BOS, EOS = "BOS", "EOS"


# ===================================================================
# [A] 한글 -> 프롬프트 토큰 (C 가 쓸 수 없는 Kiwi 를 여기서 담당한다)
#
# [이유] 규칙은 2-b(전체 토큰화)와 **똑같아야** 한다. 다르면 프롬프트가
#   학습 데이터와 미묘하게 어긋나 "없는 문맥" 이 되어 버린다.
# ===================================================================
def tokenize_text(text: str) -> list[str]:
    from kiwipiepy import Kiwi
    kiwi = Kiwi()
    pieces: list[str] = []
    cursor = 0
    for t in kiwi.tokenize(text):
        if t.start > cursor:
            pieces.append(S)
        form = t.form
        surface = text[t.start:t.start + t.len]
        if " " in form or " " in surface:
            form = surface
        for i, part in enumerate(form.split(" ")):
            if i:
                pieces.append(S)
            pieces.append(f"{part}/{t.tag}")
        cursor = t.start + t.len
    return pieces


# ===================================================================
# [B] 원본에서 다시 세기
# ===================================================================
def build_model(path: Path) -> tuple[dict[tuple, set], list[list[str]]]:
    model: dict[tuple, set] = {}
    sents: list[list[str]] = []
    with path.open(encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n").rstrip("\r")
            if not line:
                continue
            toks = [p for p in line.split(W) if p]
            sents.append(toks)
            seq = [BOS] * CTX + toks + [EOS]
            for i in range(CTX, len(seq)):
                model.setdefault(tuple(seq[i - CTX:i]), set()).add(seq[i])
    return model, sents


def read_result(path: Path) -> tuple[dict[str, str], list[list[str]]]:
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


def prompt_context(prompt: list[str]) -> list[str]:
    """BOS 를 CTX개 깐 뒤 프롬프트를 이어 붙이고 마지막 CTX개를 쓴다"""
    return ([BOS] * CTX + prompt)[-CTX:]


# ===================================================================
# [C] 검사
# ===================================================================
def check(head: dict[str, str], sents: list[list[str]],
          model: dict[tuple, set], corpus: list[list[str]],
          vocab: set[str]) -> list[str]:
    errors: list[str] = []

    for key in ("prompt", "context", "bos_left", "candidates", "corpus_starts", "seed", "count"):
        if key not in head:
            errors.append(f"[3] 머리말에 {key} 가 없습니다")
    if errors:
        return errors

    prompt = [p for p in head["prompt"].split(W) if p]
    plen = len(prompt)

    # --- [3] 진단값 독립 재계산 ---
    want_ctx = prompt_context(prompt)
    if head["context"].split() != want_ctx:
        errors.append(f"[3] context 가 다릅니다: C={head['context']} / 실제={' '.join(want_ctx)}")
    if int(head["bos_left"]) != want_ctx.count(BOS):
        errors.append(f"[3] bos_left={head['bos_left']} 인데 실제 BOS 는 {want_ctx.count(BOS)}개")

    nexts = model.get(tuple(want_ctx), set())
    if int(head["candidates"]) != len(nexts):
        errors.append(f"[3] candidates={head['candidates']} 인데 다시 세면 {len(nexts)}개")

    starts = sum(1 for s in corpus if s[:plen] == prompt) if plen else len(corpus)
    if int(head["corpus_starts"]) != starts:
        errors.append(f"[3] corpus_starts={head['corpus_starts']} 인데 다시 세면 {starts}개")

    # 막힌 경우: 문장이 없어야 한다
    if not nexts:
        if sents:
            errors.append(f"[2] 막힌 프롬프트인데 문장 {len(sents)}개가 생성됐습니다")
        return errors

    if len(sents) != int(head["count"]):
        errors.append(f"[1] 문장 수 {len(sents)} != 머리말 count {head['count']}")

    bad_prefix = bad_trans = bad_vocab = 0
    sample: list[str] = []

    for si, toks in enumerate(sents, 1):
        # --- [1] 프롬프트로 시작하는가 ---
        if toks[:plen] != prompt:
            bad_prefix += 1
            if len(sample) < 5:
                sample.append(f"      {si}번: 프롬프트로 시작하지 않습니다 ({' '.join(toks[:plen])})")
            continue

        for t in toks:
            if t not in vocab:
                bad_vocab += 1
                if len(sample) < 5:
                    sample.append(f"      {si}번: 어휘에 없는 토큰 {t!r}")
                break

        # --- [2] 프롬프트 다음부터 전이 확인 ---
        ctx = tuple(want_ctx)
        for t in toks[plen:]:
            opts = model.get(ctx)
            if opts is None or t not in opts:
                bad_trans += 1
                if len(sample) < 5:
                    sample.append(f"      {si}번: 불가능한 전이 {' '.join(ctx)} -> {t}")
                break
            ctx = ctx[1:] + (t,)

    if bad_prefix:
        errors.append(f"[1] 프롬프트로 시작하지 않는 문장 {bad_prefix}개")
    if bad_trans:
        errors.append(f"[2] 모델에 없는 전이를 쓴 문장 {bad_trans}개")
    if bad_vocab:
        errors.append(f"[2] 어휘에 없는 토큰을 쓴 문장 {bad_vocab}개")
    errors.extend(sample)
    return errors


# ===================================================================
# [D] 재현성
#
# [이유] 결과는 임시 폴더에 쓴다. 그래야 결과 파일 번호(01~99)를 소모하지 않는다.
# ===================================================================
def check_reproducible(head: dict[str, str], sents: list[list[str]]) -> list[str]:
    if not EXE_PATH.exists():
        return ["[4] 실행 파일이 없어 재현성을 확인하지 못했습니다"]
    with tempfile.TemporaryDirectory() as td:
        out = Path(td) / "repro.txt"
        r = subprocess.run([str(EXE_PATH), head["prompt"], head["count"], head["seed"], str(out)],
                           cwd=str(HERE), capture_output=True)
        if r.returncode != 0:
            return [f"[4] 재실행 실패 (종료 코드 {r.returncode})"]
        _, again = read_result(out)
    if len(again) != len(sents):
        return [f"[4] 재실행 문장 수가 다릅니다: {len(again)} vs {len(sents)}"]
    for i, (a, b) in enumerate(zip(sents, again), 1):
        if a != b:
            return [f"[4] 같은 seed 인데 {i}번 문장이 다릅니다",
                    f"      처음  : {W.join(a)[:70]}",
                    f"      재실행: {W.join(b)[:70]}"]
    return []


# ===================================================================
# [E] 자가 시험
#
# [이유] 아주 작은 모델을 직접 만들어, 검증기가 틀린 입력에 FAIL 을 내는지 본다.
#   실제 데이터를 쓰면 느리고, 어떤 입력이 틀린 것인지 통제하기 어렵다.
#   (4단계에서 배운 것: 장난감 입력이 문제가 되는 것은 Kiwi 같은 외부 도구가
#    끼어들 때다. 여기 검사는 우리 자료구조만 보므로 장난감 모델로 충분하다)
# ===================================================================
def self_test() -> int:
    corpus = [["돈", "을", "모으", "다"], ["돈", "을", "주", "다"], ["그", "는", "가", "다"]]
    model: dict[tuple, set] = {}
    for s in corpus:
        seq = [BOS] * CTX + s + [EOS]
        for i in range(CTX, len(seq)):
            model.setdefault(tuple(seq[i - CTX:i]), set()).add(seq[i])
    vocab = {t for s in corpus for t in s} | {BOS, EOS}

    prompt = ["돈", "을"]
    ctx = prompt_context(prompt)
    head = {
        "prompt": W.join(prompt), "context": " ".join(ctx),
        "bos_left": str(ctx.count(BOS)), "candidates": str(len(model[tuple(ctx)])),
        "corpus_starts": "2", "seed": "1", "count": "1",
    }
    good = [["돈", "을", "모으", "다"]]

    def h(**kw):
        d = dict(head); d.update(kw); return d

    cases: list[tuple[str, dict, list, bool]] = [
        ("정상",                   head,                        good,                  False),
        ("프롬프트로 시작 안 함",    head,                        [["그", "는", "가", "다"]], True),
        ("불가능한 전이",           head,                        [["돈", "을", "가", "다"]], True),
        ("어휘에 없는 토큰",         head,                        [["돈", "을", "없는것"]],   True),
        ("문장 수가 머리말과 다름",  head,                        good + good,           True),
        ("context 가 틀림",         h(context="BOS BOS BOS BOS 돈"), good,              True),
        ("bos_left 가 틀림",        h(bos_left="0"),             good,                  True),
        ("candidates 가 틀림",      h(candidates="99"),          good,                  True),
        ("corpus_starts 가 틀림",   h(corpus_starts="7"),        good,                  True),
    ]

    failed = 0
    for name, hd, s, must_fail in cases:
        errs = check(hd, s, model, corpus, vocab)
        got = bool(errs)
        ok = got == must_fail
        print(f"  [{'OK' if ok else 'NG'}] {name}: "
              f"기대 {'FAIL' if must_fail else 'PASS'} / 실제 {'FAIL' if got else 'PASS'}"
              + ("" if ok else f"   -> {errs[:1]}"))
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
    if len(argv) > 2 and argv[1] == "--tokenize":
        toks = tokenize_text(argv[2])
        print(W.join(toks))
        print(f"  ({len(toks)}토큰: {' '.join(toks)})", file=sys.stderr)
        return 0

    if len(argv) > 1:
        num = argv[1].zfill(2)
    else:
        found = sorted(HERE.glob("step7-6_prompt_result_[0-9][0-9].txt"))
        if not found:
            print("6단계 결과가 없습니다. 먼저 step7-6_prompt.exe 를 실행하세요.")
            return 1
        num = found[-1].stem[-2:]

    path = HERE / f"step7-6_prompt_result_{num}.txt"
    if not path.exists():
        print(f"파일이 없습니다: {path.name}")
        return 1

    head, sents = read_result(path)
    if "prompt" not in head:
        print(f"{path.name} 은 진단(--probe) 결과라 이 검증기의 대상이 아닙니다.")
        return 1

    print(f"검증 대상: {path.name} (프롬프트 {head['prompt']}, {len(sents)}문장)")

    t0 = time.time()
    model, corpus = build_model(TOKENS_PATH)
    vocab = {ln.split("\t")[1] for ln in VOCAB_PATH.read_text(encoding="utf-8").splitlines()
             if ln and not ln.startswith("#") and len(ln.split("\t")) == 3}
    print(f"  독립 재계산: 문맥 {len(model):,}개 ({time.time() - t0:.1f}초)")

    errors = check(head, sents, model, corpus, vocab)
    print("  [1][2][3] 프롬프트·전이·진단값 검사 완료")

    if sents:
        t0 = time.time()
        errors += check_reproducible(head, sents)
        print(f"  [4] 재현성 검사 완료 ({time.time() - t0:.1f}초)")
    print()

    if errors:
        print("검증 실패:")
        for e in errors[:20]:
            print("  " + e)
        return 1

    if not sents:
        print(f"검증 통과 — 막힌 프롬프트임이 확인됐습니다 (후보 {head['candidates']}개)")
        print(f"  문맥 {head['context']} 은 원본 어디에도 없습니다")
        return 0

    total = sum(len(s) for s in sents)
    print(f"검증 통과 — {len(sents)}문장 전부가 프롬프트로 시작하고, 이어진 전이가 모두 실재")
    print(f"  평균 {total / len(sents):.2f}토큰, 같은 seed 재실행 결과 일치")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
