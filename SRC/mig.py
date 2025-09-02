#!/usr/bin/env python3
"""
migrate_areas_to_section.py  (minimal fields for Area.cpp + restore mode)

Recursively walks a directory tree, finds every `info.json`, and migrates
area definitions into a single `areas` array inside each info.json.

Minimal fields kept per area (only what's required by Area.cpp JSON constructor):
  - "points"
  - "original_dimensions"  (width, height)
  - "offset_x"
  - "offset_y"

Removals:
  - Everything else on areas is dropped (e.g., "anchor", "anchor_point_in_image", "type", "json_path").
  - All `json_path` keys are removed anywhere in the document.

Other behaviors:
  - Any top-level "*_area" objects are moved into `areas[]` with `name` equal to original key.
  - Any child asset inline geometry is moved into `areas[]` and child gets `area_name` reference.
  - Any animation `area_path` (e.g., "collision_area.json") is converted to `area_name` using stem;
    if that file exists, its geometry is inlined (keeping minimal fields only).
  - Each modified `info.json` is backed up to `info.json.bak` before writing.
  - Idempotent: repeated runs won't duplicate changes.

Restore mode:
  - If `--restore` (or `-restore`) is passed, the script restores every `info.json`
    from an adjacent `info.json.bak` if present (no migration is performed in this mode).
  - A dry run is supported with `--dry-run` in restore mode too.

Usage:
  # Dry run (report only)
  python migrate_areas_to_section.py --root /path/to/assets --dry-run

  # Apply migration
  python migrate_areas_to_section.py --root /path/to/assets

  # Restore from backups (report only)
  python migrate_areas_to_section.py --root /path/to/assets --restore --dry-run

  # Restore from backups (apply)
  python migrate_areas_to_section.py --root /path/to/assets --restore
"""

from __future__ import annotations
import argparse
import json
from pathlib import Path
from typing import Dict, Any, List, Tuple
import shutil

# Fields required by Area.cpp JSON constructor
NEEDED_FIELDS = ["points", "original_dimensions", "offset_x", "offset_y"]

# Keys commonly used for top-level areas
COMMON_AREA_KEYS = {
    "impassable_area",
    "passability_area",
    "spacing_area",
    "collision_area",
    "interaction_area",
    "attack_area",
}

def load_json(p: Path):
    with p.open("r", encoding="utf-8") as f:
        return json.load(f)

