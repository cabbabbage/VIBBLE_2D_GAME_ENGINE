# map_modal.py
import tkinter as tk
from tkinter import ttk
from typing import Any, Callable, Dict, List, Optional, Tuple

class _BaseNode:
    def __init__(self, canvas: tk.Canvas, node_id: str, title: str, x: int = 50, y: int = 50, width: int = 220, height: int = 80):
        self.canvas = canvas
        self.node_id = node_id
        self.title = title
        self.x = x
        self.y = y
        self.w = width
        self.h = height
        self.on_begin_connect: Optional[Callable[[str], None]] = None
        self.on_end_connect: Optional[Callable[[str], None]] = None
        self.on_changed: Optional[Callable[[str, Dict[str, Any]], None]] = None
        self.on_moved: Optional[Callable[[str, int, int], None]] = None
        self._rect = None
        self._header = None
        self._port_in = None
        self._port_out = None
        self._selected = False
        self._dragging = False
        self._drag_off: Tuple[int, int] = (0, 0)
        self.draw()

    def draw(self):
        self.delete()
        fill = self._fill_color()
        outline = "#1e272e"
        self._rect = self.canvas.create_rectangle(self.x, self.y, self.x + self.w, self.y + self.h, fill=fill, outline=outline, width=2)
        self._header = self.canvas.create_text(self.x + 8, self.y + 12, anchor="w", fill="white", font=("Arial", 10, "bold"), text=self._header_text())
        for cid in (self._rect, self._header):
            self.canvas.tag_bind(cid, "<Button-1>", self._on_click)
            self.canvas.tag_bind(cid, "<B1-Motion>", self._on_drag)
            self.canvas.tag_bind(cid, "<ButtonRelease-1>", self._on_release)
            self.canvas.tag_bind(cid, "<Double-Button-1>", self._on_open_modal)
        self._draw_ports()

    def delete(self):
        for cid in (self._rect, self._header, self._port_in, self._port_out):
            if cid:
                self.canvas.delete(cid)
        self._rect = self._header = self._port_in = self._port_out = None

    def set_selected(self, sel: bool):
        self._selected = sel
        self.draw()

    def _fill_color(self) -> str:
        return "#2f3640" if not self._selected else "#414b57"

    def _header_text(self) -> str:
        return self.title

    def _draw_ports(self):
        pass

    def output_port_center(self) -> Optional[Tuple[int, int]]:
        return None

    def input_port_center(self) -> Optional[Tuple[int, int]]:
        return None

    def _on_open_modal(self, _evt=None):
        self.open_modal()

    def open_modal(self):
        raise NotImplementedError

    def _on_click(self, evt):
        self._dragging = True
        self._drag_off = (evt.x - self.x, evt.y - self.y)

    def _on_drag(self, evt):
        if not self._dragging:
            return
        self.x = evt.x - self._drag_off[0]
        self.y = evt.y - self._drag_off[1]
        self.draw()

    def _on_release(self, _evt):
        self._dragging = False
        if self.on_moved:
            self.on_moved(self.node_id, self.x, self.y)

class MapModal(_BaseNode):
    def __init__(self, canvas: tk.Canvas, mapping_id: str, payload: List[Dict[str, Any]], x=400, y=50):
        self.payload = self._coerce_payload(payload)
        super().__init__(canvas, mapping_id, f"Map: {mapping_id}", x, y)

    def to_list(self) -> List[Dict[str, Any]]:
        return self.payload

    def set_payload(self, lst: List[Dict[str, Any]]):
        self.payload = self._coerce_payload(lst)
        if self.on_changed:
            self.on_changed(self.node_id, {"_mapping": self.payload})

    def _fill_color(self) -> str:
        return "#8e44ad" if not self._selected else "#9b59b6"

    def _draw_ports(self):
        r = 6
        px = self.x + r + 6
        py = self.y + self.h // 2
        self._port_in = self.canvas.create_oval(px - r, py - r, px + r, py + r, fill="#e1b12c", outline="#2f3640", width=2)
        self.canvas.tag_bind(self._port_in, "<Button-1>", self._on_end_connect)

    def input_port_center(self) -> Optional[Tuple[int, int]]:
        return (self.x + 12, self.y + self.h // 2)

    def _on_end_connect(self, _evt=None):
        if self.on_end_connect:
            self.on_end_connect(self.node_id)

    def open_modal(self):
        if hasattr(self, "_dlg") and self._dlg and tk.Toplevel.winfo_exists(self._dlg):
            self._dlg.lift(); return
        self._dlg = tk.Toplevel(self.canvas)
        self._dlg.title(f"Mapping: {self.node_id}")
        self._dlg.geometry("560x520")
        root = ttk.Frame(self._dlg); root.pack(fill="both", expand=True, padx=8, pady=8)
        self._entries_frame = ttk.Frame(root); self._entries_frame.pack(fill="both", expand=True)
        self._render_entries()
        btns = ttk.Frame(root); btns.pack(fill="x", pady=6)
        ttk.Button(btns, text="Add Entry", command=self._add_entry).pack(side="left")
        ttk.Button(btns, text="Close", command=self._dlg.destroy).pack(side="right", padx=4)

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
            opt_frame = ttk.Frame(frame); opt_frame.grid(row=1, column=0, columnspan=2, sticky="we", pady=2)
            ttk.Label(opt_frame, text="Animation").grid(row=0, column=0, padx=2)
            ttk.Label(opt_frame, text="Percent").grid(row=0, column=1, padx=2)
            options = entry.setdefault("map_to", {}).setdefault("options", [])
            for oi, opt in enumerate(options):
                anim_var = tk.StringVar(value=str(opt.get("animation", "")))
                pct_var = tk.IntVar(value=int(opt.get("percent", 0)))
                anim_entry = ttk.Entry(opt_frame, textvariable=anim_var, width=32)
                anim_entry.grid(row=oi+1, column=0, padx=2, pady=1, sticky="w")
                anim_entry.bind("<FocusOut>", lambda _e, i=idx, o=opt, v=anim_var: self._set_option(i, o, "animation", v.get()))
                pct_spin = ttk.Spinbox(opt_frame, from_=0, to=100, textvariable=pct_var, width=6)
                pct_spin.grid(row=oi+1, column=1, padx=2, pady=1, sticky="w")
                pct_spin.bind("<FocusOut>", lambda _e, i=idx, o=opt, v=pct_var: self._set_option(i, o, "percent", int(v.get())))
            row_btns = ttk.Frame(frame); row_btns.grid(row=2, column=0, columnspan=2, sticky="e", pady=2)
            ttk.Button(row_btns, text="Add Option", command=lambda i=idx: self._add_option(i)).pack(side="left", padx=2)
            ttk.Button(row_btns, text="Normalize 100", command=lambda i=idx: self._normalize(i)).pack(side="left", padx=2)
            ttk.Button(row_btns, text="Delete Entry", command=lambda i=idx: self._del_entry(i)).pack(side="left", padx=6)

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
        self.payload[idx].setdefault("map_to", {}).setdefault("options", []).append({"animation": "idle", "percent": 0})
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
