import tkinter as tk
from tkinter import ttk
from PIL import Image, ImageTk, ImageFilter
import numpy as np
from shared.range import Range


class MaskModePanel(tk.Frame):
    def __init__(self, parent, frames, scale, anchor):
        super().__init__(parent, bg="#1e1e1e")
        self.frames = frames
        self.scale = scale
        self.anchor = anchor
        self.anchor_x, self.anchor_y = anchor
        self.orig_w, self.orig_h = self.frames[0].size

        self.disp_w = int(self.orig_w * self.scale)
        self.disp_h = int(self.orig_h * self.scale)

        self._build_ui()
        self._prepare_images()

    def _build_ui(self):
        self.canvas = tk.Canvas(self, bg='black', highlightthickness=0)
        self.canvas.pack(fill='both', expand=True)

        self.slider_frame = ttk.Frame(self, style="Dark.TFrame")
        self.slider_frame.pack(fill='x', padx=10, pady=(8, 12))

        ttk.Style().configure("Dark.TFrame", background="#1e1e1e")
        ttk.Style().configure("Dark.TLabel", background="#1e1e1e", foreground="#FFFFFF", font=("Segoe UI", 12))

        self.sliders = {}
        for label, key in [("Top", "top_pct"), ("Bottom", "bottom_pct"), ("Left", "left_pct"), ("Right", "right_pct")]:
            row = ttk.Frame(self.slider_frame, style="Dark.TFrame")
            row.pack(fill="x", pady=4)
            ttk.Label(row, text=f"{label} Mask %:", style="Dark.TLabel").pack(side="left", padx=(0, 8))
            rng = Range(row, min_bound=0, max_bound=100, set_min=0, set_max=0, force_fixed=True)
            rng.pack(side="left", fill="x", expand=True)
            rng.var_max.trace_add("write", lambda *_: self._refresh_preview())
            self.sliders[key] = rng

    def _prepare_images(self):
        self.mask_arr = self._get_union_mask_arr()
        self.base_img = self.frames[0].resize((self.disp_w, self.disp_h), Image.LANCZOS)
        self.tk_base = ImageTk.PhotoImage(self.base_img)
        # Center the image on the canvas
        self.canvas.delete('all')
        self.img_id = self.canvas.create_image(self.canvas.winfo_width() // 2,
                                              self.canvas.winfo_height() // 2,
                                              anchor='center', image=self.tk_base)
        # Keep centered on resize
        def _on_cfg(e):
            try:
                self.canvas.coords(self.img_id, e.width // 2, e.height // 2)
            except Exception:
                pass
        self.canvas.bind('<Configure>', _on_cfg)
        self._refresh_preview()

    def _get_union_mask_arr(self):
        alpha_layers = [np.array(frame.split()[-1]) > 0 for frame in self.frames]
        return np.logical_or.reduce(alpha_layers)

    def _refresh_preview(self):
        arr = self.mask_arr.copy()
        h0, w0 = arr.shape

        top = self.sliders['top_pct'].get_max() / 100.0
        bot = self.sliders['bottom_pct'].get_max() / 100.0
        left = self.sliders['left_pct'].get_max() / 100.0
        right = self.sliders['right_pct'].get_max() / 100.0

        arr[:int(top * h0), :] = False
        arr[int((1 - bot) * h0):, :] = False
        arr[:, :int(left * w0)] = False
        arr[:, int((1 - right) * w0):] = False

        overlay = np.zeros((h0, w0, 4), dtype=np.uint8)
        overlay[arr, 0] = 255
        overlay[arr, 3] = 128
        ov_img = Image.fromarray(overlay, 'RGBA').resize((self.disp_w, self.disp_h), Image.NEAREST)

        comp = self.base_img.copy()
        comp.paste(ov_img, (0, 0), ov_img)
        self.tk_overlay = ImageTk.PhotoImage(comp)
        self.canvas.itemconfig(self.img_id, image=self.tk_overlay)

        self.cropped_mask = arr

    def _extract_edge_pixels(self, mask_arr):
        img = Image.fromarray((mask_arr.astype(np.uint8)) * 255)
        edges = img.filter(ImageFilter.FIND_EDGES)
        arr = np.array(edges) > 0
        pts = list(zip(*np.nonzero(arr)[::-1]))
        return [(int(x), int(y)) for x, y in pts]

    def get_points(self):
        pts = self._extract_edge_pixels(self.cropped_mask)
        return [
            (x - self.anchor_x, y - self.anchor_y)
            for x, y in pts
        ]

    def update_zoom(self, new_scale):
        self.scale = new_scale
        self.disp_w = int(self.orig_w * self.scale)
        self.disp_h = int(self.orig_h * self.scale)
        self._prepare_images()

