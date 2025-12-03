#!/usr/bin/env python3
"""Utility helpers for coordinating rebuild requests between C++ and Python tools."""

from __future__ import annotations

import json
import os
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import Dict, List, Optional

TOOLS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TOOLS_DIR.parent
DEFAULT_QUEUE_PATH = TOOLS_DIR / "rebuild_requests.json"
DEFAULT_MANIFEST = REPO_ROOT / "manifest.json"
DEFAULT_CACHE_ROOT = REPO_ROOT / "cache"


class QueueMode(str, Enum):
    NONE = "none"
    FULL = "full"
    PARTIAL = "partial"


@dataclass
class AssetRequest:
    name: str
    animations: Optional[List[str]]


@dataclass
class LightRequest:
    name: str


def _resolve_path(raw: Optional[str], fallback: Path) -> Path:
    if not raw:
        return fallback
    candidate = Path(raw)
    if not candidate.is_absolute():
        candidate = (REPO_ROOT / candidate).resolve()
    return candidate


class RebuildQueue:
    """Encapsulates reading and mutating rebuild_requests.json."""

    def __init__(self, queue_path: Optional[Path] = None) -> None:
        self.path = Path(queue_path) if queue_path else DEFAULT_QUEUE_PATH
        self._data = self._load()

    @property
    def manifest_path(self) -> Path:
        raw = self._data.get("manifest_path")
        return _resolve_path(raw, DEFAULT_MANIFEST)

    @property
    def cache_root(self) -> Path:
        raw = self._data.get("cache_root")
        return _resolve_path(raw, DEFAULT_CACHE_ROOT)

    def asset_mode(self) -> QueueMode:
        assets = self._data.get("assets")
        if assets is None:
            return QueueMode.NONE
        if isinstance(assets, list) and len(assets) == 0:
            return QueueMode.FULL
        if isinstance(assets, list):
            return QueueMode.PARTIAL
        return QueueMode.NONE

    def light_mode(self) -> QueueMode:
        lights = self._data.get("lights")
        if lights is None:
            return QueueMode.NONE
        if isinstance(lights, list) and len(lights) == 0:
            return QueueMode.FULL
        if isinstance(lights, list):
            return QueueMode.PARTIAL
        return QueueMode.NONE

    def asset_requests(self) -> Dict[str, Optional[List[str]]]:
        assets = self._data.get("assets")
        if not isinstance(assets, list):
            return {}
        result: Dict[str, Optional[List[str]]] = {}
        for entry in assets:
            if not isinstance(entry, dict):
                continue
            name = entry.get("name")
            if not name:
                continue
            anims = entry.get("animations")
            if isinstance(anims, list):
                clean = [str(a) for a in anims if isinstance(a, str) and a]
                if len(anims) == 0:
                    result[name] = None
                elif clean:
                    result[name] = clean
                else:
                    result[name] = None
            else:
                result[name] = None
        return result

    def light_requests(self) -> List[str]:
        lights = self._data.get("lights")
        if not isinstance(lights, list):
            return []
        result: List[str] = []
        for entry in lights:
            if isinstance(entry, str) and entry:
                result.append(entry)
            elif isinstance(entry, dict):
                name = entry.get("name")
                if name:
                    result.append(name)
        return result

    def mark_animation_complete(self, asset_name: str, animation_id: str) -> None:
        assets = self._data.get("assets")
        if not isinstance(assets, list):
            return
        changed = False
        for entry in assets:
            if not isinstance(entry, dict):
                continue
            if entry.get("name") != asset_name:
                continue
            animations = entry.get("animations")
            if not isinstance(animations, list) or len(animations) == 0:
                continue
            before = len(animations)
            entry["animations"] = [anim for anim in animations if anim != animation_id]
            if len(entry["animations"]) != before:
                changed = True
            if len(entry["animations"]) == 0:
                changed = True
        if changed:
            self._prune_empty_asset_entries()
            self._write()

    def mark_asset_complete(self, asset_name: str) -> None:
        assets = self._data.get("assets")
        if not isinstance(assets, list):
            return
        original = len(assets)
        self._data["assets"] = [entry for entry in assets if not self._entry_matches(entry, asset_name)]
        if len(self._data["assets"]) != original:
            if len(self._data["assets"]) == 0:
                self._data["assets"] = None
            self._write()

    def mark_full_asset_rebuild_complete(self) -> None:
        if self.asset_mode() == QueueMode.FULL:
            self._data["assets"] = None
            self._write()

    def mark_light_complete(self, asset_name: str) -> None:
        lights = self._data.get("lights")
        if not isinstance(lights, list):
            return
        filtered = [entry for entry in lights if self._extract_light_name(entry) != asset_name]
        if len(filtered) != len(lights):
            self._data["lights"] = filtered if filtered else None
            self._write()

    def mark_full_light_rebuild_complete(self) -> None:
        if self.light_mode() == QueueMode.FULL:
            self._data["lights"] = None
            self._write()

    def drop_unknown_asset(self, asset_name: str) -> None:
        self.mark_asset_complete(asset_name)

    def _extract_light_name(self, entry) -> Optional[str]:
        if isinstance(entry, str):
            return entry
        if isinstance(entry, dict):
            value = entry.get("name")
            if isinstance(value, str) and value:
                return value
        return None

    def _entry_matches(self, entry, asset_name: str) -> bool:
        if not isinstance(entry, dict):
            return False
        return entry.get("name") == asset_name

    def _prune_empty_asset_entries(self) -> None:
        assets = self._data.get("assets")
        if not isinstance(assets, list):
            return
        updated = []
        for entry in assets:
            if not isinstance(entry, dict):
                continue
            animations = entry.get("animations")
            if isinstance(animations, list) and len(animations) == 0:
                continue
            updated.append(entry)
        if len(updated) != len(assets):
            self._data["assets"] = updated if updated else None

    def _load(self) -> Dict:
        if not self.path.exists():
            data = {
                "version": 1,
                "manifest_path": str(DEFAULT_MANIFEST.resolve()),
                "cache_root": str(DEFAULT_CACHE_ROOT.resolve()),
                "assets": None,
                "lights": None,
            }
            self._write_payload(data)
            return data
        try:
            with open(self.path, "r", encoding="utf-8") as fh:
                data = json.load(fh)
        except Exception:
            data = {}
        return self._normalize(data)

    def _normalize(self, data: Dict) -> Dict:
        if not isinstance(data, dict):
            data = {}
        data.setdefault("version", 1)
        data.setdefault("manifest_path", str(DEFAULT_MANIFEST.resolve()))
        data.setdefault("cache_root", str(DEFAULT_CACHE_ROOT.resolve()))
        if "assets" not in data or data["assets"] == []:
            assets = data.get("assets")
            if isinstance(assets, list) and len(assets) == 0:
                data["assets"] = []
            else:
                data["assets"] = None
        if "lights" not in data or data["lights"] == []:
            lights = data.get("lights")
            if isinstance(lights, list) and len(lights) == 0:
                data["lights"] = []
            else:
                data["lights"] = None
        return data

    def _write(self) -> None:
        self._write_payload(self._data)

    def _write_payload(self, payload: Dict) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        tmp_path = self.path.with_suffix(self.path.suffix + ".tmp")
        with open(tmp_path, "w", encoding="utf-8") as fh:
            json.dump(payload, fh, indent=2)
            fh.write("\n")
        os.replace(tmp_path, self.path)


__all__ = [
    "AssetRequest",
    "LightRequest",
    "QueueMode",
    "RebuildQueue",
]
