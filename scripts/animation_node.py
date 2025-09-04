#!/usr/bin/env python3
from __future__ import annotations
from typing import Dict, Any, List, Optional

import tkinter as tk
from tkinter import ttk

from base_node import BaseNode
from movement_modal import MovementModal


class AnimationNode(BaseNode):
    """Interactive node that edits a single animation payload (uses shared BaseNode)."""

    def __init__(self, canvas: tk.Canvas, anim_name: str, payload: Dict[str, Any], x: int = 60, y: int = 40):
        self.payload = self._coerce_payload(anim_name, payload)
        super().__init__(canvas, anim_name, anim_name, x, y)
        self._build_ui()
        self.request_layout()

    # ----- public API -------------------------------------------------
    def to_dict(self) -> Dict[str, Any]:
        return self.payload

    def set_payload(self, d: Dict[str, Any]):
        self.payload = self._coerce_payload(self.node_id, d)
        # update UI fields to reflect new payload
        self.kind_var.set(self.payload["source"]["kind"])
        self.path_var.set(self.payload["source"].get("path") or "")
        self.name_var.set(self.payload["source"].get("name") or "")
        self.flipped_var.set(bool(self.payload.get("flipped_source", False)))
        self.reversed_var.set(bool(self.payload.get("reverse_source", False)))
        self.locked_var.set(bool(self.payload.get("locked", False)))
        self.speed_var.set(int(self.payload.get("speed_factor", 1)))
        self.frames_var.set(int(self.payload.get("number_of_frames", 1)))
        if self.on_changed:
            self.on_changed(self.node_id, self.payload)

    # ----- UI construction -------------------------------------------
    def _build_ui(self):
        body = self.get_content_frame()

        # source
        sf = ttk.LabelFrame(body, text="Source")
        sf.pack(fill="x", pady=2)
        self.kind_var = tk.StringVar(value=self.payload["source"]["kind"])
        ttk.Radiobutton(sf, text="Folder", value="folder", variable=self.kind_var).grid(
            row=0, column=0, sticky="w", padx=4
        )
        ttk.Radiobutton(sf, text="Animation", value="animation", variable=self.kind_var).grid(
            row=0, column=1, sticky="w"
        )
        ttk.Label(sf, text="path").grid(row=1, column=0, sticky="e")
        self.path_var = tk.StringVar(value=self.payload["source"].get("path") or "")
        ttk.Entry(sf, textvariable=self.path_var, width=18).grid(row=1, column=1, sticky="w", padx=4)
        ttk.Label(sf, text="name").grid(row=2, column=0, sticky="e")
        self.name_var = tk.StringVar(value=self.payload["source"].get("name") or "")
        ttk.Entry(sf, textvariable=self.name_var, width=18).grid(row=2, column=1, sticky="w", padx=4)

        # playback
        pf = ttk.LabelFrame(body, text="Playback")
        pf.pack(fill="x", pady=2)
        self.flipped_var = tk.BooleanVar(value=bool(self.payload.get("flipped_source", False)))
        self.reversed_var = tk.BooleanVar(value=bool(self.payload.get("reverse_source", False)))
        self.locked_var = tk.BooleanVar(value=bool(self.payload.get("locked", False)))
        ttk.Checkbutton(pf, text="flipped", variable=self.flipped_var).grid(row=0, column=0, sticky="w")
        ttk.Checkbutton(pf, text="reverse", variable=self.reversed_var).grid(row=0, column=1, sticky="w")
        ttk.Checkbutton(pf, text="locked", variable=self.locked_var).grid(row=0, column=2, sticky="w")
        ttk.Label(pf, text="speed").grid(row=1, column=0, sticky="e")
        self.speed_var = tk.IntVar(value=int(self.payload.get("speed_factor", 1)))
        ttk.Spinbox(pf, from_=1, to=240, textvariable=self.speed_var, width=6).grid(
            row=1, column=1, sticky="w", padx=4
        )
        ttk.Label(pf, text="frames").grid(row=2, column=0, sticky="e")
        self.frames_var = tk.IntVar(value=int(self.payload.get("number_of_frames", 1)))
        ttk.Spinbox(pf, from_=1, to=9999, textvariable=self.frames_var, width=6).grid(
            row=2, column=1, sticky="w", padx=4
        )

        # movement (modal)
        mvf = ttk.LabelFrame(body, text="Movement")
        mvf.pack(fill="x", pady=2)
        ttk.Label(mvf, text="Edit per-frame movement vectors").grid(row=0, column=0, sticky="w", padx=4)
        ttk.Button(mvf, text="Edit Movement...", command=self._open_movement_modal).grid(
            row=0, column=1, sticky="e", padx=4
        )

        # actions
        ttk.Button(body, text="Save", command=self._apply_changes).pack(anchor="e", pady=4)

    # ----- movement modal --------------------------------------------
    def _open_movement_modal(self):
        parent = self.frame.winfo_toplevel()
        MovementModal(
            parent=parent,
            movement=self.payload.get("movement", []),
            frames_count=int(self.frames_var.get()),
            on_save=self._on_movement_saved,
            title=f"Movement: {self.node_id}",
        )

    def _on_movement_saved(self, new_movement: List[List[int]]):
        self.payload["movement"] = new_movement
        if self.on_changed:
            self.on_changed(self.node_id, self.payload)

    # ----- apply changes ---------------------------------------------
    def _apply_changes(self):
        payload = self.payload
        payload["source"]["kind"] = "animation" if self.kind_var.get() == "animation" else "folder"
        payload["source"]["path"] = (
            self.path_var.get().strip() if payload["source"]["kind"] == "folder" else ""
        )
        payload["source"]["name"] = (
            self.name_var.get().strip() or None if payload["source"]["kind"] == "animation" else None
        )
        payload["flipped_source"] = bool(self.flipped_var.get())
        payload["reverse_source"] = bool(self.reversed_var.get())
        payload["locked"] = bool(self.locked_var.get())
        payload["speed_factor"] = max(1, int(self.speed_var.get()))
        payload["number_of_frames"] = max(1, int(self.frames_var.get()))

        # ensure movement matches number_of_frames, keep first [0,0]
        mv = payload.get("movement", [])
        if not isinstance(mv, list):
            mv = []
        n = payload["number_of_frames"]
        if len(mv) < n:
            mv.extend([[0, 0] for _ in range(n - len(mv))])
        elif len(mv) > n:
            mv = mv[:n]
        if n >= 1:
            mv[0] = [0, 0]
        payload["movement"] = mv

        if self.on_changed:
            self.on_changed(self.node_id, payload)

    # ----- helpers ----------------------------------------------------
    @staticmethod
    def _coerce_payload(anim_name: str, p: Dict[str, Any]) -> Dict[str, Any]:
        p = dict(p or {})
        src = dict(p.get("source") or {})
        p["source"] = {
            "kind": src.get("kind", "folder"),
            "path": src.get("path", anim_name if src.get("kind", "folder") == "folder" else ""),
            "name": src.get("name", None if src.get("kind", "folder") == "folder" else anim_name),
        }
        p.setdefault("flipped_source", False)
        p.setdefault("reverse_source", False)
        p.setdefault("locked", False)
        p.setdefault("speed_factor", max(1, int(p.get("speed_factor", 1))))
        p.setdefault("number_of_frames", max(1, int(p.get("number_of_frames", 1))))
        mv = p.get("movement")
        n = p["number_of_frames"]
        if not isinstance(mv, list) or len(mv) < 1:
            mv = [[0, 0] for _ in range(n)]
        if len(mv) != n:
            if len(mv) < n:
                mv.extend([[0, 0] for _ in range(n - len(mv))])
            else:
                mv = mv[:n]
        mv[0] = [0, 0]
        p["movement"] = mv
        p.setdefault("on_end_mapping", "")
        return p
