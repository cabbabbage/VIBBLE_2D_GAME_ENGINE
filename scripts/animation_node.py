"""AnimationNode - in-canvas UI element for editing an animation.

The previous implementation used a pop-up modal dialog.  This version embeds
all editing controls directly inside a frame that lives on the main canvas.
Connections are still handled via the output port on the right-hand side.
"""

from __future__ import annotations

import tkinter as tk
from tkinter import ttk
from typing import Any, Callable, Dict, Optional, Tuple


class _BaseNode:
    """Draggable frame on a Tk canvas with optional connection ports."""

    def __init__(self, canvas: tk.Canvas, node_id: str, title: str, x: int = 50, y: int = 50):
        self.canvas = canvas
        self.node_id = node_id
        self.title = title
        self.x = x
        self.y = y

        self.on_begin_connect: Optional[Callable[[str], None]] = None
        self.on_end_connect: Optional[Callable[[str], None]] = None
        self.on_changed: Optional[Callable[[str, Dict[str, Any]], None]] = None
        self.on_moved: Optional[Callable[[str, int, int], None]] = None

        # frame + header inside the canvas window
        self.frame = ttk.Frame(canvas, relief=tk.RIDGE, borderwidth=2)
        self.header = ttk.Label(self.frame, text=title, background="#1e272e", foreground="white", anchor="w")
        self.header.pack(fill="x")
        self.body = ttk.Frame(self.frame)
        self.body.pack(fill="both", expand=True)

        self._win = canvas.create_window(x, y, window=self.frame, anchor="nw")
        self._port_in = None
        self._port_out = None

        # dragging via the header
        self._dragging = False
        self._drag_off: Tuple[int, int] = (0, 0)
        self.header.bind("<Button-1>", self._on_click)
        self.header.bind("<B1-Motion>", self._on_drag)
        self.header.bind("<ButtonRelease-1>", self._on_release)

        # size tracking
        self.w = self.frame.winfo_reqwidth()
        self.h = self.frame.winfo_reqheight()

    # ------------------------------------------------------------------
    def _update_size_and_ports(self):
        """Update cached size and redraw ports."""
        self.frame.update_idletasks()
        self.w = self.frame.winfo_width()
        self.h = self.frame.winfo_height()
        self._draw_ports()

    def _draw_ports(self):  # pragma: no cover - override in subclasses
        pass

    def output_port_center(self) -> Optional[Tuple[int, int]]:
        return None

    def input_port_center(self) -> Optional[Tuple[int, int]]:
        return None

    # ------------------------------------------------------------------
    def _on_click(self, evt):
        self._dragging = True
        self._drag_off = (evt.x, evt.y)

    def _on_drag(self, evt):
        if not self._dragging:
            return
        cx = self.canvas.canvasx(evt.x_root - self.canvas.winfo_rootx())
        cy = self.canvas.canvasy(evt.y_root - self.canvas.winfo_rooty())
        self.x = int(cx - self._drag_off[0])
        self.y = int(cy - self._drag_off[1])
        self.canvas.coords(self._win, self.x, self.y)
        self._draw_ports()

    def _on_release(self, _evt):
        if not self._dragging:
            return
        self._dragging = False
        if self.on_moved:
            self.on_moved(self.node_id, self.x, self.y)


