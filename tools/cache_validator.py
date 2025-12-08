#!/usr/bin/env python3
"""
cache_validator.py

Validates the integrity of the asset cache against the manifest.
Sets needs_rebuild=true for any frame/light entries missing their cache files.
"""

import json
import math
import sys
from pathlib import Path
from typing import Dict, List, Optional

SPEED_MULTIPLIERS = (0.25, 0.5, 1.0, 2.0, 4.0)
ANIMATION_SCALE_PCTS = (100, 75, 50, 25, 10)
LIGHT_CACHE_VERSION = 3


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


def _build_speed_frame_sequence(frame_count: int, multiplier: float) -> List[int]:
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


class CacheValidator:
    def __init__(self, manifest_path: Path) -> None:
        self.manifest_path = manifest_path
        self.manifest = self._load_manifest()

    def _load_manifest(self) -> Dict:
        try:
            with open(self.manifest_path, "r", encoding="utf-8") as fh:
                data = json.load(fh)
            if not isinstance(data, dict):
                return {"assets": {}, "maps": {}}
            data.setdefault("assets", {})
            data.setdefault("maps", {})
            return data
        except Exception:
            return {"assets": {}, "maps": {}}

    def _save_manifest(self) -> None:
        with open(self.manifest_path, "w", encoding="utf-8") as fh:
            json.dump(self.manifest, fh, indent=2)

    @staticmethod
    def _normalize_frames(anim: Dict, frame_count: int) -> List[Dict]:
        frames = anim.get("frames") if isinstance(anim, dict) else None
        if not isinstance(frames, list):
            frames = []
        if frame_count < 0:
            frame_count = 0
        if len(frames) < frame_count:
            frames.extend({"needs_rebuild": False} for _ in range(frame_count - len(frames)))
        elif len(frames) > frame_count:
            frames = frames[:frame_count]
        anim["frames"] = frames
        return frames

    def _frame_count_from_anim(self, asset_name: str, anim_name: str, asset_meta: Dict, anim: Dict) -> int:
        if isinstance(anim, dict) and isinstance(anim.get("number_of_frames"), int):
            return max(0, int(anim["number_of_frames"]))
        # Fallback: count numbered frames on disk
        asset_dir = asset_meta.get("asset_directory", "") if isinstance(asset_meta, dict) else ""
        base = Path(asset_dir)
        if not base.is_absolute():
            base = self.manifest_path.parent / (asset_dir or (Path("SRC") / "assets" / asset_name))
        anim_dir = base / anim_name
        count = 0
        while True:
            candidate = anim_dir / f"{count}.png"
            if not candidate.exists():
                break
            count += 1
        return count

    def _read_speed_multiplier(self, anim_meta: Dict) -> float:
        if not isinstance(anim_meta, dict):
            return 1.0
        raw = anim_meta.get("speed_multiplier", anim_meta.get("speed_factor", 1.0))
        try:
            return float(raw)
        except Exception:
            return 1.0

    def _each_animation(self):
        assets = self.manifest.get("assets", {})
        if not isinstance(assets, dict):
            return
        for asset_name, asset_meta in assets.items():
            if not isinstance(asset_meta, dict):
                continue
            animations = asset_meta.get("animations", {})
            if isinstance(animations, dict) and "animations" in animations and isinstance(animations["animations"], dict):
                animations = animations["animations"]
            if not isinstance(animations, dict):
                continue
            for anim_name, anim_meta in animations.items():
                if isinstance(anim_meta, dict):
                    yield asset_name, asset_meta, anim_name, anim_meta

    def _each_asset(self):
        assets = self.manifest.get("assets", {})
        if not isinstance(assets, dict):
            return
        for asset_name, asset_meta in assets.items():
            if not isinstance(asset_meta, dict):
                continue
            yield asset_name, asset_meta

    @staticmethod
    def _normalize_lighting_entries(asset_meta: Dict) -> List[Dict]:
        lights = asset_meta.get("lighting_info")
        if isinstance(lights, dict):
            lights = [lights]
        if not isinstance(lights, list):
            lights = []
        normalized: List[Dict] = []
        for entry in lights:
            if not isinstance(entry, dict):
                continue
            entry.setdefault("needs_rebuild", False)
            normalized.append(entry)
        asset_meta["lighting_info"] = normalized
        return normalized

    @staticmethod
    def _set_needs_rebuild(entry: Dict) -> bool:
        if not isinstance(entry, dict):
            return False
        if bool(entry.get("needs_rebuild")):
            return False
        entry["needs_rebuild"] = True
        return True

    @staticmethod
    def _animation_output_exists(anim_cache_root: Path, output_idx: int, has_mask: bool) -> bool:
        for scale_pct in ANIMATION_SCALE_PCTS:
            scale_dir = anim_cache_root / f"scale_{scale_pct}"
            if not scale_dir.is_dir():
                return False
            for variant in ("normal", "foreground", "background"):
                frame_path = scale_dir / variant / f"{output_idx}.png"
                if not frame_path.is_file():
                    return False
            if has_mask:
                mask_path = scale_dir / "mask" / f"{output_idx}.png"
                if not mask_path.is_file():
                    return False
        return True

    @staticmethod
    def _mark_all_frames_missing(frames: List[Dict]) -> bool:
        changed = False
        for frame in frames:
            if CacheValidator._set_needs_rebuild(frame):
                changed = True
        return changed

    @staticmethod
    def _mark_light_entries_missing(entries: List[Dict]) -> bool:
        changed = False
        for entry in entries:
            if not isinstance(entry, dict):
                continue
            if not entry.get("has_light_source"):
                continue
            if CacheValidator._set_needs_rebuild(entry):
                changed = True
        return changed

    @staticmethod
    def _load_metadata(path: Path) -> Optional[Dict]:
        if not path.is_file():
            return None
        try:
            with open(path, "r", encoding="utf-8") as fh:
                data = json.load(fh)
            if isinstance(data, dict):
                return data
        except Exception:
            return None
        return None

    def _validate_animation_cache(self,
                                  asset_name: str,
                                  asset_meta: Dict,
                                  anim_name: str,
                                  anim_meta: Dict,
                                  cache_root: Path) -> bool:
        frame_count = self._frame_count_from_anim(asset_name, anim_name, asset_meta, anim_meta)
        frames = self._normalize_frames(anim_meta, frame_count)
        if frame_count <= 0:
            return False

        anim_cache_root = cache_root / asset_name / "animations" / anim_name
        if not anim_cache_root.is_dir():
            return self._mark_all_frames_missing(frames)

        speed_multiplier = self._read_speed_multiplier(anim_meta)
        frame_sequence = _build_speed_frame_sequence(frame_count, speed_multiplier)
        if not frame_sequence:
            return False

        has_mask = bool(asset_meta.get("has_shading"))
        changed = False
        for output_idx, source_idx in enumerate(frame_sequence):
            if source_idx < 0 or source_idx >= len(frames):
                continue
            frame_entry = frames[source_idx]
            if not isinstance(frame_entry, dict):
                continue
            if bool(frame_entry.get("needs_rebuild")):
                continue
            if not self._animation_output_exists(anim_cache_root, output_idx, has_mask):
                frame_entry["needs_rebuild"] = True
                changed = True
        return changed

    def _validate_light_cache(self, asset_name: str, asset_meta: Dict, cache_root: Path) -> bool:
        entries = self._normalize_lighting_entries(asset_meta)
        if not entries:
            return False

        cache_dir = cache_root / asset_name / "lights"
        if not cache_dir.is_dir():
            return self._mark_light_entries_missing(entries)

        metadata = self._load_metadata(cache_dir / "metadata.json")
        if metadata is None or metadata.get("version") != LIGHT_CACHE_VERSION:
            return self._mark_light_entries_missing(entries)

        changed = False
        for idx, entry in enumerate(entries):
            if not isinstance(entry, dict):
                continue
            if bool(entry.get("needs_rebuild")):
                continue
            if not entry.get("has_light_source"):
                continue
            frame_path = cache_dir / f"light_{idx}.png"
            if not frame_path.is_file():
                entry["needs_rebuild"] = True
                changed = True
        return changed

    def validate_cache_integrity(self, cache_root: Optional[Path] = None) -> bool:
        root = cache_root if cache_root else self.manifest_path.parent / "cache"
        changed = False
        for asset_name, asset_meta, anim_name, anim_meta in self._each_animation():
            if self._validate_animation_cache(asset_name, asset_meta, anim_name, anim_meta, root):
                changed = True
        for asset_name, asset_meta in self._each_asset():
            if self._validate_light_cache(asset_name, asset_meta, root):
                changed = True
        return changed

    def save(self) -> None:
        self._save_manifest()


def main():
    manifest_path = None
    for i, arg in enumerate(sys.argv):
        if arg == "--manifest" and i + 1 < len(sys.argv):
            manifest_path = Path(sys.argv[i + 1])
            break
    
    if manifest_path is None:
        manifest_path = Path(__file__).resolve().parent.parent / "manifest.json"
    
    validator = CacheValidator(manifest_path)
    changed = validator.validate_cache_integrity()
    if changed:
        print("Detected missing cache files; marked entries for rebuild.")
        validator.save()
    else:
        print("Cache validation passed; no missing files detected.")


if __name__ == "__main__":
    main()
