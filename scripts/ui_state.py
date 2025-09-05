#!/usr/bin/env python3
from __future__ import annotations
from typing import Any, Dict, Optional

import copy


class HistoryManager:
    """Keeps a stack of deep-copied snapshots for undo support."""

    def __init__(self, limit: int = 200):
        self._stack: list[Dict[str, Any]] = []
        self._limit = max(1, int(limit))

    def snapshot(self, data: Dict[str, Any]) -> None:
        try:
            snap = copy.deepcopy(data)
        except Exception:
            # fall back to shallow copy if deepcopy fails
            snap = dict(data or {})
        self._stack.append(snap)
        if len(self._stack) > self._limit:
            # drop oldest
            self._stack = self._stack[-self._limit :]

    def can_undo(self) -> bool:
        return len(self._stack) > 0

    def undo(self) -> Optional[Dict[str, Any]]:
        if not self._stack:
            return None
        try:
            last = self._stack.pop()
        except Exception:
            return None
        return last


class ViewStateManager:
    """Capture and restore window/canvas view state (geometry, zoom, pan)."""

    def capture(self, win, canvas) -> Dict[str, Any]:
        try:
            geom = win.geometry()
        except Exception:
            geom = None
        try:
            zoom = float(getattr(canvas, "_zoom", 1.0))
        except Exception:
            zoom = 1.0
        try:
            xv = float(canvas.xview()[0])
            yv = float(canvas.yview()[0])
        except Exception:
            xv = 0.0
            yv = 0.0
        return {
            "geometry": geom,
            "zoom": zoom,
            "xview": xv,
            "yview": yv,
        }

    def apply(self, win, canvas, state: Dict[str, Any]) -> None:
        if not isinstance(state, dict):
            return
        try:
            geom = state.get("geometry")
            if geom:
                win.geometry(str(geom))
        except Exception:
            pass

        try:
            target_zoom = float(state.get("zoom", 1.0))
        except Exception:
            target_zoom = 1.0

        try:
            current_zoom = float(getattr(canvas, "_zoom", 1.0))
        except Exception:
            current_zoom = 1.0

        try:
            # adjust zoom at canvas center
            if target_zoom > 0 and abs(target_zoom - current_zoom) > 1e-6:
                w = max(1, int(canvas.winfo_width()))
                h = max(1, int(canvas.winfo_height()))
                factor = target_zoom / current_zoom
                # Call the canvas zoom method if available
                zoom_at = getattr(canvas, "_zoom_at", None)
                if callable(zoom_at):
                    zoom_at(factor, w // 2, h // 2)
                else:
                    setattr(canvas, "_zoom", target_zoom)
        except Exception:
            pass

        try:
            xv = float(state.get("xview", 0.0))
            yv = float(state.get("yview", 0.0))
            xv = min(max(0.0, xv), 1.0)
            yv = min(max(0.0, yv), 1.0)
            canvas.xview_moveto(xv)
            canvas.yview_moveto(yv)
        except Exception:
            pass

