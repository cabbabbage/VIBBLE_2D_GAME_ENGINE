# === File: point_configurator.py ===
import os
import tkinter as tk
from tkinter import ttk
from PIL import Image, ImageTk, ImageDraw
import numpy as np


def bottom_center_of_opaque_region(frames):
    arrs = [np.array(frame.split()[-1]) > 0 for frame in frames]
    union = np.logical_or.reduce(arrs)
    rows, cols = np.nonzero(union)
    if rows.size == 0 or cols.size == 0:
        return (0.0, 0.0)

    max_y = rows.max()
    cols_at_bottom = cols[rows == max_y]
    if cols_at_bottom.size == 0:
        return (0.0, float(max_y))
    center_x = np.mean(cols_at_bottom)
    return (center_x, float(max_y))



class PointConfigurator(tk.Toplevel):
    def __init__(self, master, base_folder):
        super().__init__(master)
        self.title("Point Configurator")
        self.state('zoomed')

        files = sorted(f for f in os.listdir(base_folder) if f.lower().endswith('.png'))
        self.frames = [Image.open(os.path.join(base_folder, f)).convert('RGBA') for f in files]
        if not self.frames:
            self.destroy()
            return

        self.orig_w, self.orig_h = self.frames[0].size
        self.anchor = bottom_center_of_opaque_region(self.frames)
        self.user_point = None
        self.scale = 0.4
        self.radius = 0
        self.result = None

        ttk.Label(self, text="Anchor Point", font=('Segoe UI', 12, 'bold')).pack(pady=(20, 5))
        self.coord_label = ttk.Label(self, text=self._coord_text())
        self.coord_label.pack(pady=(0, 10))

        self.canvas = tk.Canvas(self, width=self.orig_w * self.scale, height=self.orig_h * self.scale)
        self.canvas.pack()
        self.canvas.bind("<Button-1>", self._on_click)

        ttk.Label(self, text="Radius (px):").pack(pady=(10, 2))
        self.radius_var = tk.IntVar(value=0)
        radius_slider = tk.Scale(self, from_=0, to=200, orient='horizontal',
                                 variable=self.radius_var, command=self._on_radius_change)
        radius_slider.pack()

        self.tk_img = None
        self._draw_preview()

        ttk.Button(self, text="Return Point + Radius", command=self.finish).pack(pady=10)

    def _coord_text(self):
        if self.user_point:
            x, y = self.user_point
            return f"User-selected point: ({int(round(x))}, {int(round(y))}) | Radius: {self.radius}"
        else:
            x, y = self.anchor
            return f"Default anchor point: ({int(round(x))}, {int(round(y))}) | Radius: {self.radius}"

    def _draw_preview(self):
        base = self.frames[0].copy().convert("RGBA")
        overlay = Image.new("RGBA", base.size, (0, 0, 0, 0))
        draw = ImageDraw.Draw(overlay)

        x, y = self.user_point if self.user_point else self.anchor

        # Red dot = anchor
        ax, ay = self.anchor
        draw.ellipse((ax - 6, ay - 6, ax + 6, ay + 6), fill="red")

        # Green dot = user pick
        if self.user_point:
            draw.ellipse((x - 5, y - 5, x + 5, y + 5), fill="green")

        # Transparent radius circle
        if self.radius > 0:
            draw.ellipse((x - self.radius, y - self.radius,
                          x + self.radius, y + self.radius),
                         fill=(0, 255, 0, 64))

        # Merge overlay
        base = Image.alpha_composite(base, overlay)

        # Resize and render
        scaled = base.resize((int(base.width * self.scale), int(base.height * self.scale)), Image.LANCZOS)
        self.tk_img = ImageTk.PhotoImage(scaled)
        self.canvas.delete("all")
        self.canvas.create_image(0, 0, anchor='nw', image=self.tk_img)

    def _on_click(self, event):
        x = event.x / self.scale
        y = event.y / self.scale
        self.user_point = (x, y)
        self.coord_label.config(text=self._coord_text())
        self._draw_preview()

    def _on_radius_change(self, _=None):
        self.radius = self.radius_var.get()
        self.coord_label.config(text=self._coord_text())
        self._draw_preview()

    def finish(self):
        pt = self.user_point if self.user_point else self.anchor
        self.result = {
            "x": int(round(pt[0])),
            "y": int(round(pt[1])),
            "radius": self.radius
        }
        self.destroy()

    def get_selected_point(self):
        return self.result
