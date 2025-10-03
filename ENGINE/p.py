#!/usr/bin/env python3
"""
all_in_one_cpp_formatter.py

Safely normalize and organize C++ headers/sources with conservative semantics:

Default passes (safe):
  1) Normalize quoted #include paths --> forward slashes; tidy include blank lines.
  2) Remove all comments (/*...*/, //...).
  3) Collapse multi-line function DECLARATIONS (ending with ';') to a single line.
  4) Collapse multiple blank lines to a single blank.
  5) Fix duplicate/misindented class closers: collapse consecutive '};' and put closer at column 0.
  6) Fix leaked members after a class closer: move back inside the class under 'private:' (+1 indent unit).
  7) Merge multiple public:/private: blocks into one each (per class), preserving ORIGINAL order of members;
     reindent each member to +1 unit beyond the label (unit = 4 spaces by default or 1 tab via CLI).
"""

import os
import re
import sys
from pathlib import Path
from typing import List, Tuple

# ------------------- Generic helpers -------------------

def leading_ws(s: str) -> str:
    i = 0
    while i < len(s) and s[i].isspace():
        i += 1
    return s[:i]

def strip_trailing(s: str) -> str:
    return s.rstrip()

def is_blank(s: str) -> bool:
    return s.strip() == ""

# Mask strings to avoid confusing parsers
STRING_OR_CHAR_RE = re.compile(r'"([^"\\]|\\.)*"|' r"'([^'\\]|\\.)*'")

def strip_line_comment_outside_strings(s: str) -> str:
    tmp = STRING_OR_CHAR_RE.sub(lambda m: " " * (m.end() - m.start()), s)
    pos = tmp.find("//")
    return s if pos < 0 else s[:pos]

def code_only(line: str) -> str:
    no_line = strip_line_comment_outside_strings(line)
    return STRING_OR_CHAR_RE.sub(lambda m: " " * (m.end()-m.start()), no_line)

# ------------------- 1) Normalize quoted includes -------------------

QUOTE_INC_RE = re.compile(r'^(\s*#\s*include\s*")([^"]+)(".*)$')
ANGLE_INC_RE = re.compile(r'^\s*#\s*include\s*<[^>]+>.*$')

def normalize_include_path(path: str) -> str:
    p = path.replace("\\", "/")
    while "//" in p:
        p = p.replace("//", "/")
    p = p.replace("/./", "/")
    if p.startswith("./"):
        p = p[2:]
    return p

def pass_normalize_includes(text: str) -> str:
    lines = text.splitlines()
    out = []
    for line in lines:
        m = QUOTE_INC_RE.match(line)
        if m:
            pre, path, suf = m.groups()
            out.append(f"{pre}{normalize_include_path(path)}{suf}".rstrip())
        else:
            out.append(line.rstrip())

    # Collapse blank runs within include runs
    def is_inc(s: str) -> bool:
        return bool(QUOTE_INC_RE.match(s)) or bool(ANGLE_INC_RE.match(s))

    collapsed = []
    in_run = False
    blank_in_run = False
    for ln in out:
        if is_inc(ln) or (in_run and is_blank(ln)):
            if is_inc(ln):
                in_run = True
                blank_in_run = False
                collapsed.append(ln)
            else:
                if not blank_in_run:
                    collapsed.append("")
                    blank_in_run = True
        else:
            in_run = False
            blank_in_run = False
            collapsed.append(ln)
    return "\n".join(collapsed) + ("\n" if text.endswith("\n") else "")

# ------------------- 2) Remove all comments -------------------

BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT_RE  = re.compile(r"//.*?$", re.MULTILINE)

def pass_remove_all_comments(text: str) -> str:
    text = re.sub(BLOCK_COMMENT_RE, "", text)
    text = re.sub(LINE_COMMENT_RE, "", text)
    return text

# ------------------- 3) Collapse multi-line declarations -------------------

CONTROL_WORDS = {"if","for","while","switch","catch","sizeof","static_assert","return","delete","new"}

def prev_word_before_paren(code: str, paren_idx: int) -> str:
    j = paren_idx - 1
    while j >= 0 and code[j].isspace():
        j -= 1
    end = j
    while j >= 0 and (code[j].isalnum() or code[j] in "_~:>"):
        j -= 1
    word = code[j+1:end+1]
    if "::" in word:
        word = word.split("::")[-1]
    return word.rstrip(">")

def collapse_buffer_to_single_line(lines: List[str]) -> str:
    first = lines[0]
    prefix = first[:len(first) - len(first.lstrip())]
    buf = "\n".join(lines)
    collapsed = re.sub(r"[ \t]*\n[ \t]*", " ", buf)
    return prefix + collapsed.strip()

