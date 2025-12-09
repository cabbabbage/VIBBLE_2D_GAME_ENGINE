#!/usr/bin/env python3
"""Asset frame rebuild tool driven solely by manifest needs_rebuild flags."""

import json
import logging
import math
import multiprocessing
import os
import shutil
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from PIL import Image

from apply_color_effects import ApplyEffects
from effects import EffectsParser
from shadow_mask import ShadowMaskGenerator, ShadowMaskSettings


def normalize_variant_steps(steps):
    # Always use fixed variants: 100%, 75%, 50%, 25%, 10%.
    return [1.0, 0.75, 0.5, 0.25, 0.1]


def _configure_logger() -> logging.Logger:
    logger = logging.getLogger(__name__)
    if not logger.handlers:
        handler = logging.StreamHandler(sys.stderr)
        handler.setFormatter(logging.Formatter("%(levelname)s: %(message)s"))
        logger.addHandler(handler)
        logger.setLevel(logging.ERROR)
    return logger


LOGGER = _configure_logger()


# Global per worker to avoid constructing ApplyEffects for every frame
_APPLY_EFFECTS: Optional[ApplyEffects] = None

SPEED_MULTIPLIERS: Tuple[float, ...] = (0.25, 0.5, 1.0, 2.0, 4.0)


def _closest_speed_multiplier(value: float) -> float:
    if not math.isfinite(value) or value <= 0.0:
        return 1.0
    best = SPEED_MULTIPLIERS[0]
    best_diff = abs(best - value)
    for candidate in SPEED_MULTIPLIERS[1:]:
        diff = abs(candidate - value)
        if diff < best_diff:
            best_diff = diff
            best = candidate
    return best


def read_speed_multiplier(anim_meta: Dict[str, object]) -> float:
    if not isinstance(anim_meta, dict):
        return 1.0
    raw = anim_meta.get("speed_multiplier", anim_meta.get("speed_factor", 1.0))
    try:
        return _closest_speed_multiplier(float(raw))
    except Exception:
        return 1.0


def read_crop_frames(anim_meta: Dict[str, object]) -> bool:
    if not isinstance(anim_meta, dict):
        return False
    value = anim_meta.get("crop_frames", False)
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        text = value.strip().lower()
        return text in {"true", "1", "yes", "on"}
    return False


def build_speed_frame_sequence(frame_count: int, multiplier: float) -> List[int]:
    """Return a list of source frame indices after applying a speed multiplier."""
    if frame_count <= 0:
        return []
    multiplier = _closest_speed_multiplier(multiplier)
    if multiplier < 1.0:
        repeat = int(round(1.0 / multiplier))
        repeat = max(1, repeat)
        sequence: List[int] = []
        for idx in range(frame_count):
            sequence.extend([idx] * repeat)
        return sequence

    if multiplier > 1.0:
        step = int(round(multiplier))
        step = max(1, step)
        sequence = list(range(0, frame_count, step))
        if not sequence:
            sequence = [0]
        last_index = frame_count - 1
        if sequence[-1] != last_index:
            sequence.append(last_index)
        return sequence

    return list(range(frame_count))


def list_numeric_frame_paths(src_folder: Path) -> List[Path]:
    """Return ordered numeric frame paths from a folder (0.png, 1.png, ...)."""
    frames: List[Path] = []
    idx = 0
    while True:
        candidate = src_folder / f"{idx}.png"
        if not candidate.exists():
            break
        frames.append(candidate)
        idx += 1
    return frames


def compute_crop_bounds(frame_paths: List[Path]) -> Optional[Dict[str, int]]:
    """Compute shared crop bounds across all frames based on alpha channel."""
    if not frame_paths:
        return None

    union_left = None
    union_top = None
    union_right = None
    union_bottom = None
    base_size: Optional[Tuple[int, int]] = None

    for path in frame_paths:
        try:
            with Image.open(path) as img:
                if img.mode != "RGBA":
                    img = img.convert("RGBA")
                if base_size is None:
                    base_size = img.size
                elif img.size != base_size:
                    LOGGER.warning("Inconsistent frame sizes detected in %s; skipping crop.", path.parent)
                    return None
                alpha = img.getchannel("A")
                bbox = alpha.getbbox()
                if not bbox:
                    continue
                left, top, right, bottom = bbox  # right/bottom are exclusive
                union_left = left if union_left is None else min(union_left, left)
                union_top = top if union_top is None else min(union_top, top)
                union_right = right if union_right is None else max(union_right, right)
                union_bottom = bottom if union_bottom is None else max(union_bottom, bottom)
        except Exception as exc:
            LOGGER.warning("Failed computing crop bounds for %s: %s", path, exc)
            return None

    if base_size is None or union_left is None or union_top is None or union_right is None or union_bottom is None:
        return None

    base_w, base_h = base_size
    right_margin = max(0, base_w - union_right)
    bottom_margin = max(0, base_h - union_bottom)
    cropped_w = base_w - union_left - right_margin
    cropped_h = base_h - union_top - bottom_margin
    if cropped_w <= 0 or cropped_h <= 0:
        return None

    return {
        "left": int(union_left),
        "top": int(union_top),
        "right": int(right_margin),
        "bottom": int(bottom_margin),
        "width": int(base_w),
        "height": int(base_h),
    }


