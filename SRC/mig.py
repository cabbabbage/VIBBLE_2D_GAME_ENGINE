#!/usr/bin/env python3
"""
migrate_animations_to_new_format.py  (overwrite info.json, single .bak backup, relative source paths)

Recursively walks a directory tree, finds every `info.json`, and migrates
legacy animation definitions to the new format.

Key behavior (per request):
- Migrated versions **replace the actual info.json** (in-place).
- A backup **info.json.bak** is created/overwritten BEFORE modifying any info.json.
- `animations[*].source.path` is written as a **relative path** (not absolute).

Restore:
- `--restore` restores each info.json from the adjacent `info.json.bak`.

Usage:
  # Dry run (report only)
  python migrate_animations_to_new_format.py --root /path/to/assets --dry-run

  # Apply migration (always in-place; always creates/overwrites info.json.bak first)
  python migrate_animations_to_new_format.py --root /path/to/assets

  # Share one mapping id across all animations (still creates per-anim default entries)
  python migrate_animations_to_new_format.py --root /path/to/assets --share-one-mapping

  # Restore from backups (report only)
  python migrate_animations_to_new_format.py --root /path/to/assets --restore --dry-run

  # Restore from backups (apply)
  python migrate_animations_to_new_format.py --root /path/to/assets --restore
"""

from __future__ import annotations
import argparse
import json
from pathlib import Path
from typing import Dict, Any, List, Tuple, Optional
import shutil
import uuid
import os

IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tga", ".gif"}

def load_json(p: Path) -> Any:
  with p.open("r", encoding="utf-8") as f:
    return json.load(f)

def save_json(p: Path, data: Any) -> None:
  with p.open("w", encoding="utf-8") as f:
    json.dump(data, f, ensure_ascii=False, indent=2)

def is_pathlike(token: Any) -> bool:
  if not token or not isinstance(token, str):
    return False
  return ("/" in token) or ("\\" in token) or any(token.lower().endswith(ext) for ext in IMAGE_EXTS)

def guess_frames_dir(asset_name: str, frames_path: str, info_dir: Path) -> Path:
  """
  Resolve a directory containing frames for counting.
  Priority:
    1) If frames_path looks like a path: interpret relative to info.json dir (or absolute).
    2) info_dir / frames_path
    3) info_dir / 'assets' / asset_name / frames_path
  """
  if is_pathlike(frames_path):
    p = Path(frames_path)
    if not p.is_absolute():
      p = (info_dir / p).resolve()
    return p
  # prefer local dir next to info.json
  p2 = (info_dir / frames_path).resolve()
  if p2.exists():
    return p2
  # fallback to assets/<asset_name>/<frames_path>
  p1 = (info_dir / "assets" / (asset_name or "") / frames_path).resolve()
  return p1

def to_relative_string(original_frames_path: Any, info_dir: Path) -> str:
  """
  Return a relative path string to persist in JSON:
    - If original was a simple token (no slashes), keep it as-is (e.g., 'walk_left').
    - If it was a relative path, normalize it from info_dir (no leading ./).
    - If absolute, convert to a relative path from info_dir; on failure, use basename.
  """
  if not isinstance(original_frames_path, str) or not original_frames_path:
    return ""
  # simple token like "idle"
  if not ("/" in original_frames_path or "\\" in original_frames_path):
    return original_frames_path

  p = Path(original_frames_path)
  if p.is_absolute():
    try:
      rel = os.path.relpath(str(p), start=str(info_dir))
      return rel.replace("\\", "/")
    except Exception:
      return p.name  # best effort: just keep the leaf name
  # normalize relative (remove ./, use forward slashes)
  norm = (info_dir / p).resolve()
  try:
    rel = os.path.relpath(str(norm), start=str(info_dir))
    return rel.replace("\\", "/")
  except Exception:
    return original_frames_path.replace("\\", "/")

def count_images_in_dir(path: Path) -> int:
  if not path.exists() or not path.is_dir():
    return 0
  return sum(1 for c in path.iterdir() if c.is_file() and c.suffix.lower() in IMAGE_EXTS)

def make_default_movement(n_frames: int) -> List[List[int]]:
  n = max(1, int(n_frames or 0))
  return [[0, 0] for _ in range(n)]

def to_speed_factor(speed: Any) -> int:
  try:
    s = float(speed)
  except Exception:
    s = 1.0
  return max(1, int(round(s)))

def is_new_anim_format(anim_cfg: Any) -> bool:
  return isinstance(anim_cfg, dict) and "source" in anim_cfg and "on_end_mapping" in anim_cfg

def new_animation_from_legacy(anim_name: str,
                              legacy: Dict[str, Any],
                              asset_name: str,
                              info_dir: Path,
                              default_mapping_id: Optional[str] = None) -> Tuple[Dict[str, Any], str, Dict[str, List[Dict[str, Any]]]]:
  frames_path = legacy.get("frames_path", anim_name) or anim_name
  on_end = legacy.get("on_end", anim_name) or anim_name
  locked = legacy.get("lock_until_done", legacy.get("locked", False))

  # Resolve and count frames (uses absolute/real path for counting only)
  frames_dir = guess_frames_dir(asset_name, frames_path, info_dir)
  n_frames = count_images_in_dir(frames_dir) or 1

  # Persist a RELATIVE path string
  rel_path_str = to_relative_string(frames_path, info_dir)

  new_anim: Dict[str, Any] = {
    "source": {
      "kind": "folder",
      "path": rel_path_str,
      "name": None
    },
    "flipped_source": False,
    "reverse_source": False,
    "locked": bool(locked),
    "speed_factor": to_speed_factor(legacy.get("speed", 1.0)),
    "number_of_frames": int(n_frames),
    "movement": make_default_movement(n_frames),
    "on_end_mapping": ""  # filled below
  }

  if "area_name" in legacy:
    new_anim["area_name"] = legacy["area_name"]

  mapping_id = default_mapping_id or uuid.uuid4().hex[:8]
  new_anim["on_end_mapping"] = mapping_id

  mapping_entries = {
    mapping_id: [
      {
        "condition": "",
        "map_to": {
          "options": [
            { "animation": on_end, "percent": 100 }
          ]
        }
      }
    ]
  }
  return new_anim, mapping_id, mapping_entries

