#!/usr/bin/env python3

import os
import numpy as np
from PIL import Image, ImageFilter

# Output size
WIDTH = 1920
HEIGHT = 200

OUTPUT_FOLDER = "."

# Global opacity factors for the three textures (1.0 = 100 percent)
GLOBAL_OPACITIES = [1.0, 0.5, 0.25]


def generate_fog_alpha(width: int, height: int) -> np.ndarray:
    """
    Generate a detailed fog alpha mask using layered noise and blurring.
    Returns a 2D numpy array with values in [0, 255].
    Each call creates a new random field.
    """
    rng = np.random.default_rng()

    # Octave 1: large soft shapes
    noise1 = rng.random((height, width), dtype=np.float32)
    img1 = Image.fromarray((noise1 * 255).astype(np.uint8), mode="L")
    img1 = img1.filter(ImageFilter.GaussianBlur(radius=16))

    # Octave 2: medium detail
    noise2 = rng.random((height, width), dtype=np.float32)
    img2 = Image.fromarray((noise2 * 255).astype(np.uint8), mode="L")
    img2 = img2.filter(ImageFilter.GaussianBlur(radius=6))

    # Octave 3: fine wisps
    noise3 = rng.random((height, width), dtype=np.float32)
    img3 = Image.fromarray((noise3 * 255).astype(np.uint8), mode="L")
    img3 = img3.filter(ImageFilter.GaussianBlur(radius=2))

    # Combine octaves
    a1 = np.array(img1, dtype=np.float32)
    a2 = np.array(img2, dtype=np.float32)
    a3 = np.array(img3, dtype=np.float32)

    alpha = 0.5 * a1 + 0.35 * a2 + 0.15 * a3

    # Normalize to 0..255
    alpha -= alpha.min()
    if alpha.max() > 0:
        alpha /= alpha.max()
    alpha *= 255.0

    # Fog should sit on the bottom:
    # y = 0 at top, 1 at bottom
    y = np.linspace(0.0, 1.0, height, dtype=np.float32).reshape((height, 1))
    # 0 at top, 1 at bottom with steeper increase near bottom
    gradient = y ** 2.5
    alpha *= gradient

    return alpha.clip(0, 255).astype(np.uint8)


def build_fog_texture(width: int, height: int, global_opacity: float, index: int) -> None:
    """
    Create a fog texture with a transparent background and save as fog_X.png.
    First builds a fog alpha mask, then applies a global opacity factor.
    """
    alpha = generate_fog_alpha(width, height)

    # Apply global opacity factor
    global_opacity = max(0.0, min(1.0, float(global_opacity)))
    alpha = (alpha.astype(np.float32) * global_opacity).clip(0, 255).astype(np.uint8)

    # Fog color (white)
    fog_color = 255
    r = np.full_like(alpha, fog_color, dtype=np.uint8)
    g = np.full_like(alpha, fog_color, dtype=np.uint8)
    b = np.full_like(alpha, fog_color, dtype=np.uint8)

    # RGBA image
    rgba = np.dstack([r, g, b, alpha])
    img = Image.fromarray(rgba, mode="RGBA")

    os.makedirs(OUTPUT_FOLDER, exist_ok=True)
    out_path = os.path.join(OUTPUT_FOLDER, f"fog_{index}.png")
    img.save(out_path, format="PNG")
    print(f"Saved {out_path}")


def main():
    for i, opacity in enumerate(GLOBAL_OPACITIES, start=1):
        build_fog_texture(WIDTH, HEIGHT, opacity, i)


if __name__ == "__main__":
    main()