def scale_crop_bounds(bounds: Dict[str, int], scale_factor: float) -> Optional[Tuple[int, int, int, int]]:
    if not bounds:
        return None
    left = int(round(bounds.get("left", 0) * scale_factor))
    top = int(round(bounds.get("top", 0) * scale_factor))
    right = int(round(bounds.get("right", 0) * scale_factor))
    bottom = int(round(bounds.get("bottom", 0) * scale_factor))
    return left, top, right, bottom


def process_frame_task(task):
    """
    Worker function for a single frame.

    task is a tuple:
        (
          frame_idx,
          src_path,
          target_w,
          target_h,
          crop_bounds,
          normal_path,
          fg_path,
          bg_path,
          fg_effects,
          bg_effects,
          mask_path,
          mask_settings,
        )

    fg_effects and bg_effects are Effects instances.
    """
    global _APPLY_EFFECTS

    (
        frame_idx,
        src_path,
        target_w,
        target_h,
        crop_bounds,
        normal_path,
        fg_path,
        bg_path,
        fg_effects,
        bg_effects,
        mask_path,
        mask_settings,
    ) = task

    try:
        if _APPLY_EFFECTS is None:
            _APPLY_EFFECTS = ApplyEffects(use_gpu=None)

        with Image.open(src_path) as img:
            if img.mode != "RGBA":
                img = img.convert("RGBA")

            if target_w > 0 and target_h > 0:
                if img.size != (target_w, target_h):
                    img = img.resize((target_w, target_h), Image.LANCZOS)

            if crop_bounds:
                left, top, right, bottom = crop_bounds
                crop_left = max(0, left)
                crop_top = max(0, top)
                crop_right = max(0, right)
                crop_bottom = max(0, bottom)
                crop_width = max(1, img.width - crop_left - crop_right)
                crop_height = max(1, img.height - crop_top - crop_bottom)
                crop_box = (
                    crop_left,
                    crop_top,
                    crop_left + crop_width,
                    crop_top + crop_height,
                )
                img = img.crop(crop_box)

            # Save normal (dirs already created in parent)
            img.save(normal_path, "PNG", optimize=False)

            # Foreground
            fg_img = _APPLY_EFFECTS.apply_effects(img, fg_effects)
            fg_img.save(fg_path, "PNG", optimize=False)

            # Background
            bg_img = _APPLY_EFFECTS.apply_effects(img, bg_effects)
            bg_img.save(bg_path, "PNG", optimize=False)

            if mask_settings is not None and mask_path:
                mask_img = ShadowMaskGenerator.generate_mask_image(img, mask_settings)
                mask_img.save(mask_path, "PNG", optimize=False)

        return f"Frame {frame_idx}: OK"
    except Exception as exc:
        return f"Frame {frame_idx}: Error - {exc}"


