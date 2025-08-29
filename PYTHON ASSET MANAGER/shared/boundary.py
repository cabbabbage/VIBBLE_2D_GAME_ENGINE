import os
import tkinter as tk
from tkinter import ttk, messagebox
from PIL import Image, ImageTk
import numpy as np
from shared.draw_mode_panel import DrawModePanel
from shared.mask_mode_panel import MaskModePanel
from shared.range import Range

def union_mask_from_frames(frames):
    arrs = [np.array(frame.split()[-1]) > 0 for frame in frames]
    union = np.logical_or.reduce(arrs)
    return union.astype(np.uint8) * 255


class BoundaryConfigurator(tk.Toplevel):
    def __init__(self, master, base_folder, callback):
        super().__init__(master)
        self.title("Boundary Configurator")
        self.callback = callback
        self.state('zoomed')

        files = sorted(f for f in os.listdir(base_folder) if f.lower().endswith('.png'))
        self.frames = [Image.open(os.path.join(base_folder, f)).convert('RGBA') for f in files]
        if not self.frames:
            self.destroy()
            return

        self.orig_w, self.orig_h = self.frames[0].size
        self.anchor = (self.orig_w / 2.0, float(self.orig_h))
        self.scale = 0.2
        self.zoom_var = tk.DoubleVar(value=self.scale)
        self.mode_frame = None
        self.current_mode = None

        self._build_ui()

    def _build_ui(self):
        self.configure(bg="#1e1e1e")
        ctrl = ttk.Frame(self, style="Dark.TFrame")
        ctrl.grid(row=0, column=0, sticky='ew', pady=(10, 10), padx=10)
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)

        ttk.Style().configure("Dark.TFrame", background="#1e1e1e")
        ttk.Style().configure("Dark.TLabel", background="#1e1e1e", foreground="#FFFFFF", font=("Segoe UI", 12))
        ttk.Style().configure("DarkHeader.TLabel", background="#1e1e1e", foreground="#DDDDDD", font=("Segoe UI", 20, "bold"))
        ttk.Style().configure("Dark.TButton", background="#007BFF", foreground="white", font=("Segoe UI", 13, "bold"))

        ttk.Label(ctrl, text="Boundary Configurator", style="DarkHeader.TLabel")\
            .pack(side="left", padx=(0, 30))

        for mode in ('draw', 'mask'):
            ttk.Button(ctrl, text=mode.title(), style="Dark.TButton", command=lambda m=mode: self.select_mode(m))\
                .pack(side='left', padx=(0, 8))

        ttk.Label(ctrl, text="Zoom:", style="Dark.TLabel").pack(side='left', padx=(20, 4))
        zoom_slider = Range(ctrl, min_bound=5, max_bound=200, set_min=int(self.scale * 100), set_max=int(self.scale * 100), force_fixed=True)
        zoom_slider.pack(side='left', fill='x', expand=True)
        zoom_slider.var_max.trace_add("write", lambda *_: self._on_zoom_slider_change(zoom_slider))

        self.mode_container = tk.Frame(self, bg="#2a2a2a")
        self.mode_container.grid(row=1, column=0, sticky='nsew', padx=10, pady=(0, 10))

        next_btn = tk.Button(self, text="Next", bg="#28a745", fg="white", font=("Segoe UI", 13, "bold"), width=18, command=self.finish)
        next_btn.grid(row=2, column=0, pady=(0, 20))

        self.select_mode('draw')

    def _on_zoom_slider_change(self, slider):
        self.scale = slider.get_max() / 100.0
        if self.mode_frame and hasattr(self.mode_frame, 'update_zoom'):
            self.mode_frame.update_zoom(self.scale)

    def select_mode(self, mode):
        if self.current_mode == mode:
            return
        if self.mode_frame:
            self.mode_frame.destroy()

        if mode == 'draw':
            self.mode_frame = DrawModePanel(self.mode_container, self.frames, self.scale, self.anchor)
        elif mode == 'mask':
            self.mode_frame = MaskModePanel(self.mode_container, self.frames, self.scale, self.anchor)
        else:
            raise ValueError("Unknown mode: " + mode)

        self.mode_frame.pack(fill='both', expand=True)
        self.current_mode = mode

    def finish(self):
        if not self.mode_frame:
            messagebox.showerror("Error", "No mode active")
            return

        try:
            raw_points = self.mode_frame.get_points()
        except Exception as e:
            messagebox.showerror("Error", f"Failed to extract points: {e}")
            return

        if not raw_points:
            messagebox.showerror("Error", "No boundary points found")
            return

        simplified = self._rdp(raw_points, epsilon=2.0)

        data = {
            "points": simplified,
            "anchor": "bottom_center",
            "anchor_point_in_image": [int(round(self.anchor[0])), int(round(self.anchor[1]))],
            "original_dimensions": [self.orig_w, self.orig_h],
            "type": self.current_mode
        }

        try:
            self.callback(data)
            if hasattr(self.master, "save"):
                self.master.save()
        except Exception as e:
            messagebox.showerror("Error", f"Failed to save: {e}")
        finally:
            self.destroy()

    def _rdp(self, pts, epsilon=2.0):
        if len(pts) < 3:
            return list(pts)
        a, b = pts[0], pts[-1]

        def point_line_dist(p):
            num = (p[0] - a[0]) * (b[0] - a[0]) + (p[1] - a[1]) * (b[1] - a[1])
            den = (b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2 + 1e-9
            t = max(0, min(1, num / den))
            proj = (a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]))
            return ((p[0] - proj[0]) ** 2 + (p[1] - proj[1]) ** 2) ** 0.5

        max_d, idx = 0, -1
        for i, p in enumerate(pts[1:-1], start=1):
            d = point_line_dist(p)
            if d > max_d:
                max_d, idx = d, i
        if max_d <= epsilon:
            return [a, b]
        left = self._rdp(pts[:idx + 1], epsilon)
        right = self._rdp(pts[idx:], epsilon)
        return left[:-1] + right

