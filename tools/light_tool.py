#!/usr/bin/env python3
"""Light cache generation tool driven by manifest content (no rebuild queue)."""

import json
import logging
import math
import os
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from PIL import Image

LIGHT_CACHE_VERSION = 3


def _configure_logger() -> logging.Logger:
    logger = logging.getLogger(__name__)
    if not logger.handlers:
        handler = logging.StreamHandler(sys.stderr)
        handler.setFormatter(logging.Formatter("%(levelname)s: %(message)s"))
        logger.addHandler(handler)
        logger.setLevel(logging.ERROR)
    return logger


LOGGER = _configure_logger()

def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def light_signature(light: "LightDefinition") -> str:
    return (
        f"{light.radius}|"
        f"{light.fall_off}|"
        f"{light.flare}|"
        f"{light.intensity}|"
        f"{light.flicker_speed}|"
        f"{light.flicker_smoothness}"
    )


def compute_fade_exponent(fall_off: int) -> float:
    falloff_norm = clamp(float(fall_off) / 100.0, 0.0, 1.0)
    return 0.6 + 3.4 * falloff_norm


def lround(value: float) -> int:
    # Match std::lround semantics (half away from zero)
    if value >= 0:
        return int(math.floor(value + 0.5))
    return int(math.ceil(value - 0.5))


@dataclass
class LightDefinition:
    intensity: int = 255
    radius: int = 64
    fall_off: int = 50
    flare: int = 0
    flicker_speed: int = 0
    flicker_smoothness: int = 100
    offset_x: int = 0
    offset_y: int = 0
    color: Tuple[int, int, int] = (255, 255, 255)
    in_front: bool = False
    behind: bool = False
    render_to_dark_mask: bool = False
    render_front_and_back_to_asset_alpha_mask: bool = False

    def signature(self) -> str:
        return light_signature(self)

    def cache_json(self) -> Dict[str, Any]:
        return {
            "has_light_source": True,
            "light_intensity": self.intensity,
            "radius": self.radius,
            "fall_off": self.fall_off,
            "flare": self.flare,
            "flicker_speed": self.flicker_speed,
            "flicker_smoothness": self.flicker_smoothness,
            "offset_x": self.offset_x,
            "offset_y": self.offset_y,
            "light_color": list(self.color),
            "in_front": self.in_front,
            "behind": self.behind,
            "render_to_dark_mask": self.render_to_dark_mask,
            "render_front_and_back_to_asset_alpha_mask": self.render_front_and_back_to_asset_alpha_mask,
        }


def read_int(src: Dict[str, Any], key: str, fallback: int) -> int:
    try:
        if key in src:
            value = src[key]
            if isinstance(value, bool):
                return 100 if value else 0
            if isinstance(value, (int, float)):
                return int(round(float(value)))
            if isinstance(value, str) and value.strip():
                try:
                    return int(round(float(value.strip())))
                except ValueError:
                    return fallback
    except Exception:
        return fallback
    return fallback


def parse_light_entry(raw: Any) -> Optional[LightDefinition]:
    if not isinstance(raw, dict):
        return None
    if not raw.get("has_light_source", False):
        return None

    radius = max(1, read_int(raw, "radius", 64))

    fall_off_value = max(0, read_int(raw, "fall_off", 50))

    flare = max(0, read_int(raw, "flare", 0))
    intensity = max(1, min(255, read_int(raw, "light_intensity", 255)))
    flicker_speed = max(0, min(100, read_int(raw, "flicker_speed", 0)))
    flicker_smoothness = max(
        0, min(100, read_int(raw, "flicker_smoothness", 100))
    )
    offset_x = read_int(raw, "offset_x", 0)
    offset_y = read_int(raw, "offset_y", 0)

    color = (255, 255, 255)
    try:
        arr = raw.get("light_color")
        if isinstance(arr, list) and len(arr) >= 3:
            r = max(0, min(255, int(arr[0])))
            g = max(0, min(255, int(arr[1])))
            b = max(0, min(255, int(arr[2])))
            color = (r, g, b)
    except Exception:
        color = (255, 255, 255)

    return LightDefinition(
        intensity=intensity,
        radius=radius,
        fall_off=fall_off_value,
        flare=flare,
        flicker_speed=flicker_speed,
        flicker_smoothness=flicker_smoothness,
        offset_x=offset_x,
        offset_y=offset_y,
        color=color,
        in_front=bool(raw.get("in_front", False)),
        behind=bool(raw.get("behind", False)),
        render_to_dark_mask=bool(raw.get("render_to_dark_mask", False)),
        render_front_and_back_to_asset_alpha_mask=bool(
            raw.get("render_front_and_back_to_asset_alpha_mask", False)
        ),
    )


def build_light_image(light: LightDefinition) -> Image.Image:
    radius = max(1, light.radius)
    diameter = max(1, radius * 2)
    center = float(diameter) * 0.5
    fade_exponent = compute_fade_exponent(light.fall_off)
    radius_f = float(radius)

    img = Image.new("RGBA", (diameter, diameter))
    pixels = img.load()

    for y in range(diameter):
        for x in range(diameter):
            dx = (float(x) + 0.5) - center
            dy = (float(y) + 0.5) - center
            dist = math.hypot(dx, dy)
            ratio = dist / radius_f if radius_f > 0.0 else 0.0
            ratio = clamp(ratio, 0.0, 1.0)
            base = max(0.0, 1.0 - ratio)
            alpha_ratio = math.pow(base, fade_exponent)
            alpha = lround(alpha_ratio * 255.0)
            alpha = max(0, min(255, alpha))
            pixels[x, y] = (255, 255, 255, alpha)

    return img


