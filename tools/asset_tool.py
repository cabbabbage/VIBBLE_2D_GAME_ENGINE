#!/usr/bin/env python3
"""
VIBBLE 2D Game Engine Asset Generation Tool.

New usage:

    python asset_tool.py <manifest_path> <cache_root> [asset_list] [animation_list]

    <manifest_path>   path to manifest.json
    <cache_root>      directory where cache will be written and where cache_helper
                      will store its json snippets
    [asset_list]      optional comma separated list of asset names to regenerate
    [animation_list]  optional comma separated list of animation ids to regenerate
                      for the selected assets

Behavior:

    - Always parses foreground and background effects via EffectsParser using a
      cache file inside <cache_root>.

    - If asset_list is provided:
        * Regenerate exactly those assets, even if effects changed.
        * When animation_list is provided, only regenerate the listed
          animations for those assets.

    - If asset_list is omitted:
        * If effects changed: regenerate all assets in the manifest.
        * If effects did not change: regenerate only assets whose snippets
          changed according to basic_asset_info.Asset and cache_helper.

    - For each asset to regenerate:
        * Load frames from its source directory.
        * For each animation and each size variant:
            - Generate normal, foreground, and background variants.
            - If the asset has shading enabled, also generate shadow masks.
            - Save to:
                  <cache_root>/<AssetName>/animations/<anim_id>/scale_<pct>/
                      normal/<frame_idx>.png
                      foreground/<frame_idx>.png
                      background/<frame_idx>.png
                      mask/<frame_idx>.png   (only when has_shading is true)
"""

import json
import logging
import os
import sys
import time
import math
import shutil
from pathlib import Path
from typing import Dict, List, Tuple, Optional, Set

from PIL import Image
import multiprocessing
from concurrent.futures import ProcessPoolExecutor, as_completed

from basic_asset_info import Asset
from apply_color_effects import ApplyEffects
from effects import Effects, EffectsParser
from cache_helper import compare_and_update_json, stable_hash
from shadow_mask import ShadowMaskGenerator, ShadowMaskSettings


def normalize_variant_steps(steps):
    # Always use fixed variants: 100%, 75%, 50%, 25%.
    return [1.0, 0.75, 0.5, 0.25]


def _configure_logger() -> logging.Logger:
    logger = logging.getLogger(__name__)
    if not logger.handlers:
        handler = logging.StreamHandler(sys.stderr)
        handler.setFormatter(logging.Formatter("%(levelname)s: %(message)s"))
        logger.addHandler(handler)
        logger.setLevel(logging.INFO)
    return logger


LOGGER = _configure_logger()

# Global per worker to avoid constructing ApplyEffects for every frame
_APPLY_EFFECTS: Optional[ApplyEffects] = None


def scan_animation_frames(src_folder: Path) -> Tuple[int, int, int]:
    """Scan animation frames to count them and get original dimensions."""
    frame_count = 0
    orig_w = 0
    orig_h = 0

    while True:
        frame_path = src_folder / f"{frame_count}.png"
        if not frame_path.exists():
            break

        if frame_count == 0:
            try:
                with Image.open(frame_path) as img:
                    orig_w, orig_h = img.size
            except Exception:
                break

        frame_count += 1

    return frame_count, orig_w, orig_h


