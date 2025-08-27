# === File: pages/map_light_page.py ===
import os
import json
import tkinter as tk
from tkinter import ttk, messagebox, colorchooser
from pages.key_color import ColorKeyCircleEditor
from pages.range import Range

DEFAULTS = {
    "radius": 1920,
    "intensity": 255,
    "orbit_radius": 150,
    "update_interval": 2,
    "mult": 0.4,
    "fall_off": 1.0,
    "base_color": [255, 255, 255, 255],
    "keys": []
}

class MapLightPage(tk.Frame):
    def __init__(self, parent):
        super().__init__(parent, bg='#1e1e1e')
        self.json_path = None
        self.data = DEFAULTS.copy()
        self._suspend_save = False

        title = tk.Label(self, text="Map Lighting", font=("Segoe UI", 20, "bold"),
                         fg="#005f73", bg='#1e1e1e')
        title.pack(fill=tk.X, pady=(10, 20))

        # === Even Left/Right Split ===
        main_frame = tk.Frame(self, bg='#1e1e1e')
        main_frame.pack(fill="both", expand=True)
        main_frame.columnconfigure(0, weight=1)
        main_frame.columnconfigure(1, weight=1)

        left_frame = tk.Frame(main_frame, bg='#2a2a2a')
        left_frame.grid(row=0, column=0, sticky="nsew", padx=(10, 5), pady=10)

        right_frame = tk.Frame(main_frame, bg='#1e1e1e')
        right_frame.grid(row=0, column=1, sticky="nsew", padx=(5, 10), pady=10)
        right_frame.columnconfigure(0, weight=1)
        right_frame.rowconfigure(0, weight=1)

        # === Left scrollable settings panel ===
        canvas = tk.Canvas(left_frame, bg='#2a2a2a', highlightthickness=0)
        scroll_frame = tk.Frame(canvas, bg='#2a2a2a')
        window_id = canvas.create_window((0, 0), window=scroll_frame, anchor='nw')
        scroll_frame.bind('<Configure>', lambda e: canvas.configure(scrollregion=canvas.bbox('all')))
        canvas.bind('<Configure>', lambda e: canvas.itemconfig(window_id, width=e.width))
        scroll_frame.bind('<Enter>', lambda e: canvas.bind_all('<MouseWheel>', lambda ev: canvas.yview_scroll(int(-1 * (ev.delta / 120)), 'units')))
        scroll_frame.bind('<Leave>', lambda e: canvas.unbind_all('<MouseWheel>'))
        canvas.pack(fill=tk.BOTH, expand=True)

        # === Sliders and controls ===
        self.radius_range = Range(scroll_frame, 0, 9999, self.data["radius"], self.data["radius"], force_fixed=True, label="Radius")
        self.radius_range.pack(fill="x", padx=12, pady=4)
        self.radius_range.var_max.trace_add("write", lambda *_: self._on_change("radius", self.radius_range.get_max()))

        self.intensity_range = Range(scroll_frame, 0, 255, self.data["intensity"], self.data["intensity"], force_fixed=True, label="Intensity")
        self.intensity_range.pack(fill="x", padx=12, pady=4)
        self.intensity_range.var_max.trace_add("write", lambda *_: self._on_change("intensity", self.intensity_range.get_max()))

        self.orbit_radius_range = Range(scroll_frame, 0, 9999, self.data["orbit_radius"], self.data["orbit_radius"], force_fixed=True, label="Orbit Radius")
        self.orbit_radius_range.pack(fill="x", padx=12, pady=4)
        self.orbit_radius_range.var_max.trace_add("write", lambda *_: self._on_change("orbit_radius", self.orbit_radius_range.get_max()))

        self.update_interval_range = Range(scroll_frame, 1, 100, self.data["update_interval"], self.data["update_interval"], force_fixed=True, label="Update Interval")
        self.update_interval_range.pack(fill="x", padx=12, pady=4)
        self.update_interval_range.var_max.trace_add("write", lambda *_: self._on_change("update_interval", self.update_interval_range.get_max()))

        self.mult_range = Range(scroll_frame, 0, 100,
                                int(self.data["mult"] * 100), int(self.data["mult"] * 100),
                                force_fixed=True, label="Mult")
        self.mult_range.pack(fill="x", padx=12, pady=4)
        self.mult_range.var_max.trace_add("write", lambda *_: self._on_change("mult", round(self.mult_range.get_max() / 100.0, 3)))

        self.falloff_range = Range(scroll_frame, 0, 100,
                                   int(self.data["fall_off"]), int(self.data["fall_off"]),
                                   force_fixed=True, label="Fall-off")
        self.falloff_range.pack(fill="x", padx=12, pady=4)
        self.falloff_range.var_max.trace_add("write", lambda *_: self._on_change("fall_off", self.falloff_range.get_max()))

        # Base Color Picker
        color_frame = tk.Frame(scroll_frame, bg='#2a2a2a')
        color_frame.pack(fill="x", padx=12, pady=4)
        tk.Label(color_frame, text="Base Color", font=("Segoe UI", 12), fg='#FFFFFF', bg='#2a2a2a').pack(side="left")
        self.base_color_btn = tk.Button(
            color_frame, text="Pick Color", width=12, command=self._pick_base_color,
            bg="#%02x%02x%02x" % tuple(self.data["base_color"][:3]), relief='raised'
        )
        self.base_color_btn.pack(side="left", padx=6)

        divider = tk.Label(scroll_frame, text="Key Colors", font=("Segoe UI", 13, "bold"),
                           fg='#DDDDDD', bg='#2a2a2a')
        divider.pack(anchor='w', padx=12, pady=(16, 4))

        # === Right side: Centered ColorKeyCircleEditor ===
        self.color_editor = ColorKeyCircleEditor(
            right_frame, on_change=self._on_keys_changed,
            width=1000, height=900
        )
        self.color_editor.configure(bg='#2a2a2a')
        self.color_editor.grid(row=0, column=0, sticky="n", padx=0, pady=0)

    def _pick_base_color(self):
        hex_color = "#%02x%02x%02x" % tuple(self.data.get("base_color", DEFAULTS["base_color"])[:3])
        rgb = colorchooser.askcolor(color=hex_color)[0]
        if rgb:
            self.data["base_color"] = [int(rgb[0]), int(rgb[1]), int(rgb[2]), 255]
            self.base_color_btn.configure(bg="#%02x%02x%02x" % tuple(self.data["base_color"][:3]))
            self._save_json()

    def load_data(self, data=None, json_path=None):
        if not json_path:
            return
        self.json_path = json_path
        self.data = DEFAULTS.copy()
        if isinstance(data, dict):
            self.data.update(data)
        self._suspend_save = True

        self.radius_range.set(self.data["radius"], self.data["radius"])
        self.intensity_range.set(self.data["intensity"], self.data["intensity"])
        self.orbit_radius_range.set(self.data["orbit_radius"], self.data["orbit_radius"])
        self.update_interval_range.set(self.data["update_interval"], self.data["update_interval"])
        self.mult_range.set(int(self.data["mult"] * 100), int(self.data["mult"] * 100))
        self.falloff_range.set(int(self.data["fall_off"]), int(self.data["fall_off"]))
        self.base_color_btn.configure(bg="#%02x%02x%02x" % tuple(self.data["base_color"][:3]))
        self.color_editor.load_keys(self.data.get("keys", []))

        self._suspend_save = False

    def _on_change(self, key, value):
        if self._suspend_save:
            return
        self.data[key] = value
        self._save_json()

    def _on_keys_changed(self, keys):
        if self._suspend_save:
            return
        self.data["keys"] = keys
        self._save_json()

    def _save_json(self):
        if not self.json_path:
            return
        try:
            with open(self.json_path, "w") as f:
                json.dump(self.data, f, indent=2)
        except Exception as e:
            messagebox.showerror("Save Failed", str(e))

    @staticmethod
    def get_json_filename():
        return "map_light.json"
