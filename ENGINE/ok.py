#!/usr/bin/env python3
import os, re
from pathlib import Path

TRAILING_COMMENT_RE = re.compile(r'(?P<code>.*?)(?://.*)?$')

def strip_trailing_comment(line: str) -> str:
    stripped = line.lstrip()
    if stripped.startswith("//"):  # keep standalone comments
        return line.rstrip()
    m = TRAILING_COMMENT_RE.match(line)
    if m:
        return m.group("code").rstrip()
    return line.rstrip()

def process_lines(lines):
    result = []
    depth = 0
    blank_ready = False
    in_includes = True  # assume file starts with include section until proven otherwise

    for raw in lines:
        line = strip_trailing_comment(raw)
        content = line.strip()

        # Track when we're past the include section
        if in_includes and content and not content.startswith("#include"):
            in_includes = False

        if content == "":
            if in_includes:
                # no blank lines allowed in include sections
                continue
            elif depth > 0:
                # inside a function → no blank lines
                continue
            else:
                # top-level: allow exactly one blank line between declarations
                if not blank_ready and (result and result[-1].strip() != ""):
                    result.append("")
                    blank_ready = True
                continue

        # non-blank line
        blank_ready = False
        result.append(line)

        # Update brace depth
        open_braces = line.count("{")
        close_braces = line.count("}")
        depth += open_braces - close_braces
        if depth < 0:
            depth = 0

    return result

def process_file(path: Path):
    text = path.read_text(encoding="utf-8", errors="ignore")
    lines = text.splitlines()
    new_lines = process_lines(lines)
    formatted = "\n".join(new_lines) + "\n"
    if formatted != text:
        path.write_text(formatted, encoding="utf-8")
        print(f"[CHANGED] {path}")
    else:
        print(f"[OK]      {path}")

def find_targets(root: Path, exts=(".cpp",".hpp")):
    for dirpath, _, files in os.walk(root):
        for fn in files:
            if Path(fn).suffix.lower() in exts:
                yield Path(dirpath) / fn

def main():
    root = Path(".").resolve()
    for f in find_targets(root):
        process_file(f)

if __name__ == "__main__":
    main()
