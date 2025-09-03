#!/usr/bin/env python3
"""Standalone editor for an asset's animations and mappings.

This tool expects a path to an ``info.json`` file describing the asset.
It opens a Tk application that lets the user edit animation and mapping
entries by dragging nodes and editing their properties directly on the canvas.
"""

from __future__ import annotations
import json
import sys
from pathlib import Path
from typing import Dict, Any, List, Tuple, Optional

import tkinter as tk
from tkinter import ttk, messagebox

from animation_node import AnimationNode
from mapper_node import MapperNode


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


class GraphCanvas(tk.Canvas):
    """Canvas that hosts nodes and draws edges between them."""

    def __init__(self, master, **kwargs):
        super().__init__(master, bg="#2d3436", highlightthickness=0, **kwargs)
        self.nodes: Dict[str, object] = {}
        self.anim_nodes: Dict[str, AnimationNode] = {}
        self.map_nodes: Dict[str, MapperNode] = {}
        self.edges: List[Tuple[str, str]] = []

        # connection state
        self.pending_from_anim: Optional[str] = None

        # redraw on size change
        self.bind("<Configure>", lambda _e: self.redraw_edges())

    def clear(self):
        self.delete("all")
        self.nodes.clear()
        self.anim_nodes.clear()
        self.map_nodes.clear()
        self.edges.clear()
        self.pending_from_anim = None

    def add_anim_node(self, anim_name: str, payload: Dict[str, Any], x: int, y: int) -> AnimationNode:
        node = AnimationNode(self, anim_name, payload, x=x, y=y)
        node.on_begin_connect = self._begin_connect_from_anim
        node.on_changed = self._on_node_changed
        node.on_moved = lambda _id, _x, _y: self.redraw_edges()
        self.nodes[anim_name] = node
        self.anim_nodes[anim_name] = node
        return node

    def add_map_node(self, mapping_id: str, payload: List[Dict[str, Any]], x: int, y: int) -> MapperNode:
        node = MapperNode(self, mapping_id, payload, x=x, y=y)
        node.on_end_connect = self._end_connect_to_map
        node.on_changed = self._on_node_changed
        node.on_moved = lambda _id, _x, _y: self.redraw_edges()
        self.nodes[mapping_id] = node
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

    def _begin_connect_from_anim(self, anim_id: str):
        self.pending_from_anim = anim_id
        # visual hint around mapping nodes
        for n in self.map_nodes.values():
            self.create_rectangle(n.x, n.y, n.x + n.w, n.y + n.h,
                                 outline="#f5cd79", width=2, tags=("hint",))
        self.after(500, lambda: self.delete("hint"))

    def _end_connect_to_map(self, mapping_id: str):
        if not self.pending_from_anim:
            return
        self.event_generate("<<GraphConnect>>", data=f"{self.pending_from_anim}|{mapping_id}")
        self.pending_from_anim = None

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
            self.create_line(ax, ay, mx, my, fill="#dcdde1", width=2,
                             arrow=tk.LAST, tags=("edge",))

    def _on_node_changed(self, node_id: str, payload: Dict[str, Any]):
        self.event_generate("<<NodeChanged>>", data=node_id)


class AnimationConfiguratorApp:
    """Tk application for editing a single ``info.json`` file."""

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


def main(argv: List[str]) -> int:
    if len(argv) != 2:
        print("Usage: main.py path/to/info.json")
        return 1
    info_path = Path(argv[1]).expanduser().resolve()
    app = AnimationConfiguratorApp(info_path)
    app.run()
    return 0


if __name__ == "__main__":  # pragma: no cover - GUI entry point
    raise SystemExit(main(sys.argv))

