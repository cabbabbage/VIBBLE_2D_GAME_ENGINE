#!/usr/bin/env python3
"""animation_ui.py

Unified animation + mapping configurator.

Merged from:
- animation_node.py
- mapper_node.py
- animation_config.py
- main.py

Usage:
  - No args: multi-asset mode. Scans the directory containing this script for subfolders with info.json.
  - One arg: single-asset mode. Provide a path to a specific info.json.
"""

from __future__ import annotations
import json
import sys
from pathlib import Path
from typing import Dict, Any, List, Tuple, Optional, Callable

import tkinter as tk
from tkinter import ttk, messagebox

# ================================================================
# Base Node + Node Types
# ================================================================

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

    def _draw_ports(self):  # override in subclasses
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

# ================================================================
# JSON helpers
# ================================================================

def read_json(p: Path) -> Dict[str, Any]:
    with p.open("r", encoding="utf-8") as f:
        return json.load(f)

def write_json(p: Path, data: Dict[str, Any]) -> None:
    with p.open("w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)

def ensure_sections(d: Dict[str, Any]) -> None:
    if "animations" not in d or not isinstance(d["animations"], dict):
        d["animations"] = {}
    if "mappings" not in d or not isinstance(d["mappings"], dict):
        d["mappings"] = {}

# ================================================================
# Graph Canvas (shared)
# ================================================================

class GraphCanvas(tk.Canvas):
    """Canvas that hosts nodes and draws edges between them."""
    def __init__(self, master, **kwargs):
        super().__init__(master, bg="#2d3436", highlightthickness=0, **kwargs)
        self.anim_nodes: Dict[str, AnimationNode] = {}
        self.map_nodes: Dict[str, MapperNode] = {}
        self.edges: List[Tuple[str, str]] = []

        # connection state
        self.pending_from_anim: Optional[str] = None

        # redraw on size change
        self.bind("<Configure>", lambda _e: self.redraw_edges())

    def clear(self):
        self.delete("all")
        self.anim_nodes.clear()
        self.map_nodes.clear()
        self.edges.clear()
        self.pending_from_anim = None

    def add_anim_node(self, anim_name: str, payload: Dict[str, Any], x: int, y: int) -> AnimationNode:
        node = AnimationNode(self, anim_name, payload, x=x, y=y)
        node.on_begin_connect = self._begin_connect_from_anim
        node.on_changed = self._on_node_changed
        node.on_moved = lambda _id, _x, _y: self.redraw_edges()
        self.anim_nodes[anim_name] = node
        return node

    def add_map_node(self, mapping_id: str, payload: List[Dict[str, Any]], x: int, y: int) -> MapperNode:
        node = MapperNode(self, mapping_id, payload, x=x, y=y)
        node.on_end_connect = self._end_connect_to_map
        node.on_changed = self._on_node_changed
        node.on_moved = lambda _id, _x, _y: self.redraw_edges()
        self.map_nodes[mapping_id] = node
        return node

    def set_edges_from_data(self, data: Dict[str, Any]):
        self.edges.clear()
        anims = data.get("animations", {})
        maps = data.get("mappings", {})
        if not isinstance(anims, dict) or not isinstance(maps, dict):
            return
        for anim_name, cfg in anims.items():
            if not isinstance(cfg, dict):
                continue
            mid = cfg.get("on_end_mapping", "")
            if mid and mid in maps:
                self.edges.append((anim_name, mid))

    # ----- Connections -----
    def _begin_connect_from_anim(self, anim_id: str):
        self.pending_from_anim = anim_id
        # visual hint around mapping nodes
        for n in self.map_nodes.values():
            self.create_rectangle(n.x, n.y, n.x + n.w, n.y + n.h, outline="#f5cd79", width=2, tags=("hint",))
        self.after(500, lambda: self.delete("hint"))

    def _end_connect_to_map(self, mapping_id: str):
        if not self.pending_from_anim:
            return
        self.event_generate("<<GraphConnect>>", data=f"{self.pending_from_anim}|{mapping_id}")
        self.pending_from_anim = None

    # ----- Edges -----
    def redraw_edges(self):
        # remove previous edges
        self.delete("edge")
        for anim_name, mid in self.edges:
            an = self.anim_nodes.get(anim_name)
            mn = self.map_nodes.get(mid)
            if not an or not mn:
                continue
            ax, ay = an.output_port_center()
            mx, my = mn.input_port_center()
            if not (ax and mx):
                continue
            self.create_line(ax, ay, mx, my, fill="#dcdde1", width=2, arrow=tk.LAST, tags=("edge",))

    # ----- Change flow back to hosting app -----
    def _on_node_changed(self, node_id: str, payload: Dict[str, Any]):
        self.event_generate("<<NodeChanged>>", data=node_id)

# ================================================================
# Apps
# ================================================================

class AnimationConfiguratorAppMulti:
    """Multi-asset mode: scans a root directory for info.json and lets you pick one."""

    def __init__(self, root_dir: Path):
        self.root_dir = root_dir
        self.assets: List[Tuple[str, Path]] = []  # (name, info.json path)
        self.data: Optional[Dict[str, Any]] = None
        self.info_path: Optional[Path] = None

        self.win = tk.Tk()
        self.win.title("Animation & Mapping Configurator (Multi-asset)")
        self.win.geometry("1280x760")

        # left: asset list + actions
        left = ttk.Frame(self.win); left.pack(side="left", fill="y")
        ttk.Label(left, text="Assets (with info.json)").pack(anchor="w", padx=8, pady=6)
        self.asset_list = tk.Listbox(left, width=36, height=28)
        self.asset_list.pack(fill="y", padx=8, pady=4)
        self.asset_list.bind("<<ListboxSelect>>", self.on_select_asset)

        act = ttk.Frame(left); act.pack(fill="x", padx=8, pady=6)
        ttk.Button(act, text="New Animation", command=self.create_animation).pack(side="left", padx=2)
        ttk.Button(act, text="New Mapping", command=self.create_mapping).pack(side="left", padx=2)
        ttk.Button(act, text="Save", command=self.save_current).pack(side="right", padx=2)

        # center: graph canvas
        center = ttk.Frame(self.win); center.pack(side="left", fill="both", expand=True)
        self.canvas = GraphCanvas(center)
        self.canvas.pack(fill="both", expand=True)

        # bind custom events from canvas
        self.canvas.bind("<<GraphConnect>>", self.on_graph_connect)
        self.canvas.bind("<<NodeChanged>>", self.on_node_changed)

        # status bar
        self.status = tk.StringVar(value="Select an asset on the left.")
        status_bar = ttk.Label(self.win, textvariable=self.status, relief=tk.SUNKEN, anchor="w")
        status_bar.pack(side="bottom", fill="x")

        self.scan_assets()

    # ----- asset discovery -----
    def scan_assets(self):
        self.assets.clear()
        self.asset_list.delete(0, tk.END)
        for child in sorted(self.root_dir.iterdir()):
            if not child.is_dir(): continue
            info = child / "info.json"
            if info.exists():
                self.assets.append((child.name, info))
                self.asset_list.insert(tk.END, child.name)

    # ----- load/save -----
    def on_select_asset(self, _e=None):
        sel = self.asset_list.curselection()
        if not sel: return
        idx = sel[0]
        _, path = self.assets[idx]
        try:
            self.data = read_json(path)
        except Exception as e:
            messagebox.showerror("Load error", f"Failed to read {path}:\n{e}")
            return
        self.info_path = path
        ensure_sections(self.data)
        self.status.set(f"Loaded {path}")
        self.rebuild_graph()

    def save_current(self):
        if not (self.data and self.info_path):
            return
        try:
            write_json(self.info_path, self.data)
            self.status.set(f"Saved {self.info_path}")
        except Exception as e:
            messagebox.showerror("Save error", f"Failed to save {self.info_path}:\n{e}")

    # ----- graph build -----
    def rebuild_graph(self):
        self.canvas.clear()
        if not self.data:
            return
        anims = list(self.data["animations"].keys())
        maps = list(self.data["mappings"].keys())

        # layout: anims left, maps right
        y = 30
        for a in anims:
            self.canvas.add_anim_node(a, self.data["animations"][a], x=60, y=y)
            y += 100

        y = 30
        for m in maps:
            self.canvas.add_map_node(m, self.data["mappings"][m], x=520, y=y)
            y += 100

        self.canvas.set_edges_from_data(self.data)
        self.canvas.redraw_edges()

    # ----- node change handlers -----
    def on_node_changed(self, event):
        """A node’s modal saved changes; persist to JSON and refresh edges."""
        if not (self.data and self.info_path): return
        # sync from node objects back into self.data
        for aid, node in self.canvas.anim_nodes.items():
            self.data["animations"][aid] = node.to_dict()
        for mid, node in self.canvas.map_nodes.items():
            self.data["mappings"][mid] = node.to_list()
        self.save_current()
        self.canvas.set_edges_from_data(self.data)
        self.canvas.redraw_edges()

    def on_graph_connect(self, event):
        """User connected Animation → Mapping via ports."""
        if not (self.data and self.info_path): return
        try:
            pair = event.__dict__.get("data", "")
            anim_id, map_id = pair.split("|", 1)
        except Exception:
            return
        # set linkage
        anim = self.data["animations"].get(anim_id)
        if not anim:
            return
        anim["on_end_mapping"] = map_id
        # update node’s internal payload too
        if anim_id in self.canvas.anim_nodes:
            self.canvas.anim_nodes[anim_id].set_payload(anim)
        self.save_current()
        self.canvas.set_edges_from_data(self.data)
        self.canvas.redraw_edges()
        self.status.set(f"Linked {anim_id} → {map_id}")

    # ----- create helpers -----
    def create_animation(self):
        if not (self.data and self.info_path):
            return
        base = "new_anim"
        name = base
        i = 1
        while name in self.data["animations"]:
            name = f"{base}_{i}"; i += 1
        # minimal default payload
        payload = {
            "source": {"kind": "folder", "path": name, "name": None},
            "flipped_source": False,
            "reverse_source": False,
            "locked": False,
            "speed_factor": 1,
            "number_of_frames": 1,
            "movement": [[0, 0]],
            "on_end_mapping": ""
        }
        self.data["animations"][name] = payload
        self.save_current()
        self.rebuild_graph()

    def create_mapping(self):
        if not (self.data and self.info_path):
            return
        import uuid
        mid = uuid.uuid4().hex[:8]
        while mid in self.data["mappings"]:
            mid = uuid.uuid4().hex[:8]
        self.data["mappings"][mid] = [
            {"condition": "", "map_to": {"options": [{"animation": "idle", "percent": 100}]}}
        ]
        self.save_current()
        self.rebuild_graph()

    # ----- run -----
    def run(self):
        self.win.mainloop()


class AnimationConfiguratorAppSingle:
    """Single-asset mode: opens an explicit info.json."""

    def __init__(self, info_path: Path):
        self.info_path = info_path
        self.data: Optional[Dict[str, Any]] = None

        self.win = tk.Tk()
        self.win.title("Animation & Mapping Configurator")
        self.win.geometry("1280x760")

        # Load data after creating the window so messageboxes work
        try:
            self.data = read_json(self.info_path)
        except Exception as e:
            messagebox.showerror("Load error", f"Failed to read {self.info_path}:\n{e}")
            self.win.destroy()
            raise

        ensure_sections(self.data)

        act = ttk.Frame(self.win)
        act.pack(side="top", fill="x", padx=8, pady=6)
        ttk.Button(act, text="New Animation", command=self.create_animation).pack(side="left", padx=2)
        ttk.Button(act, text="New Mapping", command=self.create_mapping).pack(side="left", padx=2)
        ttk.Button(act, text="Save", command=self.save_current).pack(side="right", padx=2)

        self.canvas = GraphCanvas(self.win)
        self.canvas.pack(fill="both", expand=True)

        self.canvas.bind("<<GraphConnect>>", self.on_graph_connect)
        self.canvas.bind("<<NodeChanged>>", self.on_node_changed)

        self.status = tk.StringVar(value=f"Loaded {self.info_path}")
        status_bar = ttk.Label(self.win, textvariable=self.status,
                               relief=tk.SUNKEN, anchor="w")
        status_bar.pack(side="bottom", fill="x")

        self.rebuild_graph()

    def save_current(self):
        if not (self.data and self.info_path):
            return
        try:
            write_json(self.info_path, self.data)
            self.status.set(f"Saved {self.info_path}")
        except Exception as e:
            messagebox.showerror("Save error", f"Failed to save {self.info_path}:\n{e}")

    def rebuild_graph(self):
        self.canvas.clear()
        if not self.data:
            return
        anims = list(self.data["animations"].keys())
        maps = list(self.data["mappings"].keys())

        y = 30
        for a in anims:
            self.canvas.add_anim_node(a, self.data["animations"][a], x=60, y=y)
            y += 100

        y = 30
        for m in maps:
            self.canvas.add_map_node(m, self.data["mappings"][m], x=520, y=y)
            y += 100

        self.canvas.set_edges_from_data(self.data)
        self.canvas.redraw_edges()

    def on_node_changed(self, _event):
        if not (self.data and self.info_path):
            return
        for aid, node in self.canvas.anim_nodes.items():
            self.data["animations"][aid] = node.to_dict()
        for mid, node in self.canvas.map_nodes.items():
            self.data["mappings"][mid] = node.to_list()
        self.save_current()
        self.canvas.set_edges_from_data(self.data)
        self.canvas.redraw_edges()

    def on_graph_connect(self, event):
        if not (self.data and self.info_path):
            return
        try:
            pair = event.__dict__.get("data", "")
            anim_id, map_id = pair.split("|", 1)
        except Exception:
            return
        anim = self.data["animations"].get(anim_id)
        if not anim:
            return
        anim["on_end_mapping"] = map_id
        if anim_id in self.canvas.anim_nodes:
            self.canvas.anim_nodes[anim_id].set_payload(anim)
        self.save_current()
        self.canvas.set_edges_from_data(self.data)
        self.canvas.redraw_edges()
        self.status.set(f"Linked {anim_id} → {map_id}")

    def create_animation(self):
        if not (self.data and self.info_path):
            return
        base = "new_anim"
        name = base
        i = 1
        while name in self.data["animations"]:
            name = f"{base}_{i}"; i += 1
        payload = {
            "source": {"kind": "folder", "path": name, "name": None},
            "flipped_source": False,
            "reverse_source": False,
            "locked": False,
            "speed_factor": 1,
            "number_of_frames": 1,
            "movement": [[0, 0]],
            "on_end_mapping": "",
        }
        self.data["animations"][name] = payload
        self.save_current()
        self.rebuild_graph()

    def create_mapping(self):
        if not (self.data and self.info_path):
            return
        import uuid
        mid = uuid.uuid4().hex[:8]
        while mid in self.data["mappings"]:
            mid = uuid.uuid4().hex[:8]
        self.data["mappings"][mid] = [
            {"condition": "", "map_to": {"options": [{"animation": "idle", "percent": 100}]}}
        ]
        self.save_current()
        self.rebuild_graph()

    def run(self):
        self.win.mainloop()

# ================================================================
# Entry point
# ================================================================

def main(argv: List[str]) -> int:
    if len(argv) == 1:
        # multi-asset mode: scan the directory containing this script
        here = Path(__file__).parent.resolve()
        app = AnimationConfiguratorAppMulti(here)
        app.run()
        return 0

    if len(argv) == 2:
        # single-asset mode: open specific info.json
        info_path = Path(argv[1]).expanduser().resolve()
        if not info_path.exists():
            print(f"Error: {info_path} does not exist")
            return 1
        app = AnimationConfiguratorAppSingle(info_path)
        app.run()
        return 0

    print("Usage:\n  animation_ui.py                # multi-asset mode\n  animation_ui.py path/to/info.json  # single-asset mode")
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