class AnimationNode(_BaseNode):
    """Interactive node that edits a single animation payload."""

    def __init__(self, canvas: tk.Canvas, anim_name: str, payload: Dict[str, Any], x=50, y=50):
        self.payload = self._coerce_payload(anim_name, payload)
        super().__init__(canvas, anim_name, f"Anim: {anim_name}", x, y)
        self._build_ui()
        self._update_size_and_ports()

    # ----- public API -------------------------------------------------
    def to_dict(self) -> Dict[str, Any]:
        return self.payload

    def set_payload(self, d: Dict[str, Any]):
        self.payload = self._coerce_payload(self.node_id, d)
        self._refresh_mv()
        if self.on_changed:
            self.on_changed(self.node_id, self.payload)

    # ----- UI construction -------------------------------------------
    def _build_ui(self):
        sf = ttk.LabelFrame(self.body, text="Source")
        sf.pack(fill="x", pady=2)
        self.kind_var = tk.StringVar(value=self.payload["source"]["kind"])
        ttk.Radiobutton(sf, text="Folder", value="folder", variable=self.kind_var).grid(row=0, column=0, sticky="w", padx=4)
        ttk.Radiobutton(sf, text="Animation", value="animation", variable=self.kind_var).grid(row=0, column=1, sticky="w")
        ttk.Label(sf, text="path").grid(row=1, column=0, sticky="e")
        self.path_var = tk.StringVar(value=self.payload["source"].get("path") or "")
        ttk.Entry(sf, textvariable=self.path_var, width=18).grid(row=1, column=1, sticky="w", padx=4)
        ttk.Label(sf, text="name").grid(row=2, column=0, sticky="e")
        self.name_var = tk.StringVar(value=self.payload["source"].get("name") or "")
        ttk.Entry(sf, textvariable=self.name_var, width=18).grid(row=2, column=1, sticky="w", padx=4)

        pf = ttk.LabelFrame(self.body, text="Playback")
        pf.pack(fill="x", pady=2)
        self.flipped_var = tk.BooleanVar(value=bool(self.payload.get("flipped_source", False)))
        self.reversed_var = tk.BooleanVar(value=bool(self.payload.get("reverse_source", False)))
        self.locked_var = tk.BooleanVar(value=bool(self.payload.get("locked", False)))
        ttk.Checkbutton(pf, text="flipped", variable=self.flipped_var).grid(row=0, column=0, sticky="w")
        ttk.Checkbutton(pf, text="reverse", variable=self.reversed_var).grid(row=0, column=1, sticky="w")
        ttk.Checkbutton(pf, text="locked", variable=self.locked_var).grid(row=0, column=2, sticky="w")
        ttk.Label(pf, text="speed").grid(row=1, column=0, sticky="e")
        self.speed_var = tk.IntVar(value=int(self.payload.get("speed_factor", 1)))
        ttk.Spinbox(pf, from_=1, to=240, textvariable=self.speed_var, width=6).grid(row=1, column=1, sticky="w", padx=4)
        ttk.Label(pf, text="frames").grid(row=2, column=0, sticky="e")
        self.frames_var = tk.IntVar(value=int(self.payload.get("number_of_frames", 1)))
        ttk.Spinbox(pf, from_=1, to=9999, textvariable=self.frames_var, width=6).grid(row=2, column=1, sticky="w", padx=4)

        mvf = ttk.LabelFrame(self.body, text="Movement [dx,dy] (first 0,0)")
        mvf.pack(fill="both", expand=True, pady=2)
        self.mv_list = tk.Listbox(mvf, height=4)
        self.mv_list.pack(side="left", fill="both", expand=True, padx=4, pady=4)
        side = ttk.Frame(mvf)
        side.pack(side="right", fill="y")
        ttk.Button(side, text="Sync", command=self._sync_len).pack(fill="x", padx=4, pady=2)
        self.dx_var = tk.IntVar(value=0)
        self.dy_var = tk.IntVar(value=0)
        ttk.Label(side, text="dx").pack(anchor="w")
        ttk.Entry(side, textvariable=self.dx_var, width=6).pack(anchor="w")
        ttk.Label(side, text="dy").pack(anchor="w")
        ttk.Entry(side, textvariable=self.dy_var, width=6).pack(anchor="w")
        ttk.Button(side, text="Apply", command=self._apply_mv).pack(fill="x", padx=4, pady=2)
        ttk.Button(side, text="Zero All", command=self._zero_all_mv).pack(fill="x", padx=4, pady=2)

        mf = ttk.LabelFrame(self.body, text="On End")
        mf.pack(fill="x", pady=2)
        ttk.Label(mf, text="on_end_mapping").grid(row=0, column=0, sticky="e")
        self.map_var = tk.StringVar(value=self.payload.get("on_end_mapping", ""))
        ttk.Entry(mf, textvariable=self.map_var, width=18).grid(row=0, column=1, sticky="w", padx=4)

        ttk.Button(self.body, text="Save", command=self._apply_changes).pack(anchor="e", pady=4)

        self._refresh_mv()

    # ----- movement helpers ------------------------------------------
    def _refresh_mv(self):
        self.mv_list.delete(0, tk.END)
        for i, (dx, dy) in enumerate(self.payload.get("movement", [])):
            self.mv_list.insert(tk.END, f"{i:02d}: [{dx}, {dy}]")

    def _sync_len(self):
        n = max(1, int(self.frames_var.get()))
        mv = self.payload.get("movement", [])
        if not isinstance(mv, list):
            mv = []
        if len(mv) < n:
            mv.extend([[0, 0] for _ in range(n - len(mv))])
        elif len(mv) > n:
            mv = mv[:n]
        if n >= 1:
            mv[0] = [0, 0]
        self.payload["movement"] = mv
        self._refresh_mv()

    def _apply_mv(self):
        sel = self.mv_list.curselection()
        if not sel:
            return
        i = sel[0]
        mv = self.payload.get("movement", [])
        if i >= len(mv):
            return
        mv[i] = [int(self.dx_var.get()), int(self.dy_var.get())]
        if i == 0:
            mv[0] = [0, 0]
        self._refresh_mv()

    def _zero_all_mv(self):
        n = max(1, int(self.frames_var.get()))
        self.payload["movement"] = [[0, 0] for _ in range(n)]
        self.payload["movement"][0] = [0, 0]
        self._refresh_mv()

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
        mv = payload.get("movement", [])
        if not isinstance(mv, list):
            mv = []
        if len(mv) < payload["number_of_frames"]:
            mv.extend([[0, 0] for _ in range(payload["number_of_frames"] - len(mv))])
        elif len(mv) > payload["number_of_frames"]:
            mv = mv[: payload["number_of_frames"]]
        if payload["number_of_frames"] >= 1:
            mv[0] = [0, 0]
        payload["movement"] = mv
        payload["on_end_mapping"] = self.map_var.get().strip()
        if self.on_changed:
            self.on_changed(self.node_id, payload)

    # ----- ports ------------------------------------------------------
    def _draw_ports(self):
        if self._port_out:
            self.canvas.delete(self._port_out)
        r = 6
        px = self.x + self.w - r - 6
        py = self.y + self.h // 2
        self._port_out = self.canvas.create_oval(
            px - r, py - r, px + r, py + r, fill="#44bd32", outline="#2f3640", width=2
        )
        self.canvas.tag_bind(self._port_out, "<Button-1>", self._on_begin_connect)

    def output_port_center(self) -> Optional[Tuple[int, int]]:
        return (self.x + self.w - 12, self.y + self.h // 2)

    def _on_begin_connect(self, _evt=None):
        if self.on_begin_connect:
            self.on_begin_connect(self.node_id)

    # ----- payload coercion -----------------------------------------
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
        if not isinstance(mv, list) or len(mv) < 1:
            mv = [[0, 0] for _ in range(p["number_of_frames"])]
        if len(mv) != p["number_of_frames"]:
            if len(mv) < p["number_of_frames"]:
                mv.extend([[0, 0] for _ in range(p["number_of_frames"] - len(mv))])
            else:
                mv = mv[: p["number_of_frames"]]
        mv[0] = [0, 0]
        p["movement"] = mv
        p.setdefault("on_end_mapping", "")
        return p