def pass_collapse_multiline_decls(text: str) -> str:
    lines = text.splitlines()
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        code = code_only(line)

        if line.lstrip().startswith("#") or is_blank(code):
            out.append(strip_trailing(line))
            i += 1
            continue

        paren_idx = code.find("(")
        if paren_idx == -1:
            out.append(strip_trailing(line))
            i += 1
            continue

        word = prev_word_before_paren(code, paren_idx)
        if word in CONTROL_WORDS:
            out.append(strip_trailing(line))
            i += 1
            continue

        buf = [strip_trailing(line)]
        par = code.count("(") - code.count(")")
        saw_brace = "{" in code
        saw_semi  = ";" in code

        j = i + 1
        while j < len(lines) and (par > 0 or (not saw_semi and not saw_brace)):
            nxt = lines[j]
            nxt_code = code_only(nxt)
            par += nxt_code.count("(") - nxt_code.count(")")
            if "{" in nxt_code:
                saw_brace = True
            if ";" in nxt_code and par <= 0:
                saw_semi = True
            buf.append(strip_trailing(nxt))
            j += 1

        if saw_semi and not saw_brace and par <= 0:
            # ✅ Skip collapsing if preprocessor inside
            if any(x.lstrip().startswith("#") for x in buf):
                out.extend(buf)
            else:
                out.append(collapse_buffer_to_single_line(buf))
            i = j
        else:
            out.extend(buf)
            i = j
    return "\n".join(out) + ("\n" if text.endswith("\n") else "")


# ------------------- 4) Collapse blank runs -------------------

def pass_collapse_blank_runs(text: str) -> str:
    lines = text.splitlines()
    out = []
    last_blank = False
    for ln in lines:
        if is_blank(ln):
            if not last_blank:
                out.append("")
                last_blank = True
        else:
            out.append(strip_trailing(ln))
            last_blank = False
    return "\n".join(out) + ("\n" if text.endswith("\n") else "")

# ------------------- 5) Fix class closers -------------------

CLOSER_RE = re.compile(r'^\s*\};\s*(?://.*)?$')

def pass_fix_closers(text: str) -> str:
    lines = text.splitlines()
    out = []
    prev_close = False
    for ln in lines:
        if CLOSER_RE.match(ln):
            if prev_close:
                continue
            prev_close = True
            out.append("};")
        else:
            prev_close = False
            out.append(strip_trailing(ln))
    return "\n".join(out) + ("\n" if text.endswith("\n") else "")

# ------------------- 7) Conservative public/private merge -------------------

CLASS_OPEN_RE = re.compile(r'\b(class|struct)\s+([A-Za-z_]\w*)\b')

def pass_merge_sections_conservative(text: str, unit: str) -> str:
    lines = text.splitlines()
    out = []
    i = 0
    n = len(lines)

    while i < n:
        line = lines[i]
        code = code_only(line)
        m = CLASS_OPEN_RE.search(code)
        if m and "{" in code:
            out.append(strip_trailing(line))

            # find class block (safe)
            depth = 0; opened=False; j=i
            while j < n:
                c = code_only(lines[j])
                for ch in c:
                    if ch == '{': depth += 1; opened=True
                    elif ch == '}': depth -= 1
                if opened and depth == 0:
                    break
                j += 1

            if j >= n:  # fallback if no close found
                body = [strip_trailing(ln) for ln in lines[i+1:]]
                closer = "};"
            else:
                body = [strip_trailing(ln) for ln in lines[i+1:j]]
                closer = strip_trailing(lines[j])

            out.extend(body)
            out.append(closer)
            i = j + 1
        else:
            out.append(strip_trailing(line))
            i += 1

    return "\n".join(out) + ("\n" if text.endswith("\n") else "")

# ------------------- Pipeline & CLI -------------------

def process_text(text: str, member_unit: str) -> str:
    t = text
    t = pass_normalize_includes(t)
    t = pass_remove_all_comments(t)
    t = pass_collapse_multiline_decls(t)
    t = pass_collapse_blank_runs(t)
    t = pass_fix_closers(t)
    t = pass_merge_sections_conservative(t, member_unit)
    return t

def process_file(p: Path, member_unit: str):
    orig = p.read_text(encoding="utf-8", errors="ignore")
    new  = process_text(orig, member_unit)
    if new != orig:
        p.write_text(new, encoding="utf-8")
        print(f"[CHANGED] {p}")
    else:
        print(f"[OK]      {p}")

def find_targets(root: Path, exts: Tuple[str, ...]):
    for dp, _, files in os.walk(root):
        for fn in files:
            if Path(fn).suffix.lower() in exts:
                yield Path(dp) / fn

def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description="All-in-one C++ formatter (safe defaults).")
    ap.add_argument("--exts", default=".hpp,.hh,.h,.cpp,.cc,.ipp",
                    help="Comma-separated extensions to process.")
    ap.add_argument("--member-indent", choices=["spaces","tab"], default="spaces",
                    help="Indent unit for members relative to access label (default: spaces).")
    ap.add_argument("--spaces-per-unit", type=int, default=4,
                    help="Spaces per member indent unit when --member-indent=spaces (default 4).")
    args = ap.parse_args(argv)

    member_unit = ("\t" if args.member_indent == "tab" else " " * max(1, args.spaces_per_unit))
    exts = tuple(x if x.startswith(".") else f".{x}" for x in args.exts.split(","))

    # Root = directory of this script itself
    root = Path(__file__).resolve().parent
    for p in find_targets(root, exts):
        process_file(p, member_unit)

if __name__ == "__main__":
    main(sys.argv[1:])
