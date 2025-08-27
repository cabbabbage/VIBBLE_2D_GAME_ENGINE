import os
import json
import tkinter as tk
from tkinter import ttk, messagebox, colorchooser
from PIL import Image
from pages.range import Range


class LightingControls(ttk.Frame):
    def __init__(self, parent, data=None, autosave_callback=None):
        super().__init__(parent, style="LS.TFrame")
        self.light_color = (255, 255, 255)
        self._autosave = autosave_callback or (lambda: None)

        self.intensity = Range(self, min_bound=0, max_bound=255, label="Light Intensity")
        self.radius = Range(self, min_bound=0, max_bound=2000, label="Radius (px)")
        self.falloff = Range(self, min_bound=0, max_bound=100, label="Falloff (%)")
        self.flicker = Range(self, min_bound=0, max_bound=20, label="Flicker")
        self.flare = Range(self, min_bound=0, max_bound=100, label="Flare (px)")
        self.offset_x = Range(self, min_bound=-2000, max_bound=2000, label="Offset X")
        self.offset_y = Range(self, min_bound=-2000, max_bound=2000, label="Offset Y")

        for rng in (self.flicker, self.flare, self.offset_x, self.offset_y):
            rng.set_fixed()

        for rng in (
            self.intensity, self.radius, self.falloff,
            self.flicker, self.flare, self.offset_x, self.offset_y
        ):
            rng.pack(fill=tk.X, padx=10, pady=4)
            rng.var_min.trace_add("write", lambda *_: self._autosave())
            rng.var_max.trace_add("write", lambda *_: self._autosave())
            rng.var_random.trace_add("write", lambda *_: self._autosave())

        self.color_preview = tk.Label(
            self, text="Pick Light Color", bg="#FFFFFF",
            font=("Segoe UI", 12), width=20, height=2, relief='raised'
        )
        self.color_preview.pack(padx=10, pady=4)
        self.color_preview.bind("<Button-1>", self._choose_color)

        if data:
            self.load_data(data)


    def _choose_color(self, _=None):
        rgb, hex_color = colorchooser.askcolor(initialcolor=self.color_preview['bg'])
        if rgb:
            self.light_color = tuple(map(int, rgb))
            self.color_preview.config(bg=hex_color)
            self._autosave()


    def load_data(self, data):
        self.intensity.set(data.get("light_intensity", 100), data.get("light_intensity", 100))
        self.radius.set(data.get("radius", 100), data.get("radius", 100))
        falloff_val = data.get("falloff", data.get("fall_off", 100))
        self.falloff.set(falloff_val, falloff_val)
        self.flicker.set(data.get("flicker", 0), data.get("flicker", 0))
        self.flare.set(data.get("flare", 0), data.get("flare", 0))
        self.offset_x.set(data.get("offset_x", 0), data.get("offset_x", 0))
        self.offset_y.set(data.get("offset_y", 0), data.get("offset_y", 0))

        if isinstance(data.get("light_color"), list):
            r, g, b = data["light_color"]
            self.light_color = (r, g, b)
            self.color_preview.config(bg="#%02x%02x%02x" % (r, g, b))

    def get_data(self):
        return {
            "has_light_source": True,
            "light_intensity": self.intensity.get_min(),
            "light_color": list(self.light_color),
            "radius": self.radius.get_min(),
            "falloff": self.falloff.get_min(),
            "flicker": self.flicker.get_min(),
            "flare": self.flare.get_max(),
            "offset_x": self.offset_x.get_min(),
            "offset_y": self.offset_y.get_min(),
        }


class OrbitalLightingControls(ttk.Frame):
    def __init__(self, parent, data=None):
        super().__init__(parent, style="LS.TFrame")

        self.intensity = Range(self, min_bound=0, max_bound=255, label="Light Intensity")
        self.radius = Range(self, min_bound=0, max_bound=2000, label="Radius (px)")
        self.y_radius = Range(self, min_bound=0, max_bound=2000, label="Y Orbit Radius (px)")
        self.x_radius = Range(self, min_bound=0, max_bound=2000, label="X Orbit Radius (px)")
        self.falloff = Range(self, min_bound=0, max_bound=100, label="Falloff (%)")
        self.factor = Range(self, min_bound=1.0, max_bound=200.0, label="Factor")

        for rng in (
            self.intensity, self.radius, self.y_radius,
            self.x_radius, self.falloff, self.factor
        ):
            rng.pack(fill=tk.X, padx=10, pady=4)

        if data:
            self.load_data(data)

    def load_data(self, data):
        self.intensity.set(data.get("light_intensity", 100), data.get("light_intensity", 100))
        self.radius.set(data.get("radius", 300), data.get("radius", 300))
        self.y_radius.set(data.get("y_radius", 100), data.get("y_radius", 100))
        self.x_radius.set(data.get("x_radius", 100), data.get("x_radius", 100))
        falloff_val = data.get("falloff", data.get("fall_off", 80))
        self.falloff.set(falloff_val, falloff_val)
        self.factor.set(data.get("factor", 1), data.get("factor", 200))

    def get_data(self):
        return {
            "has_light_source": True,
            "light_intensity": self.intensity.get_min(),
            "radius": self.radius.get_min(),
            "y_radius": self.y_radius.get_min(),
            "x_radius": self.x_radius.get_min(),
            "falloff": self.falloff.get_min(),
            "factor": self.factor.get_min()
        }
    
class LightSourceFrame(ttk.Frame):
    def __init__(self, parent, index, data=None, on_delete=None, autosave_callback=None):
        super().__init__(parent, style="LS.TFrame")
        self.index = index
        self._on_delete = on_delete

        tk.Button(
            self, text="X", bg="#D9534F", fg="white",
            font=("Segoe UI", 12, "bold"), width=3, padx=4,
            command=self._delete_self
        ).pack(anchor="ne", padx=4, pady=4)

        ttk.Label(
            self, text=f"Light Source {index + 1}",
            font=("Segoe UI", 13, "bold"), foreground="#DDDDDD", background="#2a2a2a"
        ).pack(anchor="w", padx=10, pady=(4, 10))

        self.control = LightingControls(self, data=data, autosave_callback=autosave_callback)
        self.control.pack(fill=tk.X, expand=True)

    def _delete_self(self):
        if self._on_delete:
            self._on_delete(self.index)

    def get_data(self):
        return self.control.get_data()

    def load_data(self, data):
        self.control.load_data(data)
