#!/usr/bin/env python3
from __future__ import annotations
from typing import Callable, Dict, Any, Optional, Tuple

from pathlib import Path

try:
    from PIL import Image, ImageTk
except Exception:  # pragma: no cover
    Image = None
    ImageTk = None


class PreviewProvider:
    """
    Small helper to load a preview image (first frame) for animations.
    - Resolves folder source: base_dir / source.path
    - Resolves animation source: looks up referenced animation payload
    - Applies horizontal flip if flipped_source is set on either level
    - Returns a Tk PhotoImage ready for display
    """

    SUPPORTED_EXTS = (".png", ".jpg", ".jpeg", ".gif", ".bmp")

    def __init__(self, base_dir: Path, animation_lookup: Callable[[str], Optional[Dict[str, Any]]],
                 size: Tuple[int, int] = (72, 72)):
        self.base_dir = Path(base_dir)
        self.lookup = animation_lookup
        self.size = size

    def _find_first_image(self, folder: Path) -> Optional[Path]:
        try:
            if not folder.exists():
                return None
            files = sorted([p for p in folder.iterdir() if p.suffix.lower() in self.SUPPORTED_EXTS])
            return files[0] if files else None
        except Exception:
            return None

    def get_preview(self, anim_name: str, payload: Dict[str, Any]):
        if Image is None or ImageTk is None:
            return None  # PIL not available; caller should handle
        flip = bool(payload.get("flipped_source", False))
        src = payload.get("source") or {}
        kind = src.get("kind", "folder")
        img_path: Optional[Path] = None

        if kind == "folder":
            rel = src.get("path") or anim_name
            folder = (self.base_dir / rel).resolve()
            img_path = self._find_first_image(folder)
        elif kind == "animation":
            ref_name = src.get("name") or anim_name
            other = self.lookup(ref_name) or {}
            flip = flip or bool(other.get("flipped_source", False))
            other_src = (other.get("source") or {})
            if other_src.get("kind") == "folder":
                rel = other_src.get("path") or ref_name
                folder = (self.base_dir / rel).resolve()
                img_path = self._find_first_image(folder)
            else:
                # one level only
                rel = ref_name
                folder = (self.base_dir / rel).resolve()
                img_path = self._find_first_image(folder)
        else:
            return None

        if not img_path or not img_path.exists():
            return None

        try:
            im = Image.open(img_path)
            # scale to fit
            target_w, target_h = self.size
            im = im.convert("RGBA")
            im.thumbnail((target_w, target_h), Image.LANCZOS)
            if flip:
                im = im.transpose(Image.FLIP_LEFT_RIGHT)
            return ImageTk.PhotoImage(im)
        except Exception:
            return None