def backup_path(info_path: Path) -> Path:
  # Single fixed backup file next to info.json
  return info_path.with_name("info.json.bak")

def migrate_file(info_path: Path, share_one_mapping: bool = False, dry_run: bool = False) -> bool:
  try:
    data = load_json(info_path)
  except Exception as e:
    print(f"[skip] {info_path}: parse error: {e}")
    return False

  anims = data.get("animations", {})
  # quick no-op if already migrated
  if isinstance(anims, dict) and anims and all(is_new_anim_format(v) for v in anims.values()):
    print(f"[ok]  {info_path}: already in new format")
    return False

  original_serialized = json.dumps(data, ensure_ascii=False, sort_keys=True)
  asset_name = data.get("asset_name", "") or ""
  info_dir = info_path.parent

  # remove deprecated
  data.pop("available_animations", None)

  new_anims: Dict[str, Any] = {}
  new_maps: Dict[str, List[Dict[str, Any]]] = {}
  shared_id = uuid.uuid4().hex[:8] if share_one_mapping else None

  if isinstance(anims, dict):
    for anim_name, legacy_cfg in anims.items():
      if not isinstance(legacy_cfg, dict):
        continue
      na, mid, mentries = new_animation_from_legacy(
        anim_name, legacy_cfg, asset_name, info_dir, default_mapping_id=shared_id
      )
      new_anims[anim_name] = na
      for k, v in mentries.items():
        new_maps.setdefault(k, [])
        new_maps[k].extend(v)

  # write back animations + merge mappings
  data["animations"] = new_anims
  if "mappings" not in data or not isinstance(data["mappings"], dict):
    data["mappings"] = new_maps
  else:
    for k, v in new_maps.items():
      data["mappings"].setdefault(k, v)

  updated_serialized = json.dumps(data, ensure_ascii=False, sort_keys=True)
  if updated_serialized == original_serialized:
    print(f"[ok]  {info_path}: no changes needed")
    return False

  if dry_run:
    print(f"[dry] {info_path}: would BACKUP (info.json.bak) then MIGRATE (overwrite in place)")
    return True

  # Create/overwrite single backup
  bak = backup_path(info_path)
  try:
    shutil.copy2(info_path, bak)
    print(f"[backup] {info_path.name} -> {bak.name}")
  except Exception as e:
    print(f"[warn] {info_path}: backup failed: {e}")

  # Overwrite original
  try:
    save_json(info_path, data)
    print(f"[done]  {info_path}: migrated and overwritten in place")
    return True
  except Exception as e:
    print(f"[fail] {info_path}: write failed: {e}")
    return False

def restore_file(info_path: Path, dry_run: bool = False) -> bool:
  bak = backup_path(info_path)
  if not bak.exists():
    print(f"[skip] {info_path}: no backup found ({bak.name})")
    return False

  if dry_run:
    print(f"[dry] {info_path}: would restore from {bak.name}")
    return True

  try:
    shutil.copy2(bak, info_path)
    print(f"[restored] {info_path}: restored from {bak.name}")
    return True
  except Exception as e:
    print(f"[fail] {info_path}: restore failed: {e}")
    return False

def rglob_info_json(root: Path):
  for p in root.rglob("info.json"):
    yield p

def main() -> int:
  ap = argparse.ArgumentParser(description="Migrate legacy animations to new format (in-place) with single .bak backup, or restore.")
  ap.add_argument("--root", type=str, required=True, help="Root directory to scan recursively")
  ap.add_argument("--dry-run", action="store_true", help="Scan and report without writing changes")
  ap.add_argument("--share-one-mapping", action="store_true", help="Make all animations reference the same new mapping id")
  ap.add_argument("--restore", "-restore", action="store_true", help="Restore each info.json from info.json.bak")
  args = ap.parse_args()

  root = Path(args.root).expanduser().resolve()
  if not root.exists():
    print(f"[error] root does not exist: {root}")
    return 2

  if args.restore:
    restored = 0
    total = 0
    for p in rglob_info_json(root):
      total += 1
      if restore_file(p, dry_run=args.dry_run):
        restored += 1
    mode = "dry-run restore" if args.dry_run else "restore"
    print(f"[summary] {mode}: restored {restored} / {total} files")
    return 0

  changed = 0
  total = 0
  for p in rglob_info_json(root):
    total += 1
    if migrate_file(p, share_one_mapping=args.share_one_mapping, dry_run=args.dry_run):
      changed += 1

  mode = "dry-run" if args.dry_run else "migrated (in-place)"
  print(f"[summary] {mode}: changed {changed} / {total} files")
  return 0

if __name__ == "__main__":
  raise SystemExit(main())
