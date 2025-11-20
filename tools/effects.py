#!/usr/bin/env python3
"""
parse_effects.py

Helper for VIBBLE 2D Game Engine asset tools.

Defines:
  - Effects: container for all effect values
  - EffectsParser: reads manifest.json, returns foreground/background Effects,
    and uses cache_helper to detect if effect settings changed.

Behavior:

  parser = EffectsParser("path/to/manifest.json")
  fg, bg, unchanged = parser.parse()

  fg, bg are Effects objects.
  fg.foreground will be True
  bg.foreground will be False

  unchanged is True if both foreground and background settings match the
  cached JSON, False if they differ or the cache had to be created or updated.
"""

import json
import logging
import os
import sys
from dataclasses import dataclass, asdict
from typing import Tuple, Dict, Any, Optional

from cache_helper import compare_and_update_json


@dataclass
class Effects:
    """Container for a full set of effect values."""

    brightness: float = 0.0
    contrast: float = 0.0
    blur: float = 0.0
    saturation_red: float = 0.0
    saturation_green: float = 0.0
    saturation_blue: float = 0.0
    hue: float = 0.0

    # Indicates whether these effects are intended for the foreground or not.
    # This is set by EffectsParser based on which block (foreground/background)
    # the data came from.
    foreground: bool = False

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "Effects":
        """
        Build an Effects object from a dict, defaulting missing values to 0.0.

        The 'foreground' flag is not read from the dict, it is set by the parser.
        """
        return cls(
            brightness=float(data.get("brightness", 0.0)),
            contrast=float(data.get("contrast", 0.0)),
            blur=float(data.get("blur", 0.0)),
            saturation_red=float(data.get("saturation_red", 0.0)),
            saturation_green=float(data.get("saturation_green", 0.0)),
            saturation_blue=float(data.get("saturation_blue", 0.0)),
            hue=float(data.get("hue", 0.0)),
        )


def _configure_logger() -> logging.Logger:
    logger = logging.getLogger(__name__)
    if not logger.handlers:
        handler = logging.StreamHandler(sys.stderr)
        handler.setFormatter(logging.Formatter("%(levelname)s: %(message)s"))
        logger.addHandler(handler)
        logger.setLevel(logging.INFO)
    return logger


LOGGER = _configure_logger()


class EffectsParser:
    """
    Parses foreground and background image effects from a manifest.json file.

    Also uses cache_helper to compare the combined foreground/background effect
    block against a cache file.

    Use:
        parser = EffectsParser("path/to/manifest.json")
        fg_effects, bg_effects, unchanged = parser.parse()

    fg_effects and bg_effects are Effects instances.
      fg_effects.foreground == True
      bg_effects.foreground == False

    unchanged is True if both blocks match the cache, False otherwise.
    """

    def __init__(self, manifest_path: str, cache_path: Optional[str] = None) -> None:
        self.manifest_path = manifest_path
        self.cache_path = cache_path  # optional override

    def _load_manifest(self) -> dict:
        """Load JSON manifest file, or return empty dict on failure."""
        manifest_path = os.path.abspath(self.manifest_path)

        if not os.path.exists(manifest_path):
            LOGGER.warning(
                "Manifest file not found at '%s'. Using default effect values.",
                manifest_path,
            )
            return {}

        try:
            with open(manifest_path, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception as exc:
            LOGGER.warning(
                "Failed to load or parse manifest '%s': %s. Using default effect values.",
                manifest_path,
                exc,
            )
            return {}

    def _extract_block(self, manifest: dict, block_name: str) -> Tuple[Dict[str, float], Effects]:
        """
        Extract a single effect block (foreground or background).

        Returns:
            (block_dict, effects_obj)

            block_dict is a plain dict ready to be put in the cache snippet.
            effects_obj is an Effects instance built from block_dict, with
            effects_obj.foreground set based on block_name.

        If the block or fields are missing, defaults are used and a warning is logged.
        """
        if "image_effects" not in manifest:
            LOGGER.warning(
                "No 'image_effects' section found in manifest. "
                "Using default values for %s effects.",
                block_name,
            )
            empty = Effects()
            # Explicitly set foreground flag based on block_name
            empty.foreground = (block_name == "foreground")
            return asdict(empty), empty

        image_effects = manifest.get("image_effects", {})
        raw_block = image_effects.get(block_name)

        if not isinstance(raw_block, dict):
            LOGGER.warning(
                "'image_effects.%s' block missing or invalid. Using default values.",
                block_name,
            )
            empty = Effects()
            empty.foreground = (block_name == "foreground")
            return asdict(empty), empty

        safe_block: Dict[str, float] = {}
        for key in [
            "brightness",
            "contrast",
            "blur",
            "saturation_red",
            "saturation_green",
            "saturation_blue",
            "hue",
        ]:
            if key not in raw_block:
                LOGGER.warning(
                    "'image_effects.%s.%s' not found. Defaulting to 0.0.",
                    block_name,
                    key,
                )
                safe_block[key] = 0.0
            else:
                try:
                    safe_block[key] = float(raw_block.get(key, 0.0))
                except (TypeError, ValueError):
                    LOGGER.warning(
                        "'image_effects.%s.%s' could not be converted to float. Defaulting to 0.0.",
                        block_name,
                        key,
                    )
                    safe_block[key] = 0.0

        effects_obj = Effects.from_dict(safe_block)
        # Mark this effects object as foreground or background
        effects_obj.foreground = (block_name == "foreground")

        return safe_block, effects_obj

    def _get_cache_path(self) -> str:
        """
        Resolve the cache file path for storing the combined effects snippet.

        Default is a file called '.image_effects_cache.json' in the same
        directory as the manifest.
        """
        if self.cache_path:
            return os.path.abspath(self.cache_path)

        manifest_abs = os.path.abspath(self.manifest_path)
        manifest_dir = os.path.dirname(manifest_abs)
        return os.path.join(manifest_dir, ".image_effects_cache.json")

    def parse(self) -> Tuple[Effects, Effects, bool]:
        """
        Parse manifest and return:

            (foreground_effects, background_effects, unchanged)

        unchanged:
            True  -> cache file contents match current foreground/background
                     effects (snippet unchanged)
            False -> cache file was missing or different and was updated
        """
        manifest = self._load_manifest()

        fg_block, foreground = self._extract_block(manifest, "foreground")
        bg_block, background = self._extract_block(manifest, "background")

        snippet = {
            "foreground": fg_block,
            "background": bg_block,
        }

        cache_path = self._get_cache_path()
        unchanged = compare_and_update_json(snippet, cache_path)

        return foreground, background, unchanged


if __name__ == "__main__":
    # Manual test:
    #   python parse_effects.py path/to/manifest.json [optional_cache_path]
    if len(sys.argv) < 2:
        print(
            "Usage: python parse_effects.py <path_to_manifest.json> [cache_path]",
            file=sys.stderr,
        )
        sys.exit(1)

    manifest_file = sys.argv[1]
    cache_file = sys.argv[2] if len(sys.argv) > 2 else None

    parser = EffectsParser(manifest_file, cache_file)
    fg, bg, unchanged = parser.parse()

    print("Foreground effects:", fg)
    print("Background effects:", bg)
    print("Cache unchanged:", unchanged)