@dataclass
class LightAsset:
    name: str
    lighting_entries: List[Dict[str, Any]]
    light_defs: List[LightDefinition]
    flagged_indices: List[int]


class LightTool:
    def __init__(self, manifest_path: str, cache_root: str) -> None:
        self.manifest_path = Path(manifest_path).absolute()
        self.cache_root = Path(cache_root).absolute()
        self.manifest = self._load_manifest()
        self.any_failures = False

    def _load_manifest(self) -> Dict[str, Any]:
        try:
            with open(self.manifest_path, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception as exc:
            LOGGER.error("Failed to read manifest '%s': %s", self.manifest_path, exc)
            sys.exit(1)

    def save_manifest(self) -> None:
        try:
            with open(self.manifest_path, "w", encoding="utf-8") as f:
                json.dump(self.manifest, f, indent=2)
        except Exception as exc:
            LOGGER.error("Failed to write manifest '%s': %s", self.manifest_path, exc)
            sys.exit(1)

    @staticmethod
    def _normalize_lighting_entries(asset_meta: Dict[str, Any]) -> List[Dict[str, Any]]:
        lights = asset_meta.get("lighting_info")
        if isinstance(lights, dict):
            lights = [lights]
        if not isinstance(lights, list):
            lights = []
        normalized: List[Dict[str, Any]] = []
        for entry in lights:
            if not isinstance(entry, dict):
                continue
            entry.setdefault("needs_rebuild", False)
            normalized.append(entry)
        asset_meta["lighting_info"] = normalized
        return normalized

    def _collect_assets(self) -> List[LightAsset]:
        assets_block = self.manifest.get("assets", {})
        if not isinstance(assets_block, dict):
            LOGGER.error("Manifest 'assets' block missing or invalid.")
            return []

        light_assets: List[LightAsset] = []
        for name, entry in assets_block.items():
            if not isinstance(entry, dict):
                continue
            entries = self._normalize_lighting_entries(entry)
            flagged = [idx for idx, light in enumerate(entries) if bool(light.get("needs_rebuild"))]
            if not flagged:
                continue
            parsed_defs: List[LightDefinition] = []
            for light_entry in entries:
                parsed = parse_light_entry(light_entry)
                if parsed:
                    parsed_defs.append(parsed)
            light_assets.append(LightAsset(name, entries, parsed_defs, flagged))
        return light_assets

    def _write_metadata(self, cache_dir: Path, signatures: List[str]) -> None:
        meta = {"version": LIGHT_CACHE_VERSION, "signatures": signatures}
        meta_path = cache_dir / "metadata.json"
        cache_dir.mkdir(parents=True, exist_ok=True)
        with open(meta_path, "w", encoding="utf-8") as f:
            json.dump(meta, f, indent=2)

    def _clear_light_flags(self, asset: LightAsset) -> None:
        for idx in asset.flagged_indices:
            if 0 <= idx < len(asset.lighting_entries):
                asset.lighting_entries[idx]["needs_rebuild"] = False

    def generate_for_asset(self, asset: LightAsset) -> bool:
        cache_dir = self.cache_root / asset.name / "lights"
        if cache_dir.exists():
            shutil.rmtree(cache_dir)
        cache_dir.mkdir(parents=True, exist_ok=True)

        if not asset.light_defs:
            LOGGER.info("[LightTool] %s has no lights; cleared cache directory.", asset.name)
            self._clear_light_flags(asset)
            return True

        LOGGER.info(
            "[LightTool] Regenerating %s (%d light source%s)",
            asset.name,
            len(asset.light_defs),
            "" if len(asset.light_defs) == 1 else "s",
        )

        signatures: List[str] = []
        for idx, light in enumerate(asset.light_defs):
            img = build_light_image(light)
            target = cache_dir / f"light_{idx}.png"
            img.save(target, "PNG", optimize=False)
            signatures.append(light.signature())

        self._write_metadata(cache_dir, signatures)
        self._clear_light_flags(asset)
        return True

    def run(self) -> None:
        assets = self._collect_assets()
        if not assets:
            return

        manifest_changed = False
        for asset in assets:
            success = self.generate_for_asset(asset)
            if success:
                manifest_changed = True
            else:
                self.any_failures = True

        if manifest_changed:
            assets_block = self.manifest.get("assets", {})
            if isinstance(assets_block, dict):
                self.manifest["assets"] = assets_block
            self.save_manifest()
            LOGGER.info("Updated manifest needs_rebuild flags after light rebuilds.")
        else:
            LOGGER.info("No light assets marked for rebuild; nothing to do.")

        if self.any_failures:
            LOGGER.warning("Some light assets could not be regenerated; flags remain set.")


def main() -> None:
    tools_dir = Path(__file__).resolve().parent
    repo_root = tools_dir.parent
    manifest_path = str(repo_root / "manifest.json")
    cache_root = str(repo_root / "cache")

    tool = LightTool(manifest_path, cache_root)
    tool.run()


if __name__ == "__main__":
    main()
