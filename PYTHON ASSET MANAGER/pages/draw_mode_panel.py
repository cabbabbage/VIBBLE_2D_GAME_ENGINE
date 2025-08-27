import tkinter as tk
from tkinter import ttk
from PIL import Image, ImageTk, ImageDraw, ImageFilter
import numpy as np
from pages.range import Range


class DrawModePanel(tk.Frame):
    def __init__(self, parent, frames, scale, anchor):
        super().__init__(parent, bg="#1e1e1e")
        self.frames = frames
        self.scale = scale
        self.anchor = anchor
        self.anchor_x, self.anchor_y = anchor
        self.orig_w, self.orig_h = self.frames[0].size

        self.disp_w = int(self.orig_w * self.scale)
        self.disp_h = int(self.orig_h * self.scale)

        self.BRUSH_RADIUS = 10
        self.cursor_oval = None

        self._build_ui()
        self._prepare_images()
        self._bind_events()

    def _build_ui(self):
        self.canvas = tk.Canvas(self, bg="#000000", highlightthickness=0)
        self.canvas.pack(fill='both', expand=True)

        self.slider_frame = ttk.Frame(self, style="Dark.TFrame")
        self.slider_frame.pack(fill='x', padx=10, pady=(6, 10))

        ttk.Style().configure("Dark.TFrame", background="#1e1e1e")
        ttk.Style().configure("Dark.TLabel", background="#1e1e1e", foreground="#FFFFFF", font=("Segoe UI", 12))

        ttk.Label(self.slider_frame, text="Brush Size:", style="Dark.TLabel").pack(side='left', padx=(0, 8))

        self.brush_range = Range(self.slider_frame, min_bound=1, max_bound=max(self.disp_w, self.disp_h) // 2,
                                 set_min=self.BRUSH_RADIUS, set_max=self.BRUSH_RADIUS, force_fixed=True)
        self.brush_range.pack(side='left', fill='x', expand=True)
        self.brush_range.var_max.trace_add("write", self._on_brush_change)

    def _prepare_images(self):
        self.base_img = self.frames[0].resize((self.disp_w, self.disp_h), Image.LANCZOS)
        self.draw_mask = Image.new('L', (self.disp_w, self.disp_h), 0)

        self.tk_base = ImageTk.PhotoImage(self.base_img)
        self.canvas.delete('all')
        self.img_id = self.canvas.create_image(0, 0, anchor='nw', image=self.tk_base)

    def _bind_events(self):
        self.canvas.bind("<B1-Motion>", self._on_draw)
        self.canvas.bind("<Button-1>", self._on_draw)
        self.canvas.bind("<Motion>", self._show_brush_cursor)

    def _on_brush_change(self, *_):
        self.BRUSH_RADIUS = self.brush_range.get_max()
        self._refresh_draw_preview()

    def _on_draw(self, event):
        x, y = event.x, event.y
        if 0 <= x < self.disp_w and 0 <= y < self.disp_h:
            d = ImageDraw.Draw(self.draw_mask)
            r = self.BRUSH_RADIUS
            d.ellipse((x - r, y - r, x + r, y + r), fill=255)
            self._refresh_draw_preview()

    def _refresh_draw_preview(self):
        comp = self.base_img.copy().convert('RGBA')
        red = Image.new('RGBA', (self.disp_w, self.disp_h), (255, 0, 0, 128))
        comp.paste(red, (0, 0), self.draw_mask)
        self.tk_draw = ImageTk.PhotoImage(comp)
        self.canvas.itemconfig(self.img_id, image=self.tk_draw)

    def _show_brush_cursor(self, event):
        if self.cursor_oval:
            self.canvas.delete(self.cursor_oval)
        r = self.BRUSH_RADIUS
        self.cursor_oval = self.canvas.create_oval(
            event.x - r, event.y - r, event.x + r, event.y + r,
            outline='white', width=1, dash=(2,)
        )

    def _extract_edge_pixels(self, mask):
        edges = mask.filter(ImageFilter.FIND_EDGES)
        arr = np.array(edges) > 0
        pts = list(zip(*np.nonzero(arr)[::-1]))
        return [(int(x), int(y)) for x, y in pts]

    def get_points(self):
        pts = self._extract_edge_pixels(self.draw_mask)
        scale_x = self.orig_w / self.disp_w
        scale_y = self.orig_h / self.disp_h
        return [
            (int(x * scale_x - self.anchor_x), int(y * scale_y - self.anchor_y))
            for x, y in pts
        ]

    def update_zoom(self, new_scale):
        self.scale = new_scale
        self.disp_w = int(self.orig_w * self.scale)
        self.disp_h = int(self.orig_h * self.scale)
        self._prepare_images()
        self._refresh_draw_preview()