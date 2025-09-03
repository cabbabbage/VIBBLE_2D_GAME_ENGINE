"""MapperNode - in-canvas UI element for editing mappings.

All controls that were previously shown in a pop-up modal are now embedded
directly inside a frame on the main canvas.
"""

from __future__ import annotations

import tkinter as tk
from tkinter import ttk
from typing import Any, Dict, List, Optional, Tuple

from animation_node import _BaseNode


class MapperNode(_BaseNode):
    """Interactive node that edits a mapping payload."""

    def __init__(self, canvas: tk.Canvas, mapping_id: str, payload: List[Dict[str, Any]], x=400, y=50):
        self.payload = self._coerce_payload(payload)
        super().__init__(canvas, mapping_id, f"Map: {mapping_id}", x, y)
        self._build_ui()
        self._update_size_and_ports()

    # ----- public API -------------------------------------------------
    def to_list(self) -> List[Dict[str, Any]]:
        return self.payload

    def set_payload(self, lst: List[Dict[str, Any]]):
        self.payload = self._coerce_payload(lst)
        self._render_entries()
        if self.on_changed:
            self.on_changed(self.node_id, {"_mapping": self.payload})

    # ----- UI ---------------------------------------------------------
    def _build_ui(self):
        self._entries_frame = ttk.Frame(self.body)
        self._entries_frame.pack(fill="both", expand=True)
        self._render_entries()
        ttk.Button(self.body, text="Add Entry", command=self._add_entry).pack(anchor="w", pady=4)

    def _render_entries(self):
        for w in self._entries_frame.winfo_children():
            w.destroy()

        for idx, entry in enumerate(self.payload):
            frame = ttk.LabelFrame(self._entries_frame, text=f"Entry {idx+1}")
            frame.pack(fill="x", padx=4, pady=4)

            ttk.Label(frame, text="condition").grid(row=0, column=0, sticky="e", padx=2)
            cond_var = tk.StringVar(value=entry.get("condition", ""))
            cond_ent = ttk.Entry(frame, textvariable=cond_var, width=48)
            cond_ent.grid(row=0, column=1, sticky="w", padx=4)
            cond_ent.bind("<FocusOut>", lambda _e, i=idx, v=cond_var: self._set_condition(i, v.get()))

            opt_frame = ttk.Frame(frame)
            opt_frame.grid(row=1, column=0, columnspan=2, sticky="we", pady=2)
            ttk.Label(opt_frame, text="Animation").grid(row=0, column=0, padx=2)
            ttk.Label(opt_frame, text="Percent").grid(row=0, column=1, padx=2)

            options = entry.setdefault("map_to", {}).setdefault("options", [])
            for oi, opt in enumerate(options):
                anim_var = tk.StringVar(value=str(opt.get("animation", "")))
                pct_var = tk.IntVar(value=int(opt.get("percent", 0)))

                anim_entry = ttk.Entry(opt_frame, textvariable=anim_var, width=32)
                anim_entry.grid(row=oi + 1, column=0, padx=2, pady=1, sticky="w")
                anim_entry.bind(
                    "<FocusOut>",
                    lambda _e, i=idx, o=opt, v=anim_var: self._set_option(i, o, "animation", v.get()),
                )

                pct_spin = ttk.Spinbox(opt_frame, from_=0, to=100, textvariable=pct_var, width=6)
                pct_spin.grid(row=oi + 1, column=1, padx=2, pady=1, sticky="w")
                pct_spin.bind(
                    "<FocusOut>",
                    lambda _e, i=idx, o=opt, v=pct_var: self._set_option(i, o, "percent", int(v.get())),
                )

            row_btns = ttk.Frame(frame)
            row_btns.grid(row=2, column=0, columnspan=2, sticky="e", pady=2)
            ttk.Button(row_btns, text="Add Option", command=lambda i=idx: self._add_option(i)).pack(
                side="left", padx=2
            )
            ttk.Button(row_btns, text="Normalize 100", command=lambda i=idx: self._normalize(i)).pack(
                side="left", padx=2
            )
            ttk.Button(row_btns, text="Delete Entry", command=lambda i=idx: self._del_entry(i)).pack(
                side="left", padx=6
            )

        self._update_size_and_ports()

    # ----- entry operations -----------------------------------------
    def _add_entry(self):
        self.payload.append({"condition": "", "map_to": {"options": [{"animation": "idle", "percent": 100}]}})
        self._notify_changed()
        self._render_entries()

    def _del_entry(self, idx: int):
        if 0 <= idx < len(self.payload):
            del self.payload[idx]
            self._notify_changed()
            self._render_entries()

    def _set_condition(self, idx: int, value: str):
        self.payload[idx]["condition"] = value
        self._notify_changed()

    def _add_option(self, idx: int):
        self.payload[idx].setdefault("map_to", {}).setdefault("options", []).append(
            {"animation": "idle", "percent": 0}
        )
        self._notify_changed()
        self._render_entries()

    def _set_option(self, idx: int, opt: Dict[str, Any], key: str, value: Any):
        opt[key] = value
        self._notify_changed()

    def _normalize(self, idx: int):
        opts = self.payload[idx].setdefault("map_to", {}).setdefault("options", [])
        s = sum(int(o.get("percent", 0)) for o in opts)
        if s <= 0 and len(opts) > 0:
            even = 100 // len(opts)
            rem = 100 - even * len(opts)
            for i, o in enumerate(opts):
                o["percent"] = even + (1 if i < rem else 0)
        elif s != 100 and s > 0:
            for o in opts:
                p = int(o.get("percent", 0))
                o["percent"] = max(0, round(p * 100 / s))
            drift = 100 - sum(int(o["percent"]) for o in opts)
            if drift != 0 and opts:
                opts[0]["percent"] += drift
        self._notify_changed()
        self._render_entries()

    def _notify_changed(self):
        if self.on_changed:
            self.on_changed(self.node_id, {"_mapping": self.payload})

    # ----- ports ------------------------------------------------------
    def _draw_ports(self):
        if self._port_in:
            self.canvas.delete(self._port_in)
        r = 6
        px = self.x + r + 6
        py = self.y + self.h // 2
        self._port_in = self.canvas.create_oval(
            px - r, py - r, px + r, py + r, fill="#e1b12c", outline="#2f3640", width=2
        )
        self.canvas.tag_bind(self._port_in, "<Button-1>", self._on_end_connect)

    def input_port_center(self) -> Optional[Tuple[int, int]]:
        return (self.x + 12, self.y + self.h // 2)

    def _on_end_connect(self, _evt=None):
        if self.on_end_connect:
            self.on_end_connect(self.node_id)

    # ----- payload coercion -----------------------------------------
    @staticmethod
    def _coerce_payload(lst: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        out: List[Dict[str, Any]] = []
        if not isinstance(lst, list):
            return [{"condition": "", "map_to": {"options": [{"animation": "idle", "percent": 100}]}}]
        for entry in lst:
            e = dict(entry or {})
            e.setdefault("condition", "")
            mt = e.setdefault("map_to", {})
            opts = mt.setdefault("options", [])
            if not isinstance(opts, list) or not opts:
                mt["options"] = [{"animation": "idle", "percent": 100}]
            out.append(e)
        return out

