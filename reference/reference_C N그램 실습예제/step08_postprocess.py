"""step08_raw.txt의 토큰 시퀀스를 kiwi.join()으로 자연스러운 문장으로 변환"""
import sys, io, re
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

from kiwipiepy import Kiwi
kiwi = Kiwi()

def tokens_to_text(token_line):
    """form/tag<W>form/tag<W>... → kiwi.join()으로 자연스러운 문장 복원"""
    raw_tokens = token_line.split("<W>")
    morphs = []
    for tok in raw_tokens:
        # form/tag 파싱 (마지막 / 기준으로 분리, form에 /가 포함될 수 있음)
        idx = tok.rfind("/")
        if idx > 0:
            form = tok[:idx]
            tag = tok[idx+1:]
            morphs.append((form, tag))
        else:
            morphs.append((tok, "NNG"))  # 태그 없으면 일반 명사로 폴백

    if not morphs:
        return ""

    return kiwi.join(morphs)

# 변환
with open("step08_raw.txt", "r", encoding="utf-8-sig") as f:
    lines = f.readlines()

out = open("step08_result.txt", "w", encoding="utf-8-sig")

for line in lines:
    line = line.rstrip("\n\r")

    # 헤더/구분선은 그대로 통과
    if line.startswith("===") or line.startswith("---") or line.startswith("데이터:") or line == "":
        out.write(line + "\n")
        print(line)
        continue

    # "번호: 토큰시퀀스" 형식 파싱
    m = re.match(r'^(\d+): (.*)$', line)
    if m:
        idx = m.group(1)
        token_seq = m.group(2)
        text = tokens_to_text(token_seq)
        out.write(f"{idx}: {text}\n")
        print(f"{idx}: {text}")
    else:
        out.write(line + "\n")
        print(line)

out.close()
print("\n결과가 step08_result.txt에 저장되었습니다.")
