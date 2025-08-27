# === File: pages/passability.py ===
import os
import json
import tkinter as tk
from tkinter import ttk, messagebox
from pages.area_ui import AreaUI

class PassabilityPage(tk.Frame):
    def __init__(self, parent):
        super().__init__(parent)
        self.asset_path = None
        self._loaded = False

        # Passable toggle
        self.is_passable_var = tk.BooleanVar(value=True)
        self.is_passable_var.trace_add("write", lambda *_: self._on_toggle())

        # Page boundary
        self.configure(bg='#1e1e1e')

        # Title header
        title = tk.Label(
            self, text="Passability Settings",
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
        # Mouse-wheel scrolling on hover
        def _scroll(evt):
            canvas.yview_scroll(int(-1 * (evt.delta / 120)), 'units')
        scroll_frame.bind('<Enter>', lambda e: canvas.bind_all('<MouseWheel>', _scroll))
        scroll_frame.bind('<Leave>', lambda e: canvas.unbind_all('<MouseWheel>'))
        canvas.pack(fill=tk.BOTH, expand=True)

        # Section: Options
        hdr_opts = tk.Label(
            scroll_frame, text="Options",
            font=("Segoe UI", 13, "bold"), fg="#DDDDDD", bg='#2a2a2a'
        )
        hdr_opts.pack(anchor='w', padx=12, pady=(10, 4))
        # Checkbutton style
        ttk.Style().configure('P.TCheckbutton', font=("Segoe UI", 12), background='#2a2a2a', foreground='#FFFFFF')
        chk = ttk.Checkbutton(
            scroll_frame, text="Is Passable",
            variable=self.is_passable_var,
            style='P.TCheckbutton'
        )
        chk.pack(anchor='w', padx=12, pady=6)

        # Section: Impassable Area
        hdr_area = tk.Label(
            scroll_frame, text="Impassable Area",
            font=("Segoe UI", 13, "bold"), fg="#DDDDDD", bg='#2a2a2a'
        )
        hdr_area.pack(anchor='w', padx=12, pady=(10, 4))
        # AreaUI component
        self.area_ui = AreaUI(
            scroll_frame,
            json_path="",
            autosave_callback=self._autosave
        )
        self.area_ui.pack(fill=tk.X, padx=12, pady=8)

        # Mark loaded
        self._loaded = True
        # Initialize toggle state
        self._on_toggle()

    def _on_toggle(self):
        if not self._loaded:
            return
        passable = self.is_passable_var.get()
        # Disable area configuration when passable
        state = 'normal' if not passable else 'disabled'
        self.area_ui.btn_configure.config(state=state)
        # Update label
        label = '(unrestricted)' if passable else ('(configured)' if self.area_ui.area_data else '(none)')
        self.area_ui.area_label.config(text=label)
        # Clear or redraw preview
        if not passable and self.area_ui.area_data:
            self.area_ui._draw_preview()
        else:
            self.area_ui.preview_canvas.delete('all')
        # Autosave
        self._autosave()

    def load(self, info_path):
        self.asset_path = info_path
        if not info_path:
            return
        # Ensure file exists
        os.makedirs(os.path.dirname(info_path), exist_ok=True)
        try:
            with open(info_path, 'r') as f:
                info = json.load(f)
        except Exception:
            info = {}
        tags = set(info.get('tags', []))
        # Determine passable
        if 'impassable' in tags:
            self.is_passable_var.set(False)
        else:
            self.is_passable_var.set(True)
            tags.discard('impassable')
            tags.add('passable')
        # Configure AreaUI source and JSON path
        asset_dir = os.path.dirname(info_path)
        frames_dir = os.path.join(asset_dir, 'default')
        if os.path.isdir(frames_dir):
            self.area_ui.frames_source = frames_dir
        self.area_ui.json_path = os.path.join(asset_dir, 'impassable_area.json')
        self.area_ui._load_area_json()
        self.area_ui._draw_preview()
        self._loaded = True
        self._on_toggle()

    def _autosave(self):
        if not self._loaded or not self.asset_path:
            return
        try:
            with open(self.asset_path, 'r') as f:
                info = json.load(f)
        except Exception:
            info = {}
        tags = set(info.get('tags', []))
        # Reset tags
        tags.discard('passable')
        tags.discard('impassable')
        if self.is_passable_var.get():
            tags.add('passable')
            info['impassable_area'] = None
        else:
            tags.add('impassable')
            if not self.area_ui.area_data:
                return
            with open(self.area_ui.json_path, 'w') as f:
                json.dump(self.area_ui.area_data, f, indent=4)
            info['impassable_area'] = os.path.basename(self.area_ui.json_path)
        info['tags'] = list(tags)
        with open(self.asset_path, 'w') as f:
            json.dump(info, f, indent=4)
