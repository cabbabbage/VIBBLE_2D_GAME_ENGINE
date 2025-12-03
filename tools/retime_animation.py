#!/usr/bin/env python3
"""
Retimes an animation by duplicating or dropping source frames.

Usage:
    python tools/retime_animation.py <asset_name> <animation_name> --mode {double,half}

Arguments:
    asset_name      Name of the asset (folder inside SRC/assets).
    animation_name  Name of the animation (folder inside the asset directory).
    --mode          'double' to play twice as fast (drop every other frame),
                    'half' to play half as fast (duplicate each frame).
"""

import argparse
import json
import shutil
import sys
import tempfile
from pathlib import Path
from typing import List, Tuple

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Retimes animation frames in-place.")
    parser.add_argument("asset_name", help="Asset folder name inside SRC/assets")
    parser.add_argument("animation_name", help="Animation folder name inside the asset")
    parser.add_argument(
        "--mode",
        choices=["double", "half"],
        required=True,
        help="Speed change to apply. 'double' drops frames, 'half' duplicates frames.",
    )
    return parser.parse_args()


def discover_frames(folder: Path) -> List[Tuple[int, Path]]:
    frames: List[Tuple[int, Path]] = []
    for entry in folder.iterdir():
        if entry.suffix.lower() != ".png":
            continue
        try:
            idx = int(entry.stem)
        except ValueError:
            continue
        frames.append((idx, entry))
    frames.sort(key=lambda pair: pair[0])
    return frames


def build_frame_plan(frames: List[Tuple[int, Path]], mode: str) -> List[Path]:
    if mode == "half":
        plan: List[Path] = []
        for _, src in frames:
            plan.append(src)
            plan.append(src)
        return plan

    # mode == "double"
    if len(frames) <= 1:
        return [path for _, path in frames]

    selected: List[Path] = [path for i, (_, path) in enumerate(frames) if i % 2 == 0]
    last_path = frames[-1][1]
    if last_path not in selected:
        selected.append(last_path)
    return selected


def _avg(a, b):
    nums = [v for v in (a, b) if isinstance(v, (int, float))]
    if not nums:
        return None
    return int(round(sum(nums) / len(nums)))


def _blend_generic(prev, nxt):
    if isinstance(prev, dict) and isinstance(nxt, dict):
        keys = set(prev.keys()) | set(nxt.keys())
        return {k: _blend_generic(prev.get(k), nxt.get(k)) for k in keys}
    if isinstance(prev, list) and isinstance(nxt, list):
        size = max(len(prev), len(nxt))
        out = []
        for i in range(size):
            a = prev[i] if i < len(prev) else None
            b = nxt[i] if i < len(nxt) else None
            out.append(_blend_generic(a, b))
        return out
    if isinstance(prev, (int, float)) or isinstance(nxt, (int, float)):
        return _avg(prev, nxt)
    return prev if prev is not None else nxt


def _child_from_entry(entry):
    if entry is None:
        return None, None
    if isinstance(entry, list) and entry:
        try:
            idx = int(entry[0])
        except Exception:
            idx = None
        dx = entry[1] if len(entry) > 1 and isinstance(entry[1], (int, float)) else 0
        dy = entry[2] if len(entry) > 2 and isinstance(entry[2], (int, float)) else 0
        deg = entry[3] if len(entry) > 3 and isinstance(entry[3], (int, float)) else 0.0
        visible = entry[4] if len(entry) > 4 else True
        front = entry[5] if len(entry) > 5 else True
        return idx, {"type": "list", "dx": dx, "dy": dy, "deg": deg, "visible": visible, "front": front}
    if isinstance(entry, dict):
        idx = entry.get("child_index", entry.get("index"))
        dx = entry.get("dx", 0)
        dy = entry.get("dy", 0)
        deg = entry.get("degree", entry.get("rotation", 0.0))
        visible = entry.get("visible", True)
        front = entry.get("render_in_front", True)
        return idx, {"type": "dict", "dx": dx, "dy": dy, "deg": deg, "visible": visible, "front": front, "raw": dict(entry)}
    return None, None


