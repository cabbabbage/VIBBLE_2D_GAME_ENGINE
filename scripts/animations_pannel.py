#!/usr/bin/env python3
from __future__ import annotations
from typing import Any, Dict, List, Optional, Callable

import tkinter as tk
from tkinter import ttk

from movement_modal import MovementModal
from sources_element_pannel import SourcesElementPanel
from custom_updates_panel import CustomUpdatesPanel


class AnimationsPanel:
    """
    Composed panel for editing a single animation.

    - Header: ID (rename), Delete, preview thumbnail
    - SourcesElementPanel (replaces embedded sources UI)
    - Playback
    - Movement (modal)
    - On End (new)

    Callbacks:
      - on_changed(node_id, payload)
      - on_renamed(old_id, new_id)
      - on_delete(node_id)
    """

    def __init__(
        self,
        parent: tk.Widget,
        anim_id: str,
        payload: Dict[str, Any],
        *,
        on_changed: Optional[Callable[[str, Dict[str, Any]], None]] = None,
        on_renamed: Optional[Callable[[str, str], None]] = None,
        on_delete: Optional[Callable[[str], None]] = None,
        preview_provider: Any = None,
        asset_folder: Optional[str] = None,
        list_animation_names: Optional[Callable[[], List[str]]] = None,
    ) -> None:
        self.parent = parent
        self.node_id = str(anim_id)
        self.on_changed = on_changed
        self.on_renamed = on_renamed
        self.on_delete = on_delete
        self.preview_provider = preview_provider
        self.asset_folder = asset_folder
        self.list_animation_names = list_animation_names or (lambda: [])

        self.payload = self._coerce_payload(self.node_id, payload)

        # Use tk.LabelFrame so we can control border thickness reliably
        self.frame = tk.LabelFrame(parent, text=self.node_id, bd=6, relief=tk.GROOVE)
        self._build_ui()
        self._refresh_preview()

    # ----- UI -----
    def _build_ui(self) -> None:
        header = ttk.Frame(self.frame)
        header.grid(row=0, column=0, sticky="ew", padx=8, pady=(6, 8))
        self.frame.columnconfigure(0, weight=1)

        id_font = ("Segoe UI", 14, "bold")
        ttk.Label(header, text="ID:", font=id_font).pack(side="left")
        self.id_var = tk.StringVar(value=self.node_id)
        # Use tk.Entry to ensure custom font applies across themes
        id_entry = tk.Entry(header, textvariable=self.id_var, width=24, font=id_font)
        id_entry.pack(side="left", padx=(4, 10))
        id_entry.bind("<FocusOut>", self._commit_rename)
        id_entry.bind("<Return>", self._commit_rename)

        ttk.Button(header, text="Delete", command=self._do_delete).pack(side="right")

        self._preview_label = tk.Label(header, bd=0, highlightthickness=0, background="#2d3436")
        self._preview_label.pack(side="right", padx=6)
        self._preview_img = None

        body = ttk.Frame(self.frame)
        body.grid(row=1, column=0, sticky="nsew", padx=8, pady=(0, 8))
        self.frame.rowconfigure(1, weight=1)

        # custom animation controller panel (under preview, before sources)
        self.custom_panel = CustomUpdatesPanel(
            body,
            self.payload,
            on_changed=self._apply_changes,
            asset_folder=self.asset_folder or "",
            get_current_name=lambda: self.node_id,
        )
        self.custom_panel.get_frame().pack(fill="x", pady=4)

        # source sub-panel
        self.sources_panel = SourcesElementPanel(
            body,
            self.payload.get("source", {}),
            on_changed=self._apply_changes,
            asset_folder=self.asset_folder,
            get_current_name=lambda: self.node_id,
            list_animation_names=self.list_animation_names,
        )
        self.sources_panel.get_frame().pack(fill="x", pady=4)

        # playback
        pf = ttk.LabelFrame(body, text="Playback")
        pf.pack(fill="x", pady=4)
        self.flipped_var = tk.BooleanVar(value=bool(self.payload.get("flipped_source", False)))
        self.reversed_var = tk.BooleanVar(value=bool(self.payload.get("reverse_source", False)))
        self.locked_var = tk.BooleanVar(value=bool(self.payload.get("locked", False)))
        ttk.Checkbutton(pf, text="flipped", variable=self.flipped_var, command=self._apply_changes).grid(row=0, column=0, sticky="w")
        ttk.Checkbutton(pf, text="reverse", variable=self.reversed_var, command=self._apply_changes).grid(row=0, column=1, sticky="w")
        ttk.Checkbutton(pf, text="locked", variable=self.locked_var, command=self._apply_changes).grid(row=0, column=2, sticky="w")
        ttk.Label(pf, text="speed").grid(row=1, column=0, sticky="e")
        self.speed_var = tk.IntVar(value=int(self.payload.get("speed_factor", 1)))
        spin_speed = ttk.Spinbox(pf, from_=1, to=240, textvariable=self.speed_var, width=8, command=self._apply_changes)
        spin_speed.grid(row=1, column=1, sticky="w", padx=4)
        spin_speed.bind("<FocusOut>", lambda _e: self._apply_changes())
        ttk.Label(pf, text="frames").grid(row=2, column=0, sticky="e")
        self.frames_var = tk.IntVar(value=int(self.payload.get("number_of_frames", 1)))
        spin_frames = ttk.Spinbox(pf, from_=1, to=9999, textvariable=self.frames_var, width=8, command=self._apply_changes)
        spin_frames.grid(row=2, column=1, sticky="w", padx=4)
        spin_frames.bind("<FocusOut>", lambda _e: self._apply_changes())

        # movement
        mvf = ttk.LabelFrame(body, text="Movement")
        mvf.pack(fill="x", pady=4)
        ttk.Label(mvf, text="Edit per-frame movement vectors").grid(row=0, column=0, sticky="w", padx=4)
        ttk.Button(mvf, text="Edit Movement...", command=self._open_movement_modal).grid(row=0, column=1, sticky="e", padx=4)

        # on end (dropdown with built-ins + animations)
        oef = ttk.LabelFrame(body, text="On End")
        oef.pack(fill="x", pady=4)
        self.on_end_var = tk.StringVar(value=str(self.payload.get("on_end", "default") or "default"))
        self.on_end_combo = ttk.Combobox(oef, textvariable=self.on_end_var, state="readonly", width=36)
        self.on_end_combo.pack(side="left", padx=6, pady=4)
        self.on_end_combo.bind("<<ComboboxSelected>>", lambda _e: self._apply_changes())
        self._refresh_on_end_options()

    # ----- public API -----
    def get_frame(self) -> ttk.Frame:
        return self.frame

    def set_payload(self, new_payload: Dict[str, Any]) -> None:
        self.payload = self._coerce_payload(self.node_id, new_payload)
        # sync sub-panels + vars
        self.sources_panel.set_values(self.payload.get("source", {}))
        try:
            self.custom_panel.set_values(self.payload)
        except Exception:
            pass
        self.flipped_var.set(bool(self.payload.get("flipped_source", False)))
        self.reversed_var.set(bool(self.payload.get("reverse_source", False)))
        self.locked_var.set(bool(self.payload.get("locked", False)))
        self.speed_var.set(int(self.payload.get("speed_factor", 1)))
        self.frames_var.set(int(self.payload.get("number_of_frames", 1)))
        self.on_end_var.set(str(self.payload.get("on_end", "default") or "default"))
        self._refresh_on_end_options()
        self._refresh_preview()
        if self.on_changed:
            self.on_changed(self.node_id, self.payload)

    # ----- internal -----
    def _commit_rename(self, _evt=None):
        new_id = self.id_var.get().strip()
        if not new_id or new_id == self.node_id:
            self.id_var.set(self.node_id)
            return
        old = self.node_id
        self.node_id = new_id
        try:
            self.frame.configure(text=new_id)
        except Exception:
            pass
        # refresh any lists that depend on id
        try:
            self._refresh_on_end_options()
        except Exception:
            pass
        if self.on_renamed:
            self.on_renamed(old, new_id)

    def _do_delete(self):
        if self.on_delete:
            self.on_delete(self.node_id)

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
        self._refresh_preview()

    def _apply_changes(self):
        payload = self.payload
        payload["source"] = self.sources_panel.read_values()
        payload["flipped_source"] = bool(self.flipped_var.get())
        payload["reverse_source"] = bool(self.reversed_var.get())
        payload["locked"] = bool(self.locked_var.get())
        payload["speed_factor"] = max(1, int(self.speed_var.get()))
        payload["number_of_frames"] = max(1, int(self.frames_var.get()))
        sel = str(self.on_end_var.get() or "default")
        payload["on_end"] = sel

        # movement normalization
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

        # custom updates
        try:
            payload.update(self.custom_panel.get_values())
        except Exception:
            pass

        if self.on_changed:
            self.on_changed(self.node_id, payload)
        self._refresh_preview()

    def _refresh_preview(self):
        provider = self.preview_provider
        if not provider:
            return
        try:
            img = provider.get_preview(self.node_id, self.payload)
            self._preview_img = img
            if img is not None:
                self._preview_label.configure(image=img)
        except Exception:
            pass

    # ----- helpers -----
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
        try:
            p.setdefault("speed_factor", max(1, int(p.get("speed_factor", 1))))
        except Exception:
            p.setdefault("speed_factor", 1)
        try:
            p.setdefault("number_of_frames", max(1, int(p.get("number_of_frames", 1))))
        except Exception:
            p.setdefault("number_of_frames", 1)
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
        val = p.get("on_end")
        if not val:
            val = "default"
        p["on_end"] = str(val)
        # migrate legacy custom update keys to new custom animation controller keys
        if "has_custom_animation_controller" not in p:
            p["has_custom_animation_controller"] = bool(
                p.get("has_custom_animation_controller", p.get("has_custom_tick_update", False))
            )
        if "custom_animation_controller_hpp_path" not in p:
            p["custom_animation_controller_hpp_path"] = str(
                p.get("custom_animation_controller_hpp_path", p.get("custom_update_hpp_path", "")) or ""
            )
        if "custom_animation_controller_key" not in p:
            p["custom_animation_controller_key"] = str(
                p.get("custom_animation_controller_key", p.get("custom_update_key", "")) or ""
            )
        return p

    def _refresh_on_end_options(self):
        try:
            names = sorted(self.list_animation_names())
        except Exception:
            names = []
        base = ["default", "loop", "reverse", "end"]
        # merge and deduplicate while preserving base order
        seen = set()
        values = []
        for x in base + names:
            if x in seen:
                continue
            seen.add(x)
            values.append(x)
        try:
            self.on_end_combo["values"] = values
        except Exception:
            pass
        # ensure current selection is valid
        cur = str(self.on_end_var.get() or "default")
        if cur not in values:
            self.on_end_var.set("default")
