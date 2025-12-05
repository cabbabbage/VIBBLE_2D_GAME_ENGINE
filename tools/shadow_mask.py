#!/usr/bin/env python3
"""
Shadow mask generation utilities and CLI.

Provides ShadowMaskGenerator.generate_mask_image() for in-process use and a
small CLI for generating a single preview mask:

    python shadow_mask.py <input_png> <output_png> <expansion_ratio> <blur_scale> <falloff_start> <falloff_exponent> <alpha_multiplier> [meta_path]

If meta_path is provided, the script will skip regeneration when both the
settings and source file signature (size + mtime_ns) match the existing
metadata, regenerating only when needed.
"""

import json
import math
import os
import sys
from dataclasses import dataclass
from heapq import heappush, heappop
from pathlib import Path
from typing import Dict, Optional, Tuple

from PIL import Image


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


@dataclass
class ShadowMaskSettings:
    expansion_ratio: float = 0.8
    blur_scale: float = 1.0
    falloff_start: float = 0.0
    falloff_exponent: float = 1.05
    alpha_multiplier: float = 1.0
    chunk_resolution: int = 3

    @staticmethod
    def sanitize(raw: Dict) -> "ShadowMaskSettings":
        if not isinstance(raw, dict):
            if hasattr(raw, "__dict__"):
                raw = dict(raw.__dict__)
            elif raw is not None:
                raw = {}
            else:
                raw = {}
        def _get(key: str, default: float) -> float:
            try:
                value = raw.get(key, default)
                return float(value)
            except Exception:
                return default

        settings = ShadowMaskSettings(
            expansion_ratio=_get("expansion_ratio", 0.8),
            blur_scale=_get("blur_scale", 1.0),
            falloff_start=_get("falloff_start", 0.0),
            falloff_exponent=_get("falloff_exponent", 1.05),
            alpha_multiplier=_get("alpha_multiplier", 1.0),
            chunk_resolution=int(raw.get("chunk_resolution", 3) or 3),
        )

        settings.expansion_ratio = clamp(settings.expansion_ratio, 0.0, 4.0)
        settings.blur_scale = clamp(settings.blur_scale, 0.0, 8.0)
        settings.falloff_start = clamp(settings.falloff_start, 0.0, 0.99)
        settings.falloff_exponent = max(0.01, settings.falloff_exponent)
        settings.alpha_multiplier = clamp(settings.alpha_multiplier, 0.0, 4.0)
        return settings

    def as_dict(self) -> Dict:
        return {
            "expansion_ratio": self.expansion_ratio,
            "blur_scale": self.blur_scale,
            "falloff_start": self.falloff_start,
            "falloff_exponent": self.falloff_exponent,
            "alpha_multiplier": self.alpha_multiplier,
            "chunk_resolution": self.chunk_resolution,
        }