def _build_child_entry(idx, data, template_type):
    if template_type == "dict":
        base = data.get("raw", {})
        base["child_index"] = idx
        base["dx"] = data["dx"]
        base["dy"] = data["dy"]
        base["degree"] = data["deg"]
        base["visible"] = data["visible"]
        base["render_in_front"] = data["front"]
        return base
    # default to list representation
    return [idx, data["dx"], data["dy"], data["deg"], data["visible"], data["front"]]


def interpolate_children(prev_children, next_children):
    prev_map = {}
    next_map = {}
    if isinstance(prev_children, list):
        for entry in prev_children:
            idx, parsed = _child_from_entry(entry)
            if idx is not None and parsed:
                prev_map[idx] = parsed
    if isinstance(next_children, list):
        for entry in next_children:
            idx, parsed = _child_from_entry(entry)
            if idx is not None and parsed:
                next_map[idx] = parsed

    all_indices = sorted(set(prev_map.keys()) | set(next_map.keys()))
    result = []
    for idx in all_indices:
        p = prev_map.get(idx)
        n = next_map.get(idx)
        template_type = p["type"] if p else n["type"]
        dx = _avg(p["dx"] if p else None, n["dx"] if n else None) or 0
        dy = _avg(p["dy"] if p else None, n["dy"] if n else None) or 0
        deg = _avg(p["deg"] if p else None, n["deg"] if n else None) or 0.0
        visible = p["visible"] if p is not None else (n["visible"] if n is not None else True)
        front = p["front"] if p is not None else (n["front"] if n is not None else True)
        result.append(_build_child_entry(idx, {"dx": dx, "dy": dy, "deg": deg, "visible": visible, "front": front}, template_type))
    return result


def interpolate_movement(prev, nxt):
    # Normalize inputs
    if isinstance(prev, list):
        prev_children = prev[3] if len(prev) >= 4 and isinstance(prev[3], list) else []
        next_children = nxt[3] if isinstance(nxt, list) and len(nxt) >= 4 and isinstance(nxt[3], list) else []
        dx = prev[0] if len(prev) > 0 else 0
        dy = prev[1] if len(prev) > 1 else 0
        ndx = nxt[0] if isinstance(nxt, list) and len(nxt) > 0 else dx
        ndy = nxt[1] if isinstance(nxt, list) and len(nxt) > 1 else dy
        new_dx = _avg(dx, ndx) or 0
        new_dy = _avg(dy, ndy) or 0
        resort = prev[2] if len(prev) > 2 else False
        children = interpolate_children(prev_children, next_children)
        entry = [new_dx, new_dy, resort]
        entry.append(children)
        return entry
    if isinstance(prev, dict):
        dx = prev.get("dx", 0)
        dy = prev.get("dy", 0)
        ndx = nxt.get("dx", dx) if isinstance(nxt, dict) else dx
        ndy = nxt.get("dy", dy) if isinstance(nxt, dict) else dy
        children = interpolate_children(prev.get("children", []), nxt.get("children", []) if isinstance(nxt, dict) else [])
        blended = dict(prev)
        blended["dx"] = _avg(dx, ndx) or 0
        blended["dy"] = _avg(dy, ndy) or 0
        blended["children"] = children
        return blended
    # Fallback: return previous
    return prev


