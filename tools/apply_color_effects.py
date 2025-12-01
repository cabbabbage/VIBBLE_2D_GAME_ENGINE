#!/usr/bin/env python3
"""
VIBBLE 2D Game Engine
Apply color and lighting effects to a single image.

Usage:
  python apply_color_effects.py <img_path> <output_path> <layer_type>
                                <contrast> <brightness> <blur>
                                <saturation_red> <saturation_green> <saturation_blue>
                                <hue>

Where:
  <layer_type> is either "foreground" or "background"
  <hue> is in degrees in the range [-180, 180]
"""

from typing import Optional
from effects import Effects

import numpy as np
from PIL import Image, ImageFilter

# Optional GPU backend: PyTorch (no CuPy)
try:
    import torch

    _TORCH_AVAILABLE = torch.cuda.is_available()
except Exception:
    torch = None
    _TORCH_AVAILABLE = False


class ApplyEffects:
    """
    Effect processor.

    Uses GPU with PyTorch if available and enabled, otherwise falls back
    to NumPy CPU.

    Parameters are expected to be normalized floats (except hue in degrees).

      brightness       approx [-1.0, 1.0]
      contrast         approx [-1.0, 1.0] where 0.0 means no change
      blur             in [-1.0, 1.0]
                        > 0: defocus blur
                        < 0: sharpening
      saturation_red   approx [-1.0, 1.0]
      saturation_green approx [-1.0, 1.0]
      saturation_blue  approx [-1.0, 1.0]
      hue              degrees in [-180.0, 180.0], 0 means no change
    """

    def __init__(self, use_gpu: Optional[bool] = None) -> None:
        """
        If use_gpu is None, GPU is used when torch with CUDA is available.
        If use_gpu is True, try GPU, otherwise CPU.

        If torch or CUDA is not available, always falls back to CPU.
        """
        if use_gpu is None:
            self.use_gpu = _TORCH_AVAILABLE
        else:
            self.use_gpu = bool(use_gpu) and _TORCH_AVAILABLE

    # ---------- NumPy HSV helpers ----------

    @staticmethod
    def _rgb_to_hsv_np(r: np.ndarray, g: np.ndarray, b: np.ndarray):
        maxc = np.maximum(np.maximum(r, g), b)
        minc = np.minimum(np.minimum(r, g), b)
        v = maxc
        delta = maxc - minc

        s = np.where(maxc > 0.0, delta / np.where(maxc == 0.0, 1.0, maxc), 0.0)

        h = np.zeros_like(maxc)
        non_zero = delta > 1e-6

        r_is_max = non_zero & (maxc == r)
        g_is_max = non_zero & (maxc == g)
        b_is_max = non_zero & (maxc == b)

        if np.any(r_is_max):
            h_r = (g - b) / np.where(delta == 0.0, 1.0, delta)
            h = np.where(r_is_max, (h_r % 6.0), h)

        if np.any(g_is_max):
            h_g = (b - r) / np.where(delta == 0.0, 1.0, delta) + 2.0
            h = np.where(g_is_max, h_g, h)

        if np.any(b_is_max):
            h_b = (r - g) / np.where(delta == 0.0, 1.0, delta) + 4.0
            h = np.where(b_is_max, h_b, h)

        h = (h / 6.0) % 1.0
        return h, s, v

    @staticmethod
    def _hsv_to_rgb_np(h: np.ndarray, s: np.ndarray, v: np.ndarray):
        h6 = h * 6.0
        c = v * s
        x = c * (1.0 - np.abs((h6 % 2.0) - 1.0))
        m = v - c

        r = np.zeros_like(h)
        g = np.zeros_like(h)
        b = np.zeros_like(h)

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

    # ---------- Torch HSV helpers ----------

    @staticmethod
    def _rgb_to_hsv_torch(r, g, b):
        """
        r, g, b in [0, 1], torch tensors.
        Returns h, s, v in [0, 1].
        """
        stacked = torch.stack([r, g, b], dim=0)
        maxc, _ = torch.max(stacked, dim=0)
        minc, _ = torch.min(stacked, dim=0)
        v = maxc
        delta = maxc - minc

        s = torch.where(
            maxc > 0.0,
            delta / torch.where(maxc == 0.0, torch.ones_like(maxc), maxc),
            torch.zeros_like(maxc),
        )

        h = torch.zeros_like(maxc)
        non_zero = delta > 1e-6

        r_is_max = non_zero & (maxc == r)
        g_is_max = non_zero & (maxc == g)
        b_is_max = non_zero & (maxc == b)

        denom = torch.where(delta == 0.0, torch.ones_like(delta), delta)

        if r_is_max.any():
            h_r = (g - b) / denom
            h = torch.where(r_is_max, h_r.remainder(6.0), h)

        if g_is_max.any():
            h_g = (b - r) / denom + 2.0
            h = torch.where(g_is_max, h_g, h)

        if b_is_max.any():
            h_b = (r - g) / denom + 4.0
            h = torch.where(b_is_max, h_b, h)

        h = (h / 6.0) % 1.0
        return h, s, v

    @staticmethod
    def _hsv_to_rgb_torch(h, s, v):
        """
        h, s, v in [0, 1], torch tensors.
        Returns r, g, b in [0, 1].
        """
        h6 = h * 6.0
        c = v * s
        x = c * (1.0 - torch.abs((h6 % 2.0) - 1.0))
        m = v - c

        r = torch.zeros_like(h)
        g = torch.zeros_like(h)
        b = torch.zeros_like(h)

        cond0 = (0.0 <= h6) & (h6 < 1.0)
        cond1 = (1.0 <= h6) & (h6 < 2.0)
        cond2 = (2.0 <= h6) & (h6 < 3.0)
        cond3 = (3.0 <= h6) & (h6 < 4.0)
        cond4 = (4.0 <= h6) & (h6 < 5.0)
        cond5 = (5.0 <= h6) & (h6 < 6.0)

        if cond0.any():
            r = torch.where(cond0, c, r)
            g = torch.where(cond0, x, g)
            b = torch.where(cond0, torch.zeros_like(b), b)

        if cond1.any():
            r = torch.where(cond1, x, r)
            g = torch.where(cond1, c, g)
            b = torch.where(cond1, torch.zeros_like(b), b)

        if cond2.any():
            r = torch.where(cond2, torch.zeros_like(r), r)
            g = torch.where(cond2, c, g)
            b = torch.where(cond2, x, b)

        if cond3.any():
            r = torch.where(cond3, torch.zeros_like(r), r)
            g = torch.where(cond3, x, g)
            b = torch.where(cond3, c, b)

        if cond4.any():
            r = torch.where(cond4, x, r)
            g = torch.where(cond4, torch.zeros_like(g), g)
            b = torch.where(cond4, c, b)

        if cond5.any():
            r = torch.where(cond5, c, r)
            g = torch.where(cond5, torch.zeros_like(g), g)
            b = torch.where(cond5, x, b)

        r = r + m
        g = g + m
        b = b + m

        return r, g, b

    # ---------- Blur / sharpen ----------

    @staticmethod
    def _apply_blur_or_sharpen(img: Image.Image, blur_val: float) -> Image.Image:
        """
        Apply blur or sharpening to img based on blur_val in [-1.0, 1.0].

        > 0: defocus blur, interpreted differently for foreground vs background:
             - foreground: larger, slightly ringy blur (strong foreground bokeh)
             - background: smoother blur with strong highlight bloom (background bokeh)
        < 0: UnsharpMask sharpening, strength mapped from 0 to 1.
        0:   no change.
        """
        from PIL import ImageEnhance

        try:
            v = float(blur_val)
        except Exception:
            v = 0.0

        v = max(-1.0, min(1.0, v))

        if abs(v) < 1e-3:
            return img

        is_foreground = bool(getattr(img, "foreground", False))

        if v > 0.0:
            max_radius = 20.0
            base_radius = v * max_radius
            if base_radius < 1.0:
                base_radius = 1.0

            if is_foreground:
                radius = base_radius * 2.0
                blurred = img.filter(ImageFilter.GaussianBlur(radius=radius))

                ring_radius = max(1.0, radius * 0.5)
                ring_percent = 80
                ring = blurred.filter(
                    ImageFilter.UnsharpMask(
                        radius=ring_radius,
                        percent=ring_percent,
                        threshold=3,
                    )
                )
                return ring
            else:
                radius = base_radius * 1.3
                blurred = img.filter(ImageFilter.GaussianBlur(radius=radius))

                lum = img.convert("L")

                def bright_curve(x):
                    if x < 170:
                        return 0
                    return min(255, int((x - 170) * 3))

                mask = lum.point(bright_curve, mode="L")
                mask = mask.filter(
                    ImageFilter.GaussianBlur(radius=max(1.0, radius * 0.8))
                )

                bright_blurred = ImageEnhance.Brightness(blurred).enhance(1.4)
                result = Image.composite(bright_blurred, blurred, mask)
                return result

        strength = -v
        radius = 0.7 + strength * 3.3
        percent = 80 + strength * 220
        threshold = 0
        return img.filter(
            ImageFilter.UnsharpMask(radius=radius, percent=int(percent), threshold=threshold)
        )

    # ---------- CPU implementation ----------

    def _apply_cpu(self, image: Image.Image, params: Effects) -> Image.Image:
        if image.mode != "RGBA":
            image = image.convert("RGBA")

        def clamp_unit(v: float) -> float:
            return max(-1.0, min(1.0, float(v)))

        brightness = clamp_unit(getattr(params, "brightness", 0.0))
        contrast = clamp_unit(getattr(params, "contrast", 0.0))
        blur = clamp_unit(getattr(params, "blur", 0.0))
        sat_r = clamp_unit(getattr(params, "saturation_red", 0.0))
        sat_g = clamp_unit(getattr(params, "saturation_green", 0.0))
        sat_b = clamp_unit(getattr(params, "saturation_blue", 0.0))

        raw_hue_deg = float(getattr(params, "hue", 0.0))
        hue_deg = max(-180.0, min(180.0, raw_hue_deg))
        hue_offset = hue_deg / 360.0

        img_np = np.asarray(image, dtype=np.uint8)
        img_float = img_np.astype(np.float32) / 255.0

        r = img_float[:, :, 0]
        g = img_float[:, :, 1]
        b = img_float[:, :, 2]
        a = img_float[:, :, 3]

        alpha_mask = a > 0

        if (
            not np.any(alpha_mask)
            or (
                abs(brightness) < 1e-6
                and abs(contrast) < 1e-6
                and abs(sat_r) < 1e-6
                and abs(sat_g) < 1e-6
                and abs(sat_b) < 1e-6
                and abs(hue_deg) < 1e-6
            )
        ):
            img_uint8 = (img_float * 255.0).astype(np.uint8)
            result = Image.fromarray(img_uint8, mode="RGBA")
            result = self._apply_blur_or_sharpen(result, blur)
            return result

        r_vis = r[alpha_mask]
        g_vis = g[alpha_mask]
        b_vis = b[alpha_mask]

        if abs(brightness) > 1e-6:
            r_vis = np.clip(r_vis + brightness, 0.0, 1.0)
            g_vis = np.clip(g_vis + brightness, 0.0, 1.0)
            b_vis = np.clip(b_vis + brightness, 0.0, 1.0)

        if abs(contrast) > 1e-6:
            c = 1.0 + contrast
            r_vis = np.clip((r_vis - 0.5) * c + 0.5, 0.0, 1.0)
            g_vis = np.clip((g_vis - 0.5) * c + 0.5, 0.0, 1.0)
            b_vis = np.clip((b_vis - 0.5) * c + 0.5, 0.0, 1.0)

        if abs(hue_deg) > 1e-6:
            h, s, v = self._rgb_to_hsv_np(r_vis, g_vis, b_vis)
            h = (h + hue_offset) % 1.0
            r_vis, g_vis, b_vis = self._hsv_to_rgb_np(h, s, v)

        def sat_factor(sat_val: float) -> float:
            return max(0.0, min(3.0, 1.0 + 2.0 * sat_val))

        if abs(sat_r) > 1e-6 or abs(sat_g) > 1e-6 or abs(sat_b) > 1e-6:
            gray = (r_vis + g_vis + b_vis) / 3.0

            if abs(sat_r) > 1e-6:
                f_r = sat_factor(sat_r)
                r_vis = np.clip(gray + (r_vis - gray) * f_r, 0.0, 1.0)

            if abs(sat_g) > 1e-6:
                f_g = sat_factor(sat_g)
                g_vis = np.clip(gray + (g_vis - gray) * f_g, 0.0, 1.0)

            if abs(sat_b) > 1e-6:
                f_b = sat_factor(sat_b)
                b_vis = np.clip(gray + (b_vis - gray) * f_b, 0.0, 1.0)

        r[alpha_mask] = r_vis
        g[alpha_mask] = g_vis
        b[alpha_mask] = b_vis

        img_float = np.stack([r, g, b, a], axis=2)
        img_uint8 = np.clip(img_float * 255.0, 0, 255).astype(np.uint8)
        result = Image.fromarray(img_uint8, mode="RGBA")
        result = self._apply_blur_or_sharpen(result, blur)
        return result

    # ---------- GPU implementation (PyTorch) ----------

    def _apply_gpu(self, image: Image.Image, params: Effects) -> Image.Image:
        if not _TORCH_AVAILABLE:
            return self._apply_cpu(image, params)

        device = torch.device("cuda")

        if image.mode != "RGBA":
            image = image.convert("RGBA")

        def clamp_unit(v: float) -> float:
            return max(-1.0, min(1.0, float(v)))

        brightness = clamp_unit(getattr(params, "brightness", 0.0))
        contrast = clamp_unit(getattr(params, "contrast", 0.0))
        blur = clamp_unit(getattr(params, "blur", 0.0))
        sat_r = clamp_unit(getattr(params, "saturation_red", 0.0))
        sat_g = clamp_unit(getattr(params, "saturation_green", 0.0))
        sat_b = clamp_unit(getattr(params, "saturation_blue", 0.0))

        raw_hue_deg = float(getattr(params, "hue", 0.0))
        hue_deg = max(-180.0, min(180.0, raw_hue_deg))
        hue_offset = hue_deg / 360.0

        img_np = np.asarray(image, dtype=np.uint8)
        img_float = img_np.astype(np.float32) / 255.0

        img_t = torch.from_numpy(img_float).to(device=device, dtype=torch.float32)

        r = img_t[:, :, 0]
        g = img_t[:, :, 1]
        b = img_t[:, :, 2]
        a = img_t[:, :, 3]

        alpha_mask = a > 0

        if (
            not bool(torch.any(alpha_mask).item())
            or (
                abs(brightness) < 1e-6
                and abs(contrast) < 1e-6
                and abs(sat_r) < 1e-6
                and abs(sat_g) < 1e-6
                and abs(sat_b) < 1e-6
                and abs(hue_deg) < 1e-6
            )
        ):
            img_uint8 = (img_t.clamp(0.0, 1.0) * 255.0).byte().cpu().numpy()
            result = Image.fromarray(img_uint8, mode="RGBA")
            result = self._apply_blur_or_sharpen(result, blur)
            return result

        r_vis = r[alpha_mask]
        g_vis = g[alpha_mask]
        b_vis = b[alpha_mask]

        if abs(brightness) > 1e-6:
            r_vis = torch.clamp(r_vis + brightness, 0.0, 1.0)
            g_vis = torch.clamp(g_vis + brightness, 0.0, 1.0)
            b_vis = torch.clamp(b_vis + brightness, 0.0, 1.0)

        if abs(contrast) > 1e-6:
            c = 1.0 + contrast
            r_vis = torch.clamp((r_vis - 0.5) * c + 0.5, 0.0, 1.0)
            g_vis = torch.clamp((g_vis - 0.5) * c + 0.5, 0.0, 1.0)
            b_vis = torch.clamp((b_vis - 0.5) * c + 0.5, 0.0, 1.0)

        if abs(hue_deg) > 1e-6:
            h, s, v = self._rgb_to_hsv_torch(r_vis, g_vis, b_vis)
            h = (h + hue_offset) % 1.0
            r_vis, g_vis, b_vis = self._hsv_to_rgb_torch(h, s, v)

        def sat_factor(sat_val: float) -> float:
            return max(0.0, min(3.0, 1.0 + 2.0 * sat_val))

        if abs(sat_r) > 1e-6 or abs(sat_g) > 1e-6 or abs(sat_b) > 1e-6:
            gray = (r_vis + g_vis + b_vis) / 3.0

            if abs(sat_r) > 1e-6:
                f_r = sat_factor(sat_r)
                r_vis = torch.clamp(gray + (r_vis - gray) * f_r, 0.0, 1.0)

            if abs(sat_g) > 1e-6:
                f_g = sat_factor(sat_g)
                g_vis = torch.clamp(gray + (g_vis - gray) * f_g, 0.0, 1.0)

            if abs(sat_b) > 1e-6:
                f_b = sat_factor(sat_b)
                b_vis = torch.clamp(gray + (b_vis - gray) * f_b, 0.0, 1.0)

        r[alpha_mask] = r_vis
        g[alpha_mask] = g_vis
        b[alpha_mask] = b_vis

        img_t = torch.stack([r, g, b, a], dim=2)
        img_uint8 = (img_t.clamp(0.0, 1.0) * 255.0).byte().cpu().numpy()
        result = Image.fromarray(img_uint8, mode="RGBA")
        result = self._apply_blur_or_sharpen(result, blur)
        return result

    # ---------- public API ----------

    def apply_effects(self, image: Image.Image, Effects_params: Effects) -> Image.Image:
        if self.use_gpu and _TORCH_AVAILABLE:
            return self._apply_gpu(image, Effects_params)
        return self._apply_cpu(image, Effects_params)


