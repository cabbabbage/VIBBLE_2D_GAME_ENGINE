import tkinter as tk
from tkinter import ttk
from shared.range import Range
from shared.search import AssetSearchWindow
import math
import random

class BatchAssetEditor(ttk.Frame):
    def __init__(self, parent):
        super().__init__(parent)

        self.pie = []  # list of {name, percent, color}
        self.slider = None
        self.selected_index = None

        self.colors = []
        self._generate_colors()

        self.grid_spacing = Range(self, label="Grid Spacing", min_bound=0, max_bound=400, set_min=100, set_max=100)
        self.grid_spacing.pack(fill=tk.X, padx=10, pady=5)

        self.canvas = tk.Canvas(self, bg="white", height=300)
        self.canvas.pack(fill=tk.BOTH, expand=True, padx=10, pady=(0, 10))
        self.canvas.bind("<Double-Button-1>", self._on_double_click)

        button_frame = ttk.Frame(self)
        button_frame.pack(fill=tk.X, padx=10, pady=4)

        ttk.Button(button_frame, text="Add Asset", command=self._add_asset).pack(side=tk.LEFT)
        ttk.Button(button_frame, text="Save", command=self.save).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="Load", command=self.load).pack(side=tk.LEFT, padx=5)

    def _generate_colors(self):
        for _ in range(100):
            self.colors.append("#%06x" % random.randint(0, 0xFFFFFF))

    def _add_asset(self):
        window = AssetSearchWindow(self)
        window.wait_window()
        name = getattr(window, "selected_asset", None)
        if name:
            if any(s["name"] == name for s in self.pie):
                return
            self._adjust_existing(5)
            self.pie.append({"name": name, "percent": 5, "color": self.colors[len(self.pie) % len(self.colors)]})
            self._redraw()

    def _adjust_existing(self, remove_amount):
        total = sum(s["percent"] for s in self.pie)
        if total == 0:
            return
        for s in self.pie:
            share = s["percent"] / total
            s["percent"] -= share * remove_amount

    def _redraw(self):
        self.canvas.delete("all")
        x, y, r = 200, 150, 100
        angle = 0
        for i, s in enumerate(self.pie):
            extent = 360 * (s["percent"] / 100)
            self.canvas.create_arc(x - r, y - r, x + r, y + r, start=angle, extent=extent, fill=s["color"], outline="black", tags=("sector", str(i)))
            mid_angle = math.radians(angle + extent / 2)
            label_x = x + r * 0.6 * math.cos(mid_angle)
            label_y = y + r * 0.6 * math.sin(mid_angle)
            self.canvas.create_text(label_x, label_y, text=f"{s['name']}\n{s['percent']:.1f}%", font=("Segoe UI", 9, "bold"))
            angle += extent

    def _on_double_click(self, event):
        clicked = self.canvas.find_withtag("current")
        if not clicked:
            return
        tags = self.canvas.gettags(clicked[0])
        if "sector" not in tags:
            return
        index = int(tags[1])
        self._open_slider(index)

    def _open_slider(self, index):
        if self.slider:
            self.slider.destroy()
        self.selected_index = index

        sector = self.pie[index]
        self.slider = ttk.Scale(self, from_=1, to=100, orient=tk.HORIZONTAL, command=self._on_slide)
        self.slider.set(sector["percent"])
        self.slider.pack(fill=tk.X, padx=10, pady=(0, 10))
        self.slider.bind("<Return>", lambda e: self.slider.pack_forget())

    def _on_slide(self, val):
        new_val = float(val)
        rest = [i for i in range(len(self.pie)) if i != self.selected_index]
        if not rest:
            self.pie[self.selected_index]["percent"] = new_val
            self._redraw()
            return

        leftover = 100 - new_val
        current_total = sum(self.pie[i]["percent"] for i in rest)
        for i in rest:
            if current_total == 0:
                self.pie[i]["percent"] = leftover / len(rest)
            else:
                ratio = self.pie[i]["percent"] / current_total
                self.pie[i]["percent"] = leftover * ratio
        self.pie[self.selected_index]["percent"] = new_val
        self._redraw()

    def save(self):
        return {
            "grid_spacing_min": self.grid_spacing.get_min(),
            "grid_spacing_max": self.grid_spacing.get_max(),
            "pie": self.pie
        }

    def load(self, data=None):
        if not data:
            return
        self.grid_spacing.set(data.get("grid_spacing_min", 100), data.get("grid_spacing_max", 100))
        self.pie = data.get("pie", [])
        self._redraw()
