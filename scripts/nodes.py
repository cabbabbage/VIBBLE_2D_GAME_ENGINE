# ui_nodes.py
# Two reusable Tkinter UI element classes for the animation/mapping editor:
# - AnimationModal: self-contained editor for a single animation (new format)
# - MapModal:       self-contained editor for a single mapping (ID + entries)
#
# Each class owns:
#   1) A draggable "node" drawn on a Tk Canvas with connection ports
#      - AnimationModal: 1 output port (to a MapModal)
#      - MapModal:       1 input port   (from an AnimationModal)
#   2) A details modal (Toplevel) that fully edits its JSON payload
#      - Double-click the node rectangle to open the modal
#
# Graph connectivity:
#   - Drag-and-drop connectivity is coordinated via callbacks:
#       .on_begin_connect(node_id)  -> user clicked an output port
#       .on_end_connect(node_id)    -> user clicked an input port
#   - The main configurator will wire these to create edges and redraw lines.
#
# Persistence hooks (main configurator can bind these):
#   - .on_changed(node_id, new_payload_dict) -> called after any modal edit
#   - .on_moved(node_id, x, y)               -> called after node drag

import tkinter as tk
from tkinter import ttk, messagebox
from typing import Any, Callable, Dict, List, Optional, Tuple


# ----------------------------- Shared Base -----------------------------

class _BaseNode:
    """A draggable, selectable node drawn on a Tk Canvas with optional ports."""
    def __init__(
        self,
        canvas: tk.Canvas,
        node_id: str,
        title: str,
        x: int = 50,
        y: int = 50,
        width: int = 220,
        height: int = 80,
    ):
        self.canvas = canvas
        self.node_id = node_id
        self.title = title
        self.x = x
        self.y = y
        self.w = width
        self.h = height

        # callbacks set by main configurator
        self.on_begin_connect: Optional[Callable[[str], None]] = None
        self.on_end_connect: Optional[Callable[[str], None]] = None
        self.on_changed: Optional[Callable[[str, Dict[str, Any]], None]] = None
        self.on_moved: Optional[Callable[[str, int, int], None]] = None

        # canvas items
        self._rect = None
        self._header = None
        self._port_in = None
        self._port_out = None
        self._selected = False

        # drag state
        self._dragging = False
        self._drag_off: Tuple[int, int] = (0, 0)

        self.draw()

    # ---- drawing / selection ----
    def draw(self):
        self.delete()
        fill = self._fill_color()
        outline = "#1e272e"
        self._rect = self.canvas.create_rectangle(
            self.x, self.y, self.x + self.w, self.y + self.h,
            fill=fill, outline=outline, width=2
        )
        self._header = self.canvas.create_text(
            self.x + 8, self.y + 12, anchor="w",
            fill="white", font=("Arial", 10, "bold"),
            text=self._header_text()
        )

        # bind dragging/select
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
        # override in subclasses
        return "#2f3640" if not self._selected else "#414b57"

    def _header_text(self) -> str:
        return self.title

    # ---- ports (override per node type) ----
    def _draw_ports(self):
        """Create ports and bind click → begin/end connect."""
        pass

    def output_port_center(self) -> Optional[Tuple[int, int]]:
        """Return center (x,y) of output port for edge drawing."""
        return None

    def input_port_center(self) -> Optional[Tuple[int, int]]:
        """Return center (x,y) of input port for edge drawing."""
        return None

    # ---- modal (override) ----
    def _on_open_modal(self, _evt=None):
        self.open_modal()

    def open_modal(self):
        """Open Toplevel editor for this node."""
        raise NotImplementedError

    # ---- drag handling ----
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


# --------------------------- Animation Modal ---------------------------