class ShadowMaskGenerator:
    META_VERSION = 1

    @staticmethod
    def _compute_expand_radius(width: int, height: int, expansion_ratio: float) -> int:
        max_dim = max(width, height)
        scaled = float(max_dim) * (expansion_ratio * 0.5)
        radius = int(math.ceil(scaled))
        return max(1, radius)

    @staticmethod
    def _compute_base_blur_radius(expand_radius: int, blur_scale: float) -> int:
        if expand_radius <= 2:
            return 0
        base = max(1, expand_radius // 6)
        scaled = int(round(float(base) * blur_scale))
        return max(0, scaled)

    @staticmethod
    def _box_blur(values, width, height, radius):
        if radius <= 0 or width <= 0 or height <= 0:
            return

        horizontal = [0.0] * (width * height)
        prefix_row = [0.0] * (width + 1)

        for y in range(height):
            row_offset = y * width
            for i in range(width + 1):
                prefix_row[i] = 0.0
            for x in range(width):
                prefix_row[x + 1] = prefix_row[x] + values[row_offset + x]
            for x in range(width):
                left = max(0, x - radius)
                right = min(width - 1, x + radius)
                summation = prefix_row[right + 1] - prefix_row[left]
                count = right - left + 1
                horizontal[row_offset + x] = summation / float(count)

        prefix_col = [0.0] * (height + 1)
        blurred = [0.0] * (width * height)
        for x in range(width):
            for i in range(height + 1):
                prefix_col[i] = 0.0
            for y in range(height):
                prefix_col[y + 1] = prefix_col[y] + horizontal[y * width + x]
            for y in range(height):
                top = max(0, y - radius)
                bottom = min(height - 1, y + radius)
                summation = prefix_col[bottom + 1] - prefix_col[top]
                count = bottom - top + 1
                blurred[y * width + x] = summation / float(count)

        values[:] = blurred

    @staticmethod
    def _bell_curve_weight(t: float) -> float:
        clamped = clamp(t, 0.0, 1.0)
        return 0.5 - 0.5 * math.cos(clamped * math.pi)

    @staticmethod
    def _distance_transform(alpha, width, height, expand_radius):
        inf = float("inf")
        expanded_w = width + expand_radius * 2
        expanded_h = height + expand_radius * 2
        distance = [inf] * (expanded_w * expanded_h)
        queue = []
        has_opaque = False

        for y in range(height):
            row_offset = y * width
            for x in range(width):
                a = alpha[row_offset + x]
                if a > 0:
                    has_opaque = True
                    ex = x + expand_radius
                    ey = y + expand_radius
                    idx = ey * expanded_w + ex
                    distance[idx] = 0.0
                    heappush(queue, (0.0, ex, ey))

        if not has_opaque:
            return distance, expanded_w, expanded_h, False

        neighbors = (
            (1, 0, 1.0),
            (-1, 0, 1.0),
            (0, 1, 1.0),
            (0, -1, 1.0),
            (1, 1, math.sqrt(2.0)),
            (-1, 1, math.sqrt(2.0)),
            (1, -1, math.sqrt(2.0)),
            (-1, -1, math.sqrt(2.0)),
        )

        while queue:
            dist, x, y = heappop(queue)
            idx = y * expanded_w + x
            if dist > distance[idx] + 1e-4:
                continue
            if dist > expand_radius + 1e-4:
                continue
            for dx, dy, step in neighbors:
                nx = x + dx
                ny = y + dy
                if nx < 0 or ny < 0 or nx >= expanded_w or ny >= expanded_h:
                    continue
                candidate = dist + step
                if candidate > expand_radius + 1e-4:
                    continue
                nidx = ny * expanded_w + nx
                if candidate + 1e-4 < distance[nidx]:
                    distance[nidx] = candidate
                    heappush(queue, (candidate, nx, ny))

        return distance, expanded_w, expanded_h, True

    @staticmethod
    def generate_mask_image(img: Image.Image, settings_dict: Dict) -> Image.Image:
        settings = ShadowMaskSettings.sanitize(settings_dict).as_dict()
        rgba = img.convert("RGBA")
        width, height = rgba.size
        # Pillow's getdata(bands=3) is not available consistently; split instead.
        a_channel = list(rgba.split()[3].getdata())

        expand_radius = ShadowMaskGenerator._compute_expand_radius(width, height, settings["expansion_ratio"])
        base_blur_radius = ShadowMaskGenerator._compute_base_blur_radius(expand_radius, settings["blur_scale"]) // 2

        distance, expanded_w, expanded_h, has_opaque = ShadowMaskGenerator._distance_transform(
            a_channel, width, height, expand_radius
        )

        mask = Image.new("RGBA", (expanded_w, expanded_h), (0, 0, 0, 0))
        if not has_opaque:
            return mask

        base_alpha = [0.0] * (expanded_w * expanded_h)
        for i, d in enumerate(distance):
            if math.isfinite(d) and d <= float(expand_radius):
                base_alpha[i] = 255.0

        ShadowMaskGenerator._box_blur(base_alpha, expanded_w, expanded_h, base_blur_radius)

        expand_radius_f = float(expand_radius)
        start_distance = clamp(settings["falloff_start"], 0.0, 0.99) * expand_radius_f
        fade_span = max(1.0, expand_radius_f - start_distance)
        exponent = max(0.01, settings["falloff_exponent"])
        alpha_mult = clamp(settings["alpha_multiplier"], 0.0, 4.0)

        alpha_values = [0.0] * len(distance)
        for i, d in enumerate(distance):
            if not math.isfinite(d) or d > expand_radius_f:
                alpha_values[i] = 0.0
                continue
            if d <= start_distance:
                fade_progress = 0.0
            else:
                fade_progress = (d - start_distance) / fade_span
            fade_progress = clamp(fade_progress, 0.0, 1.0)
            weight = 1.0 - fade_progress
            eased = ShadowMaskGenerator._bell_curve_weight(weight)
            faded = math.pow(eased, exponent)
            alpha_values[i] = base_alpha[i] * faded * alpha_mult

        pixels = [(0, 0, 0, 0)] * len(alpha_values)
        for i, a in enumerate(alpha_values):
            alpha_byte = int(max(0, min(255, round(a))))
            pixels[i] = (0, 0, 0, alpha_byte)
        mask.putdata(pixels)
        return mask

    @staticmethod
    def generate_to_path(input_path: Path, output_path: Path, settings_dict: Dict) -> bool:
        try:
            with Image.open(input_path) as img:
                mask = ShadowMaskGenerator.generate_mask_image(img, settings_dict)
                output_path.parent.mkdir(parents=True, exist_ok=True)
                mask.save(output_path, "PNG", optimize=False)
            return True
        except Exception as exc:
            print(f"[ShadowMask] Failed to generate mask for {input_path}: {exc}", file=sys.stderr)
            return False

    @staticmethod
    def _write_meta(meta_path: Path, settings: Dict, input_path: Path) -> None:
        try:
            stat = input_path.stat()
            payload = {
                "version": ShadowMaskGenerator.META_VERSION,
                "settings": settings,
                "input_size": stat.st_size,
                "input_mtime_ns": stat.st_mtime_ns,
            }
            meta_path.parent.mkdir(parents=True, exist_ok=True)
            meta_path.write_text(json.dumps(payload, indent=2))
        except Exception:
            pass

    @staticmethod
    def _meta_matches(meta_path: Path, settings: Dict, input_path: Path) -> bool:
        try:
            if not meta_path.exists():
                return False
            meta = json.loads(meta_path.read_text())
            if meta.get("version") != ShadowMaskGenerator.META_VERSION:
                return False
            stat = input_path.stat()
            expected = {
                "input_size": stat.st_size,
                "input_mtime_ns": stat.st_mtime_ns,
                "settings": ShadowMaskSettings.sanitize(settings).as_dict(),
            }
            return (
                meta.get("input_size") == expected["input_size"]
                and meta.get("input_mtime_ns") == expected["input_mtime_ns"]
                and meta.get("settings") == expected["settings"]
            )
        except Exception:
            return False

    @staticmethod
    def generate_preview(input_path: Path, output_path: Path, settings_dict: Dict, meta_path: Optional[Path] = None) -> bool:
        if meta_path and output_path.exists() and ShadowMaskGenerator._meta_matches(meta_path, settings_dict, input_path):
            # Preview is already up to date
            return True
        ok = ShadowMaskGenerator.generate_to_path(input_path, output_path, settings_dict)
        if ok and meta_path:
            ShadowMaskGenerator._write_meta(meta_path, ShadowMaskSettings.sanitize(settings_dict).as_dict(), input_path)
        return ok


def _usage() -> None:
    print(
        "Usage: python shadow_mask.py <input_png> <output_png> <expansion_ratio> <blur_scale> <falloff_start> <falloff_exponent> <alpha_multiplier> [meta_path]",
        file=sys.stderr,
    )


def _parse_cli(argv) -> Tuple[Optional[Path], Optional[Path], Optional[ShadowMaskSettings], Optional[Path]]:
    if len(argv) < 8:
        _usage()
        return None, None, None, None
    try:
        input_path = Path(argv[1])
        output_path = Path(argv[2])
        settings = ShadowMaskSettings.sanitize(
            {
                "expansion_ratio": float(argv[3]),
                "blur_scale": float(argv[4]),
                "falloff_start": float(argv[5]),
                "falloff_exponent": float(argv[6]),
                "alpha_multiplier": float(argv[7]),
            }
        )
        meta_path = Path(argv[8]) if len(argv) > 8 else None
        return input_path, output_path, settings, meta_path
    except Exception as exc:
        print(f"[ShadowMask] Failed to parse arguments: {exc}", file=sys.stderr)
        _usage()
        return None, None, None, None


def main(argv=None) -> int:
    argv = argv or sys.argv
    input_path, output_path, settings, meta_path = _parse_cli(argv)
    if not input_path or not output_path or not settings:
        return 1
    settings_dict = settings.as_dict()
    ok = ShadowMaskGenerator.generate_preview(input_path, output_path, settings_dict, meta_path)
    return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(main())
