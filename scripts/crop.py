#!/usr/bin/env python3
from __future__ import annotations
import os
from pathlib import Path
from typing import Dict, List, Tuple

try:
    from PIL import Image  # type: ignore
except Exception:
    Image = None  # Pillow optional

# Treat alpha <= THRESHOLD as transparent (helps remove faint halos)
ALPHA_THRESHOLD = 2


def is_numbered_png(filename: str) -> bool:
    return filename.lower().endswith(".png") and filename[:-4].isdigit()


def get_image_paths(folder: str) -> List[str]:
    files = os.listdir(folder)
    numbered_pngs = [f for f in files if is_numbered_png(f)]
    return sorted(
        [os.path.join(folder, f) for f in numbered_pngs],
        key=lambda x: int(os.path.basename(x)[:-4]),
    )


def compute_union_bounds(image_paths: List[str]) -> Tuple[int, int, int, int, int, int]:
    """
    Compute a single union bounding box (over all frames) using alpha threshold.
    Returns (top, bottom, left, right, base_w, base_h). If none found → zeros.
    """
    if Image is None or not image_paths:
        return (0, 0, 0, 0, 0, 0)

    union_bbox = None
    base_w = base_h = 0

    for path in image_paths:
        with Image.open(path).convert("RGBA") as img:
            if base_w == 0:
                base_w, base_h = img.size
            a = img.split()[3]
            # Binary mask: > threshold => 255, else 0
            mask = a.point(lambda v: 255 if v > ALPHA_THRESHOLD else 0, mode="L")
            bbox = mask.getbbox()  # (L, T, R, B) or None
            if bbox is None:
                continue
            union_bbox = bbox if union_bbox is None else (
                min(union_bbox[0], bbox[0]),
                min(union_bbox[1], bbox[1]),
                max(union_bbox[2], bbox[2]),
                max(union_bbox[3], bbox[3]),
            )

    if union_bbox is None or base_w == 0:
        return (0, 0, 0, 0, 0, 0)

    L, T, R, B = union_bbox
    crop_left   = L
    crop_top    = T
    crop_right  = max(0, base_w - R)
    crop_bottom = max(0, base_h - B)
    return (crop_top, crop_bottom, crop_left, crop_right, base_w, base_h)


def crop_images_with_bounds(image_paths: List[str], crop_top: int, crop_bottom: int, crop_left: int, crop_right: int) -> int:
    """Crop each image in-place using fixed margins. Returns count cropped."""
    if Image is None or not image_paths:
        return 0

    count = 0
    for path in image_paths:
        with Image.open(path).convert("RGBA") as img:
            w, h = img.size
            L = crop_left
            T = crop_top
            R = w - crop_right
            B = h - crop_bottom
            if L >= R or T >= B:
                continue
            img.crop((L, T, R, B)).save(path, format="PNG", optimize=True)
            count += 1
    return count


def collect_all_numbered_pngs(root: Path) -> Dict[Path, List[str]]:
    """
    Walk 'root' and gather numbered PNG frames per directory.
    Returns {folder_path: [image_paths...]} only for folders with numbered PNGs.
    """
    images_by_folder: Dict[Path, List[str]] = {}
    for dirpath, _dirs, files in os.walk(root):
        if any(is_numbered_png(f) for f in files):
            p = Path(dirpath)
            try:
                images_by_folder[p] = get_image_paths(str(p))
            except Exception:
                images_by_folder[p] = []
    return images_by_folder


def main() -> int:
    # Start at directory containing this script (like your original)
    start_dir = Path(os.path.dirname(os.path.abspath(__file__))).resolve()
    print(f"Starting GLOBAL crop (multi-folder) from: {start_dir}")

    images_by_folder = collect_all_numbered_pngs(start_dir)

    # Flatten all paths to compute a single global bound
    all_image_paths: List[str] = []
    for lst in images_by_folder.values():
        all_image_paths.extend(lst)

    if not all_image_paths:
        print("No numbered PNG frames found. Nothing to do.")
        return 0

    # Compute one global union across ALL frames in ALL folders
    t, b, l, r, _w, _h = compute_union_bounds(all_image_paths)
    if t == b == l == r == 0:
        print("Global bounds empty (no non-transparent pixels found). Nothing to crop.")
        return 0

    print(f"Global crop margins -> T:{t} B:{b} L:{l} R:{r}")

    # Apply same crop to every folder’s frames
    total = 0
    for folder, paths in images_by_folder.items():
        if not paths:
            print(f"{folder}: 0 frames")
            continue
        n = crop_images_with_bounds(paths, t, b, l, r)
        total += n
        print(f"{folder}: cropped {n} frames")

    print(f"\nCropping complete. Total frames cropped: {total}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
