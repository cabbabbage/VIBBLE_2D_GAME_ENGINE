#!/usr/bin/env python3
import os, re
from pathlib import Path

# Match /* ... */ blocks (multi-line)
BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)
# Match // comments
LINE_COMMENT_RE = re.compile(r"//.*?$", re.MULTILINE)

def remove_all_comments(text: str) -> str:
    # Remove block comments
    text = re.sub(BLOCK_COMMENT_RE, "", text)
    # Remove // comments
    text = re.sub(LINE_COMMENT_RE, "", text)
    return text

def process_file(path: Path):
    original = path.read_text(encoding="utf-8", errors="ignore")
    formatted = remove_all_comments(original)
    if formatted != original:
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
