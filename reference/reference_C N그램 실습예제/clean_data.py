"""data 폴더의 소설 텍스트를 정제하여 data_clean 폴더에 저장"""
import sys, io, os, re, glob
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

SRC = "data"
DST = "data_clean"
os.makedirs(DST, exist_ok=True)

# SF 소설 머리말 패턴 (출판사 메타데이터)
SF_HEADER_KEYWORDS = [
    "SF 고전 명작 시리즈", "쿠키북스", "다이피아", "DIYPIA",
    "diypia.com", "info@", "전자책 제작", "저작권자",
    "수정, 보완 사항", "직지 프로젝트", "협의에 따라 조치",
    "지은이", "펴낸이", "펴낸날", "대표전화", "메일주소",
    "구글 웹폰트", "전자책을 열람", "아이디어회관",
]

def is_sf_header(line):
    return any(kw in line for kw in SF_HEADER_KEYWORDS)

def clean_text(text, filename):
    lines = text.split("\n")

    # 1) SF 소설: 앞부분 메타데이터 제거 (첫 25줄 내에서)
    start = 0
    for i, line in enumerate(lines[:25]):
        if is_sf_header(line):
            start = i + 1
    # start 이후 빈 줄 건너뛰기
    while start < len(lines) and lines[start].strip() == "":
        start += 1

    text = "\n".join(lines[start:])

    # 2) 한자 제거: 괄호 안 한자 + 본문 속 한자 모두
    text = re.sub(r"\([一-龥㐀-䶵\u3400-\u4DBF\u4E00-\u9FFF\uF900-\uFAFF]+\)", "", text)
    text = re.sub(r"[一-龥㐀-䶵\u3400-\u4DBF\u4E00-\u9FFF\uF900-\uFAFF]", "", text)

    # 3) 위키 기호 제거
    text = re.sub(r"[🙝🙟]", "", text)

    # 4) 영문/한자만으로 된 줄 제거
    text = re.sub(r"^[A-Za-z\s\-:,.'\"()]+$", "", text, flags=re.MULTILINE)
    text = re.sub(r"^[一-龥㐀-䶵\u3400-\u4DBF\u4E00-\u9FFF\uF900-\uFAFF\s]+$", "", text, flags=re.MULTILINE)

    # 5) 한글 자모 제거 (옛한글 등)
    text = re.sub(r"[\u1100-\u11FF\u3130-\u318F]", "", text)

    # 6) 특수 기호 정리
    text = re.sub(r"<[^>]*>", "", text)                  # HTML 태그
    text = re.sub(r"[─×○☆△◑◈□▲→←↑↓■●◇▷◁▽]", "", text)  # 도형/화살표
    text = re.sub(r"⋯", "…", text)                       # 줄임표 통일
    text = re.sub(r"～", "~", text)                       # 전각 물결 → 반각
    text = re.sub(r"[\uE000-\uF8FF]", "", text)          # 사설 유니코드(PUA)
    text = re.sub(r"[\u200B\u3013`\xAD]", "", text)      # 제로폭공백/게타/백틱/소프트하이픈
    text = re.sub(r"[=+<>―—–\-~]", "", text)              # 구분선/꺽쇠/대시/물결 잔여
    text = re.sub(r"℃", "도", text)                       # 섭씨 → 도
    text = re.sub(r"°", "", text)                         # 도 기호

    # 7) 연속 공백 → 하나 (다른 정제 후 마지막에)
    text = re.sub(r"  +", " ", text)

    # 8) 빈 줄 정리 (3줄 이상 연속 → 2줄로)
    text = re.sub(r"\n{3,}", "\n\n", text)

    # 6) 앞뒤 공백 정리
    text = text.strip() + "\n"

    return text

total_before = 0
total_after = 0
results = []

for filepath in sorted(glob.glob(os.path.join(SRC, "*.txt"))):
    filename = os.path.basename(filepath)
    with open(filepath, "r", encoding="utf-8") as f:
        raw = f.read()

    # 자모가 100개 이상이면 옛한글 텍스트 → 제외
    jamo_count = len(re.findall(r"[\u1100-\u11FF\u3130-\u318F]", raw))
    if jamo_count >= 100:
        print(f"[제외] {filename} (한글 자모 {jamo_count}개 — 옛한글 텍스트)")
        continue

    cleaned = clean_text(raw, filename)

    outpath = os.path.join(DST, filename)
    with open(outpath, "w", encoding="utf-8") as f:
        f.write(cleaned)

    before = len(raw)
    after = len(cleaned)
    total_before += before
    total_after += after
    results.append((filename, before, after))

print(f"{'파일':<45} {'원본':>8} {'정제':>8} {'감소':>6}")
print("-" * 72)
for name, before, after in results:
    diff = before - after
    print(f"{name:<45} {before//1024:>6}KB {after//1024:>6}KB {diff//1024:>4}KB")
print("-" * 72)
print(f"{'합계':<45} {total_before//1024:>6}KB {total_after//1024:>6}KB {(total_before-total_after)//1024:>4}KB")
