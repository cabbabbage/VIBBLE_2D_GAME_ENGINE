#!/usr/bin/env python3
from __future__ import annotations
import json
import sys
from pathlib import Path
from typing import Dict, Any, List, Tuple, Optional, Any

import tkinter as tk
from tkinter import ttk, messagebox, filedialog

from animation_node import AnimationNode
from mapping_node import RegularMappingNode, RandomMappingNode


# ---------- json helpers ----------
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


# ---------- graph ----------
class GraphCanvas(tk.Canvas):
    PORT_HIT_RADIUS = 12

    def __init__(self, master, **kwargs):
        super().__init__(master, bg="#2d3436", highlightthickness=0, **kwargs)
        self.anim_nodes: Dict[str, AnimationNode] = {}
        self.map_nodes: Dict[str, tk.Widget] = {}   # id -> mapping node
        # edges are stored as tuples (src_id, dst_id)
        self.edges: List[Tuple[str, str]] = []

        self._drag_src_id: Optional[str] = None
        self._drag_line_id: Optional[int] = None
        self._drag_src_pos: Optional[Tuple[int, int]] = None

        self.bind("<Configure>", lambda _e: self.redraw_edges())
        self.bind("<B1-Motion>", self._on_mouse_drag)
        self.bind("<ButtonRelease-1>", self._on_mouse_release)

    def clear(self):
        self.delete("all")
        self.anim_nodes.clear()
        self.map_nodes.clear()
        self.edges.clear()
        self._reset_drag()

    def add_anim_node(self, anim_name: str, payload: Dict[str, Any], x: int, y: int):
        node = AnimationNode(self, anim_name, payload, x=x, y=y)
        node.on_begin_connect = self._begin_drag_from
        node.on_changed = self._on_node_changed
        node.on_moved = lambda _id, _x, _y: self.redraw_edges()
        self.anim_nodes[anim_name] = node
        return node

    def add_map_node(self, mapping_id: str, payload: Any, x: int, y: int):
        is_random = isinstance(payload, dict) and payload.get("type") == "random"
        node = RandomMappingNode(self, mapping_id, payload, x=x, y=y) if is_random else RegularMappingNode(self, mapping_id, payload, x=x, y=y)
        node.on_begin_connect = self._begin_drag_from
        node.on_changed = self._on_node_changed
        node.on_moved = lambda _id, _x, _y: self.redraw_edges()
        self.map_nodes[mapping_id] = node
        return node

    # --- load edges from data ---
    def set_edges_from_data(self, data: Dict[str, Any]):
        self.edges.clear()
        anims = data.get("animations", {})
        maps = data.get("mappings", {})
        if not isinstance(anims, dict) or not isinstance(maps, dict):
            return

        # A → M
        for aid, cfg in anims.items():
            if isinstance(cfg, dict):
                mid = cfg.get("on_end_mapping", "")
                if mid and mid in maps:
                    self.edges.append((aid, mid))

        # M → M
        for mid, cfg in maps.items():
            if isinstance(cfg, dict):
                nxt = cfg.get("on_end_mapping", "")
                if nxt and nxt in maps:
                    self.edges.append((mid, nxt))

        # M → A
        for mid, cfg in maps.items():
            if isinstance(cfg, dict):
                nxt_a = cfg.get("on_end_animation", "")
                if nxt_a and nxt_a in anims:
                    self.edges.append((mid, nxt_a))

    # --- drag connect flow ---
    def _begin_drag_from(self, src_id: str):
        self._drag_src_id = src_id
        sx, sy = self._node_output_center(src_id)
        if sx is None:
            self._reset_drag()
            return
        self._drag_src_pos = (sx, sy)
        if self._drag_line_id:
            self.delete(self._drag_line_id)
        self._drag_line_id = self.create_line(sx, sy, sx, sy, fill="#9AECDB", width=2, dash=(4, 2), tags=("drag_line",))

    def _on_mouse_drag(self, event):
        if not self._drag_line_id or not self._drag_src_pos:
            return
        x = self.canvasx(event.x)
        y = self.canvasy(event.y)
        sx, sy = self._drag_src_pos
        self.coords(self._drag_line_id, sx, sy, x, y)

    def _on_mouse_release(self, event):
        if not self._drag_src_id:
            return
        tx = self.canvasx(event.x)
        ty = self.canvasy(event.y)

        # prefer mapping input if close, else animation input
        dst_id = self._hit_test_inputs(self.map_nodes, tx, ty)
        if not dst_id:
            dst_id = self._hit_test_inputs(self.anim_nodes, tx, ty)

        if dst_id and dst_id != self._drag_src_id:
            self.event_generate("<<GraphConnect>>", data=f"{self._drag_src_id}|{dst_id}")
        self._reset_drag()

    def _node_output_center(self, node_id: str) -> Tuple[Optional[int], Optional[int]]:
        if node_id in self.anim_nodes:
            return self.anim_nodes[node_id].output_port_center()
        n = self.map_nodes.get(node_id)
        return n.output_port_center() if n else (None, None)

    def _hit_test_inputs(self, node_dict: Dict[str, Any], x: float, y: float) -> Optional[str]:
        r = self.PORT_HIT_RADIUS
        for nid, node in node_dict.items():
            cx, cy = node.input_port_center()
            if cx is None:
                continue
            if (x - cx) ** 2 + (y - cy) ** 2 <= r ** 2:
                return nid
        return None

    def _reset_drag(self):
        self._drag_src_id = None
        self._drag_src_pos = None
        if self._drag_line_id:
            self.delete(self._drag_line_id)
            self._drag_line_id = None

    # --- edges + dbl-click delete ---
    def redraw_edges(self):
        self.delete("edge")
        for src_id, dst_id in self.edges:
            sx, sy = self._node_output_center(src_id)
            # dst can be map or anim
            if dst_id in self.map_nodes:
                dx, dy = self.map_nodes[dst_id].input_port_center()
            else:
                dn = self.anim_nodes.get(dst_id)
                dx, dy = dn.input_port_center() if dn else (None, None)
            if not (sx and dx):
                continue
            line_id = self.create_line(sx, sy, dx, dy, fill="#dcdde1", width=2, arrow=tk.LAST,
                                       tags=("edge", f"edge:{src_id}|{dst_id}"))
            self.tag_bind(line_id, "<Double-Button-1>", lambda _e, s=src_id, d=dst_id: self._request_disconnect(s, d))

    def _request_disconnect(self, src_id: str, dst_id: str):
        self.event_generate("<<GraphDisconnect>>", data=f"{src_id}|{dst_id}")

    def _on_node_changed(self, node_id: str, payload: Dict[str, Any]):
        self.event_generate("<<NodeChanged>>", data=node_id)


