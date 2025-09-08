#!/usr/bin/env python3
"""
Normalize indentation for multi-line function parameter lists:
- Continuation lines align to a hanging indent after '('
- Closing ')' aligns with the line that opened '('
- Only adjusts leading whitespace (safe)
"""

import os
import re
import sys
from pathlib import Path
from typing import Tuple

# ==== String/comment masking (so parens in strings/comments don't confuse us) ====

STRING_OR_CHAR_RE = re.compile(r'''
    "([^"\\]|\\.)*"     |   # " ... "
    '([^'\\]|\\.)*'         # ' ... '
''', re.VERBOSE)

def strip_line_comment_outside_strings(s: str) -> str:
    tmp = STRING_OR_CHAR_RE.sub(lambda m: " " * (m.end()-m.start()), s)
    pos = tmp.find("//")
    return s if pos < 0 else s[:pos]

def strip_block_comments_for_counting(s: str, in_block: bool) -> Tuple[str, bool]:
    out = []
    i = 0
    while i < len(s):
        if in_block:
            j = s.find("*/", i)
            if j == -1:
                return "".join(out), True
            i = j + 2
            in_block = False
            continue
        j = s.find("/*", i)
        if j == -1:
            out.append(s[i:])
            break
        out.append(s[i:j])
        i = j + 2
        in_block = True
    return "".join(out), in_block

def code_only(line: str, in_block: bool) -> Tuple[str, bool]:
    no_blocks, in_block = strip_block_comments_for_counting(line, in_block)
    no_line = strip_line_comment_outside_strings(no_blocks)
    no_strings = STRING_OR_CHAR_RE.sub(lambda m: " " * (m.end()-m.start()), no_line)
    return no_strings, in_block

# ==== Utilities ====

def leading_ws_len(s: str) -> int:
    i = 0
    while i < len(s) and s[i].isspace():
        i += 1
    return i

def first_non_ws_col(s: str) -> int:
    i = 0
    while i < len(s) and s[i].isspace():
        i += 1
    return i

def reindent_to_col(line: str, col: int) -> str:
    content = line.lstrip()
    return (" " * max(col, 0)) + content

# ==== Parameter-list alignment logic ====

def find_open_paren_and_hang_col(line: str, in_block: bool, indent_step: int) -> Tuple[int | None, int | None, bool]:
    """
    Return (open_paren_col, hang_col, found) for the first OUTERMOST '('.
    hang_col is column to align continuation params:
      - if there's a token after '(' on the same line, align to that token
      - else align to (open_paren_col + 1 + indent_step)
    """
    code, in_block = code_only(line, in_block)
    depth = 0
    open_col = None
    hang_col = None

    # Find first outermost '('
    for i, ch in enumerate(code):
        if ch == '(':
            if depth == 0 and open_col is None:
                open_col = i
            depth += 1
        elif ch == ')':
            depth = max(depth - 1, 0)

        if open_col is not None and depth >= 1 and hang_col is None:
            # Look ahead from i+1 to find first non-space token after '('
            if i == open_col:
                j = i + 1
                while j < len(code) and code[j].isspace():
                    j += 1
                if j < len(code) and code[j] != ')':
                    hang_col = j
                else:
                    hang_col = open_col + 1 + indent_step

    if open_col is None:
        return None, None, in_block
    if hang_col is None:
        hang_col = open_col + 1 + indent_step
    return open_col, hang_col, in_block

def line_starts_with_close_paren(line: str, in_block: bool) -> Tuple[bool, bool]:
    code, in_block = code_only(line, in_block)
    i = first_non_ws_col(code)
    return (i < len(code) and code[i] == ')'), in_block

def count_paren_delta(line: str, in_block: bool) -> Tuple[int, bool]:
    code, in_block = code_only(line, in_block)
    return code.count("(") - code.count(")"), in_block

# ==== Main processing ====

def process_text(text: str, indent_step: int = 4) -> str:
    lines = text.splitlines()
    out = []

    in_block = False
    paren_depth = 0

    # State for an active param block (outermost paren group)
    in_params = False
    opener_indent_col = 0     # indent of the line containing '('
    hang_col = None           # continuation alignment column

    for idx, raw in enumerate(lines):
        line = raw.rstrip("\n")

        # detect new param list opening on this line (only when not already in one)
        open_col, this_hang, in_block = find_open_paren_and_hang_col(line, in_block, indent_step)
        starts_with_close, in_block = line_starts_with_close_paren(line, in_block)
        delta, in_block = count_paren_delta(line, in_block)

        if not in_params and open_col is not None:
            # We just encountered an outermost '(' → start tracking
            in_params = True
            opener_indent_col = first_non_ws_col(line[:open_col])  # indent of the function token area
            hang_col = this_hang

        # Reindent rules
        if in_params:
            if starts_with_close:
                # Align a line that begins with ')' to the opener indent
                line = reindent_to_col(line, opener_indent_col)
            else:
                # Continuation line within params (and not blank)
                if line.strip() != "":
                    # If this same line also contained the open '(', don't force-align it
                    # Only align lines after the opener line
                    if open_col is None:
                        line = reindent_to_col(line, hang_col)

        # Emit
        out.append(line.rstrip())

        # Update paren depth and exit condition for param list
        paren_depth += delta
        if in_params and paren_depth <= 0:
            in_params = False
            hang_col = None

        if paren_depth < 0:
            paren_depth = 0

    return "\n".join(out) + ("\n" if text.endswith("\n") else "")

# ==== Files ====

def process_file(p: Path, indent_step: int):
    orig = p.read_text(encoding="utf-8", errors="ignore")
    new = process_text(orig, indent_step=indent_step)
    if new != orig:
        p.write_text(new, encoding="utf-8")
        print(f"[CHANGED] {p}")
    else:
        print(f"[OK]      {p}")

def find_targets(root: Path, exts=(".cpp", ".hpp")):
    for dp, _, files in os.walk(root):
        for fn in files:
            if Path(fn).suffix.lower() in exts:
                yield Path(dp) / fn

def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description="Normalize indentation for multi-line function parameter lists (safe).")
    ap.add_argument("--indent-step", type=int, default=4, help="Spaces to use after '(' when first param wraps (default 4).")
    ap.add_argument("--exts", default=".cpp,.hpp", help="Comma-separated file extensions.")
    args = ap.parse_args(argv)

    exts = tuple(x if x.startswith(".") else f".{x}" for x in args.exts.split(","))
    root = Path(".").resolve()
    for p in find_targets(root, exts):
        process_file(p, indent_step=args.indent_step)

if __name__ == "__main__":
    main(sys.argv[1:])