class AnimationModal(_BaseNode):
    """
    Node + Modal for a single animation in the *new* format.

    JSON payload shape (new spec):
      {
        "source": { "kind": "folder|animation", "path": "<rel>", "name": null|str },
        "flipped_source": bool,
        "reverse_source": bool,
        "locked": bool,
        "speed_factor": int,
        "number_of_frames": int,
        "movement": [[dx,dy], ...],  # len == number_of_frames, movement[0]==[0,0]
        "on_end_mapping": "<mapping_id>"
      }
    """
    def __init__(self, canvas: tk.Canvas, anim_name: str, payload: Dict[str, Any], x=50, y=50):
        self.payload = self._coerce_payload(anim_name, payload)
        super().__init__(canvas, anim_name, f"Anim: {anim_name}", x, y)

    # ---- public API ----
    def to_dict(self) -> Dict[str, Any]:
        return self.payload

    def set_payload(self, d: Dict[str, Any]):
        self.payload = self._coerce_payload(self.node_id, d)
        if self.on_changed:
            self.on_changed(self.node_id, self.payload)

    # ---- base overrides ----
    def _fill_color(self) -> str:
        return "#273c75" if not self._selected else "#40739e"

    def _draw_ports(self):
        # one output port on the right
        r = 6
        px = self.x + self.w - r - 6
        py = self.y + self.h // 2
        self._port_out = self.canvas.create_oval(px - r, py - r, px + r, py + r,
                                                 fill="#44bd32", outline="#2f3640", width=2)
        self.canvas.tag_bind(self._port_out, "<Button-1>", self._on_begin_connect)

    def output_port_center(self) -> Optional[Tuple[int, int]]:
        return (self.x + self.w - 12, self.y + self.h // 2)

    # ---- connect callbacks ----
    def _on_begin_connect(self, _evt=None):
        if self.on_begin_connect:
            self.on_begin_connect(self.node_id)

    # ---- modal editor ----
    def open_modal(self):
        if hasattr(self, "_dlg") and self._dlg and tk.Toplevel.winfo_exists(self._dlg):
            self._dlg.lift(); return

        self._dlg = tk.Toplevel(self.canvas)
        self._dlg.title(f"Animation: {self.node_id}")
        self._dlg.geometry("420x520")
        frm = ttk.Frame(self._dlg); frm.pack(fill="both", expand=True, padx=8, pady=8)

        # source
        sf = ttk.LabelFrame(frm, text="Source")
        sf.pack(fill="x", pady=6)
        kind = tk.StringVar(value=self.payload["source"]["kind"])
        ttk.Radiobutton(sf, text="Folder", value="folder", variable=kind).grid(row=0, column=0, sticky="w", padx=4)
        ttk.Radiobutton(sf, text="Animation", value="animation", variable=kind).grid(row=0, column=1, sticky="w")

        ttk.Label(sf, text="path").grid(row=1, column=0, sticky="e")
        path_var = tk.StringVar(value=self.payload["source"].get("path") or "")
        path_ent = ttk.Entry(sf, textvariable=path_var, width=32); path_ent.grid(row=1, column=1, sticky="w", padx=4)

        ttk.Label(sf, text="name").grid(row=2, column=0, sticky="e")
        name_var = tk.StringVar(value=self.payload["source"].get("name") or "")
        name_ent = ttk.Entry(sf, textvariable=name_var, width=32); name_ent.grid(row=2, column=1, sticky="w", padx=4)

        # flags
        pf = ttk.LabelFrame(frm, text="Playback")
        pf.pack(fill="x", pady=6)
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

        # movement
        mvf = ttk.LabelFrame(frm, text="Movement [dx,dy] (first is 0,0)")
        mvf.pack(fill="both", expand=True, pady=6)
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

        # mapping
        mf = ttk.LabelFrame(frm, text="On End")
        mf.pack(fill="x", pady=6)
        ttk.Label(mf, text="on_end_mapping").grid(row=0, column=0, sticky="e")
        map_var = tk.StringVar(value=self.payload.get("on_end_mapping", ""))
        ttk.Entry(mf, textvariable=map_var, width=28).grid(row=0, column=1, sticky="w", padx=4)

        # buttons
        btns = ttk.Frame(frm); btns.pack(fill="x", pady=6)
        ttk.Button(btns, text="Cancel", command=self._dlg.destroy).pack(side="right", padx=4)
        ttk.Button(btns, text="Save", command=lambda: self._apply_and_close(
            kind.get(), path_var.get(), name_var.get(), flipped.get(), reversed_.get(),
            locked.get(), speed.get(), frames.get(), map_var.get()
        )).pack(side="right")

    def _zero_all_mv(self, refresh_cb):
        n = max(1, int(self.payload.get("number_of_frames", 1)))
        self.payload["movement"] = [[0, 0] for _ in range(n)]
        self.payload["movement"][0] = [0, 0]
        refresh_cb()

    def _apply_and_close(self, kind, path, name, flipped, reversed_, locked, speed, frames, on_end_mapping):
        # coerce + validate
        payload = self.payload
        payload["source"]["kind"] = "animation" if kind == "animation" else "folder"
        payload["source"]["path"] = (path.strip() if payload["source"]["kind"] == "folder" else "")
        payload["source"]["name"] = (name.strip() or None) if payload["source"]["kind"] == "animation" else None
        payload["flipped_source"] = bool(flipped)
        payload["reverse_source"] = bool(reversed_)
        payload["locked"] = bool(locked)
        payload["speed_factor"] = max(1, int(speed))
        payload["number_of_frames"] = max(1, int(frames))
        # ensure movement length/first frame
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
        # supply sensible defaults if missing
        p = dict(p or {})
        src = dict(p.get("source") or {})
        p["source"] = {
            "kind": src.get("kind", "folder"),
            "path": src.get("path", anim_name if src.get("kind", "folder") == "folder" else ""),
            "name": src.get("name", None if src.get("kind", "folder") == "folder" else anim_name)
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
                mv = mv[:p["number_of_frames"]]
        mv[0] = [0, 0]
        p["movement"] = mv
        p.setdefault("on_end_mapping", "")
        return p


# ------------------------------ Map Modal ------------------------------

class MapModal(_BaseNode):
    """
    Node + Modal for a single mapping.

    JSON payload shape:
      [
        { "condition": "<string or empty>",
          "map_to": { "options": [ { "animation": "<name>", "percent": int }, ... ] }
        },
        ...
      ]
    """
    def __init__(self, canvas: tk.Canvas, mapping_id: str, payload: List[Dict[str, Any]], x=400, y=50):
        self.payload = self._coerce_payload(payload)
        super().__init__(canvas, mapping_id, f"Map: {mapping_id}", x, y)

    # ---- public API ----
    def to_list(self) -> List[Dict[str, Any]]:
        return self.payload

    def set_payload(self, lst: List[Dict[str, Any]]):
        self.payload = self._coerce_payload(lst)
        if self.on_changed:
            self.on_changed(self.node_id, {"_mapping": self.payload})

    # ---- base overrides ----
    def _fill_color(self) -> str:
        return "#8e44ad" if not self._selected else "#9b59b6"

    def _draw_ports(self):
        # one input port on the left
        r = 6
        px = self.x + r + 6
        py = self.y + self.h // 2
        self._port_in = self.canvas.create_oval(px - r, py - r, px + r, py + r,
                                                fill="#e1b12c", outline="#2f3640", width=2)
        self.canvas.tag_bind(self._port_in, "<Button-1>", self._on_end_connect)

    def input_port_center(self) -> Optional[Tuple[int, int]]:
        return (self.x + 12, self.y + self.h // 2)

    # ---- connect callbacks ----
    def _on_end_connect(self, _evt=None):
        if self.on_end_connect:
            self.on_end_connect(self.node_id)

    # ---- modal editor ----
    def open_modal(self):
        if hasattr(self, "_dlg") and self._dlg and tk.Toplevel.winfo_exists(self._dlg):
            self._dlg.lift(); return

        self._dlg = tk.Toplevel(self.canvas)
        self._dlg.title(f"Mapping: {self.node_id}")
        self._dlg.geometry("560x520")

        root = ttk.Frame(self._dlg); root.pack(fill="both", expand=True, padx=8, pady=8)

        # entries list
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

            # condition
            ttk.Label(frame, text="condition").grid(row=0, column=0, sticky="e", padx=2)
            cond_var = tk.StringVar(value=entry.get("condition", ""))
            cond_ent = ttk.Entry(frame, textvariable=cond_var, width=48)
            cond_ent.grid(row=0, column=1, sticky="w", padx=4)
            cond_ent.bind("<FocusOut>", lambda _e, i=idx, v=cond_var: self._set_condition(i, v.get()))

            # options
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

            # per-entry buttons
            row_btns = ttk.Frame(frame); row_btns.grid(row=2, column=0, columnspan=2, sticky="e", pady=2)
            ttk.Button(row_btns, text="Add Option", command=lambda i=idx: self._add_option(i)).pack(side="left", padx=2)
            ttk.Button(row_btns, text="Normalize 100", command=lambda i=idx: self._normalize(i)).pack(side="left", padx=2)
            ttk.Button(row_btns, text="Delete Entry", command=lambda i=idx: self._del_entry(i)).pack(side="left", padx=6)

    # ---- entry operations ----
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
            # wrap mapping payload in a dict for symmetry; main UI can unwrap
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