if __name__ == "__main__":
    import sys

    if len(sys.argv) != 11:
        print(
            "Usage: python apply_color_effects.py "
            "<img_path> <output_path> <layer_type> "
            "<contrast> <brightness> <blur> "
            "<saturation_red> <saturation_green> <saturation_blue> <hue>"
        )
        sys.exit(1)

    img_path = sys.argv[1]
    output_path = sys.argv[2]
    layer_type = sys.argv[3].strip().lower()

    try:
        contrast = float(sys.argv[4])
        brightness = float(sys.argv[5])
        blur = float(sys.argv[6])
        saturation_red = float(sys.argv[7])
        saturation_green = float(sys.argv[8])
        saturation_blue = float(sys.argv[9])
        hue = float(sys.argv[10])
    except ValueError as e:
        print(f"Invalid effect parameter: {e}")
        sys.exit(1)

    try:
        image_in = Image.open(img_path).convert("RGBA")
    except Exception as e:
        print(f"Failed to open image: {e}")
        sys.exit(1)

    # Mark foreground or background so blur behaves correctly
    image_in.foreground = layer_type in ("foreground", "fg", "front")

    # Default: try GPU if available
    eff = ApplyEffects(use_gpu=None)

    try:
        effects = Effects(
            contrast=contrast,
            brightness=brightness,
            blur=blur,
            saturation_red=saturation_red,
            saturation_green=saturation_green,
            saturation_blue=saturation_blue,
            hue=hue,
        )

        if hasattr(effects, "foreground"):
            effects.foreground = image_in.foreground

        processed = eff.apply_effects(image_in, effects)
        processed.save(output_path, "PNG")
        print(f"Preview image saved to: {output_path}")
    except Exception as e:
        print(f"Failed to apply effects: {e}")
        sys.exit(1)
