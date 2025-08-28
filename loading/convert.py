import os
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageEnhance, ImageOps

BORDER_CROP = 2  # pixels to crop from each edge

# ==== CONFIG (edit these) ====
CONFIG = {
    "radius": 0,              # 0 = auto (~10% of min dimension), or set explicit px
    "palette_colors": 10,     # number of colors for cartoon look
    "smooth_passes": 2,       # increase for extra smooth/flat areas
    "blur_radius": 0.8,       # per-pass Gaussian blur radius
    "contrast": 1.1,          # 1.0 = no change
    "brightness": 1.2,        # ~+20%
    "saturation": 1.1,        # ~+10%
    "outline_strength": 0.35, # 0..1 (lower = subtler)
    "outline_threshold": 40,  # higher keeps only major edges
    "outline_soften": 1.0     # blur the edge mask a bit to avoid jaggies
}
# ============================

def rounded_mask(size, radius):
    """Return a mask with rounded corners."""
    w, h = size
    mask = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(mask)
    draw.rounded_rectangle((0, 0, w, h), radius=radius, fill=255)
    return mask

def smooth_image(rgb, passes=2, blur_radius=0.8):
    """Gentle smoothing: blur + mode filter to flatten colors."""
    for _ in range(max(1, passes)):
        rgb = rgb.filter(ImageFilter.GaussianBlur(blur_radius))
        rgb = rgb.filter(ImageFilter.ModeFilter(size=3))
    return rgb

def quantize_palette(rgb, colors=10):
    """Reduce to a small palette with no dithering for cartoon look."""
    colors = max(2, int(colors))
    q = rgb.quantize(colors=colors, method=Image.MEDIANCUT, dither=Image.Dither.NONE)
    return q.convert("RGB")

def ink_outline(rgb, strength=0.35, threshold=40, soften=1.0):
    """Subtle dark outline from edges."""
    edges = rgb.filter(ImageFilter.FIND_EDGES).convert("L")
    inv = ImageOps.invert(edges)
    inv = ImageEnhance.Contrast(inv).enhance(1.3)
    mask = inv.point(lambda p: 255 if p > threshold else 0).convert("L")
    if soften > 0:
        mask = mask.filter(ImageFilter.GaussianBlur(soften))
    if strength <= 0:
        return rgb
    if strength < 1.0:
        mask = mask.point(lambda p: int(p * strength))
    black = Image.new("RGB", rgb.size, (0, 0, 0))
    outlined = rgb.copy()
    outlined.paste(black, (0, 0), mask)
    return outlined

def cartoonize(rgb,
               palette_colors=10,
               smooth_passes=2,
               blur_radius=0.8,
               contrast=1.1,
               brightness=1.2,
               saturation=1.1,
               outline_strength=0.35,
               outline_threshold=40,
               outline_soften=1.0):
    """Full cartoon pipeline with smoothing, color reduction, and outline."""
    # 1) Smooth
    rgb = smooth_image(rgb, passes=smooth_passes, blur_radius=blur_radius)

    # 2) Brightness / Contrast / Saturation tweaks
    if brightness and brightness != 1.0:
        rgb = ImageEnhance.Brightness(rgb).enhance(brightness)
    if contrast and contrast != 1.0:
        rgb = ImageEnhance.Contrast(rgb).enhance(contrast)
    if saturation and saturation != 1.0:
        rgb = ImageEnhance.Color(rgb).enhance(saturation)

    # 3) Reduce colors
    rgb = quantize_palette(rgb, colors=palette_colors)

    # 4) Add outline
    rgb = ink_outline(rgb,
                      strength=outline_strength,
                      threshold=outline_threshold,
                      soften=outline_soften)
    return rgb

def process_image(jpg_path: Path,
                  radius: int,
                  palette_colors: int,
                  smooth_passes: int,
                  blur_radius: float,
                  contrast: float,
                  brightness: float,
                  saturation: float,
                  outline_strength: float,
                  outline_threshold: int,
                  outline_soften: float):
    png_path = jpg_path.with_suffix(".png")

    try:
        with Image.open(jpg_path) as img:
            img = img.convert("RGBA")
            w, h = img.size
            if w <= BORDER_CROP * 2 or h <= BORDER_CROP * 2:
                print(f"Too small to crop 2px border, skipping: {jpg_path}")
                return

            # Crop 2px border
            img = img.crop((
                BORDER_CROP,
                BORDER_CROP,
                w - BORDER_CROP,
                h - BORDER_CROP
            ))
            cw, ch = img.size

            radius_px = max(4, min(cw, ch) // 10) if radius <= 0 else radius

            # Stylize on RGB
            rgb = img.convert("RGB")
            rgb = cartoonize(
                rgb,
                palette_colors=palette_colors,
                smooth_passes=smooth_passes,
                blur_radius=blur_radius,
                contrast=contrast,
                brightness=brightness,
                saturation=saturation,
                outline_strength=outline_strength,
                outline_threshold=outline_threshold,
                outline_soften=outline_soften
            )

            # Reapply rounded alpha
            alpha = rounded_mask((cw, ch), radius_px)
            out = rgb.convert("RGBA")
            out.putalpha(alpha)

            # Overwrite PNG; optimize & low compression
            out.save(png_path, "PNG", optimize=True, compress_level=1)
            print(f"Converted (cartoonized): {jpg_path} -> {png_path}")

    except Exception as e:
        print(f"Failed to convert {jpg_path}: {e}")

def convert_folder(root_dir: Path, **kwargs):
    for path, _, files in os.walk(root_dir):
        for file in files:
            if file.lower().endswith(".jpg"):
                process_image(Path(path) / file, **kwargs)

if __name__ == "__main__":
    # Start from the folder where the script is located
    script_dir = Path(__file__).parent
    convert_folder(script_dir, **CONFIG)
