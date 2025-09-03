# animation_modal.py
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

class AnimationModal(_BaseNode):
    def __init__(self, canvas: tk.Canvas, anim_name: str, payload: Dict[str, Any], x=50, y=50):
        self.payload = self._coerce_payload(anim_name, payload)
        super().__init__(canvas, anim_name, f"Anim: {anim_name}", x, y)

    def to_dict(self) -> Dict[str, Any]:
        return self.payload

    def set_payload(self, d: Dict[str, Any]):
        self.payload = self._coerce_payload(self.node_id, d)
        if self.on_changed:
            self.on_changed(self.node_id, self.payload)

    def _fill_color(self) -> str:
        return "#273c75" if not self._selected else "#40739e"

    def _draw_ports(self):
        r = 6
        px = self.x + self.w - r - 6
        py = self.y + self.h // 2
        self._port_out = self.canvas.create_oval(px - r, py - r, px + r, py + r, fill="#44bd32", outline="#2f3640", width=2)
        self.canvas.tag_bind(self._port_out, "<Button-1>", self._on_begin_connect)

    def output_port_center(self) -> Optional[Tuple[int, int]]:
        return (self.x + self.w - 12, self.y + self.h // 2)

    def _on_begin_connect(self, _evt=None):
        if self.on_begin_connect:
            self.on_begin_connect(self.node_id)

    def open_modal(self):
        if hasattr(self, "_dlg") and self._dlg and tk.Toplevel.winfo_exists(self._dlg):
            self._dlg.lift(); return
        self._dlg = tk.Toplevel(self.canvas)
        self._dlg.title(f"Animation: {self.node_id}")
        self._dlg.geometry("420x520")
        frm = ttk.Frame(self._dlg); frm.pack(fill="both", expand=True, padx=8, pady=8)
        sf = ttk.LabelFrame(frm, text="Source"); sf.pack(fill="x", pady=6)
        kind = tk.StringVar(value=self.payload["source"]["kind"])
        ttk.Radiobutton(sf, text="Folder", value="folder", variable=kind).grid(row=0, column=0, sticky="w", padx=4)
        ttk.Radiobutton(sf, text="Animation", value="animation", variable=kind).grid(row=0, column=1, sticky="w")
        ttk.Label(sf, text="path").grid(row=1, column=0, sticky="e")
        path_var = tk.StringVar(value=self.payload["source"].get("path") or "")
        ttk.Entry(sf, textvariable=path_var, width=32).grid(row=1, column=1, sticky="w", padx=4)
        ttk.Label(sf, text="name").grid(row=2, column=0, sticky="e")
        name_var = tk.StringVar(value=self.payload["source"].get("name") or "")
        ttk.Entry(sf, textvariable=name_var, width=32).grid(row=2, column=1, sticky="w", padx=4)
        pf = ttk.LabelFrame(frm, text="Playback"); pf.pack(fill="x", pady=6)
        flipped = tk.BooleanVar(value=bool(self.payload.get("flipped_source", False)))
        reversed_ = tk.BooleanVar(value=bool(self.payload.get("reverse_source", False)))
        locked = tk.BooleanVar(value=bool(self.payload.get("locked", False)))
        ttk.Checkbutton(pf, text="flipped_source", variable=flipped).grid(row=0, column=0, sticky="w")
        ttk.Checkbutton(pf, text="reverse_source", variable=reversed_).grid(row=0, column=1, sticky="w")
        ttk.Checkbutton(pf, text="locked", variable=locked).grid(row=0, column=2, sticky="w")
        ttk.Label(pf, text="speed_factor").grid(row=1, column=0, sticky="e")
        speed = tk.IntVar(value=int(self.payload.get("speed_factor", 1)))
        ttk.Spinbox(pf, from_=1, to=240, textvariable=speed, width=6).grid(row=1, column=1, sticky="w", padx=4)
        ttk.Label(pf, text="number_of_frames").grid(row=2, column=0, sticky="e")
        frames = tk.IntVar(value=int(self.payload.get("number_of_frames", 1)))
        ttk.Spinbox(pf, from_=1, to=9999, textvariable=frames, width=6).grid(row=2, column=1, sticky="w", padx=4)
        mvf = ttk.LabelFrame(frm, text="Movement [dx,dy] (first is 0,0)"); mvf.pack(fill="both", expand=True, pady=6)
        lst = tk.Listbox(mvf, height=8); lst.pack(side="left", fill="both", expand=True, padx=4, pady=4)
        def refresh_mv():
            lst.delete(0, tk.END)
            for i, (dx, dy) in enumerate(self.payload.get("movement", [])):
                lst.insert(tk.END, f"{i:02d}: [{dx}, {dy}]")
        def sync_len():
            n = max(1, int(frames.get()))
            mv = self.payload.get("movement", [])
            if not isinstance(mv, list): mv = []
            if len(mv) < n:
                mv.extend([[0, 0] for _ in range(n - len(mv))])
            elif len(mv) > n:
                mv = mv[:n]
            if n >= 1: mv[0] = [0, 0]
            self.payload["movement"] = mv
            refresh_mv()
        side = ttk.Frame(mvf); side.pack(side="right", fill="y")
        ttk.Button(side, text="Sync Length", command=sync_len).pack(fill="x", padx=4, pady=2)
        dxv, dyv = tk.IntVar(value=0), tk.IntVar(value=0)
        ttk.Label(side, text="dx").pack(anchor="w"); ttk.Entry(side, textvariable=dxv, width=6).pack(anchor="w")
        ttk.Label(side, text="dy").pack(anchor="w"); ttk.Entry(side, textvariable=dyv, width=6).pack(anchor="w")
        def apply_mv():
            sel = lst.curselection()
            if not sel: return
            i = sel[0]
            mv = self.payload["movement"]
            mv[i] = [int(dxv.get()), int(dyv.get())]
            if i == 0: mv[0] = [0, 0]
            refresh_mv()
        ttk.Button(side, text="Apply to Selected", command=apply_mv).pack(fill="x", padx=4, pady=2)
        ttk.Button(side, text="Zero All", command=lambda: self._zero_all_mv(refresh_mv)).pack(fill="x", padx=4, pady=2)
        refresh_mv()
        mf = ttk.LabelFrame(frm, text="On End"); mf.pack(fill="x", pady=6)
        ttk.Label(mf, text="on_end_mapping").grid(row=0, column=0, sticky="e")
        map_var = tk.StringVar(value=self.payload.get("on_end_mapping", ""))
        ttk.Entry(mf, textvariable=map_var, width=28).grid(row=0, column=1, sticky="w", padx=4)
        btns = ttk.Frame(frm); btns.pack(fill="x", pady=6)
        ttk.Button(btns, text="Cancel", command=self._dlg.destroy).pack(side="right", padx=4)
        ttk.Button(btns, text="Save", command=lambda: self._apply_and_close(kind.get(), path_var.get(), name_var.get(), flipped.get(), reversed_.get(), locked.get(), speed.get(), frames.get(), map_var.get())).pack(side="right")

    def _zero_all_mv(self, refresh_cb):
        n = max(1, int(self.payload.get("number_of_frames", 1)))
        self.payload["movement"] = [[0, 0] for _ in range(n)]
        self.payload["movement"][0] = [0, 0]
        refresh_cb()

    def _apply_and_close(self, kind, path, name, flipped, reversed_, locked, speed, frames, on_end_mapping):
        payload = self.payload
        payload["source"]["kind"] = "animation" if kind == "animation" else "folder"
        payload["source"]["path"] = (path.strip() if payload["source"]["kind"] == "folder" else "")
        payload["source"]["name"] = (name.strip() or None) if payload["source"]["kind"] == "animation" else None
        payload["flipped_source"] = bool(flipped)
        payload["reverse_source"] = bool(reversed_)
        payload["locked"] = bool(locked)
        payload["speed_factor"] = max(1, int(speed))
        payload["number_of_frames"] = max(1, int(frames))
        mv = payload.get("movement", [])
        if not isinstance(mv, list): mv = []
        if len(mv) < payload["number_of_frames"]:
            mv.extend([[0, 0] for _ in range(payload["number_of_frames"] - len(mv))])
        elif len(mv) > payload["number_of_frames"]:
            mv = mv[:payload["number_of_frames"]]
        if payload["number_of_frames"] >= 1:
            mv[0] = [0, 0]
        payload["movement"] = mv
        payload["on_end_mapping"] = on_end_mapping.strip()
        if self.on_changed:
            self.on_changed(self.node_id, payload)
        self._dlg.destroy()

    @staticmethod
    def _coerce_payload(anim_name: str, p: Dict[str, Any]) -> Dict[str, Any]:
        p = dict(p or {})
        src = dict(p.get("source") or {})
        p["source"] = {"kind": src.get("kind", "folder"), "path": src.get("path", anim_name if src.get("kind", "folder") == "folder" else ""), "name": src.get("name", None if src.get("kind", "folder") == "folder" else anim_name)}
        p.setdefault("flipped_source", False)
        p.setdefault("reverse_source", False)
        p.setdefault("locked", False)
        p.setdefault("speed_factor", max(1, int(p.get("speed_factor", 1))))
        p.setdefault("number_of_frames", max(1, int(p.get("number_of_frames", 1))))
        mv = p.get("movement")
        if not isinstance(mv, list) or len(mv) < 1:
            mv = [[0, 0] for _ in range(p["number_of_frames"])]
        if len(mv) != p["number_of_frames"]:
            if len(mv) < p["number_of_frames"]:
                mv.extend([[0, 0] for _ in range(p["number_of_frames"] - len(mv))])
            else:
                mv = mv[:p["number_of_frames"]]
        mv[0] = [0, 0]
        p["movement"] = mv
        p.setdefault("on_end_mapping", "")
        return p
