#!/usr/bin/env python3
import os
import sys
from pathlib import Path
from tempfile import NamedTemporaryFile

try:
    from PIL import Image, ImageOps, UnidentifiedImageError
except Exception as e:
    print("Missing dependency: Pillow. Install with `pip install pillow`.")
    sys.exit(1)

# Image file extensions to consider
EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp", ".tif", ".tiff", ".gif"}

def half_scale_image(path: Path) -> bool:
    """Resize a single image to 50% size in each dimension and overwrite atomically.
       Returns True if changed, False if skipped."""
    try:
        with Image.open(path) as im:
            # Skip animated images to avoid frame handling here
            if getattr(im, "is_animated", False):
                return False

            # Normalize orientation from EXIF
            im = ImageOps.exif_transpose(im)

            w, h = im.size
            new_w = max(1, w // 2)
            new_h = max(1, h // 2)

            # No change if already at minimum
            if new_w == w and new_h == h:
                return False

            resized = im.resize((new_w, new_h), resample=Image.LANCZOS)

            # Preserve EXIF for JPEG if present
            exif_bytes = im.info.get("exif", None)

            save_params = {}
            ext = path.suffix.lower()
            if ext in {".jpg", ".jpeg"}:
                save_params.update({"quality": 85, "optimize": True, "progressive": True})
                if exif_bytes:
                    save_params["exif"] = exif_bytes
            elif ext in {".png"}:
                save_params.update({"optimize": True})
            # Other formats use defaults

            # Write to a temp file in the same directory, then replace
            with NamedTemporaryFile(dir=str(path.parent), suffix=path.suffix, delete=False) as tmp:
                temp_path = Path(tmp.name)
            try:
                # Let Pillow infer format from extension
                resized.save(temp_path, **save_params)
                os.replace(temp_path, path)
            finally:
                # If something failed before replace, clean up temp
                if temp_path.exists():
                    try:
                        temp_path.unlink()
                    except Exception:
                        pass

            return True

    except UnidentifiedImageError:
        return False
    except Exception as e:
        print(f"ERROR processing {path}: {e}")
        return False

def main():
    # Start in the directory where this script lives, not the call site
    script_dir = Path(__file__).resolve().parent
    os.chdir(script_dir)

    processed = 0
    skipped = 0

    for p in script_dir.rglob("*"):
        if not p.is_file():
            continue
        if p.suffix.lower() not in EXTS:
            continue
        changed = half_scale_image(p)
        if changed:
            processed += 1
            print(f"Scaled: {p}")
        else:
            skipped += 1

    print(f"Done. Scaled {processed} file(s). Skipped {skipped} file(s).")

if __name__ == "__main__":
    main()