def retime_manifest(manifest_path: Path, asset_name: str, anim_name: str, mode: str, new_frame_count: int) -> None:
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except Exception as exc:
        print(f"[retime_animation] Failed to load manifest: {exc}", file=sys.stderr)
        return

    assets = manifest.get("assets")
    if not isinstance(assets, dict):
        return
    asset = assets.get(asset_name)
    if not isinstance(asset, dict):
        return
    animations = asset.get("animations")
    if not isinstance(animations, dict):
        return
    anim = animations.get(anim_name)
    if not isinstance(anim, dict):
        return

    movement = anim.get("movement", [])
    hit = anim.get("hit_geometry", [])
    attack = anim.get("attack_geometry", [])

    def select_indices(count: int) -> List[int]:
        keep = [i for i in range(count) if i % 2 == 0]
        if keep and keep[-1] != count - 1:
            keep.append(count - 1)
        elif not keep and count > 0:
            keep.append(count - 1)
        return keep

    def retime_list(entries, interp_func):
        if not isinstance(entries, list):
            entries = []
        if mode == "double":
            idxs = select_indices(len(entries))
            return [entries[i] for i in idxs]
        # half speed: double frames
        if not entries:
            return []
        out = []
        for i, entry in enumerate(entries):
            out.append(entry)
            if i < len(entries) - 1:
                out.append(interp_func(entry, entries[i + 1]))
            else:
                out.append(entry)
        return out

    new_movement = retime_list(movement, interpolate_movement)
    new_hit = retime_list(hit, _blend_generic)
    new_attack = retime_list(attack, _blend_generic)

    # Ensure lengths match frame count when possible
    def pad_or_trim(arr):
        if not isinstance(arr, list):
            arr = []
        if len(arr) > new_frame_count:
            return arr[:new_frame_count]
        if len(arr) < new_frame_count:
            filler = arr[-1] if arr else {}
            arr = arr + [filler] * (new_frame_count - len(arr))
        return arr

    new_movement = pad_or_trim(new_movement)
    new_hit = pad_or_trim(new_hit)
    new_attack = pad_or_trim(new_attack)

    anim["movement"] = new_movement
    anim["hit_geometry"] = new_hit
    anim["attack_geometry"] = new_attack
    anim["number_of_frames"] = new_frame_count
    total_dx = sum((entry[0] if isinstance(entry, list) and entry else entry.get("dx", 0) if isinstance(entry, dict) else 0) for entry in new_movement)
    total_dy = sum((entry[1] if isinstance(entry, list) and len(entry) > 1 else entry.get("dy", 0) if isinstance(entry, dict) else 0) for entry in new_movement)
    anim["movement_total"] = {"dx": int(total_dx), "dy": int(total_dy)}

    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")


def replace_frames(target_dir: Path, sources: List[Path]) -> None:
    temp_dir = Path(tempfile.mkdtemp(prefix="retime_", dir=target_dir.parent))
    try:
        # Copy into a temporary directory with normalized numbering
        for idx, src in enumerate(sources):
            dest = temp_dir / f"{idx}.png"
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dest)

        # Remove old numbered frames
        for entry in target_dir.glob("*.png"):
            try:
                int(entry.stem)
            except ValueError:
                continue
            entry.unlink(missing_ok=True)

        # Move new frames into place
        for entry in sorted(temp_dir.glob("*.png"), key=lambda p: int(p.stem)):
            shutil.move(str(entry), target_dir / entry.name)
    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)


def main() -> int:
    args = parse_args()
    asset_dir = PROJECT_ROOT / "SRC" / "assets" / args.asset_name
    anim_dir = asset_dir / args.animation_name

    if not anim_dir.exists():
        print(f"[retime_animation] Animation folder not found: {anim_dir}", file=sys.stderr)
        return 1

    frames = discover_frames(anim_dir)
    if not frames:
        print(f"[retime_animation] No numbered PNG frames found in {anim_dir}", file=sys.stderr)
        return 1

    new_order = build_frame_plan(frames, args.mode)
    new_frame_count = len(new_order)
    replace_frames(anim_dir, new_order)

    manifest_path = PROJECT_ROOT / "manifest.json"
    retime_manifest(manifest_path, args.asset_name, args.animation_name, args.mode, new_frame_count)

    print(
        f"[retime_animation] Updated '{args.asset_name}/{args.animation_name}' "
        f"({len(frames)} -> {len(new_order)} frames) using mode '{args.mode}'."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
