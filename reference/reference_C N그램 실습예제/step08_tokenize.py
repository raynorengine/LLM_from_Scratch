"""Kiwi 형태소 분석기로 소설 텍스트를 토큰 단위로 분리 (줄 단위 처리)
토큰 형식: form/tag<W>form/tag<W>...
후처리에서 kiwi.join()으로 복원하므로 <S> 불필요, POS 태그 필수.
"""
import sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

from kiwipiepy import Kiwi

kiwi = Kiwi()

SKIP_TOKENS = {'"', "'", '『', '』', '(', ')', '"', '"', ''', ''', '「', '」', '\u201c', '\u201d'}

out = open("data_tokens.txt", "w", encoding="utf-8")
token_count = 0
sent_count = 0
vocab = set()

sent_buffer = []  # 줄 경계를 넘어서 이어가는 문장 버퍼

with open("data_all.txt", "r", encoding="utf-8") as f:
    for line_no, line in enumerate(f, 1):
        line = line.strip()
        if not line:
            # 빈 줄 = 문단 경계 → 버퍼에 남은 토큰이 있으면 문장으로 출력
            if sent_buffer:
                out.write("<W>".join(sent_buffer) + "\n")
                sent_count += 1
                token_count += len(sent_buffer)
                sent_buffer = []
            continue

        result = kiwi.tokenize(line)
        if not result:
            continue

        # 자모 합성: 종성 자모를 앞 토큰에 합치기
        JONGSEONG_MAP = {
            '\u11A8': 1, '\u11A9': 2, '\u11AB': 4, '\u11AF': 8,
            '\u11B7': 16, '\u11B8': 17, '\u11BA': 19, '\u11BC': 21,
            '\u11BD': 22, '\u11BE': 23, '\u11BF': 24, '\u11C0': 25,
            '\u11C1': 26, '\u11C2': 27,
        }

        def merge_jongseong(prev_char, jong_char):
            if '\uAC00' <= prev_char <= '\uD7A3':
                code = ord(prev_char) - 0xAC00
                cur_jong = code % 28
                jong_idx = JONGSEONG_MAP.get(jong_char, 0)
                if not jong_idx:
                    return None
                if cur_jong == 0:
                    return chr(ord(prev_char) + jong_idx)
                base = ord(prev_char) - cur_jong
                if cur_jong in (8, 27):  # ㄹ탈락, ㅎ탈락
                    return chr(base + jong_idx)
                if cur_jong == 17 and jong_idx in (4, 16):  # ㅂ불규칙
                    return chr(base + jong_idx)
            return None

        merged = []
        for t in result:
            form = t.form
            if form and '\u1100' <= form[0] <= '\u11FF' and merged:
                prev_form, prev_tag = merged[-1]
                if prev_form:
                    combined = merge_jongseong(prev_form[-1], form[0])
                    if combined:
                        new_form = prev_form[:-1] + combined + form[1:]
                        merged[-1] = (new_form, prev_tag)
                        continue
                merged[-1] = (prev_form + form, prev_tag)
                continue
            merged.append((form, t.tag))

        # 문장부호 기준으로 문장 분리, form/tag 형식 출력
        # sent_buffer는 줄 경계를 넘어서 이어감 (루프 밖에서 선언)
        for form, tag in merged:
            if form in SKIP_TOKENS:
                continue
            sent_buffer.append(f"{form}/{tag}")
            vocab.add(form)
            if form in (".", "!", "?"):
                out.write("<W>".join(sent_buffer) + "\n")
                sent_count += 1
                token_count += len(sent_buffer)
                sent_buffer = []

        # 남은 토큰은 다음 줄로 이어감

        if line_no % 10000 == 0:
            print(f"  {line_no}줄 처리... ({sent_count}문장)")

# 파일 끝에 남은 버퍼 처리
if sent_buffer:
    out.write("<W>".join(sent_buffer) + "\n")
    sent_count += 1
    token_count += len(sent_buffer)
    sent_buffer = []

out.close()

print(f"\n완료:")
print(f"  문장 수: {sent_count}")
print(f"  토큰 수: {token_count}")
print(f"  어휘 수: {len(vocab)}")

# 예시 출력
print(f"\n예시 (처음 3문장):")
with open("data_tokens.txt", "r", encoding="utf-8") as f:
    for i, line in enumerate(f):
        if i >= 3:
            break
        print(f"  {line.rstrip()}")
