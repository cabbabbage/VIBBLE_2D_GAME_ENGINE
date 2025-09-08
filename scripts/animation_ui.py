#!/usr/bin/env python3
from __future__ import annotations
import json
import sys
from pathlib import Path
from typing import Dict, Any, List, Optional

import threading
from frame_cropper import get_image_paths, compute_union_bounds, crop_images_with_bounds  # type: ignore

import tkinter as tk
from tkinter import ttk, messagebox, filedialog

from animations_pannel import AnimationsPanel
from ui_state import HistoryManager, ViewStateManager
from custom_controller_manager import CustomControllerManager


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
    # keep for compatibility; not used by this UI
    if "mappings" not in d or not isinstance(d["mappings"], dict):
        d["mappings"] = {}
    if "layout" not in d or not isinstance(d["layout"], dict):
        d["layout"] = {}
    # optional start field
    if "start" not in d:
        d["start"] = ""


# ---------- app (list-based UI) ----------
class AnimationConfiguratorAppSingle:
    def __init__(self, info_path: Path):
        self.info_path = info_path
        self.data: Optional[Dict[str, Any]] = None

        self.win = tk.Tk()
        self.win.title("Animation Configurator")
        self.win.geometry("1100x780")

        try:
            self.data = read_json(self.info_path)
        except Exception as e:
            messagebox.showerror("Load error", f"Failed to read {self.info_path}:\n{e}")
            self.win.destroy()
            raise

        ensure_sections(self.data)

        # managers: undo + view persistence
        self.history = HistoryManager(limit=200)
        self.view_state = ViewStateManager()
        self.history.snapshot(self.data)

        # Actions bar
        act = ttk.Frame(self.win)
        act.pack(side="top", fill="x", padx=8, pady=6)

        # Custom controller button (asset-level)
        self.cc_manager = CustomControllerManager(self.info_path)
        self.custom_ctrl_btn = ttk.Button(
            act,
            text=self._custom_ctrl_button_text(),
            command=self._on_custom_ctrl,
        )
        self.custom_ctrl_btn.pack(side="left", padx=(2, 8))

        # Proactively ensure engine sources include all existing controllers
        try:
            self._ensure_all_custom_includes()
        except Exception:
            pass

        ttk.Button(act, text="New Animation", command=self.create_animation).pack(side="left", padx=2)

        # Crop All (Global Bounds) button
        ttk.Button(
            act,
            text="Crop All (Global Bounds)",
            command=self._crop_all_global,
        ).pack(side="left", padx=2)

        ttk.Label(act, text="Start:").pack(side="left", padx=(16, 4))
        self.start_var = tk.StringVar(value=str(self.data.get("start", "")))
        self.start_cb = ttk.Combobox(act, textvariable=self.start_var, width=24, state="readonly")
        self.start_cb.pack(side="left")
        self.start_cb.bind("<<ComboboxSelected>>", self._on_start_changed)

        # Scrollable list area
        list_wrap = ttk.Frame(self.win)
        list_wrap.pack(fill="both", expand=True)
        self.scroll_canvas = tk.Canvas(list_wrap, bg="#2d3436", highlightthickness=0)
        vsb = ttk.Scrollbar(list_wrap, orient="vertical", command=self.scroll_canvas.yview)
        self.scroll_canvas.configure(yscrollcommand=vsb.set)
        vsb.pack(side="right", fill="y")
        self.scroll_canvas.pack(side="left", fill="both", expand=True)
        self.inner = ttk.Frame(self.scroll_canvas)
        self.scroll_window = self.scroll_canvas.create_window((0, 0), window=self.inner, anchor="nw")
        self.inner.bind("<Configure>", lambda _e: self.scroll_canvas.configure(scrollregion=self.scroll_canvas.bbox("all")))
        # keep inner frame width matching canvas width
        self.scroll_canvas.bind("<Configure>", lambda e: self.scroll_canvas.itemconfigure(self.scroll_window, width=e.width))
        # grid config for multi-column layout
        self.grid_cols = 2  # number of columns in the panel grid
        for c in range(self.grid_cols):
            self.inner.columnconfigure(c, weight=1, uniform="panels")

        self.status = tk.StringVar(value=f"Loaded {self.info_path}")
        ttk.Label(self.win, textvariable=self.status, relief=tk.SUNKEN, anchor="w").pack(side="bottom", fill="x")

        # keyboard + close
        try:
            self.win.bind_all("<Control-z>", self._undo_last_change)
            self.win.bind_all("<Control-Z>", self._undo_last_change)
        except Exception:
            pass
        try:
            self.win.protocol("WM_DELETE_WINDOW", self._on_close)
        except Exception:
            pass

        # panels registry
        self.panels: Dict[str, AnimationsPanel] = {}

        # preview provider (optional)
        try:
            from preview_provider import PreviewProvider  # type: ignore
            self.preview_provider = PreviewProvider(
                base_dir=self.info_path.parent,
                animation_lookup=lambda name: self.data.get("animations", {}).get(name),
                size=(72, 72),
            )
        except Exception:
            self.preview_provider = None

        # apply geometry if saved
        try:
            view = self.data.get("layout", {}).get("__view", {})
            if isinstance(view, dict):
                geom = view.get("geometry")
                if geom:
                    self.win.geometry(str(geom))
        except Exception:
            pass

        self.rebuild_list()
        self._restore_view_state()

    # ----- helpers for cropping -----
    def _anim_source_folder(self, anim_payload: Dict[str, Any]) -> Optional[Path]:
        try:
            src = anim_payload.get("source", {})
            if not isinstance(src, dict):
                return None
            if src.get("kind") != "folder":
                return None
            rel = src.get("path")
            if not rel:
                return None
            return (self.info_path.parent / rel).resolve()
        except Exception:
            return None

    def _run_in_thread(self, fn, on_done=None):
        def _wrap():
            try:
                result = fn()
            except Exception as e:
                result = e
            finally:
                if on_done:
                    self.win.after(0, lambda r=result: on_done(r))
        threading.Thread(target=_wrap, daemon=True).start()

    def _crop_all_global(self):
        if not (self.data and isinstance(self.data.get("animations"), dict)):
            messagebox.showinfo("Crop All", "No animations found.")
            return

        # 1) Collect all folders and image paths
        anims = list(self.data["animations"].items())
        images_by_folder: Dict[Path, List[str]] = {}

        for _name, payload in anims:
            folder = self._anim_source_folder(payload)
            if folder and folder.exists():
                try:
                    images_by_folder[folder] = get_image_paths(str(folder))
                except Exception:
                    images_by_folder[folder] = []

        all_image_paths: List[str] = []
        for lst in images_by_folder.values():
            all_image_paths.extend(lst)

        if not all_image_paths:
            messagebox.showinfo("Crop All", "No numbered PNG frames found.")
            return

        # 2) Background work: compute global bounds, then crop everything with those bounds
        def work():
            # Slightly tolerant threshold to ignore faint halos
            top, bottom, left, right, _w, _h = compute_union_bounds(all_image_paths, alpha_threshold=2)
            if top == bottom == left == right == 0:
                return {"bounds": (0, 0, 0, 0), "total": 0, "per_folder": []}

            total_cropped = 0
            per_folder = []
            for folder, imgs in images_by_folder.items():
                if not imgs:
                    per_folder.append((str(folder), 0))
                    continue
                n = crop_images_with_bounds(imgs, top, bottom, left, right)
                total_cropped += n
                per_folder.append((str(folder), n))
            return {"bounds": (top, bottom, left, right), "total": total_cropped, "per_folder": per_folder}

        def done(res):
            if isinstance(res, Exception):
                messagebox.showerror("Crop All", f"Error: {res}")
                return
            bounds = res.get("bounds", (0, 0, 0, 0))
            total = res.get("total", 0)
            per_folder = res.get("per_folder", [])
            t, b, l, r = bounds
            if total == 0:
                messagebox.showinfo("Crop All", "No cropping needed (global bounds empty).")
            else:
                lines = [f"Global crop (T:{t}, B:{b}, L:{l}, R:{r})", f"Total frames cropped: {total}", ""]
                for folder, n in per_folder:
                    lines.append(f"{folder}: {n} frames")
                messagebox.showinfo("Crop All", "\n".join(lines))
            # refresh previews and save
            try:
                self.save_current()
                self.rebuild_list()
            except Exception:
                pass

        self._run_in_thread(work, on_done=done)

    # ----- autosave -----
    def save_current(self):
        if not (self.data and self.info_path):
            return
        try:
            # persist view state
            self._save_view_state_to_data()
            write_json(self.info_path, self.data)
            self.status.set(f"Saved {self.info_path}")
        except Exception as e:
            messagebox.showerror("Save error", f"Failed to save {self.info_path}:\n{e}")

    # ----- list build -----
    def rebuild_list(self):
        for w in self.inner.winfo_children():
            w.destroy()
        self.panels.clear()
        anims = list(self.data.get("animations", {}).keys())
        anims.sort()
        # place panels in a grid with spacing
        cmax = max(1, int(getattr(self, "grid_cols", 2)))
        r = 0
        c = 0
        for a in anims:
            p = AnimationsPanel(
                self.inner,
                a,
                self.data["animations"][a],
                on_changed=self._on_panel_changed,
                on_renamed=self._on_panel_renamed,
                on_delete=self._on_panel_delete,
                preview_provider=self.preview_provider,
                asset_folder=str(self.info_path.parent),
                list_animation_names=lambda: list(self.data.get("animations", {}).keys()),
                resolve_animation_payload=lambda name: self.data.get("animations", {}).get(str(name), None),
            )
            fr = p.get_frame()
            fr.grid(row=r, column=c, sticky="nsew", padx=12, pady=12)
            self.panels[a] = p
            c += 1
            if c >= cmax:
                c = 0
                r += 1
        self._refresh_start_selector()

    # ----- panel callbacks -----
    def _on_panel_changed(self, node_id: str, payload: Dict[str, Any]):
        if not (self.data and self.info_path):
            return
        self._snapshot()
        self.data.setdefault("animations", {})[node_id] = payload
        self.save_current()

    def _on_panel_renamed(self, old_id: str, new_id: str):
        if not (self.data and self.info_path):
            return
        new_id = str(new_id).strip()
        if not new_id:
            return
        if new_id in self.data.get("animations", {}) and new_id != old_id:
            messagebox.showerror("Rename", f"'{new_id}' already exists.")
            # rebuild to reset UI to old id
            self.rebuild_list()
            return
        self._snapshot()
        anims = self.data.setdefault("animations", {})
        anims[new_id] = anims.pop(old_id)
        # update start if matches
        if str(self.data.get("start", "")) == old_id:
            self.data["start"] = new_id
        self.save_current()
        self.rebuild_list()

    def _custom_ctrl_button_text(self) -> str:
        return "Open Custom Controller in IDE" if self.cc_manager.exists() else "Create Custom Controller"

    def _on_custom_ctrl(self):
        if self.cc_manager.exists():
            self.cc_manager.open_in_ide()
            return
        # create files and update JSON with key
        try:
            self.cc_manager.create()
        except Exception as e:
            messagebox.showerror("Create error", f"Failed to create custom controller files:\n{e}")
            return
        # store key at top-level
        try:
            self.data["custom_controller_key"] = self.cc_manager.key()
        except Exception:
            pass
        self.save_current()
        try:
            self.custom_ctrl_btn.configure(text=self._custom_ctrl_button_text())
        except Exception:
            pass

    def _ensure_all_custom_includes(self):
        # Scan ENGINE/custom_controllers for headers and ensure includes exist in key engine files
        engine_dir = self.info_path.parents[2] / "ENGINE"
        cc_dir = engine_dir / "custom_controllers"
        if not cc_dir.exists():
            return
        targets = [
            engine_dir / "asset_info_methods" / "animation_loader.cpp",
            engine_dir / "asset_info_methods" / "animation_loader.hpp",
            engine_dir / "ui" / "asset_info_ui.cpp",
        ]
        headers = [p for p in cc_dir.glob("*.hpp")]
        for hdr in headers:
            base = hdr.stem  # without extension
            inc = f"#include \"custom_controllers/{base}.hpp\"\n"
            for t in targets:
                try:
                    if not t.exists():
                        continue
                    txt = t.read_text(encoding="utf-8")
                    if inc.strip() in txt:
                        continue
                    lines = txt.splitlines(True)
                    ins = 0
                    for i, ln in enumerate(lines[:100]):
                        if ln.lstrip().startswith('#include'):
                            ins = i + 1
                    lines.insert(ins, inc)
                    t.write_text("".join(lines), encoding="utf-8")
                except Exception:
                    pass

    def _on_panel_delete(self, node_id: str):
        if not (self.data and self.info_path):
            return
        self._snapshot()
        self.data.setdefault("animations", {}).pop(node_id, None)
        if str(self.data.get("start", "")) == node_id:
            self.data["start"] = ""
        self.save_current()
        self.rebuild_list()

    # ----- start selector -----
    def _refresh_start_selector(self):
        node_ids = list(self.data.get("animations", {}).keys())
        node_ids.sort()
        self.start_cb["values"] = node_ids
        cur = str(self.data.get("start", ""))
        if cur and cur not in node_ids:
            cur = ""
            self.data["start"] = ""
        self.start_var.set(cur)

    def _on_start_changed(self, _evt=None):
        if not (self.data and self.info_path):
            return
        self._snapshot()
        self.data["start"] = self.start_var.get()
        self.save_current()

    # ----- create -----
    def create_animation(self):
        if not (self.data and self.info_path):
            return
        self._snapshot()
        base = "new_anim"
        name, i = base, 1
        while name in self.data["animations"]:
            name = f"{base}_{i}"
            i += 1
        self.data["animations"][name] = {
            "source": {"kind": "folder", "path": name, "name": None},
            "flipped_source": False,
            "reverse_source": False,
            "locked": False,
            "rnd_start": False,
            "speed_factor": 1,
            "number_of_frames": 1,
            "movement": [[0, 0]],
            "on_end": "",
        }
        self.save_current()
        self.rebuild_list()

    # ----- view persistence + undo -----
    def _save_view_state_to_data(self):
        try:
            layout = self.data.setdefault("layout", {})
            layout["__view"] = self.view_state.capture(self.win, self.scroll_canvas)
        except Exception:
            pass

    def _restore_view_state(self):
        try:
            view = self.data.get("layout", {}).get("__view", {})
            if isinstance(view, dict):
                self.win.after(50, lambda v=view: self.view_state.apply(self.win, self.scroll_canvas, v))
        except Exception:
            pass

    def _snapshot(self):
        try:
            if self.data is not None:
                self.history.snapshot(self.data)
        except Exception:
            pass

    def _undo_last_change(self, _evt=None):
        try:
            snap = self.history.undo()
            if snap is None:
                return
            self.data = snap
            write_json(self.info_path, self.data)
            self.rebuild_list()
            self._restore_view_state()
            self.status.set("Undid last change")
        except Exception:
            pass

    def _on_close(self):
        try:
            self._save_view_state_to_data()
            self.save_current()
        finally:
            try:
                self.win.destroy()
            except Exception:
                pass

    def run(self):
        self.win.mainloop()


# ---------- entry ----------
def _choose_info_json() -> Optional[Path]:
    root = tk.Tk()
    root.withdraw()
    root.update_idletasks()
    path_str = filedialog.askopenfilename(
        title="Select info.json",
        filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
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