class AssetTool:
    """Main class for asset generation tool."""

    def __init__(self, manifest_path: str, cache_root: str) -> None:
        self.manifest_path = os.path.abspath(manifest_path)
        self.cache_root = Path(os.path.abspath(cache_root))
        self.manifest = self.load_manifest()

        effects_cache = self.cache_root / "effects_cache.json"
        self.fg_effects, self.bg_effects, _effects_unchanged = EffectsParser(
            self.manifest_path, str(effects_cache)
        ).parse()

        cpu_count = multiprocessing.cpu_count()
        self.max_workers = max(1, cpu_count - 1)
        self.executor = ProcessPoolExecutor(max_workers=self.max_workers)

    def get_normalized_steps_for_asset(self, asset_name):
        # Manifest-driven scaling profiles are disabled; always use fixed variants.
        return normalize_variant_steps([])

    def load_manifest(self) -> Dict:
        """Load and parse the manifest JSON file."""
        try:
            with open(self.manifest_path, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception as exc:
            LOGGER.error("Failed to load manifest '%s': %s", self.manifest_path, exc)
            sys.exit(1)

    def save_manifest(self) -> None:
        try:
            with open(self.manifest_path, "w", encoding="utf-8") as f:
                json.dump(self.manifest, f, indent=2)
        except Exception as exc:
            LOGGER.error("Failed to write manifest '%s': %s", self.manifest_path, exc)
            sys.exit(1)

    def _manifest_dir(self) -> Path:
        return Path(os.path.dirname(self.manifest_path))

    def _resolve_asset_src_dir(self, asset_name: str, asset_meta: Dict) -> Path:
        """Resolve the source directory for an asset."""
        src = asset_meta.get("asset_directory", "") if isinstance(asset_meta, dict) else ""
        if not src:
            return self._manifest_dir() / "SRC" / "assets" / asset_name

        src_path = Path(src)
        if src_path.is_absolute():
            return src_path
        return self._manifest_dir() / src_path

    def _animation_payloads_for_asset(self, asset_meta: Dict) -> Dict[str, Dict]:
        payloads = asset_meta.get("animations", {}) if isinstance(asset_meta, dict) else {}
        if isinstance(payloads, dict) and "animations" in payloads and isinstance(payloads["animations"], dict):
            payloads = payloads["animations"]
        if not isinstance(payloads, dict):
            return {}
        return {k: v for k, v in payloads.items() if isinstance(v, dict)}

    def _ensure_frame_metadata(self, anim_meta: Dict, frame_count: int) -> List[Dict]:
        frames = anim_meta.get("frames") if isinstance(anim_meta, dict) else None
        if not isinstance(frames, list):
            frames = []
        if frame_count < 0:
            frame_count = 0
        if len(frames) < frame_count:
            frames.extend({"needs_rebuild": False} for _ in range(frame_count - len(frames)))
        anim_meta["frames"] = frames
        return frames

    def _frames_requiring_rebuild(self, frames: List[Dict]) -> List[int]:
        indices: List[int] = []
        for idx, entry in enumerate(frames):
            if isinstance(entry, dict) and entry.get("needs_rebuild") is True:
                indices.append(idx)
        return indices

    def generate_animation_cache_for_asset(self, asset_name: str, asset_meta: Dict) -> bool:
        """Regenerate animations for a single asset. Returns True if any work was done."""
        start_time = time.time()
        asset_src_dir = self._resolve_asset_src_dir(asset_name, asset_meta)

        if not asset_src_dir.exists():
            LOGGER.error(
                "Source directory for asset '%s' does not exist: %s",
                asset_name,
                asset_src_dir,
            )
            return False

        asset_cache_root = self.cache_root / asset_name / "animations"
        fg_cfg = self.fg_effects
        bg_cfg = self.bg_effects

        subdirs = [d for d in sorted(asset_src_dir.iterdir()) if d.is_dir()]
        if subdirs:
            animations = [(d, d.name) for d in subdirs]
        else:
            animations = [(asset_src_dir, "default")]

        mask_enabled = bool(asset_meta.get("has_shading", False))
        mask_settings = None
        if mask_enabled:
            mask_settings = ShadowMaskSettings.sanitize(asset_meta.get("shadow_mask_settings", {})).as_dict()

        steps = self.get_normalized_steps_for_asset(asset_name)
        scale_pcts = [round(s * 100) for s in steps]
        scale_pcts = sorted(set(scale_pcts), reverse=True)

        animation_payloads = self._animation_payloads_for_asset(asset_meta)

        did_work = False
        manifest_changed = False

        for anim_dir, anim_id in animations:
            frame_paths = list_numeric_frame_paths(anim_dir)
            if not frame_paths:
                LOGGER.warning(
                    "No frames found for asset '%s' animation '%s' in %s. Skipping.",
                    asset_name,
                    anim_id,
                    anim_dir,
                )
                continue

            anim_meta = animation_payloads.get(anim_id, {})
            if not isinstance(anim_meta, dict):
                anim_meta = {}
            animation_payloads[anim_id] = anim_meta
            anim_meta_root = anim_meta

            existing_len = len(anim_meta_root.get("frames", [])) if isinstance(anim_meta_root, dict) else 0
            frames_meta = self._ensure_frame_metadata(anim_meta_root, max(len(frame_paths), existing_len))
            flagged_frames = self._frames_requiring_rebuild(frames_meta)
            if not flagged_frames:
                continue

            try:
                with Image.open(frame_paths[0]) as img:
                    orig_w, orig_h = img.size
            except Exception as exc:
                LOGGER.warning(
                    "Failed to read first frame for asset '%s' animation '%s': %s",
                    asset_name,
                    anim_id,
                    exc,
                )
                continue

            speed_multiplier = read_speed_multiplier(anim_meta_root)
            crop_requested = read_crop_frames(anim_meta_root)

            frame_sequence = build_speed_frame_sequence(len(frame_paths), speed_multiplier)
            output_frame_count = len(frame_sequence)
            if output_frame_count == 0:
                LOGGER.warning(
                    "No output frames for asset '%s' animation '%s' after speed processing.",
                    asset_name,
                    anim_id,
                )
                continue

            crop_bounds = compute_crop_bounds(frame_paths) if crop_requested else None
            if crop_requested and crop_bounds is None:
                LOGGER.warning(
                    "Crop requested for asset '%s' animation '%s' but bounds could not be determined; leaving uncropped.",
                    asset_name,
                    anim_id,
                )

            anim_cache_root = asset_cache_root / anim_id
            if anim_cache_root.exists():
                shutil.rmtree(anim_cache_root)

            had_errors = False

            for scale_pct in scale_pcts:
                scale_factor = scale_pct / 100.0
                target_w = max(1, int(orig_w * scale_factor))
                target_h = max(1, int(orig_h * scale_factor))

                scale_dir = anim_cache_root / f"scale_{scale_pct}"
                normal_dir = scale_dir / "normal"
                fg_dir = scale_dir / "foreground"
                bg_dir = scale_dir / "background"
                mask_dir = scale_dir / "mask"

                os.makedirs(normal_dir, exist_ok=True)
                os.makedirs(fg_dir, exist_ok=True)
                os.makedirs(bg_dir, exist_ok=True)
                if mask_enabled:
                    os.makedirs(mask_dir, exist_ok=True)
                else:
                    if mask_dir.exists():
                        shutil.rmtree(mask_dir, ignore_errors=True)

                tasks = []
                scaled_crop = scale_crop_bounds(crop_bounds, scale_factor) if crop_bounds else None
                for output_idx, source_idx in enumerate(frame_sequence):
                    if source_idx < 0 or source_idx >= len(frame_paths):
                        continue
                    src_path = str(frame_paths[source_idx])
                    normal_path = str(normal_dir / f"{output_idx}.png")
                    fg_path = str(fg_dir / f"{output_idx}.png")
                    bg_path = str(bg_dir / f"{output_idx}.png")
                    mask_path = str(mask_dir / f"{output_idx}.png") if mask_enabled else None

                    tasks.append(
                        (
                            output_idx,
                            src_path,
                            target_w,
                            target_h,
                            scaled_crop,
                            normal_path,
                            fg_path,
                            bg_path,
                            fg_cfg,
                            bg_cfg,
                            mask_path,
                            mask_settings,
                        )
                    )

                futures = [self.executor.submit(process_frame_task, t) for t in tasks]
                for fut in as_completed(futures):
                    result = fut.result()
                    if "Error" in result:
                        had_errors = True
                        print("      " + result, file=sys.stderr)

            if had_errors:
                LOGGER.warning(
                    "Animation '%s' had errors during processing; needs_rebuild flags remain set.",
                    anim_id,
                )
                continue

            for idx in flagged_frames:
                if 0 <= idx < len(frames_meta):
                    frames_meta[idx]["needs_rebuild"] = False
            did_work = True
            manifest_changed = True

        elapsed = time.time() - start_time
        if manifest_changed:
            asset_meta.setdefault("animations", {})
            if isinstance(asset_meta["animations"], dict):
                asset_meta["animations"].update(animation_payloads)
            else:
                asset_meta["animations"] = animation_payloads
        return did_work


def main():
    tools_dir = Path(__file__).resolve().parent
    repo_root = tools_dir.parent
    manifest_path = repo_root / "manifest.json"
    cache_root_path = repo_root / "cache"

    tool = AssetTool(str(manifest_path), str(cache_root_path))

    assets_block = tool.manifest.get("assets", {})
    if not isinstance(assets_block, dict):
        LOGGER.error("Manifest 'assets' block is missing or invalid.")
        sys.exit(1)

    manifest_changed = False
    try:
        for asset_name, asset_meta in assets_block.items():
            if not isinstance(asset_meta, dict):
                continue
            did_work = tool.generate_animation_cache_for_asset(asset_name, asset_meta)
            manifest_changed = manifest_changed or did_work
    finally:
        tool.executor.shutdown(wait=True)

    if manifest_changed:
        tool.manifest["assets"] = assets_block
        tool.save_manifest()
        LOGGER.info("Updated manifest needs_rebuild flags after rebuilds.")
    else:
        LOGGER.info("No frames marked for rebuild; nothing to do.")


if __name__ == "__main__":
    main()
