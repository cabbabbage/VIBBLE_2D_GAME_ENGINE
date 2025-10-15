#!/usr/bin/env python3
import os
import json
import shutil
from pathlib import Path
from tempfile import NamedTemporaryFile
from json import JSONDecodeError

TARGET_KEYS = {"child_assets", "areas"}

def strip_keys(obj):
    """Remove TARGET_KEYS anywhere in a JSON structure."""
    if isinstance(obj, dict):
        # Delete matching keys at this level
        for k in list(obj.keys()):
            if k in TARGET_KEYS:
                del obj[k]
        # Recurse into remaining values
        for v in obj.values():
            strip_keys(v)
    elif isinstance(obj, list):
        for item in obj:
            strip_keys(item)
    # primitives: nothing to do
    return obj

def process_info_json(path: Path) -> bool:
    """Load, strip keys, and write back atomically. Returns True if changed."""
    try:
        original = path.read_text(encoding="utf-8")
        data = json.loads(original)
    except (OSError, JSONDecodeError):
        print(f"[skip] Could not read or parse: {path}")
        return False

    before = json.dumps(data, sort_keys=True)
    strip_keys(data)
    after = json.dumps(data, sort_keys=True)

    if before == after:
        print(f"[ok] No changes needed: {path}")
        return False

    # Write atomically and preserve permissions
    try:
        with NamedTemporaryFile("w", encoding="utf-8", delete=False, dir=str(path.parent), prefix=path.name + ".tmp.") as tf:
            json.dump(data, tf, ensure_ascii=False, indent=2)
            tf.write("\n")
            temp_name = tf.name
        shutil.copymode(path, temp_name)
        os.replace(temp_name, path)
        print(f"[fix] Updated: {path}")
        return True
    except OSError as e:
        print(f"[err] Failed to write: {path} ({e})")
        try:
            if os.path.exists(temp_name):
                os.remove(temp_name)
        except Exception:
            pass
        return False

def main():
    # Run from the script's own directory
    base_dir = Path(__file__).resolve().parent
    os.chdir(base_dir)

    changed = 0
    for root, _, files in os.walk(base_dir):
        for name in files:
            if name == "info.json":
                path = Path(root) / name
                if process_info_json(path):
                    changed += 1

    print(f"[done] Files updated: {changed}")

if __name__ == "__main__":
    main()
