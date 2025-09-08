#!/usr/bin/env python3
import os
import sys
from pathlib import Path

def is_blank(s: str) -> bool:
    return s.strip() == ""

def collapse_blank_runs(text: str) -> str:
    lines = text.splitlines()
    out = []
    last_was_blank = False
    for ln in lines:
        if is_blank(ln):
            if not last_was_blank:  # allow one
                out.append("")
                last_was_blank = True
            # else skip extra blanks
        else:
            out.append(ln.rstrip())  # keep content, drop trailing spaces
            last_was_blank = False
    # Preserve final newline if file had one; otherwise add one for POSIX friendliness
    return "\n".join(out) + "\n"

def process_file(p: Path, dry: bool) -> None:
    orig = p.read_text(encoding="utf-8", errors="ignore")
    new = collapse_blank_runs(orig)
    if new != orig:
        if not dry:
            p.write_text(new, encoding="utf-8")
        print(f"[CHANGED] {p}")
    else:
        print(f"[OK]      {p}")

def find_targets(root: Path, exts=(".cpp",".hpp",".cc",".hh",".h",".ipp")):
    for dp, _, files in os.walk(root):
        for fn in files:
            if Path(fn).suffix.lower() in exts:
                yield Path(dp) / fn

def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description="Collapse runs of multiple blank lines into a single blank line.")
    ap.add_argument("--dry-run", action="store_true", help="Preview changes; do not write files.")
    ap.add_argument("--exts", default=".cpp,.hpp,.cc,.hh,.h,.ipp",
                    help="Comma-separated extensions to process.")
    args = ap.parse_args(argv)

    exts = tuple(x if x.startswith(".") else f".{x}" for x in args.exts.split(","))
    root = Path(".").resolve()
    for p in find_targets(root, exts):
        process_file(p, args.dry_run)

if __name__ == "__main__":
    main(sys.argv[1:])
