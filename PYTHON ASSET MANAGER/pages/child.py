import os
import json
import copy
import tkinter as tk
from tkinter import ttk, messagebox
from pages.boundary import BoundaryConfigurator
from pages.assets_editor import AssetEditor
from pages.range import Range

SRC_DIR = "SRC"

class ChildAssetsPage(ttk.Frame):
    def __init__(self, parent):
        super().__init__(parent)
        self.child_frames = []
        self.asset_path = None

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

        json_path = entry_data.get("json_path") if entry_data else None
        z_offset = entry_data.get("z_offset", 0) if entry_data else 0

        if isinstance(entry_data, dict):
            area_data = {k: copy.deepcopy(v)
                        for k, v in entry_data.items()
                        if k not in ("json_path", "z_offset", "assets")}
        else:
            area_data = None

        ttk.Label(frame, text="Z Offset:").grid(row=0, column=0, sticky="w")
        z_var = tk.IntVar(value=z_offset)
        ttk.Spinbox(frame, from_=-9999, to=9999, textvariable=z_var, width=10).grid(
            row=0, column=1, sticky="w", padx=6
        )

        area_label = ttk.Label(frame, text="(none)" if not area_data else "Area defined")
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
                entry['area_label'].config(text="Area defined")

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
            "area_edited": False
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
            with open(self.asset_path, 'r') as f:
                data = json.load(f)
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load parent info.json: {e}")
            return

        new_paths = []
        for entry in self.child_frames:
            if entry["json_path"]:
                new_paths.append(entry["json_path"])

        data["child_assets"] = new_paths
        try:
            with open(self.asset_path, 'w') as f:
                json.dump(data, f, indent=2)
        except Exception as e:
            messagebox.showerror("Error", f"Failed to update info.json: {e}")

    def load(self, info_path):
        self.asset_path = info_path
        for e in self.child_frames:
            e['frame'].destroy()
        self.child_frames.clear()

        try:
            data = json.load(open(info_path))
            for item in data.get("child_assets", []):
                if isinstance(item, dict):
                    path = item.get("json_path")
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

        base_folder = os.path.dirname(self.asset_path)
        saved = []

        for idx, entry in enumerate(self.child_frames):
            z_offset = entry['z_var'].get()
            assets = entry['asset_editor'].get_assets()
            filename = f"child_assets_{idx + 1}.json"
            full_path = os.path.join(base_folder, filename)

            data = {
                "z_offset": z_offset,
                "assets": assets
            }

            if entry["area_edited"] and isinstance(entry["area_data"], dict) and entry["area_data"]:
                data.update(entry["area_data"])
            elif isinstance(entry["original_area_data"], dict):
                data.update(entry["original_area_data"])

            try:
                with open(full_path, 'w') as f:
                    json.dump(data, f, indent=2)
                saved.append(filename)
                entry["json_path"] = filename
            except Exception as e:
                messagebox.showerror("Error", f"Failed to save {filename}: {e}")

        try:
            parent = json.load(open(self.asset_path))
        except:
            parent = {}

        parent['child_assets'] = saved

        try:
            with open(self.asset_path, 'w') as f:
                json.dump(parent, f, indent=2)
            messagebox.showinfo("Saved", "Child asset regions saved.")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to update parent info.json: {e}")
