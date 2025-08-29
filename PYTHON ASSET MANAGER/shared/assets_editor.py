# === File: pages/assets_editor.py ===
import os
import json
import copy
import tkinter as tk
from tkinter import ttk, messagebox, filedialog
from PIL import Image, ImageTk
from shared.range import Range
from shared.search import AssetSearchWindow
from shared.random_asset_generator import RandomAssetGenerator
from shared.load_existing import open_window_and_return_data

SRC_DIR = "SRC"

class AssetEditor(tk.Frame):
    def __init__(self, parent, get_asset_list, set_asset_list, save_callback,
                 positioning=True, current_path=None):
        super().__init__(parent, bg='#1e1e1e')
        self.get_asset_list = get_asset_list
        self.set_asset_list = set_asset_list
        self.save_callback = save_callback
        self.asset_frames = []
        self.positioning = positioning
        self.current_path = current_path
        self.inherit_var = tk.BooleanVar(value=False)
        self.inherit_state = False
        self.selected_frame = None

        # Enlarge checkboxes
        style = ttk.Style(self)
        style.configure('Big.TCheckbutton', padding=(8,8), background='#1e1e1e', foreground='#FFFFFF')
        style.configure('Selected.TFrame', background='#333333')

        # Top bar
        top_bar = tk.Frame(self, bg='#1e1e1e')
        top_bar.pack(fill=tk.X, padx=12, pady=(10,4))
        if not (self.current_path and self.current_path.endswith("map_assets.json")):
            inherit_chk = ttk.Checkbutton(
                top_bar, text="Inherit Asset", variable=self.inherit_var,
                style='Big.TCheckbutton', command=self._handle_inherit_toggle
            )
            inherit_chk.pack(side=tk.LEFT, padx=6)
        # Button styling
        btn_style = {"bg":"#007BFF","fg":"white","font":("Segoe UI",11,"bold")}
        tk.Button(top_bar, text="Add Asset", command=self._add_asset_dialog,
                  width=15, **btn_style).pack(side=tk.LEFT, padx=6)
        tk.Button(top_bar, text="Generate Set", command=self._open_random_generator,
                  width=26, **btn_style).pack(side=tk.LEFT, padx=6)
        tk.Button(top_bar, text="Add Existing", command=self._add_existing_asset,
                  width=15, **btn_style).pack(side=tk.LEFT, padx=6)

        # Scrollable asset list
        container = tk.Frame(self, bg='#1e1e1e')
        container.pack(fill=tk.BOTH, expand=True)
        self.canvas = tk.Canvas(container, bg='#2a2a2a', highlightthickness=0)
        self.scroll_frame = tk.Frame(self.canvas, bg='#2a2a2a')
        window_id = self.canvas.create_window((0,0), window=self.scroll_frame, anchor='nw')
        # adjust scroll region
        self.scroll_frame.bind('<Configure>', lambda e: self.canvas.configure(scrollregion=self.canvas.bbox('all')))
        # span full width
        self.canvas.bind('<Configure>', lambda e: self.canvas.itemconfig(window_id, width=e.width))
        # mouse wheel on hover
        self.scroll_frame.bind('<Enter>', lambda e: self.canvas.bind_all('<MouseWheel>', lambda ev: self.canvas.yview_scroll(int(-1*(ev.delta/120)), 'units')))
        self.scroll_frame.bind('<Leave>', lambda e: self.canvas.unbind_all('<MouseWheel>'))
        self.canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        ttk.Style(self).configure('Vertical.TScrollbar', background='#2a2a2a')
        scroll = ttk.Scrollbar(container, orient='vertical', command=self.canvas.yview, style='Vertical.TScrollbar')
        scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.canvas.configure(yscrollcommand=scroll.set)

    def load_assets(self):
        for w in self.scroll_frame.winfo_children():
            w.destroy()
        self.asset_frames.clear()
        for asset in self.get_asset_list():
            self._create_asset_widget(asset)
        self.save_assets()

    def _add_existing_asset(self):
        new_assets = open_window_and_return_data("assets")
        if not new_assets or not isinstance(new_assets, list): return
        lst = self.get_asset_list()
        for a in new_assets:
            lst.append(a)
            self._create_asset_widget(a)
        self.set_asset_list(lst)
        self.save_assets()

    def _create_asset_widget(self, asset):
        # Container for each asset
        frame = tk.Frame(
            self.scroll_frame,
            bg='#2a2a2a', bd=1, relief='solid'
        )
        frame.pack(fill=tk.X, expand=True, padx=10, pady=4)

        # Store asset data
        frame.asset_data = asset
        frame.asset_name = asset['name']
        frame.inherited = asset.get('inherited', False)

        # Selection highlight
        def on_click(event, fr=frame):
            if self.selected_frame:
                self.selected_frame.config(bg='#2a2a2a')
            fr.config(bg='#333333')
            self.selected_frame = fr
        frame.bind('<Button-1>', on_click)

        # Icon area
        if asset.get('tag', False):
            icon = tk.Label(
                frame, text='#', font=('Segoe UI', 32, 'bold'),
                bg='#2a2a2a', fg='#FFFFFF', width=2, height=1
            )
            icon.pack(side=tk.LEFT, padx=6)
        else:
            path = os.path.join(SRC_DIR, asset['name'], 'default', '0.png')
            if os.path.isfile(path):
                try:
                    img = Image.open(path)
                    img.thumbnail((64, 64), Image.Resampling.LANCZOS)
                    ph = ImageTk.PhotoImage(img)
                    lbl = tk.Label(frame, image=ph, bg='#2a2a2a')
                    lbl.image = ph
                    lbl.pack(side=tk.LEFT, padx=6)
                except Exception:
                    pass

        # Content area
        content = tk.Frame(frame, bg='#2a2a2a')
        content.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=6)
        tk.Label(
            content, text=asset['name'], font=('Segoe UI', 11, 'bold'),
            bg='#2a2a2a', fg='#FFFFFF'
        ).pack(anchor='w', pady=(4,2))

        # Quantity Range
        range_widget = Range(
            content,
            min_bound=-100, max_bound=2000,
            set_min=asset.get('min_number', 0),
            set_max=asset.get('max_number', 0)
        )
        range_widget.pack(fill=tk.X, pady=2)
        frame.range = range_widget
        for v in (range_widget.var_min, range_widget.var_max, getattr(range_widget, 'var_random', None)):
            if v:
                v.trace_add('write', lambda *_: self.save_assets())

        # Positioning controls
        if self.positioning:
            frame.check_overlap_var = tk.BooleanVar(value=asset.get('check_overlap', False))
            frame.check_min_spacing_var = tk.BooleanVar(value=asset.get('check_min_spacing', False))
            ttk.Checkbutton(
                content, text='Check Overlap', variable=frame.check_overlap_var,
                style='Big.TCheckbutton', command=self.save_assets
            ).pack(anchor='w', pady=(6,0))
            ttk.Checkbutton(
                content, text='Check Min Spacing', variable=frame.check_min_spacing_var,
                style='Big.TCheckbutton', command=self.save_assets
            ).pack(anchor='w', pady=(0,6))

            position_var = tk.StringVar(value=asset.get('position', 'Random'))
            frame.position_var = position_var
            cb = ttk.Combobox(
                content, textvariable=position_var, state='readonly',
                values=["Random","Center","Perimeter","Entrance","Distributed","Exact Position","Intersection"]
            )
            cb.pack(fill=tk.X, pady=(0,4))
            # Option container
            option_container = tk.Frame(content, bg='#2a2a2a')
            option_container.pack(fill=tk.X)
            frame.position_options = {}

            def update_position():
                # Clear
                for child in option_container.winfo_children():
                    child.destroy()
                frame.position_options.clear()

                pos = position_var.get()

                # Helper to add a labeled Range and wire autosave
                def add_range(key, label, min_bound, max_bound, default_min=0, default_max=0):
                    rw = Range(
                        option_container,
                        min_bound=min_bound,
                        max_bound=max_bound,
                        set_min=asset.get(f"{key}_min", default_min),
                        set_max=asset.get(f"{key}_max", default_max),
                        label=label
                    )
                    # Hook autosave when any value changes
                    rw.on_change = self.save_assets
                    # Also trace internal vars for safety
                    for v in (rw.var_min, rw.var_max, getattr(rw, 'var_random', None)):
                        if v:
                            try:
                                v.trace_add('write', lambda *_: self.save_assets())
                            except Exception:
                                pass
                    rw.pack(fill=tk.X, pady=(2, 6))
                    frame.position_options[key] = rw

                # Perimeter placement controls
                if pos == "Perimeter":
                    # Sector selection around the area's center (degrees)
                    add_range(
                        key="sector_center",
                        label="Sector Center (deg)",
                        min_bound=0,
                        max_bound=359,
                        default_min=0,
                        default_max=0
                    )
                    add_range(
                        key="sector_range",
                        label="Sector Range (deg)",
                        min_bound=0,
                        max_bound=360,
                        default_min=360,
                        default_max=360
                    )
                    # Shift the perimeter inward/outward as a percent of radius
                    add_range(
                        key="border_shift",
                        label="Border Shift (%)",
                        min_bound=0,
                        max_bound=100,
                        default_min=0,
                        default_max=0
                    )
                    # Pixel offsets from computed perimeter point
                    add_range(
                        key="perimeter_x_offset",
                        label="X Offset (px)",
                        min_bound=-2000,
                        max_bound=2000,
                        default_min=0,
                        default_max=0
                    )
                    add_range(
                        key="perimeter_y_offset",
                        label="Y Offset (px)",
                        min_bound=-2000,
                        max_bound=2000,
                        default_min=0,
                        default_max=0
                    )

                # Save after rebuilding option UI so current values persist
                self.save_assets()

            cb.bind('<<ComboboxSelected>>', lambda *_: update_position())
            update_position()

        # Action buttons
        btn_frame = tk.Frame(frame, bg='#2a2a2a')
        btn_frame.pack(side=tk.RIGHT, padx=6, pady=4)
        btn_style = {'bg': 'white', 'fg': 'black', 'font': ('Segoe UI', 10, 'bold'), 'width': 15}
        tk.Button(btn_frame, text='Change', command=lambda:self._change_asset(frame), **btn_style).pack(pady=(0,4))
        tk.Button(btn_frame, text='Duplicate', command=lambda:self._dup_asset(frame.asset_data), **btn_style).pack(pady=(0,4))
        tk.Button(btn_frame, text='Delete', command=lambda:self._delete_asset(frame), **btn_style).pack()

        self.asset_frames.append(frame)

    def _add_asset_dialog(self):
        window = AssetSearchWindow(self)
        window.wait_window()
        result = getattr(window, "selected_result", None)
        if isinstance(result, tuple):
            kind, name = result
            is_tag = (kind == "tag")
        else:
            return  # Invalid result
        if name:
            new_asset = {
                "name": name,
                "min_number": 0,
                "max_number": 0,
                "position": "Random",
                "exact_position": None,
                "tag": is_tag,
                "check_overlap": False,
                "check_min_spacing": False
            }
            self.get_asset_list().append(new_asset)
            self._create_asset_widget(new_asset)
            self.save_assets()


    def _delete_asset(self, frame):
        if frame in self.asset_frames:
            self.asset_frames.remove(frame)
        self.asset_frames = [f for f in self.asset_frames if f != frame]
        self.get_asset_list()[:] = self.get_assets()
        frame.destroy()
        self.save_assets()

    def get_assets(self):
        assets = []
        for f in self.asset_frames:
            # 1) Start with a *copy* of the original dict
            data = f.asset_data.copy()
            # 2) Overwrite just the fields we actually have controls for:
            data["min_number"] = f.range.get_min()
            data["max_number"] = f.range.get_max()
            if self.positioning and hasattr(f, "position_var"):
                data["position"] = f.position_var.get()
            # Keep any existing exact_position if you had one:
            data["exact_position"] = data.get("exact_position", None)
            data["inherited"] = getattr(f, "inherited", data.get("inherited", False))
            data["check_overlap"] = f.check_overlap_var.get() if hasattr(f, "check_overlap_var") else False
            data["check_min_spacing"] = f.check_min_spacing_var.get() if hasattr(f, "check_min_spacing_var") else False
            data["tag"] = getattr(f, "tag", data.get("tag", False))
            # 3) Overwrite any position-option ranges
            for key, rw in getattr(f, "position_options", {}).items():
                data[f"{key}_min"] = rw.get_min()
                data[f"{key}_max"] = rw.get_max()
            assets.append(data)
        return assets


    def save_assets(self):
        self.set_asset_list(self.get_assets())
        self.save_callback()

    def _handle_inherit_toggle(self):
        self.inherit_state = self.inherit_var.get()
        self.save_callback()

    def refresh(self):
        self.load_assets()

    def reload(self):
        self.load_assets()

    def _open_random_generator(self):
        current_assets = self.get_assets()
        def callback(result):
            if isinstance(result, list):
                self.set_asset_list(result)
                self.refresh()
                self.save_callback()
        RandomAssetGenerator(self, current_assets, callback)

