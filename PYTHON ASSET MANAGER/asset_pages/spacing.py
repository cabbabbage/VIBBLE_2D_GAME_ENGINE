# === File: pages/spacing.py ===
import os
import json
import tkinter as tk
from tkinter import ttk, messagebox
from shared.range import Range
from shared.area_ui import AreaUI
from shared.asset_io import load_info, save_info

class SpacingThresholdPage(tk.Frame):
    def __init__(self, parent):
        super().__init__(parent)
        self.asset_path = None
        self.area_file = "spacing_area.json"
        self._loaded = False

        # Page boundary
        self.configure(bg='#1e1e1e')

        # Title header
        title = tk.Label(
            self, text="Spacing Threshold",
            font=("Segoe UI", 20, "bold"),
            fg="#005f73", bg='#1e1e1e'
        )
        title.pack(fill=tk.X, pady=(10, 20))

        # Scrollable content area
        canvas = tk.Canvas(self, bg='#2a2a2a', highlightthickness=0)
        scroll_frame = tk.Frame(canvas, bg='#2a2a2a')
        window_id = canvas.create_window((0, 0), window=scroll_frame, anchor='nw')
        scroll_frame.bind(
            '<Configure>',
            lambda e: canvas.configure(scrollregion=canvas.bbox('all'))
        )
        canvas.bind(
            '<Configure>',
            lambda e: canvas.itemconfig(window_id, width=e.width)
        )
        def _scroll(evt):
            canvas.yview_scroll(int(-1*(evt.delta/120)), 'units')
        scroll_frame.bind('<Enter>', lambda e: canvas.bind_all('<MouseWheel>', _scroll))
        scroll_frame.bind('<Leave>', lambda e: canvas.unbind_all('<MouseWheel>'))
        canvas.pack(fill=tk.BOTH, expand=True)

        # Section: Options
        hdr_opts = tk.Label(
            scroll_frame, text="Options",
            font=("Segoe UI", 13, "bold"), fg="#DDDDDD", bg='#2a2a2a'
        )
        hdr_opts.pack(anchor='w', padx=12, pady=(10, 4))
        ttk.Style().configure('S.TCheckbutton', font=("Segoe UI", 12), background='#2a2a2a', foreground='#FFFFFF')
        self.has_spacing_var = tk.BooleanVar(value=True)
        chk = ttk.Checkbutton(
            scroll_frame, text="Has Spacing",
            variable=self.has_spacing_var,
            style='S.TCheckbutton',
            command=self._on_toggle
        )
        chk.pack(anchor='w', padx=12, pady=6)

        # Section: Forbidden Spawn Region
        hdr_area = tk.Label(
            scroll_frame, text="Forbidden Spawn Region", 
            font=("Segoe UI", 13, "bold"), fg="#DDDDDD", bg='#2a2a2a'
        )
        hdr_area.pack(anchor='w', padx=12, pady=(10, 4))
        self.area_ui = AreaUI(
            scroll_frame,
            json_path=self.area_file,
            autosave_callback=self._autosave
        )
        self.area_ui.pack(fill=tk.X, padx=12, pady=(0, 8))

        # Section: Distance Controls
        hdr_dist = tk.Label(
            scroll_frame, text="Minimum Distances", 
            font=("Segoe UI", 13, "bold"), fg="#DDDDDD", bg='#2a2a2a'
        )
        hdr_dist.pack(anchor='w', padx=12, pady=(10, 4))

        tk.Label(
            scroll_frame, text="Min Distance From Same Type:",
            font=("Segoe UI", 12), fg="#FFFFFF", bg='#2a2a2a'
        ).pack(anchor='w', padx=12, pady=(2, 0))
        self.min_dist_range = Range(
            scroll_frame, min_bound=0, max_bound=2000,
            set_min=0, set_max=0, force_fixed=True
        )
        self.min_dist_range.pack(fill='x', padx=12, pady=6)

        tk.Label(
            scroll_frame, text="Min Distance From All Assets:",
            font=("Segoe UI", 12), fg="#FFFFFF", bg='#2a2a2a'
        ).pack(anchor='w', padx=12, pady=(6, 0))
        self.min_dist_all_range = Range(
            scroll_frame, min_bound=0, max_bound=2000,
            set_min=0, set_max=0, force_fixed=True
        )
        self.min_dist_all_range.pack(fill='x', padx=12, pady=(0, 10))

        # Bind autosave on range change
        for rw in (self.min_dist_range, self.min_dist_all_range):
            rw.var_min.trace_add("write", lambda *_: self._autosave())
            rw.var_max.trace_add("write", lambda *_: self._autosave())

        # Finalize
        self._loaded = True
        self._on_toggle()

    def _on_toggle(self):
        if not self._loaded:
            return
        has_spacing = self.has_spacing_var.get()
        # Enable/disable area and ranges
        state = 'normal' if has_spacing else 'disabled'
        self.area_ui.btn_configure.config(state=state)
        self.min_dist_range.enable() if has_spacing else self.min_dist_range.disable()
        self.min_dist_all_range.enable() if has_spacing else self.min_dist_all_range.disable()
        # Update area label
        label = '(configured)' if has_spacing and self.area_ui.area_data else '(none)'
        self.area_ui.area_label.config(text=label)
        # Redraw or clear preview
        if has_spacing and self.area_ui.area_data:
            self.area_ui._draw_preview()
        else:
            self.area_ui.preview_canvas.delete('all')
        self._autosave()

    def load(self, info_path):
        self.asset_path = info_path
        if not info_path:
            return
        try:
            info = load_info(info_path)
        except Exception:
            info = {}
        self._loaded = False
        self.has_spacing_var.set(info.get('has_spacing', True))
        # Load area JSON
        asset_dir = os.path.dirname(info_path)
        spacing_val = info.get('spacing_area')
        # If inline area object, populate AreaUI from dict; else load from file
        if isinstance(spacing_val, dict):
            self.area_ui.json_path = None
            self.area_ui.area_data = spacing_val
            self.area_ui._draw_preview()
        else:
            json_path = os.path.join(asset_dir, spacing_val or self.area_file)
            self.area_ui.json_path = json_path
            self.area_ui._load_area_json()
        if os.path.isdir(os.path.join(asset_dir, 'default')):
            self.area_ui.frames_source = os.path.join(asset_dir, 'default')
        # Load distances
        same = info.get('min_same_type_distance', 0)
        all_ = info.get('min_distance_all', 0)
        if isinstance(same, list):
            self.min_dist_range.set(*same)
        else:
            self.min_dist_range.set(same, same)
        if isinstance(all_, list):
            self.min_dist_all_range.set(*all_)
        else:
            self.min_dist_all_range.set(all_, all_)
        self._loaded = True
        self._on_toggle()

    def _autosave(self):
        if not self._loaded or not self.asset_path:
            return
        try:
            info = load_info(self.asset_path)
        except Exception:
            info = {}
        info['has_spacing'] = self.has_spacing_var.get()
        asset_dir = os.path.dirname(self.asset_path)
        if self.has_spacing_var.get():
            if not self.area_ui.area_data:
                return
            # Store inline area
            info['spacing_area'] = self.area_ui.area_data
            s_min, s_max = self.min_dist_range.get()
            info['min_same_type_distance'] = s_min if s_min == s_max else [s_min, s_max]
            a_min, a_max = self.min_dist_all_range.get()
            info['min_distance_all'] = a_min if a_min == a_max else [a_min, a_max]
        else:
            info['spacing_area'] = None
            info['min_same_type_distance'] = 0
            info['min_distance_all'] = 0
        save_info(self.asset_path, info)

