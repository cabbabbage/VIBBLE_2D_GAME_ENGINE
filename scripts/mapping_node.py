#!/usr/bin/env python3
from __future__ import annotations
from typing import Dict, Any, List, Optional, Tuple, Callable

import tkinter as tk
from tkinter import ttk

from base_node import BaseNode


# ----------------------- helpers: geometry -----------------------
def _widget_center_y_in_canvas(canvas: tk.Canvas, w: tk.Widget) -> int:
    """Return the Y center of widget `w` in canvas coordinates."""
    w.update_idletasks()
    # screen coordinates
    wy = w.winfo_rooty()
    wh = w.winfo_height()
    cy = canvas.winfo_rooty()
    # convert from screen to canvas coords
    center_screen_y = wy + wh // 2
    return int(canvas.canvasy(center_screen_y - cy))


# ----------------------- payload coercion ------------------------
def _coerce_regular(payload: Any) -> Dict[str, Any]:
    out: Dict[str, Any] = {"type": "regular", "entries": []}
    # legacy support: list of {condition, map_to:{options:[{animation}]}}
    if isinstance(payload, list):
        for e in payload:
            cond = (e or {}).get("condition", "")
            out["entries"].append({"condition": cond})
        if not out["entries"]:
            out["entries"] = [{"condition": ""}]
        return out

    if isinstance(payload, dict) and payload.get("type") == "regular":
        out.update(payload)
        out.setdefault("entries", [{"condition": ""}])
        return out

    # fallback
    out["entries"] = [{"condition": ""}]
    return out


def _coerce_random(payload: Any) -> Dict[str, Any]:
    out: Dict[str, Any] = {"type": "random", "options": []}
    if isinstance(payload, dict) and payload.get("type") == "random":
        out.update(payload)
        out.setdefault("options", [{"percent": 100}])
        return out

    if isinstance(payload, list):
        # legacy → even distribution across discovered items count
        n = max(1, len(payload))
        even = 100 // n
        rem = 100 - even * n
        out["options"] = [{"percent": even + (1 if i < rem else 0)} for i in range(n)]
        return out

    # fallback
    out["options"] = [{"percent": 100}]
    return out


