#!/usr/bin/env python3
"""
asset_info.py

Simple asset descriptor wrapper around manifest.json.
"""

import json
import logging
import os
import sys
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional

from cache_helper import compare_and_update_json


def _configure_logger() -> logging.Logger:
    logger = logging.getLogger(__name__)
    if not logger.handlers:
        handler = logging.StreamHandler(sys.stderr)
        handler.setFormatter(logging.Formatter("%(levelname)s: %(message)s"))
        logger.addHandler(handler)
        logger.setLevel(logging.INFO)
    return logger


LOGGER = _configure_logger()


@dataclass
class Asset:
    """Represents a single asset entry from manifest.json."""

    name: str
    manifest_path: str

    json_entry: Dict[str, Any] = field(default_factory=dict)
    src_path: str = ""
    size_variants: List[int] = field(default_factory=list)
    type: str = ""
    needs_regen: bool = True

    cache_dir: Optional[str] = None  # optional override for cache location

    def __post_init__(self) -> None:
        self._load_from_manifest()

    def _load_from_manifest(self) -> None:
        """Load manifest, extract snippet and populate fields, then check cache."""
        manifest_abs = os.path.abspath(self.manifest_path)

        if not os.path.exists(manifest_abs):
            LOGGER.error("Manifest file not found at '%s'. Marking asset for regen.", manifest_abs)
            self.needs_regen = True
            return

        try:
            with open(manifest_abs, "r", encoding="utf-8") as f:
                manifest = json.load(f)
        except Exception as exc:
            LOGGER.error(
                "Failed to load or parse manifest '%s': %s. Marking asset for regen.",
                manifest_abs,
                exc,
            )
            self.needs_regen = True
            return

        assets_block = manifest.get("assets", {})
        if not isinstance(assets_block, dict):
            LOGGER.error("Manifest 'assets' block is missing or invalid. Marking asset for regen.")
            self.needs_regen = True
            return

        raw_entry = assets_block.get(self.name)
        if not isinstance(raw_entry, dict):
            LOGGER.error(
                "Asset '%s' not found in manifest '%s'. Marking asset for regen.",
                self.name,
                manifest_abs,
            )
            self.needs_regen = True
            return

        # Store the raw snippet
        self.json_entry = raw_entry

        # Extract simple fields from the snippet
        self.src_path = str(raw_entry.get("asset_directory", ""))
        self.type = str(raw_entry.get("asset_type", ""))

        # Size variants from scaling_profile.recommended_percentages if present
        scaling_profile = raw_entry.get("scaling_profile", {})
        if isinstance(scaling_profile, dict):
            variants = scaling_profile.get("recommended_percentages")
            if isinstance(variants, list):
                try:
                    self.size_variants = [int(v) for v in variants]
                except (TypeError, ValueError):
                    LOGGER.warning(
                        "Asset '%s' has non integer values in scaling_profile.recommended_percentages. "
                        "Falling back to [100].",
                        self.name,
                    )
                    self.size_variants = [100]
            else:
                self.size_variants = [100]
        else:
            self.size_variants = [100]

        # Determine cache path
        cache_file = self._get_cache_path(manifest_abs)

        # Use the full snippet as the basis for cache comparison
        same = compare_and_update_json(self.json_entry, cache_file)

        # If cache content matches snippet, no regen needed
        self.needs_regen = not same

    def _get_cache_path(self, manifest_abs: str) -> str:
        """
        Resolve cache file path for this asset.

        Default: <manifest_dir>/.asset_cache/<asset_name>.json
        Directory creation is handled by cache_helper when writing.
        """
        if self.cache_dir:
            cache_root = os.path.abspath(self.cache_dir)
        else:
            manifest_dir = os.path.dirname(manifest_abs)
            cache_root = os.path.join(manifest_dir, ".asset_cache")

        return os.path.join(cache_root, f"{self.name}.json")


if __name__ == "__main__":
    # Simple manual test:
    #   python asset_info.py <asset_name> <path_to_manifest.json> [cache_dir]
    if len(sys.argv) < 3:
        print(
            "Usage: python asset_info.py <asset_name> <path_to_manifest.json> [cache_dir]",
            file=sys.stderr,
        )
        sys.exit(1)

    asset_name = sys.argv[1]
    manifest_file = sys.argv[2]
    cache_dir_arg = sys.argv[3] if len(sys.argv) > 3 else None

    a = Asset(asset_name, manifest_file, cache_dir=cache_dir_arg)
    print("Name:", a.name)
    print("Type:", a.type)
    print("Source path:", a.src_path)
    print("Size variants:", a.size_variants)
    print("Needs regen:", a.needs_regen)
