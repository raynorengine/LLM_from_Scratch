"""
step1_char_freq.py - [STEP 1] 국어사전 데이터의 글자(음절) 빈도 계산 (Python 포팅판)

목적
  step1_char_freq.c 와 동일한 기능을 Python으로 옮긴 **비교 학습용** 프로그램.
  C에서 손으로 하던 일이 Python에서는 어디로 사라지는지 보는 것이 목적이다.
  정식 구현은 C 버전이다. (검증기 step1_verify.py도 Python + Counter라서,
  이걸 정식 구현으로 쓰면 "다른 구현으로 교차검증" 규칙이 깨진다)

C 버전과의 대응
  read_all (fopen/fseek/malloc)  -> open().read()       메모리 관리는 언어가 한다
  utf8_len / decode / encode     -> 없음                str이 이미 글자 단위다
  크기 11,172 count 배열         -> collections.Counter 글자를 그대로 키로 쓴다
  Entry + cmp_desc + qsort       -> sorted(key=...)     비교 함수 대신 정렬 키
  SetConsoleOutputCP(CP_UTF8)    -> stdout.reconfigure  이 함정은 Python에도 있다
  fopen(..., "rb")               -> open(newline="")    개행 자동 변환을 막는 장치

출력
  - 콘솔 : 요약 + 상위 N개 (C와 같은 형식)
  - 파일 : 전체 글자 빈도표 (C와 같은 형식)
    기본 파일명이 C와 다르다. 같으면 C 결과를 덮어써서 step1_verify.py가
    C가 아니라 이 프로그램의 결과를 검증하게 된다.

사용법
  python step1_char_freq.py [입력파일] [상위N개] [출력파일]
  기본값: ../../data/korean_dictionary/words.txt  10  step1_char_freq_py_result.txt

C 버전과 결과가 달라지는 경우 (깨진 UTF-8 입력일 때만)
  C는 잘못된 선두 바이트를 1바이트씩 건너뛰며 세고, 파일 끝에서 잘린 글자는 세지 않는다.
  Python 디코더는 깨진 구간을 U+FFFD 하나로 바꾸므로 '범위 외 문자' 개수가 다를 수 있다.
  정상 UTF-8인 words.txt에서는 결과가 완전히 같다.
"""

from __future__ import annotations

import collections
import sys

# C처럼 배열 인덱스로 쓰지 않는다. 범위 판정에만 필요하다.
HANGUL_BASE = 0xAC00  # '가'
HANGUL_LAST = 0xD7A3  # '힣'

DEFAULT_INPUT = "../../data/korean_dictionary/words.txt"
DEFAULT_OUTPUT = "step1_char_freq_py_result.txt"
DEFAULT_TOPN = 10


def parse_topn(arg: str) -> int:
    """C의 atoi처럼 숫자가 아니면 0을 돌려준다. (0 이하는 뒤에서 기본값으로 바뀐다)"""
    try:
        return int(arg)
    except ValueError:
        return 0


def count_chars(text: str) -> tuple[collections.Counter[str], int, int]:
    """글자별 빈도, 건너뛴 개행 수, 범위 외 문자 수를 센다.

    C에서는 바이트를 3개씩 묶어 코드포인트로 바꿔야 했지만,
    Python의 str은 이미 디코딩된 글자의 나열이라 그냥 순회하면 된다.
    """
    counts: collections.Counter[str] = collections.Counter()
    newlines = 0
    others = 0

    for ch in text:
        # 개행은 글자가 아니라 단어 구분자이므로 세지 않는다.
        # 이걸 세면 '다'의 확률이 4.91%가 아니라 3.87%로 나온다. (C와 같은 함정)
        if ch == "\n" or ch == "\r":
            newlines += 1
        elif HANGUL_BASE <= ord(ch) <= HANGUL_LAST:
            counts[ch] += 1
        else:
            others += 1  # 현재 데이터에는 없지만 방어적으로 집계

    return counts, newlines, others


