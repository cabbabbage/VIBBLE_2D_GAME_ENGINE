#!/usr/bin/env python3
"""
VIBBLE 2D Game Engine
Apply color and lighting effects to a single image.

Usage:
  python apply_color_effects.py <img_path> <output_path> <layer_type> <rgb_boost> <contrast> <brightness> <blur> <saturation_red> <saturation_green> <saturation_blue> <hue>

Where:
  <layer_type> is either "foreground" or "background"
"""

from typing import Optional
from effects import Effects

import numpy as np
from PIL import Image, ImageFilter


class ApplyEffects:
    """
    Vectorized effect processor (CPU only, NumPy).

    Parameters are expected to be normalized floats. You can tune ranges
    in your manifest, but a reasonable assumption is:

      brightness       approx [-1.0, 1.0]
      contrast         approx [-1.0, 1.0] where 0.0 means no change
      rgb_boost        approx [-1.0, 1.0] final multiplier on RGB
      blur             in [-1.0, 1.0]
                        > 0: Gaussian blur (1.0 = strong blur)
                        < 0: sharpening (-1.0 = strong sharpen)
      saturation_red   approx [-1.0, 1.0]
      saturation_green approx [-1.0, 1.0]
      saturation_blue  approx [-1.0, 1.0]
      hue              approx [-1.0, 1.0] where 1.0 is a full turn (360 degrees)

    Implementation notes:

    - brightness, contrast, rgb_boost are applied in RGB space on visible pixels.
    - Per channel saturation knobs are treated as per channel boosts. Each
      channel is pushed away from or toward the per pixel gray level.
    - hue rotates H in HSV space for visible pixels only.
    - blur/sharpen is applied at the end via PIL.
    """

    def __init__(self, use_gpu: Optional[bool] = None) -> None:
        """
        Kept for API compatibility. All processing is CPU only now.

        Parameters:
            use_gpu: ignored, present only so existing calls do not break.
        """
        self.use_gpu = False

    @staticmethod
    def _rgb_to_hsv(r: np.ndarray, g: np.ndarray, b: np.ndarray):
        """
        Vectorized RGB to HSV conversion.

        r, g, b in [0, 1].
        Returns h, s, v in [0, 1].
        """
        maxc = np.maximum(np.maximum(r, g), b)
        minc = np.minimum(np.minimum(r, g), b)
        v = maxc
        delta = maxc - minc

        # Avoid division by zero
        s = np.where(maxc > 0.0, delta / np.where(maxc == 0.0, 1.0, maxc), 0.0)

        h = np.zeros_like(maxc)
        non_zero = delta > 1e-6

        # Masks for which channel is max
        r_is_max = non_zero & (maxc == r)
        g_is_max = non_zero & (maxc == g)
        b_is_max = non_zero & (maxc == b)

        # Compute hue in [0, 6)
        if np.any(r_is_max):
            h_r = (g - b) / np.where(delta == 0.0, 1.0, delta)
            h = np.where(r_is_max, (h_r % 6.0), h)

        if np.any(g_is_max):
            h_g = (b - r) / np.where(delta == 0.0, 1.0, delta) + 2.0
            h = np.where(g_is_max, h_g, h)

        if np.any(b_is_max):
            h_b = (r - g) / np.where(delta == 0.0, 1.0, delta) + 4.0
            h = np.where(b_is_max, h_b, h)

        # Normalize to [0, 1]
        h = (h / 6.0) % 1.0

        return h, s, v

    @staticmethod
    def _hsv_to_rgb(h: np.ndarray, s: np.ndarray, v: np.ndarray):
        """
        Vectorized HSV to RGB conversion.

        h, s, v in [0, 1].
        Returns r, g, b in [0, 1].
        """
        h6 = h * 6.0
        c = v * s
        x = c * (1.0 - np.abs((h6 % 2.0) - 1.0))
        m = v - c

        r = np.zeros_like(h)
        g = np.zeros_like(h)
        b = np.zeros_like(h)

        # Ranges for h6
        cond0 = (0.0 <= h6) & (h6 < 1.0)
        cond1 = (1.0 <= h6) & (h6 < 2.0)
        cond2 = (2.0 <= h6) & (h6 < 3.0)
        cond3 = (3.0 <= h6) & (h6 < 4.0)
        cond4 = (4.0 <= h6) & (h6 < 5.0)
        cond5 = (5.0 <= h6) & (h6 < 6.0)

        if np.any(cond0):
            r = np.where(cond0, c, r)
            g = np.where(cond0, x, g)
            b = np.where(cond0, 0.0, b)

        if np.any(cond1):
            r = np.where(cond1, x, r)
            g = np.where(cond1, c, g)
            b = np.where(cond1, 0.0, b)

        if np.any(cond2):
            r = np.where(cond2, 0.0, r)
            g = np.where(cond2, c, g)
            b = np.where(cond2, x, b)

        if np.any(cond3):
            r = np.where(cond3, 0.0, r)
            g = np.where(cond3, x, g)
            b = np.where(cond3, c, b)

        if np.any(cond4):
            r = np.where(cond4, x, r)
            g = np.where(cond4, 0.0, g)
            b = np.where(cond4, c, b)

        if np.any(cond5):
            r = np.where(cond5, c, r)
            g = np.where(cond5, 0.0, g)
            b = np.where(cond5, x, b)

        r = r + m
        g = g + m
        b = b + m

        return r, g, b

    @staticmethod
    def _apply_blur_or_sharpen(img: Image.Image, blur_val: float) -> Image.Image:
        """
        Apply blur or sharpening to img based on blur_val in [-1.0, 1.0].

        > 0: defocus blur, interpreted differently for foreground vs background:
             - foreground: larger, slightly ringy blur (strong foreground bokeh)
             - background: smoother blur with highlight bloom (background bokeh)
        < 0: UnsharpMask sharpening, strength mapped from 0 to 1.
        0:   no change.

        The caller is expected to optionally attach a boolean attribute:
            img.foreground = True  for foreground objects
            img.foreground = False for background objects
        """
        from PIL import ImageChops, ImageEnhance

        # Clamp to [-1, 1]
        try:
            v = float(blur_val)
        except Exception:
            v = 0.0

        v = max(-1.0, min(1.0, v))

        # Tiny values are treated as no-op
        if abs(v) < 1e-3:
            return img

        # Foreground/background flag (defaults to background if missing)
        is_foreground = bool(getattr(img, "foreground", False))

        # Positive: defocus blur (foreground vs background behavior)
        if v > 0.0:
            # Base physical-ish blur radius
            # v in (0,1] -> radius in roughly [0.5, 10]
            max_radius = 10.0
            base_radius = v * max_radius
            if base_radius < 0.5:
                base_radius = 0.5

            if is_foreground:
                # Foreground out of focus:
                # Larger circle of confusion, slightly harder edge / ring-like bokeh.
                radius = base_radius * 1.6

                # Heavy blur first
                blurred = img.filter(ImageFilter.GaussianBlur(radius=radius))

                # Mild unsharp pass on the blurred image itself to
                # tighten the edges of the blur disks (gives a ringy feel).
                ring_radius = max(0.5, radius * 0.5)
                ring_percent = 60
                ring = blurred.filter(
                    ImageFilter.UnsharpMask(
                        radius=ring_radius,
                        percent=ring_percent,
                        threshold=3,
                    )
                )
                return ring

            else:
                # Background out of focus:
                # Slightly smaller radius for the same blur_val and
                # bloom specular highlights for bokeh balls.
                radius = base_radius * 0.9
                blurred = img.filter(ImageFilter.GaussianBlur(radius=radius))

                # Build a luminance-based mask of bright areas
                # to exaggerate highlight bokeh.
                lum = img.convert("L")

                def bright_curve(x):
                    # Push only the very bright pixels into the mask
                    # x>200 gets ramped to 255, below that stays near 0.
                    return 0 if x < 200 else min(255, int((x - 200) * 4))

                mask = lum.point(bright_curve, mode="L")

                # Soften the mask a bit so bloom is not harsh
                mask = mask.filter(ImageFilter.GaussianBlur(radius=max(1.0, radius * 0.5)))

                # Slightly brighten blurred image where mask is strong
                bright_blurred = ImageEnhance.Brightness(blurred).enhance(1.2)

                # Composite: brightened blur where highlights are, normal blur elsewhere
                result = Image.composite(bright_blurred, blurred, mask)
                return result

        # v < 0 -> sharpening using UnsharpMask
        strength = -v  # in (0, 1]
        # Map to radius and percent
        radius = 0.5 + strength * 2.5      # 0.5..3.0
        percent = 50 + strength * 200      # 50..250
        threshold = 0
        return img.filter(
            ImageFilter.UnsharpMask(radius=radius, percent=int(percent), threshold=threshold)
        )

    def apply_effects(
        self,
        image: Image.Image,
        Effects_params: Effects
    ) -> Image.Image:
        """
        Apply all effects to a PIL Image and return a new image.

        Parameters:
            image:                 PIL Image (converted to RGBA if needed)
            brightness:            additive offset in roughly [-1.0, 1.0]
            contrast:              contrast adjustment in [-1.0, 1.0], 0.0 is no change
            rgb_boost:             RGB multiplier offset in [-1.0, 1.0],
                                   0.25 increases channels by 25 percent
            blur:                  in [-1.0, 1.0], >0 blur, <0 sharpen
            saturation_red:        red channel saturation style boost
            saturation_green:      green channel saturation style boost
            saturation_blue:       blue channel saturation style boost
            hue:                   hue rotation fraction in [-1.0, 1.0],
                                   1.0 is full circle (360 degrees)

        Returns:
            New PIL Image with effects applied.
        """
        if not isinstance(image, Image.Image):
            return image

        if image.mode != "RGBA":
            image = image.convert("RGBA")

        # Clamp effect parameters to expected ranges
        def clamp01(v: float) -> float:
            return max(-1.0, min(1.0, float(v)))

        brightness = clamp01(getattr(Effects_params, "brightness", 0.0))
        contrast = clamp01(getattr(Effects_params, "contrast", 0.0))
        rgb_boost = clamp01(getattr(Effects_params, "rgb_boost", 0.0))
        blur = clamp01(getattr(Effects_params, "blur", 0.0))
        sat_r = clamp01(getattr(Effects_params, "saturation_red", 0.0))
        sat_g = clamp01(getattr(Effects_params, "saturation_green", 0.0))
        sat_b = clamp01(getattr(Effects_params, "saturation_blue", 0.0))
        hue = clamp01(getattr(Effects_params, "hue", 0.0))

        # CPU NumPy array
        img_np = np.asarray(image, dtype=np.uint8)

        # Normalize to [0, 1] float32
        img_float = img_np.astype(np.float32) / 255.0

        # Split channels
        r = img_float[:, :, 0]
        g = img_float[:, :, 1]
        b = img_float[:, :, 2]
        a = img_float[:, :, 3]

        alpha_mask = a > 0

        # Fast path: no color / tone changes, only possible blur/sharpen.
        if (
            not np.any(alpha_mask)
            or (
                abs(brightness) < 1e-6
                and abs(contrast) < 1e-6
                and abs(rgb_boost) < 1e-6
                and abs(sat_r) < 1e-6
                and abs(sat_g) < 1e-6
                and abs(sat_b) < 1e-6
                and abs(hue) < 1e-6
            )
        ):
            img_uint8 = (img_float * 255.0).astype(np.uint8)
            result = Image.fromarray(img_uint8, mode="RGBA")
            # Apply blur/sharpen if needed
            result = self._apply_blur_or_sharpen(result, blur)
            return result

        # Visible pixels only
        r_vis = r[alpha_mask]
        g_vis = g[alpha_mask]
        b_vis = b[alpha_mask]

        # 1. brightness (additive)
        if abs(brightness) > 1e-6:
            r_vis = np.clip(r_vis + brightness, 0.0, 1.0)
            g_vis = np.clip(g_vis + brightness, 0.0, 1.0)
            b_vis = np.clip(b_vis + brightness, 0.0, 1.0)

        # 2. contrast
        # contrast in [-1, 1]; c = 1 + contrast so:
        #   contrast = 0   -> c = 1 (no change)
        #   contrast = 1   -> c = 2 (stronger contrast)
        #   contrast = -1  -> c = 0 (everything pulled to mid gray 0.5)
        if abs(contrast) > 1e-6:
            c = 1.0 + contrast
            r_vis = np.clip((r_vis - 0.5) * c + 0.5, 0.0, 1.0)
            g_vis = np.clip((g_vis - 0.5) * c + 0.5, 0.0, 1.0)
            b_vis = np.clip((b_vis - 0.5) * c + 0.5, 0.0, 1.0)

        # 3. RGB boost (final linear multiplier)
        if abs(rgb_boost) > 1e-6:
            m = 1.0 + rgb_boost
            r_vis = np.clip(r_vis * m, 0.0, 1.0)
            g_vis = np.clip(g_vis * m, 0.0, 1.0)
            b_vis = np.clip(b_vis * m, 0.0, 1.0)

        # 4. hue rotation (HSV) on visible pixels
        if abs(hue) > 1e-6:
            # hue is in [-1, 1]; treat it as fraction of a full cycle
            # so 1.0 is full circle, -1.0 is full circle in the other direction.
            h, s, v = self._rgb_to_hsv(r_vis, g_vis, b_vis)
            h = (h + hue) % 1.0
            r_vis, g_vis, b_vis = self._hsv_to_rgb(h, s, v)

        # 5. Per channel saturation style boosts
        if abs(sat_r) > 1e-6 or abs(sat_g) > 1e-6 or abs(sat_b) > 1e-6:
            # Per pixel gray level
            gray = (r_vis + g_vis + b_vis) / 3.0

            # For each channel, push away from or toward gray
            if abs(sat_r) > 1e-6:
                f_r = 1.0 + sat_r
                r_vis = np.clip(gray + (r_vis - gray) * f_r, 0.0, 1.0)

            if abs(sat_g) > 1e-6:
                f_g = 1.0 + sat_g
                g_vis = np.clip(gray + (g_vis - gray) * f_g, 0.0, 1.0)

            if abs(sat_b) > 1e-6:
                f_b = 1.0 + sat_b
                b_vis = np.clip(gray + (b_vis - gray) * f_b, 0.0, 1.0)

        # Write visible pixels back
        r[alpha_mask] = r_vis
        g[alpha_mask] = g_vis
        b[alpha_mask] = b_vis

        # Reassemble RGBA
        img_float = np.stack([r, g, b, a], axis=2)

        # Back to uint8 on CPU
        img_uint8 = np.clip(img_float * 255.0, 0, 255).astype(np.uint8)
        result = Image.fromarray(img_uint8, mode="RGBA")

        # 6. blur/sharpen at the end in PIL
        result = self._apply_blur_or_sharpen(result, blur)

        return result


