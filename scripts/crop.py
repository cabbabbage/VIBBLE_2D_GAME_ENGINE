#!/usr/bin/env python3
from __future__ import annotations
import os
from typing import List, Tuple

try:
    from PIL import Image, ImageSequence  # type: ignore
except Exception:
    Image = None  # Pillow optional
    ImageSequence = None


def is_numbered_png(filename: str) -> bool:
    return filename.lower().endswith('.png') and filename[:-4].isdigit()


def get_image_paths(folder: str) -> List[str]:
    files = os.listdir(folder)
    numbered_pngs = [f for f in files if is_numbered_png(f)]
    return sorted(
        [os.path.join(folder, f) for f in numbered_pngs],
        key=lambda x: int(os.path.basename(x)[:-4])
    )


def get_crop_bounds(image_paths: List[str]) -> Tuple[int, int, int, int]:
    """Compute crop bounds that preserve all non‑transparent pixels across all images."""
    if Image is None or not image_paths:
        return (0, 0, 0, 0)
    images = []
    for path in image_paths:
        img = Image.open(path).convert("RGBA")
        images.append(img)

    width, height = images[0].size

    top_crop = 0
    bottom_crop = 0
    left_crop = 0
    right_crop = 0

    # top
    for y in range(height):
        if all(all(img.getpixel((x, y))[3] == 0 for x in range(width)) for img in images):
            top_crop += 1
        else:
            break

    # bottom
    for y in range(height - 1, -1, -1):
        if all(all(img.getpixel((x, y))[3] == 0 for x in range(width)) for img in images):
            bottom_crop += 1
        else:
            break

    # left
    for x in range(width):
        if all(all(img.getpixel((x, y))[3] == 0 for y in range(height)) for img in images):
            left_crop += 1
        else:
            break

    # right
    for x in range(width - 1, -1, -1):
        if all(all(img.getpixel((x, y))[3] == 0 for y in range(height)) for img in images):
            right_crop += 1
        else:
            break

    for img in images:
        img.close()

    return top_crop, bottom_crop, left_crop, right_crop


def crop_and_save_images(image_paths: List[str], crop_top: int, crop_bottom: int, crop_left: int, crop_right: int) -> int:
    if Image is None or not image_paths:
        return 0
    count = 0
    for path in image_paths:
        with Image.open(path).convert("RGBA") as img:
            width, height = img.size
            left = crop_left
            top = crop_top
            right = width - crop_right
            bottom = height - crop_bottom

            if left >= right or top >= bottom:
                # skip invalid crop
                continue

            cropped = img.crop((left, top, right, bottom))
            cropped.save(path)
            count += 1
    return count


def crop_folder(folder: str, verbose: bool = False) -> Tuple[Tuple[int, int, int, int], int]:
    """
    Crop numbered PNG frames in a folder based on combined transparency bounds.
    Returns ((top, bottom, left, right), cropped_count)
    """
    image_paths = get_image_paths(folder)
    if not image_paths:
        return (0, 0, 0, 0), 0
    bounds = get_crop_bounds(image_paths)
    t, b, l, r = bounds
    if verbose:
        print(f"Crop (T:{t}, B:{b}, L:{l}, R:{r}) for {folder}")
    if t == 0 and b == 0 and l == 0 and r == 0:
        return bounds, 0
    n = crop_and_save_images(image_paths, t, b, l, r)
    return bounds, n


def process_folder(folder: str):
    bounds, n = crop_folder(folder, verbose=True)
    if n == 0:
        print("    No cropping needed.")
    else:
        t, b, l, r = bounds
        print(f"    Cropped {n} frames with (T:{t}, B:{b}, L:{l}, R:{r}).")


def recursive_search(start_dir: str):
    for root, _, files in os.walk(start_dir):
        if any(is_numbered_png(f) for f in files):
            process_folder(root)


if __name__ == "__main__":
    base_dir = os.path.dirname(os.path.abspath(__file__))
    print(f"Starting recursive crop from: {base_dir}")
    recursive_search(base_dir)
    print("\nCropping complete.")

