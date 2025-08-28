import os
import argparse
from pathlib import Path
from PIL import Image, ImageDraw

BORDER_CROP = 2  # pixels to crop from each edge

def chamfer_mask(size, radius):
    """Return an RGBA mask with chamfered (beveled) corners."""
    w, h = size
    mask = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(mask)

    # Octagon (rectangle with chamfered corners)
    r = max(1, min(radius, min(w, h)//2))
    polygon = [
        (r, 0), (w - r, 0),
        (w, r), (w, h - r),
        (w - r, h), (r, h),
        (0, h - r), (0, r)
    ]
    draw.polygon(polygon, fill=255)
    return mask

def process_image(jpg_path: Path, radius: int, overwrite: bool):
    png_path = jpg_path.with_suffix(".png")
    if png_path.exists() and not overwrite:
        print(f"Skip (exists): {png_path}")
        return

    try:
        with Image.open(jpg_path) as img:
            img = img.convert("RGBA")
            w, h = img.size

            # Ensure we can crop 2 px on each side
            if w <= BORDER_CROP * 2 or h <= BORDER_CROP * 2:
                print(f"Too small to crop 2px border, skipping: {jpg_path}")
                return

            # Crop 2px border all around
            img = img.crop((
                BORDER_CROP,
                BORDER_CROP,
                w - BORDER_CROP,
                h - BORDER_CROP
            ))

            # Recompute size after crop
            cw, ch = img.size

            # Auto radius if 0 passed: ~5% of min dimension, at least 4px
            if radius <= 0:
                radius_px = max(4, min(cw, ch) // 20)
            else:
                radius_px = radius

            # Apply beveled (chamfered) corners via alpha mask
            mask = chamfer_mask((cw, ch), radius_px)
            img.putalpha(mask)

            # Save PNG with transparency
            img.save(png_path, "PNG")
            print(f"Converted with crop+bevel: {jpg_path} -> {png_path}")

    except Exception as e:
        print(f"Failed to convert {jpg_path}: {e}")

def convert_folder(root_dir: Path, radius: int, overwrite: bool):
    for path, _, files in os.walk(root_dir):
        for file in files:
            if file.lower().endswith(".jpg"):
                process_image(Path(path) / file, radius, overwrite)

def main():
    parser = argparse.ArgumentParser(
        description="Recursively convert .jpg to .png with 2px border crop and beveled corners (keep original .jpg)."
    )
    parser.add_argument(
        "--radius", type=int, default=0,
        help="Bevel radius in pixels (0 = auto ~5% of min dimension)."
    )
    parser.add_argument(
        "--overwrite", action="store_true",
        help="Overwrite existing .png files if present."
    )
    args = parser.parse_args()

    script_dir = Path(__file__).parent
    convert_folder(script_dir, args.radius, args.overwrite)

if __name__ == "__main__":
    main()