if __name__ == "__main__":
    import sys

    if len(sys.argv) != 12:
        print(
            "Usage: python apply_color_effects.py "
            "<img_path> <output_path> <layer_type> "
            "<rgb_boost> <contrast> <brightness> <blur> "
            "<saturation_red> <saturation_green> <saturation_blue> <hue>"
        )
        sys.exit(1)

    img_path = sys.argv[1]
    output_path = sys.argv[2]
    layer_type = sys.argv[3].strip().lower()

    try:
        rgb_boost = float(sys.argv[4])
        contrast = float(sys.argv[5])
        brightness = float(sys.argv[6])
        blur = float(sys.argv[7])
        saturation_red = float(sys.argv[8])
        saturation_green = float(sys.argv[9])
        saturation_blue = float(sys.argv[10])
        hue = float(sys.argv[11])
    except ValueError as e:
        print(f"Invalid effect parameter: {e}")
        sys.exit(1)

    try:
        image_in = Image.open(img_path).convert("RGBA")
    except Exception as e:
        print(f"Failed to open image: {e}")
        sys.exit(1)

    # Mark foreground/background on the image so blur can react appropriately
    image_in.foreground = layer_type in ("foreground", "fg", "front")

    eff = ApplyEffects()  # GPU flag ignored, CPU only

    try:
        effects = Effects(
            rgb_boost=rgb_boost,
            contrast=contrast,
            brightness=brightness,
            blur=blur,
            saturation_red=saturation_red,
            saturation_green=saturation_green,
            saturation_blue=saturation_blue,
            hue=hue,
        )
        # Also mark the effects object, in case caller uses it elsewhere
        if hasattr(effects, "foreground"):
            effects.foreground = image_in.foreground

        processed = eff.apply_effects(image_in, effects)
        processed.save(output_path, "PNG")
        print(f"Preview image saved to: {output_path}")
    except Exception as e:
        print(f"Failed to apply effects: {e}")
        sys.exit(1)
