import os
import json
import tkinter as tk
from tkinter import ttk, messagebox
from pages.boundary import BoundaryConfigurator
from pages.button import BlueButton
from pages.range import Range
from PIL import Image, ImageTk, ImageDraw

class AreaUI(ttk.Frame):
    def __init__(self, parent, json_path, label_text="Area:", scale=0.5, autosave_callback=None):
        super().__init__(parent)
        self.json_path = json_path
        self.label_text = label_text
        self.scale = scale
        self.autosave_callback = autosave_callback  # ✅ Trigger parent autosave

        self.frames_source = None
        self.area_data = None

        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)

        header = ttk.Frame(self)
        header.grid(row=0, column=0, sticky="ew", pady=(10, 0), padx=12)
        header.grid_columnconfigure(1, weight=1)

        ttk.Label(header, text=label_text, style='Large.TLabel')\
            .grid(row=0, column=0, sticky="w")

        self.area_label = ttk.Label(header, text="(none)", style='Large.TLabel')
        self.area_label.grid(row=0, column=1, sticky="w", padx=12)

        self.btn_configure = BlueButton(header, "Configure Area",
                                        command=self._configure_area, x=50, y=10)
        self.btn_configure.grid(row=0, column=2, sticky="e", padx=12)

        self.preview_canvas = tk.Canvas(self, bg='black', bd=2, relief='sunken')
        self.preview_canvas.grid(row=1, column=0, sticky='nsew', padx=12, pady=12)

        self.offset_x_range = Range(self, min_bound=-1000, max_bound=1000, set_min=0, set_max=0, force_fixed=True, label="Offset X")
        self.offset_x_range.grid(row=2, column=0, sticky='we', padx=12, pady=(2, 0))
        self.offset_x_range.var_max.trace_add("write", lambda *_: self._update_offset('offset_x'))

        self.offset_y_range = Range(self, min_bound=-1000, max_bound=1000, set_min=0, set_max=0, force_fixed=True, label="Offset Y")
        self.offset_y_range.grid(row=3, column=0, sticky='we', padx=12, pady=(2, 10))
        self.offset_y_range.var_max.trace_add("write", lambda *_: self._update_offset('offset_y'))

        self._load_area_json()
        self._draw_preview()

    def _trigger_autosave(self):
        if self.autosave_callback:
            self.autosave_callback()

    def _load_area_json(self):
        if not os.path.isfile(self.json_path):
            self.area_data = None
            self.area_label.config(text="(none)")
            return

        try:
            with open(self.json_path, 'r') as f:
                self.area_data = json.load(f)
            self.area_label.config(text="(configured)")
        except Exception as e:
            print(f"[AreaUI] Failed to load json: {e}")
            self.area_data = None
            self.area_label.config(text="(invalid)")

        ox = self.area_data.get("offset_x", 0)
        oy = self.area_data.get("offset_y", 0)
        self.offset_x_range.set(ox, ox)
        self.offset_y_range.set(oy, oy)

    def _configure_area(self):
        if not self.frames_source:
            messagebox.showerror("Error", "Set frames_source before configuring.")
            return
        BoundaryConfigurator(self, base_folder=self.frames_source, callback=self._boundary_callback)

    def _boundary_callback(self, geo):
        if isinstance(geo, list):
            geo = {'points': geo}
        self.area_data = geo

        ox, oy = self.offset_x_range.get()
        self.area_data["offset_x"] = ox
        self.area_data["offset_y"] = oy

        self.area_label.config(text="(configured)")
        self._save_json()
        self._draw_preview()
        self._trigger_autosave()

    def _update_offset(self, key):
        if not self.area_data:
            return
        val = self.offset_x_range.get()[0] if key == 'offset_x' else self.offset_y_range.get()[0]
        self.area_data[key] = val
        self._save_json()
        self._trigger_autosave()

    def _save_json(self):
        try:
            with open(self.json_path, "w") as f:
                json.dump(self.area_data, f, indent=2)
        except Exception as e:
            print(f"[AreaUI] Failed to write json: {e}")

    def _draw_preview(self):
        if not self.area_data:
            return

        default_frame = os.path.join(os.path.dirname(self.json_path), "default", "0.png")
        if not os.path.isfile(default_frame):
            return

        img = Image.open(default_frame).convert('RGBA')
        disp = (int(img.width * self.scale), int(img.height * self.scale))
        base = img.resize(disp, Image.LANCZOS)

        overlay = Image.new('RGBA', disp, (0, 0, 0, 0))
        draw = ImageDraw.Draw(overlay)

        anchor_x, anchor_y = self.area_data.get("anchor_point_in_image", [0, 0])
        offset_x = self.area_data.get("offset_x", 0)
        offset_y = self.area_data.get("offset_y", 0)

        points = [
            (int((x + anchor_x + offset_x) * self.scale), int((y + anchor_y + offset_y) * self.scale))
            for x, y in self.area_data.get("points", [])
        ]

        r = 2
        for dx, dy in points:
            if 0 <= dx < disp[0] and 0 <= dy < disp[1]:
                draw.ellipse((dx - r, dy - r, dx + r, dy + r), fill=(255, 0, 0, 180))

        full_width, full_height = img.size
        px = img.load()
        x_center = full_width // 2
        for y in reversed(range(full_height)):
            r, g, b, a = px[x_center, y]
            if a > 10:
                scaled_x = int(x_center * self.scale)
                scaled_y = int(y * self.scale)
                draw.ellipse(
                    (scaled_x - 4, scaled_y - 4, scaled_x + 4, scaled_y + 4),
                    fill=(0, 255, 0, 255)
                )
                break

        comp = Image.alpha_composite(base, overlay)
        self.tk_preview = ImageTk.PhotoImage(comp)

        self.preview_canvas.delete("all")
        self.preview_canvas.config(width=disp[0], height=disp[1])
        self.preview_canvas.create_image(0, 0, anchor='nw', image=self.tk_preview)
