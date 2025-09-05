#!/usr/bin/env python3
import os
import json

TOP_LEVEL_REMOVE = {"layout", "edges", "shading_info", "visited_tabs", "has_spacing"}
ANIM_FIELD_REMOVE = {"on_end_mapping", "custom_update_hpp_path", "custom_update_key"}

def clean_info_json(payload: dict) -> dict:
    """Remove specified sections and animation fields (in place) and return payload."""
    # Remove top-level sections
    for key in list(TOP_LEVEL_REMOVE):
        if key in payload:
            payload.pop(key, None)

    # Clean animations.* fields
    animations = payload.get("animations")
    if isinstance(animations, dict):
        for anim_name, anim in animations.items():
            if isinstance(anim, dict):
                for k in list(ANIM_FIELD_REMOVE):
                    anim.pop(k, None)
    return payload

def process_file(path: str) -> tuple[int, int]:
    """Process a single info.json; returns (#top_level_removed, #anim_fields_removed_total)."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception as e:
        print(f"[SKIP] {path}: failed to parse JSON ({e})")
        return (0, 0)

    # Count before
    tl_before = sum(1 for k in TOP_LEVEL_REMOVE if k in data)
    anim_before = 0
    if isinstance(data.get("animations"), dict):
        for anim in data["animations"].values():
            if isinstance(anim, dict):
                anim_before += sum(1 for k in ANIM_FIELD_REMOVE if k in anim)

    clean_info_json(data)

    # Count after (for sanity)
    tl_after = sum(1 for k in TOP_LEVEL_REMOVE if k in data)
    anim_after = 0
    if isinstance(data.get("animations"), dict):
        for anim in data["animations"].values():
            if isinstance(anim, dict):
                anim_after += sum(1 for k in ANIM_FIELD_REMOVE if k in anim)

    # Only write if changes occurred
    if (tl_before != tl_after) or (anim_before != anim_after):
        try:
            with open(path, "w", encoding="utf-8") as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
                f.write("\n")
            print(f"[OK]   {path}: removed {tl_before - tl_after} top-level sections, "
                  f"{anim_before - anim_after} animation fields")
        except Exception as e:
            print(f"[ERR]  {path}: failed to write changes ({e})")
    else:
        print(f"[SKIP] {path}: no target fields found")

    return (tl_before - tl_after, anim_before - anim_after)

def main():
    total_files = 0
    total_tl_removed = 0
    total_anim_removed = 0

    for root, _, files in os.walk("."):
        for name in files:
            if name == "info.json":
                total_files += 1
                path = os.path.join(root, name)
                tl, anim = process_file(path)
                total_tl_removed += tl
                total_anim_removed += anim

    print(f"\nDone. Scanned {total_files} file(s). "
          f"Removed {total_tl_removed} top-level section(s) and "
          f"{total_anim_removed} animation field(s).")

if __name__ == "__main__":
    main()