def save_json(p: Path, data):
    with p.open("w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)

def is_area_object(obj: Any) -> bool:
    return isinstance(obj, dict) and isinstance(obj.get("points"), list)

def normalize_area_block(block: Dict[str, Any]) -> Dict[str, Any]:
    """Return ONLY fields needed by Area.cpp (drop everything else)."""
    out = {}
    for k in NEEDED_FIELDS:
        if k in block:
            out[k] = block[k]
    return out

def make_area_entry(name: str, block: Dict[str, Any]) -> Dict[str, Any]:
    entry = {"name": name}
    entry.update(normalize_area_block(block))
    return entry

def add_or_merge_area(areas: List[Dict[str, Any]], new_area: Dict[str, Any]) -> None:
    """Insert or merge by name (fill in missing minimal fields only)."""
    name = new_area.get("name")
    if not name:
        return
    for a in areas:
        if a.get("name") == name:
            for k in NEEDED_FIELDS:
                if k not in a and k in new_area:
                    a[k] = new_area[k]
            return
    areas.append(new_area)

def migrate_top_level_areas(data: Dict[str, Any], areas: List[Dict[str, Any]]) -> None:
    keys = list(data.keys())
    for k in keys:
        if k == "areas":
            continue
        if k in COMMON_AREA_KEYS or k.endswith("_area"):
            block = data.get(k)
            if is_area_object(block):
                add_or_merge_area(areas, make_area_entry(k, block))
                del data[k]

def try_inline_animation_area(area_path: str, info_dir: Path):
    """Return (area_name, block or None). area_name is file stem; block if file exists and valid."""
    name = Path(area_path).stem
    area_json = info_dir / area_path
    if area_json.exists():
        try:
            block = load_json(area_json)
            if isinstance(block, dict) and is_area_object(block):
                return name, block
        except Exception:
            pass
    return name, None

def migrate_animation_areas(data: Dict[str, Any], info_dir: Path, areas: List[Dict[str, Any]]) -> None:
    anims = data.get("animations")
    if not isinstance(anims, dict):
        return
    for anim in anims.values():
        if not isinstance(anim, dict):
            continue
        if "area_path" in anim and anim["area_path"]:
            name, block = try_inline_animation_area(anim["area_path"], info_dir)
            anim["area_name"] = name
            del anim["area_path"]
            if block:
                add_or_merge_area(areas, make_area_entry(name, block))

def migrate_child_assets(data: Dict[str, Any], areas: List[Dict[str, Any]]) -> None:
    children = data.get("child_assets")
    if not isinstance(children, list):
        return
    existing_names = {a.get("name") for a in areas if "name" in a}
    for idx, child in enumerate(children):
        if not isinstance(child, dict):
            continue
        child.pop("json_path", None)
        if isinstance(child.get("area_name"), str) and child["area_name"]:
            # clean stray geometry on child
            for k in list(child.keys()):
                if k in NEEDED_FIELDS or k == "points":
                    child.pop(k, None)
            continue
        if is_area_object(child):
            base = child.get("name") or f"child_area_{idx}"
            name = base
            n = 1
            while name in existing_names:
                name = f"{base}_{n}"
                n += 1
            existing_names.add(name)
            add_or_merge_area(areas, make_area_entry(name, child))
            for k in NEEDED_FIELDS + ["points", "name"]:
                child.pop(k, None)
            child["area_name"] = name

def strip_json_path_everywhere(data: Any) -> None:
    if isinstance(data, dict):
        data.pop("json_path", None)
        for v in list(data.values()):
            strip_json_path_everywhere(v)
    elif isinstance(data, list):
        for v in data:
            strip_json_path_everywhere(v)

def migrate_info_json(info_path: Path, dry_run: bool = False) -> bool:
    try:
        data = load_json(info_path)
    except Exception as e:
        print(f"[skip] {info_path}: parse error: {e}")
        return False

    original = json.dumps(data, ensure_ascii=False, sort_keys=True)
    areas = data.get("areas")
    if not isinstance(areas, list):
        areas = []
        data["areas"] = areas

    migrate_top_level_areas(data, areas)
    migrate_animation_areas(data, info_path.parent, areas)
    migrate_child_assets(data, areas)
    strip_json_path_everywhere(data)

    updated = json.dumps(data, ensure_ascii=False, sort_keys=True)
    if updated == original:
        print(f"[ok]  {info_path}: no changes needed")
        return False

    if dry_run:
        print(f"[dry] {info_path}: would update")
        return True

    backup = info_path.with_suffix(".json.bak")
    try:
        shutil.copy2(info_path, backup)
    except Exception as e:
        print(f"[warn] {info_path}: backup failed: {e}")
    try:
        save_json(info_path, data)
    except Exception as e:
        print(f"[fail] {info_path}: write failed: {e}")
        return False

    print(f"[done] {info_path}: updated (backup -> {backup.name})")
    return True

def restore_info_json(info_path: Path, dry_run: bool = False) -> bool:
    bak = info_path.with_suffix(".json.bak")
    if not bak.exists():
        print(f"[skip] {info_path}: no backup found")
        return False
    if dry_run:
        print(f"[dry] {info_path}: would restore from {bak.name}")
        return True
    try:
        shutil.copy2(bak, info_path)
    except Exception as e:
        print(f"[fail] {info_path}: restore failed: {e}")
        return False
    print(f"[restored] {info_path}: restored from {bak.name}")
    return True

def walk_and_migrate(root: Path, dry_run: bool = False) -> Tuple[int, int]:
    changed = 0
    total = 0
    for p in root.rglob("info.json"):
        total += 1
        if migrate_info_json(p, dry_run=dry_run):
            changed += 1
    return changed, total

def walk_and_restore(root: Path, dry_run: bool = False) -> Tuple[int, int]:
    restored = 0
    total = 0
    for p in root.rglob("info.json"):
        total += 1
        if restore_info_json(p, dry_run=dry_run):
            restored += 1
    return restored, total

def parse_args():
    ap = argparse.ArgumentParser(description="Migrate areas into unified 'areas' or restore from backups.")
    ap.add_argument("--root", type=str, required=True, help="Root directory to scan recursively")
    ap.add_argument("--dry-run", action="store_true", help="Scan and report without writing changes")
    ap.add_argument("--restore", "-restore", action="store_true", help="Restore each info.json from adjacent info.json.bak")
    return ap.parse_args()

def main():
    args = parse_args()
    root = Path(args.root).expanduser().resolve()
    if not root.exists():
        print(f"[error] root does not exist: {root}")
        return 2

    if args.restore:
        restored, total = walk_and_restore(root, dry_run=args.dry_run)
        mode = "dry-run restore" if args.dry_run else "restore"
        print(f"[summary] {mode}: restored {restored} / {total} files")
    else:
        changed, total = walk_and_migrate(root, dry_run=args.dry_run)
        mode = "dry-run" if args.dry_run else "updated"
        print(f"[summary] {mode}: changed {changed} / {total} files")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
