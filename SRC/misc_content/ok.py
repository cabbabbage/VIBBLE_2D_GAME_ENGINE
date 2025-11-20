#!/usr/bin/env python3
"""
Convert every image in the folder this script lives in to PNG.

- Looks in the script directory.
- Converts common image formats to .png.
- Leaves files that are already .png alone unless you set CONVERT_PNG_TOO=True.
"""

from pathlib import Path

try:
    from PIL import Image
except ImportError:
    raise SystemExit("Pillow not installed. Run: pip install pillow")

# Set to True if you want to re-save existing PNGs too
CONVERT_PNG_TOO = False

# Extensions to treat as images (lowercase)
IMAGE_EXTS = {
    ".jpg", ".jpeg", ".bmp", ".gif", ".tif", ".tiff", ".webp", ".heic", ".heif"
}

def main():
    script_dir = Path(__file__).resolve().parent
    files = [p for p in script_dir.iterdir() if p.is_file()]

    converted = 0
    skipped = 0
    failed = 0

    for p in files:
        ext = p.suffix.lower()

        if ext == ".png" and not CONVERT_PNG_TOO:
            skipped += 1
            continue

        if ext not in IMAGE_EXTS and ext != ".png":
            continue

        out_path = p.with_suffix(".png")

        try:
            with Image.open(p) as img:
                # Ensure something sane for PNG saving
                if img.mode in ("P", "LA"):
                    img = img.convert("RGBA")
                elif img.mode != "RGBA":
                    img = img.convert("RGB")

                img.save(out_path, format="PNG")
            converted += 1
            print(f"Converted: {p.name} -> {out_path.name}")

        except Exception as e:
            failed += 1
            print(f"Failed: {p.name} ({e})")

    print()
    print(f"Done. Converted {converted}, skipped {skipped}, failed {failed}.")

if __name__ == "__main__":
    main()
