# === File: pages/map_info_page.py ===
import os
import json
import tkinter as tk
from tkinter import messagebox, ttk
from map_pages.map_render import MapRenderer
from map_pages.map_layer_info import MapLayerInfo

class MapInfoPage(tk.Frame):
    @staticmethod
    def get_json_filename():
        return "map_info.json"

    def __init__(self, parent):
        super().__init__(parent, bg='#1e1e1e')
        self.rooms_dir     = None
        self.json_path     = None
        self.map_data      = {}
        self.layers_data   = []
        self.layer_widgets = []
        self.factor        = 40.0
        # half-size preview
        self.PREVIEW_SIZE = 250

        # ─── Header ────────────────────────────────────────────────────────────
        header = tk.Frame(self, bg='#1e1e1e')
        header.pack(fill=tk.X, padx=12, pady=(10,20))
        tk.Label(
            header, text="Map Layers",
            font=("Segoe UI",20,"bold"), fg="#005f73", bg='#1e1e1e'
        ).pack(side=tk.LEFT)
        tk.Button(
            header, text="Add New Layer",
            bg="#28a745", fg="white",
            font=("Segoe UI",13,"bold"), width=18,
            command=self._on_add_layer
        ).pack(side=tk.RIGHT)

        # ─── Main split: layers on left, preview on right ─────────────────────
        paned = ttk.Panedwindow(self, orient=tk.HORIZONTAL)
        paned.pack(fill=tk.BOTH, expand=True, padx=12, pady=(0,12))

        # Left pane: scrollable list of layers
        left = tk.Frame(paned, bg='#2a2a2a')
        paned.add(left, weight=3)

        layers_canvas = tk.Canvas(left, bg='#2a2a2a', highlightthickness=0)
        layers_scroll_frame = tk.Frame(layers_canvas, bg='#2a2a2a')
        win_id = layers_canvas.create_window((0,0), window=layers_scroll_frame, anchor='nw')
        layers_scroll_frame.bind(
            '<Configure>',
            lambda e: layers_canvas.configure(scrollregion=layers_canvas.bbox('all'))
        )
        layers_canvas.bind(
            '<Configure>',
            lambda e: layers_canvas.itemconfig(win_id, width=e.width)
        )
        # wheel only when over the list
        layers_scroll_frame.bind(
            '<Enter>',
            lambda e: layers_canvas.bind_all('<MouseWheel>', lambda ev: layers_canvas.yview_scroll(int(-1*(ev.delta/120)), 'units'))
        )
        layers_scroll_frame.bind('<Leave>', lambda e: layers_canvas.unbind_all('<MouseWheel>'))

        layers_canvas.pack(fill=tk.BOTH, expand=True)
        self.layers_frame = layers_scroll_frame

        # Right pane: preview + button
        right = tk.Frame(paned, bg='#2a2a2a')
        paned.add(right, weight=1)

        tk.Label(
            right, text="Random Preview",
            font=("Segoe UI",13,"bold"), fg="#DDDDDD", bg='#1e1e1e'
        ).pack(anchor='w', pady=(0,6), padx=12)

        preview_canvas = tk.Canvas(
            right,
            width=self.PREVIEW_SIZE,
            height=self.PREVIEW_SIZE,
            bg='white',
            highlightthickness=0
        )
        preview_canvas.pack(padx=12, pady=(0,12))
        self.preview_canvas = preview_canvas

        tk.Button(
            right, text="Generate Random Preview",
            command=self.calculate_radii,
            bg="#007BFF", fg="white",
            font=("Segoe UI",12,"bold"), width=20
        ).pack(pady=(0,12))

        # renderer will draw into preview_canvas
        self.renderer = None

    def load_data(self, data, json_path):
        self.json_path = json_path
        base = os.path.dirname(json_path)
        self.rooms_dir = os.path.join(base, 'rooms')
        os.makedirs(self.rooms_dir, exist_ok=True)

        try:
            self.map_data    = data or {}
            self.layers_data = self.map_data.get('map_layers', [])

            # clear old widgets
            for w in self.layers_frame.winfo_children():
                w.destroy()
            self.layer_widgets.clear()

            # recreate
            for ld in self.layers_data:
                self._add_layer_widget(ld)

            # init renderer
            self.renderer = MapRenderer(
                rooms_dir=self.rooms_dir,
                preview_canvas=self.preview_canvas,
                preview_size=self.PREVIEW_SIZE,
                layer_widgets=self.layer_widgets,
                factor=self.factor
            )
            self.calculate_radii()

        except Exception as e:
            messagebox.showerror("Load Failed", f"Could not load map_info.json:\n{e}")
            self.map_data = {}
            self.layers_data = []
            self.layer_widgets.clear()

        self.save()

    def _add_layer_widget(self, layer_data):
        layer = MapLayerInfo(
            self.layers_frame,
            level=layer_data.get("level", 0),
            rooms_dir=self.rooms_dir,
            name=layer_data.get("name", ""),
            radius=layer_data.get("radius", 0),
            rooms_data=layer_data.get("rooms", []),
            save_callback=self.save,
            delete_callback=self._on_delete_layer
        )
        layer.min_rooms_var.set(layer_data.get("min_rooms",0))
        layer.max_rooms_var.set(layer_data.get("max_rooms",0))
        layer.bg_frame.pack(side='left', fill='y', padx=6, pady=6)
        self.layer_widgets.append(layer)

    def _on_add_layer(self):
        lvl = len(self.layer_widgets)
        new_data = {'level':lvl, 'name':f"layer_{lvl}", 'radius':0, 'rooms':[]}
        self._add_layer_widget(new_data)
        self._reorder_levels()
        self.save()

    def _on_delete_layer(self, lw):
        if lw in self.layer_widgets:
            self.layer_widgets.remove(lw)
        self._reorder_levels()
        self.save()

    def _reorder_levels(self):
        for i, layer in enumerate(self.layer_widgets):
            layer.set_level(i)

    def calculate_radii(self):
        if not self.renderer:
            return
        self.renderer.calculate_radii()
        for i, lw in enumerate(self.layer_widgets):
            if i < len(self.renderer.layer_radii):
                lw.set_radius(self.renderer.layer_radii[i])

    def save(self):
        if not self.renderer:
            return
        self.calculate_radii()
        for i, lw in enumerate(self.layer_widgets):
            if i < len(self.renderer.layer_radii):
                lw.set_radius(self.renderer.layer_radii[i])

        self.map_data['map_layers'] = [self._layer_to_data(lw) for lw in self.layer_widgets]
        if hasattr(self.renderer, 'map_radius_actual'):
            self.map_data['map_radius'] = self.renderer.map_radius_actual

        with open(self.json_path, 'w') as f:
            json.dump(self.map_data, f, indent=2)
        print(f"Saved map_layers and map_radius to {self.json_path}")

    def _layer_to_data(self, layer):
        d = layer.get_data()
        d['min_rooms'] = layer.min_rooms_var.get()
        d['max_rooms'] = layer.max_rooms_var.get()
        d['radius']    = layer.radius_var.get()
        return d