def pad_like_c(ch: str, width: int) -> str:
    """C의 printf("%-4s")를 흉내 낸다.

    C의 폭 지정은 '바이트' 기준이라 3바이트인 한글 한 글자 뒤에 공백이 1개만 붙는다.
    Python의 f"{ch:<4}"는 '글자' 기준이라 공백이 3개 붙는다.
    콘솔 출력을 C와 똑같이 맞추려고 바이트 수로 계산한다.
    """
    return ch + " " * max(0, width - len(ch.encode("utf-8")))


def main() -> int:
    # Windows 콘솔은 기본 코드페이지가 UTF-8이 아니다. C의 SetConsoleOutputCP와 같은 역할
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")

    # 경로를 Path로 바꾸지 않는다. Windows에서 "../" 가 "..\\" 로 바뀌어
    # 결과 파일의 input_file 줄이 C 결과와 달라진다.
    in_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_INPUT
    topn = parse_topn(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_TOPN
    out_path = sys.argv[3] if len(sys.argv) > 3 else DEFAULT_OUTPUT

    if topn <= 0:
        topn = DEFAULT_TOPN

    # newline="" : 텍스트 모드의 개행 변환(\r\n -> \n)을 끈다. C의 "rb"와 같은 이유
    # errors="replace" : 깨진 바이트에서 죽지 않고 U+FFFD로 바꿔 '범위 외'로 센다
    try:
        with open(in_path, encoding="utf-8", errors="replace", newline="") as fp:
            text = fp.read()
    except OSError:
        print(f"[오류] 입력 파일을 열 수 없습니다: {in_path}", file=sys.stderr)
        return 1

    counts, newlines, others = count_chars(text)
    total = sum(counts.values())

    # 빈도 내림차순, 같으면 코드포인트 오름차순 (C의 cmp_desc와 같은 규칙)
    # 비교 함수를 짜는 대신 "무엇으로 정렬할지"만 키 튜플로 말한다
    entries = sorted(counts.items(), key=lambda kv: (-kv[1], ord(kv[0])))
    kinds = len(entries)

    def percent(count: int) -> float:
        return count * 100.0 / total if total > 0 else 0.0

    # --- 콘솔 출력: 요약 + 상위 N개 ---
    print(f"입력 파일        : {in_path}")
    print(f"전체 글자수      : {total}")
    print(f"고유 글자 종류   : {kinds}")
    print(f"건너뛴 개행      : {newlines}")
    print(f"범위 외 문자     : {others}")
    print(f"\n상위 {min(topn, kinds)}개 글자")
    print("순위  글자      빈도      확률")
    print("----  ----  --------  --------")

    for rank, (ch, count) in enumerate(entries[:topn], start=1):
        print(f"{rank:4d}  {pad_like_c(ch, 4)}  {count:8d}  {percent(count):7.2f}%")

    # --- 파일 출력: 전체 글자 빈도표 ---
    # newline="\n" : Windows에서도 \r\n이 아니라 \n으로 쓴다. C의 "wb"와 같은 이유
    try:
        with open(out_path, "w", encoding="utf-8", newline="\n") as out:
            out.write("# 글자 빈도 계산 결과\n")
            out.write(f"# input_file\t{in_path}\n")
            out.write(f"# total_chars\t{total}\n")
            out.write(f"# unique_chars\t{kinds}\n")
            out.write(f"# newlines\t{newlines}\n")
            out.write(f"# out_of_range\t{others}\n")
            out.write("#\n")
            out.write("# rank\tchar\tcount\tpercent\n")
            for rank, (ch, count) in enumerate(entries, start=1):
                out.write(f"{rank}\t{ch}\t{count}\t{percent(count):.6f}\n")
    except OSError:
        print(f"\n[오류] 출력 파일을 쓸 수 없습니다: {out_path}", file=sys.stderr)
        return 1

    print(f"\n결과 파일 저장   : {out_path} ({kinds}행)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
