# frame_cropper.py
from __future__ import annotations
import os
from typing import List, Tuple, Iterable, Dict, Any, Optional

try:
    from PIL import Image  # type: ignore
except Exception:
    Image = None

# ---------- file helpers ----------
def is_numbered_png(filename: str) -> bool:
    return filename.lower().endswith(".png") and filename[:-4].isdigit()

def get_image_paths(folder: str) -> List[str]:
    files = os.listdir(folder)
    numbered_pngs = [f for f in files if is_numbered_png(f)]
    return sorted(
        [os.path.join(folder, f) for f in numbered_pngs],
        key=lambda x: int(os.path.basename(x)[:-4]),
    )

# ---------- bounds & crop ----------
def compute_union_bounds(image_paths: Iterable[str], alpha_threshold: int = 0) -> Tuple[int, int, int, int, int, int]:
    """
    Returns (top, bottom, left, right, base_w, base_h) as margins,
    computed from the union bbox across all given images.
    If no non-transparent pixels are found, returns zeros.
    """
    if Image is None:
        return (0, 0, 0, 0, 0, 0)

    union_bbox = None
    base_w = base_h = 0

    for path in image_paths:
        with Image.open(path).convert("RGBA") as img:
            if base_w == 0:
                base_w, base_h = img.size
            a = img.split()[3]
            # treat alpha <= threshold as transparent
            if alpha_threshold > 0:
                mask = a.point(lambda v: 255 if v > alpha_threshold else 0, mode="L")
            else:
                mask = a.point(lambda v: 255 if v != 0 else 0, mode="L")

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

def crop_images_with_bounds(image_paths: Iterable[str], crop_top: int, crop_bottom: int, crop_left: int, crop_right: int) -> int:
    """Crops each image in-place using the fixed margins. Returns count cropped."""
    if Image is None:
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
