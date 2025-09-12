import os
import json
from shared.asset_io import load_info, save_info
import copy
import tkinter as tk
from tkinter import ttk, messagebox
from shared.boundary import BoundaryConfigurator
from shared.assets_editor import AssetEditor
from shared.range import Range

SRC_DIR = "SRC"

class ChildAssetsPage(ttk.Frame):
    def __init__(self, parent):
        super().__init__(parent)
        self.child_frames = []
        self.asset_path = None
        self.parent_info = None

        self.FONT = ('Segoe UI', 14)
        self.FONT_BOLD = ('Segoe UI', 18, 'bold')

        ttk.Label(self, text="Child Asset Regions", font=self.FONT_BOLD).pack(pady=(10, 10))

        self.container = ttk.Frame(self)
        self.container.pack(fill=tk.BOTH, expand=True)

        self.scroll_canvas = tk.Canvas(self.container)
        self.scroll_frame = ttk.Frame(self.scroll_canvas)
        self.scrollbar = ttk.Scrollbar(self.container, orient="vertical", command=self.scroll_canvas.yview)
        self.scroll_canvas.configure(yscrollcommand=self.scrollbar.set)

        self.scroll_canvas.create_window((0, 0), window=self.scroll_frame, anchor="nw")
        self.scroll_frame.bind(
            "<Configure>",
            lambda e: self.scroll_canvas.configure(scrollregion=self.scroll_canvas.bbox("all"))
        )

        self.scroll_canvas.pack(side="left", fill="both", expand=True)
        self.scrollbar.pack(side="right", fill="y")

        btn_frame = ttk.Frame(self)
        btn_frame.pack(fill=tk.X, pady=12)
        ttk.Button(btn_frame, text="Add Region", command=self._add_child_region).pack(side=tk.LEFT, padx=10)
        ttk.Button(btn_frame, text="Save", command=self.save).pack(side=tk.LEFT, padx=10)

    def _add_child_region(self, entry_data=None):
        idx = len(self.child_frames)
        frame = ttk.LabelFrame(self.scroll_frame, text=f"Region {idx+1}", padding=10)
        frame.grid(row=idx, column=0, pady=10, sticky="ew")
        frame.columnconfigure(1, weight=1)

        json_path = entry_data.get("json_path") if isinstance(entry_data, dict) else None
        z_offset = entry_data.get("z_offset", 0) if isinstance(entry_data, dict) else 0

        # Resolve area via area_name if present; otherwise accept inline area dict
        area_name = entry_data.get("area_name") if isinstance(entry_data, dict) else None
        area_data = None
        if isinstance(entry_data, dict):
            if area_name and isinstance(self.parent_info, dict):
                # look up area by name in parent_info['areas']
                for a in self.parent_info.get('areas', []):
                    if isinstance(a, dict) and a.get('name') == area_name:
                        # Keep only points for editing
                        pts = a.get('points')
                        if isinstance(pts, list):
                            area_data = { 'points': copy.deepcopy(pts) }
                        break
            if area_data is None:
                # Back-compat: if inline area present in child entry
                area_data = {k: copy.deepcopy(v)
                             for k, v in entry_data.items()
                             if k not in ("json_path", "z_offset", "assets", "area_name")}

        ttk.Label(frame, text="Z Offset:").grid(row=0, column=0, sticky="w")
        z_var = tk.IntVar(value=z_offset)
        ttk.Spinbox(frame, from_=-9999, to=9999, textvariable=z_var, width=10).grid(
            row=0, column=1, sticky="w", padx=6
        )

        area_label = ttk.Label(frame, text=(area_name or "(none)") if not area_data else (area_name or "Area defined"))
        area_label.grid(row=1, column=0, columnspan=2, sticky="w")

        # Buttons container (top-right)
        btn_frame = ttk.Frame(frame)
        btn_frame.grid(row=0, column=2, rowspan=2, sticky="ne", padx=4)

        def delete_this_region():
            self._delete_region(entry)

        delete_btn = tk.Button(
            btn_frame,
            text="Delete",
            bg="#FF4C4C",  # Red
            fg="black",
            font=("Segoe UI", 10, "bold"),
            width=15,
            command=delete_this_region
        )
        delete_btn.pack(pady=(0, 6))

        def edit_area():
            if not self.asset_path:
                messagebox.showerror("Error", "No parent asset path defined.")
                return
            base_folder = os.path.join(os.path.dirname(self.asset_path), "default")
            if not os.path.isdir(base_folder):
                messagebox.showerror("Error", f"Parent frame folder does not exist: {base_folder}")
                return

            def save_callback(new_area):
                entry['area_data'] = new_area
                entry['area_edited'] = True
                # If no name yet, propose one based on index
                if not entry.get('area_name'):
                    entry['area_name'] = f"child_area_{idx}"
                entry['area_label'].config(text=entry.get('area_name') or "Area defined")

            editor = BoundaryConfigurator(self.winfo_toplevel(), base_folder, save_callback)
            editor.grab_set()

        edit_btn = tk.Button(
            btn_frame,
            text="Edit Area",
            bg="#FFD700",  # Yellow
            fg="black",
            font=("Segoe UI", 10, "bold"),
            width=15,
            command=edit_area
        )
        edit_btn.pack()

        asset_list = entry_data.get("assets", []) if isinstance(entry_data, dict) else []
        asset_editor = AssetEditor(
            frame,
            lambda: asset_list,
            lambda v: asset_list.clear() or asset_list.extend(v),
            lambda: None
        )
        asset_editor.grid(row=2, column=0, columnspan=3, sticky="ew", pady=10)
        asset_editor.load_assets()

        entry = {
            "frame": frame,
            "z_var": z_var,
            "json_path": json_path,
            "area_label": area_label,
            "asset_editor": asset_editor,
            "area_data": area_data,
            "original_area_data": copy.deepcopy(area_data) if isinstance(area_data, dict) else None,
            "area_edited": False,
            "area_name": area_name
        }

        self.child_frames.append(entry)


    def _delete_region(self, entry):
        confirm = messagebox.askyesno("Delete Region", "Are you sure you want to delete this region?")
        if not confirm:
            return

        json_path = entry.get("json_path")
        if json_path:
            full_path = os.path.join(os.path.dirname(self.asset_path), json_path)
            try:
                if os.path.isfile(full_path):
                    os.remove(full_path)
                    print(f"[ChildAssetsPage] Deleted child asset JSON: {full_path}")
            except Exception as e:
                messagebox.showerror("Error", f"Failed to delete file '{full_path}': {e}")

        self.child_frames.remove(entry)
        entry["frame"].destroy()
        self._update_info_json()


    def _update_info_json(self):
        try:
            data = load_info(self.asset_path)
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load parent info.json: {e}")
            return

        # Delegate to save() which performs proper normalization
        self.parent_info = data
        self.save()

    def load(self, info_path):
        self.asset_path = info_path
        for e in self.child_frames:
            e['frame'].destroy()
        self.child_frames.clear()

        try:
            data = load_info(info_path)
            self.parent_info = data
            for item in data.get("child_assets", []):
                if isinstance(item, dict):
                    self._add_child_region(item)
                else:
                    path = item
                    full = os.path.join(os.path.dirname(info_path), path)
                    if not os.path.exists(full):
                        continue
                    with open(full) as f:
                        child = json.load(f)
                    child["json_path"] = path
                    self._add_child_region(child)
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load child assets: {e}")

    def save(self):
        if not self.asset_path:
            return
        if not isinstance(self.parent_info, dict):
            try:
                self.parent_info = load_info(self.asset_path)
            except Exception:
                self.parent_info = {}

        out_children = []
        base_dir = os.path.dirname(self.asset_path)

        # Ensure areas list exists
        if 'areas' not in self.parent_info or not isinstance(self.parent_info.get('areas'), list):
            self.parent_info['areas'] = []

        # Helper to upsert an area by name with given points
        def upsert_area(area_name, points):
            if not area_name or not isinstance(points, list):
                return
            # find existing
            for a in self.parent_info['areas']:
                if isinstance(a, dict) and a.get('name') == area_name:
                    a['name'] = area_name
                    a['points'] = points
                    return
            self.parent_info['areas'].append({ 'name': area_name, 'points': points })

        for idx, entry in enumerate(self.child_frames):
            z_offset = entry['z_var'].get()
            assets = entry['asset_editor'].get_assets()
            # Determine area name
            area_name = entry.get('area_name') or f"child_area_{idx}"
            entry['area_name'] = area_name
            # Upsert area into parent areas if we have data
            points = None
            if isinstance(entry.get('area_data'), dict) and entry['area_data']:
                points = entry['area_data'].get('points')
            elif isinstance(entry.get('original_area_data'), dict) and entry['original_area_data']:
                points = entry['original_area_data'].get('points')
            if points:
                upsert_area(area_name, points)

            child = {
                'z_offset': z_offset,
                'area_name': area_name
            }
            if assets:
                child['assets'] = assets
            out_children.append(child)

        self.parent_info['child_assets'] = out_children
        try:
            save_info(self.asset_path, self.parent_info)
            messagebox.showinfo("Saved", "Child asset regions saved.")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to update parent info.json: {e}")