def process_frame_task(task):
    """
    Worker function for a single frame.

    task is a tuple:
        (
          frame_idx,
          src_path,
          target_w,
          target_h,
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
    """
    Main class for asset generation tool.
    """

    def __init__(self, manifest_path: str, cache_root: str, animation_filter: Optional[Set[str]] = None):
        self.manifest_path = os.path.abspath(manifest_path)
        self.cache_root = Path(os.path.abspath(cache_root))
        self.manifest = self.load_manifest()
        self.animation_filter: Optional[Set[str]] = (
            set(animation_filter) if animation_filter else None
        )

        # Parse effects with cache located in cache_root
        effects_cache = self.cache_root / "effects_cache.json"
        self.fg_effects, self.bg_effects, effects_unchanged = EffectsParser(
            self.manifest_path, str(effects_cache)
        ).parse()
        self.effects_changed = not effects_unchanged

        LOGGER.info(
            "Effects changed: %s",
            "yes" if self.effects_changed else "no",
        )

        # One process pool reused across all assets and frames
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

    def _manifest_dir(self) -> Path:
        return Path(os.path.dirname(self.manifest_path))

    def _resolve_asset_src_dir(self, asset: Asset) -> Path:
        """
        Resolve the source directory for an asset, relative to manifest dir
        if the path in the manifest is not absolute.
        """
        src = asset.src_path
        if not src:
            return self._manifest_dir() / "SRC" / "assets" / asset.name

        src_path = Path(src)
        if src_path.is_absolute():
            return src_path
        return self._manifest_dir() / src_path

    def _animation_meta_path(self, asset_name: str, anim_id: str) -> Path:
        """Location for per-animation cache fingerprints."""
        return (
            self.cache_root
            / ".asset_cache"
            / "animations"
            / asset_name
            / f"{anim_id}.json"
        )

    def _compute_animation_signature(
        self,
        asset: Asset,
        anim_id: str,
        anim_dir: Path,
        frame_count: int,
        orig_w: int,
        orig_h: int,
        scale_pcts: List[int],
        mask_enabled: bool,
        mask_settings: Optional[Dict],
    ) -> Dict:
        """
        Build a lightweight fingerprint for an animation so we can skip
        regenerating unchanged animations.
        """
        frame_meta = []
        for frame_idx in range(frame_count):
            frame_path = anim_dir / f"{frame_idx}.png"
            if not frame_path.exists():
                break
            try:
                stat_res = frame_path.stat()
                mtime_ns = getattr(stat_res, "st_mtime_ns", int(stat_res.st_mtime * 1e9))
                size = stat_res.st_size
            except OSError:
                mtime_ns = 0
                size = -1

            frame_meta.append(
                {
                    "idx": frame_idx,
                    "size": size,
                    "mtime_ns": mtime_ns,
                }
            )

        effects_digest = stable_hash(
            {
                "foreground": vars(self.fg_effects),
                "background": vars(self.bg_effects),
            }
        )

        signature_payload = {
            "asset": asset.name,
            "animation": anim_id,
            "source": {
                "dir": str(anim_dir),
                "frame_count": frame_count,
                "orig_size": [orig_w, orig_h],
                "frames_digest": stable_hash(frame_meta),
            },
            "scales": scale_pcts,
            "mask": {
                "enabled": mask_enabled,
                "settings": mask_settings or {},
            },
            "effects_digest": effects_digest,
        }
        signature_payload["digest"] = stable_hash(signature_payload)
        return signature_payload

    def collect_assets_to_regen(self, asset_list_str: Optional[str]) -> List[Asset]:
        """
        Decide which assets to regenerate based on effects changes and optional
        user provided list.

        If effects changed, regenerate all assets regardless of asset_list_str.
        Otherwise, if asset_list_str is provided, regenerate only those.
        Otherwise, regenerate only assets whose snippet changed.

        asset_list_str is either:
            None                -> auto mode
            "AssetA,AssetB"     -> explicit list

        Returns a list of Asset objects.
        """
        assets_block = self.manifest.get("assets", {})
        if not isinstance(assets_block, dict):
            LOGGER.error("Manifest 'assets' block is missing or invalid.")
            return []

        all_asset_names = list(assets_block.keys())

        result: List[Asset] = []

        # Honor explicit asset list first, regardless of effects changes
        if asset_list_str is not None:
            # User explicitly requested a set of assets
            requested_names = [
                name.strip()
                for name in asset_list_str.split(",")
                if name.strip()
            ]
            if not requested_names:
                LOGGER.warning("Asset list was provided but empty after parsing.")
                return []

            for name in requested_names:
                if name not in assets_block:
                    LOGGER.warning(
                        "Requested asset '%s' is not in manifest. Skipping.", name
                    )
                    continue
                asset = Asset(
                    name=name,
                    manifest_path=self.manifest_path,
                    cache_dir=str(self.cache_root / ".asset_cache"),
                )
                result.append(asset)

            return result

        # No explicit list; if effects changed, regenerate everything
        if self.effects_changed:
            LOGGER.info("Effects changed. Regenerating all assets.")
            for name in all_asset_names:
                asset = Asset(
                    name=name,
                    manifest_path=self.manifest_path,
                    cache_dir=str(self.cache_root / ".asset_cache"),
                )
                result.append(asset)
            return result

        # Auto mode: regenerate only assets whose snippet changed
        LOGGER.info("Effects unchanged. Regenerating only assets that need regen.")
        for name in all_asset_names:
            asset = Asset(
                name=name,
                manifest_path=self.manifest_path,
                cache_dir=str(self.cache_root / ".asset_cache"),
            )
            if asset.needs_regen:
                result.append(asset)

        return result

    def generate_animation_cache_for_asset(self, asset: Asset) -> None:
        """
        Generate cache for a single asset:
          - all animations under its source dir
          - all size variants from asset.size_variants
          - normal, foreground, background for each frame
        """
        start_time = time.time()
        asset_src_dir = self._resolve_asset_src_dir(asset)

        if not asset_src_dir.exists():
            LOGGER.error(
                "Source directory for asset '%s' does not exist: %s",
                asset.name,
                asset_src_dir,
            )
            return

        # Root in cache for this asset
        asset_cache_root = self.cache_root / asset.name / "animations"

        # Effects passed directly as objects so they are pickled once per worker
        fg_cfg = self.fg_effects
        bg_cfg = self.bg_effects

        # Detect animations: either subdirectories or frames in root
        subdirs = [d for d in sorted(asset_src_dir.iterdir()) if d.is_dir()]
        if subdirs:
            animations = [(d, d.name) for d in subdirs]
        else:
            animations = [(asset_src_dir, "default")]

        if self.animation_filter:
            animations = [
                (path, anim_id)
                for path, anim_id in animations
                if anim_id in self.animation_filter
            ]
            if not animations:
                print(
                    f"[AssetTool] Asset '{asset.name}' has no animations matching filter "
                    f"{sorted(self.animation_filter)}. Skipping."
                )
                return

        print(f"[AssetTool] Regenerating asset '{asset.name}' from {asset_src_dir}")

        mask_enabled = bool(asset.is_shaded)
        mask_settings = None
        if mask_enabled:
            mask_settings = ShadowMaskSettings.sanitize(asset.shadow_mask_settings).as_dict()

        steps = self.get_normalized_steps_for_asset(asset.name)
        scale_pcts = [round(s * 100) for s in steps]
        scale_pcts = sorted(set(scale_pcts), reverse=True)

        for anim_dir, anim_id in animations:
            frame_count, orig_w, orig_h = scan_animation_frames(anim_dir)
            if frame_count == 0:
                LOGGER.warning(
                    "No frames found for asset '%s' animation '%s' in %s. Skipping.",
                    asset.name,
                    anim_id,
                    anim_dir,
                )
                continue

            print(
                f"  Animation '{anim_id}': {frame_count} frames, size {orig_w}x{orig_h}"
            )

            anim_cache_root = asset_cache_root / anim_id
            anim_meta_path = self._animation_meta_path(asset.name, anim_id)

            anim_signature = self._compute_animation_signature(
                asset,
                anim_id,
                anim_dir,
                frame_count,
                orig_w,
                orig_h,
                scale_pcts,
                mask_enabled,
                mask_settings,
            )

            cache_match = compare_and_update_json(
                anim_signature, str(anim_meta_path), write_if_different=False
            )
            cache_exists = anim_cache_root.exists()

            if cache_match and cache_exists:
                print(
                    f"  Animation '{anim_id}': no changes detected, skipping regeneration."
                )
                continue

            if anim_cache_root.exists():
                shutil.rmtree(anim_cache_root)

            had_errors = False

            for scale_pct in scale_pcts:
                scale_factor = scale_pct / 100.0
                target_w = max(1, int(orig_w * scale_factor))
                target_h = max(1, int(orig_h * scale_factor))

                print(
                    f"    Scale {scale_pct}% -> {target_w}x{target_h}"
                )

                scale_dir = anim_cache_root / f"scale_{scale_pct}"
                normal_dir = scale_dir / "normal"
                fg_dir = scale_dir / "foreground"
                bg_dir = scale_dir / "background"
                mask_dir = scale_dir / "mask"

                # Create directories once, not per frame
                os.makedirs(normal_dir, exist_ok=True)
                os.makedirs(fg_dir, exist_ok=True)
                os.makedirs(bg_dir, exist_ok=True)
                if mask_enabled:
                    os.makedirs(mask_dir, exist_ok=True)
                else:
                    if mask_dir.exists():
                        shutil.rmtree(mask_dir, ignore_errors=True)

                # Prepare tasks
                tasks = []
                for frame_idx in range(frame_count):
                    src_path = str(anim_dir / f"{frame_idx}.png")
                    normal_path = str(normal_dir / f"{frame_idx}.png")
                    fg_path = str(fg_dir / f"{frame_idx}.png")
                    bg_path = str(bg_dir / f"{frame_idx}.png")
                    mask_path = str(mask_dir / f"{frame_idx}.png") if mask_enabled else None

                    tasks.append(
                        (
                            frame_idx,
                            src_path,
                            target_w,
                            target_h,
                            normal_path,
                            fg_path,
                            bg_path,
                            fg_cfg,
                            bg_cfg,
                            mask_path,
                            mask_settings,
                        )
                    )

                # Submit tasks to shared executor
                futures = [self.executor.submit(process_frame_task, t) for t in tasks]
                for fut in as_completed(futures):
                    result = fut.result()
                    if "Error" in result:
                        had_errors = True
                        print("      " + result, file=sys.stderr)

            if not had_errors:
                compare_and_update_json(anim_signature, str(anim_meta_path))
            else:
                LOGGER.warning(
                    "Animation '%s' had errors during processing; cache fingerprint not updated.",
                    anim_id,
                )

        elapsed = time.time() - start_time
        print(f"[AssetTool] Finished asset '{asset.name}' in {elapsed:.2f} seconds")

    def process_assets(self, assets: List[Asset]) -> None:
        """Regenerate cache for all assets in list."""
        if not assets:
            print("No assets need regeneration.")
            return

        try:
            for asset in assets:
                self.generate_animation_cache_for_asset(asset)
        finally:
            # Clean shutdown of process pool
            self.executor.shutdown(wait=True)


def main():
    if len(sys.argv) < 3:
        print(
            "Usage: python asset_tool.py <manifest_path> <cache_root> [asset_list] [animation_list]",
            file=sys.stderr,
        )
        print("  <manifest_path>  path to manifest.json", file=sys.stderr)
        print("  <cache_root>     cache directory root", file=sys.stderr)
        print(
            "  [asset_list]     optional comma separated asset names to regen",
            file=sys.stderr,
        )
        print(
            "  [animation_list] optional comma separated animation ids to regen for selected assets",
            file=sys.stderr,
        )
        sys.exit(1)

    manifest_path = sys.argv[1]
    cache_root = sys.argv[2]
    asset_list_str = sys.argv[3] if len(sys.argv) > 3 else None
    animation_list_str = sys.argv[4] if len(sys.argv) > 4 else None
    animation_filter = None
    if animation_list_str:
        animation_filter = [
            name.strip()
            for name in animation_list_str.split(",")
            if name.strip()
        ] or None

    tool = AssetTool(manifest_path, cache_root, set(animation_filter) if animation_filter else None)
    assets_to_regen = tool.collect_assets_to_regen(asset_list_str)
    tool.process_assets(assets_to_regen)


if __name__ == "__main__":
    main()
