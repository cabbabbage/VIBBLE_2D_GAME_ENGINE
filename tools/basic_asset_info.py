#!/usr/bin/env python3

import json
import logging
import os
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional

from cache_helper import compare_and_update_json, stable_hash


def _configure_logger() -> logging.Logger:
    logger = logging.getLogger(__name__)
    if not logger.handlers:
        handler = logging.StreamHandler(sys.stderr)
        handler.setFormatter(logging.Formatter("%(levelname)s: %(message)s"))
        logger.addHandler(handler)
        logger.setLevel(logging.INFO)
    return logger


LOGGER = _configure_logger()


def _strip_generated_at_fields(obj: Any) -> Any:
    if isinstance(obj, dict):
        return {
            key: _strip_generated_at_fields(value)
            for key, value in obj.items()
            if key != "generated_at"
        }
    if isinstance(obj, list):
        return [_strip_generated_at_fields(value) for value in obj]
    return obj


@dataclass
class Asset:
    name: str
    manifest_path: str

    json_entry: Dict[str, Any] = field(default_factory=dict)
    src_path: str = ""
    size_variants: List[int] = field(default_factory=list)
    type: str = ""
    needs_regen: bool = True
    is_shaded: bool = False
    shadow_mask_settings: Dict[str, Any] = field(default_factory=dict)
    source_fingerprint: Dict[str, Any] = field(default_factory=dict)

    cache_dir: Optional[str] = None

    def __post_init__(self) -> None:
        self._load_from_manifest()

    def _load_from_manifest(self) -> None:
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

        cleaned_entry = _strip_generated_at_fields(raw_entry)
        self.json_entry = cleaned_entry

        self.src_path = str(cleaned_entry.get("asset_directory", ""))
        self.type = str(cleaned_entry.get("asset_type", ""))
        self.is_shaded = bool(cleaned_entry.get("has_shading", False))
        if isinstance(cleaned_entry.get("shadow_mask_settings"), dict):
            self.shadow_mask_settings = dict(cleaned_entry.get("shadow_mask_settings"))

        scaling_profile = cleaned_entry.get("scaling_profile", {})
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

        manifest_dir = os.path.dirname(manifest_abs)
        source_dir = self._resolve_asset_src_dir(manifest_dir)
        # Track source fingerprint so asset content edits trigger regeneration without nuking caches.
        self.source_fingerprint = self._compute_source_fingerprint(source_dir)

        cache_payload = {
            "version": 2,
            "manifest": self.json_entry,
            "source": self.source_fingerprint,
        }

        cache_file = self._get_cache_path(manifest_abs)
        same = compare_and_update_json(cache_payload, cache_file)
        self.needs_regen = not same

    def _get_cache_path(self, manifest_abs: str) -> str:
        if self.cache_dir:
            cache_root = os.path.abspath(self.cache_dir)
        else:
            manifest_dir = os.path.dirname(manifest_abs)
            cache_root = os.path.join(manifest_dir, ".asset_cache")
        return os.path.join(cache_root, f"{self.name}.json")

    def _resolve_asset_src_dir(self, manifest_dir: str) -> Path:
        if not self.src_path:
            return Path(manifest_dir) / "SRC" / "assets" / self.name
        candidate = Path(self.src_path)
        if candidate.is_absolute():
            return candidate
        return Path(manifest_dir) / candidate

    def _compute_source_fingerprint(self, asset_dir: Path) -> Dict[str, Any]:
        if not asset_dir.exists():
            return {
                "digest": "__missing__",
                "file_count": 0,
                "latest_mtime_ns": 0,
                "missing": True,
            }

        entries: List[Dict[str, Any]] = []
        latest_mtime_ns = 0

        try:
            for path in sorted(p for p in asset_dir.rglob("*") if p.is_file()):
                try:
                    stat_res = path.stat()
                except OSError:
                    continue

                mtime_ns = getattr(stat_res, "st_mtime_ns", int(stat_res.st_mtime * 1e9))
                if mtime_ns > latest_mtime_ns:
                    latest_mtime_ns = mtime_ns

                entries.append(
                    {
                        "path": path.relative_to(asset_dir).as_posix(),
                        "size": stat_res.st_size,
                        "mtime_ns": mtime_ns,
                    }
                )
        except Exception as exc:
            LOGGER.warning(
                "Failed to enumerate source files for asset '%s' in %s: %s",
                self.name,
                asset_dir,
                exc,
            )
            return {
                "digest": "__error__",
                "file_count": 0,
                "latest_mtime_ns": 0,
                "missing": False,
            }

        digest = stable_hash(entries)
        return {
            "digest": digest,
            "file_count": len(entries),
            "latest_mtime_ns": latest_mtime_ns,
            "missing": False,
        }


if __name__ == "__main__":
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