# ---------- app ----------
class AnimationConfiguratorAppSingle:
    def __init__(self, info_path: Path):
        self.info_path = info_path
        self.data: Optional[Dict[str, Any]] = None

        self.win = tk.Tk()
        self.win.title("Animation & Mapping Configurator")
        self.win.geometry("1280x760")

        try:
            self.data = read_json(self.info_path)
        except Exception as e:
            messagebox.showerror("Load error", f"Failed to read {self.info_path}:\n{e}")
            self.win.destroy()
            raise

        ensure_sections(self.data)

        act = ttk.Frame(self.win); act.pack(side="top", fill="x", padx=8, pady=6)
        ttk.Button(act, text="New Animation", command=self.create_animation).pack(side="left", padx=2)
        ttk.Button(act, text="New Regular Map", command=self.create_regular_mapping).pack(side="left", padx=2)
        ttk.Button(act, text="New Random Map", command=self.create_random_mapping).pack(side="left", padx=2)
        ttk.Button(act, text="Save", command=self.save_current).pack(side="right", padx=2)

        self.canvas = GraphCanvas(self.win)
        self.canvas.pack(fill="both", expand=True)

        self.canvas.bind("<<GraphConnect>>", self.on_graph_connect)
        self.canvas.bind("<<GraphDisconnect>>", self.on_graph_disconnect)
        self.canvas.bind("<<NodeChanged>>", self.on_node_changed)

        self.status = tk.StringVar(value=f"Loaded {self.info_path}")
        ttk.Label(self.win, textvariable=self.status, relief=tk.SUNKEN, anchor="w").pack(side="bottom", fill="x")

        self.rebuild_graph()

    # save
    def save_current(self):
        if not (self.data and self.info_path):
            return
        try:
            write_json(self.info_path, self.data)
            self.status.set(f"Saved {self.info_path}")
        except Exception as e:
            messagebox.showerror("Save error", f"Failed to save {self.info_path}:\n{e}")

    # graph
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
            self.canvas.add_map_node(m, self.data["mappings"][m], x=560, y=y)
            y += 120

        self.canvas.set_edges_from_data(self.data)
        self.canvas.redraw_edges()

    # events
    def on_node_changed(self, _event):
        if not (self.data and self.info_path):
            return
        for aid, node in self.canvas.anim_nodes.items():
            self.data["animations"][aid] = node.to_dict()
        for mid, node in self.canvas.map_nodes.items():
            self.data["mappings"][mid] = node.to_payload()
        self.save_current()
        self.canvas.set_edges_from_data(self.data)
        self.canvas.redraw_edges()

    def on_graph_connect(self, event):
        if not (self.data and self.info_path):
            return
        try:
            pair = event.__dict__.get("data", "")
            src_id, dst_id = pair.split("|", 1)
        except Exception:
            return

        if src_id in self.data["animations"]:
            # Animation → (Mapping only)
            if dst_id in self.data["mappings"]:
                self.data["animations"][src_id]["on_end_mapping"] = dst_id
            else:
                return  # ignore A→A
        elif src_id in self.data["mappings"]:
            # Mapping → Mapping or Animation
            m = self.data["mappings"].get(src_id) or {}
            if not isinstance(m, dict):
                m = {}
            m["type"] = m.get("type", "regular")
            if dst_id in self.data["mappings"]:
                m["on_end_mapping"] = dst_id
                m["on_end_animation"] = ""
            elif dst_id in self.data["animations"]:
                m["on_end_animation"] = dst_id
                m["on_end_mapping"] = ""
            else:
                return
            self.data["mappings"][src_id] = m
        else:
            return

        self.save_current()
        self.canvas.set_edges_from_data(self.data)
        self.canvas.redraw_edges()
        self.status.set(f"Linked {src_id} → {dst_id}")

    def on_graph_disconnect(self, event):
        if not (self.data and self.info_path):
            return
        try:
            pair = event.__dict__.get("data", "")
            src_id, dst_id = pair.split("|", 1)
        except Exception:
            return

        changed = False
        if src_id in self.data["animations"]:
            if self.data["animations"][src_id].get("on_end_mapping") == dst_id:
                self.data["animations"][src_id]["on_end_mapping"] = ""
                changed = True
        elif src_id in self.data["mappings"]:
            m = self.data["mappings"].get(src_id)
            if isinstance(m, dict):
                if m.get("on_end_mapping") == dst_id:
                    m["on_end_mapping"] = ""
                    changed = True
                if m.get("on_end_animation") == dst_id:
                    m["on_end_animation"] = ""
                    changed = True
        if changed:
            self.save_current()
            self.canvas.set_edges_from_data(self.data)
            self.canvas.redraw_edges()
            self.status.set(f"Removed link {src_id} → {dst_id}")

    # creators
    def create_animation(self):
        base = "new_anim"
        name, i = base, 1
        while name in self.data["animations"]:
            name = f"{base}_{i}"; i += 1
        self.data["animations"][name] = {
            "source": {"kind": "folder", "path": name, "name": None},
            "flipped_source": False,
            "reverse_source": False,
            "locked": False,
            "speed_factor": 1,
            "number_of_frames": 1,
            "movement": [[0, 0]],
            "on_end_mapping": "",
        }
        self.save_current()
        self.rebuild_graph()

    def create_regular_mapping(self):
        import uuid
        mid = uuid.uuid4().hex[:8]
        while mid in self.data["mappings"]:
            mid = uuid.uuid4().hex[:8]
        self.data["mappings"][mid] = {
            "type": "regular",
            "entries": [{"condition": "", "animation": "idle"}],
            "on_end_mapping": "",
            "on_end_animation": "",
        }
        self.save_current()
        self.rebuild_graph()

    def create_random_mapping(self):
        import uuid
        mid = uuid.uuid4().hex[:8]
        while mid in self.data["mappings"]:
            mid = uuid.uuid4().hex[:8]
        self.data["mappings"][mid] = {
            "type": "random",
            "options": [{"animation": "idle", "percent": 100}],
            "on_end_mapping": "",
            "on_end_animation": "",
        }
        self.save_current()
        self.rebuild_graph()

    def run(self):
        self.win.mainloop()


# ---------- entry ----------
def _choose_info_json() -> Optional[Path]:
    root = tk.Tk(); root.withdraw(); root.update_idletasks()
    path_str = filedialog.askopenfilename(
        title="Select info.json",
        filetypes=[("JSON files", "*.json"), ("All files", "*.*")]
    )
    root.destroy()
    if not path_str:
        return None
    p = Path(path_str).expanduser().resolve()
    return p if p.exists() else None


def main(argv: List[str]) -> int:
    if len(argv) == 1:
        info_path = _choose_info_json()
        if not info_path:
            print("No file selected. Exiting.")
            return 1
        app = AnimationConfiguratorAppSingle(info_path)
        app.run()
        return 0

    if len(argv) == 2:
        info_path = Path(argv[1]).expanduser().resolve()
        if not info_path.exists():
            print(f"Error: {info_path} does not exist")
            return 1
        app = AnimationConfiguratorAppSingle(info_path)
        app.run()
        return 0

    print("Usage:\n  animation_ui.py                # pick JSON via dialog\n  animation_ui.py path/to/info.json")
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