# ===================== Regular Mapping Node ======================
class RegularMappingNode(BaseNode):
    """
    Deterministic mapping:
      rows of (condition) and a PER-ROW OUTPUT PORT on the right.
      Each row maps to exactly one downstream node via an edge.
    """

    def __init__(self, canvas: tk.Canvas, mapping_id: str, payload: Any, x: int = 420, y: int = 60):
        self.payload = _coerce_regular(payload)
        super().__init__(canvas, mapping_id, mapping_id, x, y)

        # storage for row widgets and their per-row ports
        self._row_frames: List[ttk.Frame] = []
        self._row_ports: List[int] = []  # canvas item ids (ovals), kept in sync with _row_frames

        self._build_ui()
        # keep drawing row ports as layout changes
        self.get_content_frame().bind("<Configure>", lambda _e: self._redraw_row_ports())
        self.request_layout()

    # ----- public API -----
    def to_payload(self) -> Dict[str, Any]:
        return self.payload

    def set_payload(self, payload: Any):
        self.payload = _coerce_regular(payload)
        self._render_rows()
        if self.on_changed:
            self.on_changed(self.node_id, {"_mapping": self.payload})

    # ----- UI -----
    def _build_ui(self):
        body = self.get_content_frame()
        self._rows_container = ttk.Frame(body)
        self._rows_container.pack(fill="both", expand=True)
        self._render_rows()

        row = ttk.Frame(body)
        row.pack(fill="x", pady=4)
        ttk.Button(row, text="Add Rule", command=self._add_entry).pack(side="left", padx=2)

    def _render_rows(self):
        # clear widgets
        for w in self._rows_container.winfo_children():
            w.destroy()
        self._row_frames.clear()

        for idx, entry in enumerate(self.payload.get("entries", [])):
            f = ttk.LabelFrame(self._rows_container, text=f"Rule {idx+1}")
            f.pack(fill="x", padx=4, pady=4)
            self._row_frames.append(f)

            ttk.Label(f, text="condition").grid(row=0, column=0, sticky="e", padx=2)
            cond_var = tk.StringVar(value=entry.get("condition", ""))
            ent_c = ttk.Entry(f, textvariable=cond_var, width=40)
            ent_c.grid(row=0, column=1, sticky="w", padx=4)
            ent_c.bind("<FocusOut>", lambda _e, i=idx, v=cond_var: self._set_field(i, "condition", v.get()))

            ttk.Button(f, text="Delete", command=lambda i=idx: self._del_entry(i)).grid(row=0, column=2, padx=6)

        self.request_layout()
        self._redraw_row_ports()

    # ----- per-row ports -----
    def _redraw_row_ports(self):
        # remove old row ports
        for item in getattr(self, "_row_ports", []):
            try:
                self.canvas.delete(item)
            except Exception:
                pass
        self._row_ports = []

        # create a port aligned to each row's vertical center, on the right edge of the node
        x, y, w, h = self._current_win_geometry()  # BaseNode method
        r = self.PORT_RADIUS  # from BaseNode
        m = self.PORT_MARGIN

        for i, row in enumerate(self._row_frames):
            cy = _widget_center_y_in_canvas(self.canvas, row)
            cx_out = x + w - (m + r)
            item = self.canvas.create_oval(
                cx_out - r, cy - r, cx_out + r, cy + r,
                fill=self.COLOR_OUTPUT, outline=self.COLOR_PORT_OUTLINE, width=2
            )
            # bind click to start a connection from this specific row ("entry:{i}")
            self.canvas.tag_bind(item, "<Button-1>", lambda _e, idx=i: self._begin_connect_entry(idx))
            self._row_ports.append(item)

        # keep above edges/window
        for item in self._row_ports:
            self.canvas.tag_raise(item)

    def _begin_connect_entry(self, idx: int):
        # Encode the source slot with the node id so host can route properly.
        if self.on_begin_connect:
            self.on_begin_connect(f"{self.node_id}::entry:{idx}")

    def slot_output_port_center(self, slot: str) -> Tuple[Optional[int], Optional[int]]:
        try:
            kind, idx_str = slot.split(":", 1)
            idx = int(idx_str)
        except Exception:
            return (None, None)
        if kind != "entry" or not (0 <= idx < len(self._row_frames)):
            return (None, None)
        x, _, w, _ = self._current_win_geometry()
        r = self.PORT_RADIUS
        m = self.PORT_MARGIN
        cy = _widget_center_y_in_canvas(self.canvas, self._row_frames[idx])
        cx = x + w - (m + r)
        return (cx, cy)

    # ----- mutate data -----
    def _add_entry(self):
        self.payload.setdefault("entries", []).append({"condition": ""})
        self._notify_changed()
        self._render_rows()

    def _del_entry(self, idx: int):
        entries = self.payload.setdefault("entries", [])
        if 0 <= idx < len(entries):
            del entries[idx]
        self._notify_changed()
        self._render_rows()

    def _set_field(self, idx: int, key: str, value: str):
        entries = self.payload.setdefault("entries", [])
        if 0 <= idx < len(entries):
            entries[idx][key] = value
        self._notify_changed()

    def _notify_changed(self):
        if self.on_changed:
            self.on_changed(self.node_id, {"_mapping": self.payload})


# ===================== Random Mapping Node =======================
class RandomMappingNode(BaseNode):
    """
    Stochastic mapping:
      rows of (percent) and a PER-ROW OUTPUT PORT on the right.
      No conditions; downstream is selected by weights.
    """

    def __init__(self, canvas: tk.Canvas, mapping_id: str, payload: Any, x: int = 520, y: int = 60):
        self.payload = _coerce_random(payload)
        super().__init__(canvas, mapping_id, mapping_id, x, y)

        self._row_frames: List[ttk.Frame] = []
        self._row_ports: List[int] = []

        self._build_ui()
        self.get_content_frame().bind("<Configure>", lambda _e: self._redraw_row_ports())
        self.request_layout()

    # ----- public API -----
    def to_payload(self) -> Dict[str, Any]:
        return self.payload

    def set_payload(self, payload: Any):
        self.payload = _coerce_random(payload)
        self._render_opts()
        if self.on_changed:
            self.on_changed(self.node_id, {"_mapping": self.payload})

    # ----- UI -----
    def _build_ui(self):
        body = self.get_content_frame()
        self._opts_container = ttk.Frame(body)
        self._opts_container.pack(fill="both", expand=True)
        self._render_opts()

        row = ttk.Frame(body)
        row.pack(fill="x", pady=4)
        ttk.Button(row, text="Add Option", command=self._add_option).pack(side="left", padx=2)
        ttk.Button(row, text="Normalize 100", command=self._normalize).pack(side="left", padx=2)

    def _render_opts(self):
        for w in self._opts_container.winfo_children():
            w.destroy()
        self._row_frames.clear()

        # headers
        hrow = ttk.Frame(self._opts_container)
        hrow.pack(fill="x", padx=4, pady=(2, 0))
        ttk.Label(hrow, text="Percent").pack(side="left", padx=(0, 10))

        for i, opt in enumerate(self.payload.get("options", [])):
            f = ttk.Frame(self._opts_container)
            f.pack(fill="x", padx=4, pady=3)
            self._row_frames.append(f)

            pct_var = tk.IntVar(value=int(opt.get("percent", 0)))
            spin = ttk.Spinbox(f, from_=0, to=100, textvariable=pct_var, width=6)
            spin.pack(side="left")
            spin.bind("<FocusOut>", lambda _e, idx=i, v=pct_var: self._set_option(idx, "percent", int(v.get())))

            ttk.Button(f, text="Delete", command=lambda idx=i: self._del_option(idx)).pack(side="left", padx=6)

        self.request_layout()
        self._redraw_row_ports()

    # ----- per-row ports -----
    def _redraw_row_ports(self):
        for item in getattr(self, "_row_ports", []):
            try:
                self.canvas.delete(item)
            except Exception:
                pass
        self._row_ports = []

        x, y, w, h = self._current_win_geometry()
        r = self.PORT_RADIUS
        m = self.PORT_MARGIN

        for i, row in enumerate(self._row_frames):
            cy = _widget_center_y_in_canvas(self.canvas, row)
            cx_out = x + w - (m + r)
            item = self.canvas.create_oval(
                cx_out - r, cy - r, cx_out + r, cy + r,
                fill=self.COLOR_OUTPUT, outline=self.COLOR_PORT_OUTLINE, width=2
            )
            self.canvas.tag_bind(item, "<Button-1>", lambda _e, idx=i: self._begin_connect_option(idx))
            self._row_ports.append(item)

        for item in self._row_ports:
            self.canvas.tag_raise(item)

    def _begin_connect_option(self, idx: int):
        if self.on_begin_connect:
            self.on_begin_connect(f"{self.node_id}::option:{idx}")

    def slot_output_port_center(self, slot: str) -> Tuple[Optional[int], Optional[int]]:
        try:
            kind, idx_str = slot.split(":", 1)
            idx = int(idx_str)
        except Exception:
            return (None, None)
        if kind != "option" or not (0 <= idx < len(self._row_frames)):
            return (None, None)
        x, _, w, _ = self._current_win_geometry()
        r = self.PORT_RADIUS
        m = self.PORT_MARGIN
        cy = _widget_center_y_in_canvas(self.canvas, self._row_frames[idx])
        cx = x + w - (m + r)
        return (cx, cy)

    # ----- mutate data -----
    def _add_option(self):
        self.payload.setdefault("options", []).append({"percent": 0})
        self._notify_changed()
        self._render_opts()

    def _del_option(self, idx: int):
        opts = self.payload.setdefault("options", [])
        if 0 <= idx < len(opts):
            del opts[idx]
        self._notify_changed()
        self._render_opts()

    def _set_option(self, idx: int, key: str, value: Any):
        opts = self.payload.setdefault("options", [])
        if 0 <= idx < len(opts):
            if key == "percent":
                try:
                    value = int(value)
                except Exception:
                    value = 0
            opts[idx][key] = value
        self._notify_changed()

    def _normalize(self):
        opts = self.payload.setdefault("options", [])
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
        self._render_opts()

    def _notify_changed(self):
        if self.on_changed:
            self.on_changed(self.node_id, {"_mapping": self.payload})
