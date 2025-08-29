import tkinter as tk
from tkinter import ttk
from PIL import Image, ImageTk, ImageDraw, ImageFilter
import numpy as np
from shared.range import Range


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

        # Canvas state
        self.canvas_w = 1
        self.canvas_h = 1

        # Anchor-locked drawing mask (in display pixels)
        self.mask_img = None  # PIL 'L'
        self.mask_ox = 0      # mask coordinate of anchor X
        self.mask_oy = 0      # mask coordinate of anchor Y

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
        # Base (resized) image used for preview; image is centered on the canvas
        self.base_img = self.frames[0].resize((self.disp_w, self.disp_h), Image.LANCZOS)
        self.tk_base = ImageTk.PhotoImage(self.base_img)
        self.canvas.delete('all')
        # Place image at canvas center
        self.img_id = self.canvas.create_image(self.canvas_w // 2, self.canvas_h // 2,
                                               anchor='center', image=self.tk_base)
        # Initialize a large mask if missing
        if self.mask_img is None:
            self._init_mask()

    def _bind_events(self):
        self.canvas.bind("<B1-Motion>", self._on_draw)
        self.canvas.bind("<Button-1>", self._on_draw)
        self.canvas.bind("<Motion>", self._show_brush_cursor)
        self.canvas.bind("<Configure>", self._on_canvas_configure)

    def _on_canvas_configure(self, event):
        # Track canvas size and keep image centered
        self.canvas_w, self.canvas_h = event.width, event.height
        try:
            self.canvas.coords(self.img_id, self.canvas_w // 2, self.canvas_h // 2)
        except Exception:
            pass
        # Ensure mask exists
        if self.mask_img is None:
            self._init_mask()
        # Redraw preview with new centering
        self._refresh_draw_preview()

    def _init_mask(self):
        # Start with a mask as large as the current canvas (or the image), anchor at its corresponding position
        w = max(self.canvas_w, self.disp_w, 1)
        h = max(self.canvas_h, self.disp_h, 1)
        self.mask_img = Image.new('L', (w, h), 0)
        # Place anchor in the mask where it would be on the canvas: center x, center y + disp_h/2
        self.mask_ox = w // 2
        self.mask_oy = h // 2 + self.disp_h // 2

    def _on_brush_change(self, *_):
        self.BRUSH_RADIUS = self.brush_range.get_max()
        self._refresh_draw_preview()

    def _on_draw(self, event):
        # Allow drawing anywhere on the canvas (not limited to image bounds)
        x, y = int(event.x), int(event.y)
        # Convert canvas coords to mask coords (anchor-locked)
        ax, ay = self._anchor_canvas_coords()
        px = int(self.mask_ox + (x - ax))
        py = int(self.mask_oy + (y - ay))
        r = int(self.BRUSH_RADIUS)
        self._ensure_mask_capacity(px - r, py - r, px + r, py + r)
        d = ImageDraw.Draw(self.mask_img)
        d.ellipse((px - r, py - r, px + r, py + r), fill=255)
        self._refresh_draw_preview()

    def _anchor_canvas_coords(self):
        # Bottom-center of the image on the canvas
        cx = self.canvas_w / 2.0
        cy = self.canvas_h / 2.0 + self.disp_h / 2.0
        return cx, cy

    def _ensure_mask_capacity(self, left, top, right, bottom):
        # Expand mask image if drawing goes out-of-bounds
        w, h = self.mask_img.size
        new_left = min(0, left)
        new_top = min(0, top)
        new_right = max(w, right)
        new_bottom = max(h, bottom)
        if new_left == 0 and new_top == 0 and new_right == w and new_bottom == h:
            return
        new_w = int(new_right - new_left)
        new_h = int(new_bottom - new_top)
        new_mask = Image.new('L', (new_w, new_h), 0)
        # Paste old mask into new, adjusting origin for anchor
        offset_x = -int(new_left)
        offset_y = -int(new_top)
        new_mask.paste(self.mask_img, (offset_x, offset_y))
        self.mask_img = new_mask
        self.mask_ox += offset_x
        self.mask_oy += offset_y

    def _refresh_draw_preview(self):
        # Compose a full-canvas preview so drawing is visible beyond image bounds
        cw = max(1, int(self.canvas_w))
        ch = max(1, int(self.canvas_h))
        comp = Image.new('RGBA', (cw, ch), (0, 0, 0, 255))

        # Paste the centered base image
        img_left = int(round(cw / 2.0 - self.disp_w / 2.0))
        img_top = int(round(ch / 2.0 - self.disp_h / 2.0))
        comp.paste(self.base_img.convert('RGBA'), (img_left, img_top))

        # Build a red overlay from the mask and paste aligned to the anchor
        if self.mask_img is not None:
            ax, ay = self._anchor_canvas_coords()
            ox = int(round(ax - self.mask_ox))
            oy = int(round(ay - self.mask_oy))
            overlay = Image.new('RGBA', self.mask_img.size, (255, 0, 0, 0))
            # semi-transparent red
            overlay.putalpha(self.mask_img.point(lambda v: int(v * 0.5)))
            comp.paste(overlay, (ox, oy), overlay)

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
        # Extract edge points from the full mask (anchor-locked)
        pts = self._extract_edge_pixels(self.mask_img)
        result = []
        for mx, my in pts:
            dx_disp = mx - self.mask_ox
            dy_disp = my - self.mask_oy
            # Convert display pixels back to original pixel units (relative to bottom-center)
            rx = int(round(dx_disp / max(self.scale, 1e-6)))
            ry = int(round(dy_disp / max(self.scale, 1e-6)))
            result.append((rx, ry))
        return result

    def update_zoom(self, new_scale):
        if new_scale <= 0:
            return
        # Resample mask to keep geometry consistent while zooming
        ratio = new_scale / self.scale if self.scale else 1.0
        self.scale = new_scale
        self.disp_w = int(self.orig_w * self.scale)
        self.disp_h = int(self.orig_h * self.scale)
        # Resize mask and adjust anchor origin accordingly
        if self.mask_img is not None and abs(ratio - 1.0) > 1e-6:
            new_w = max(1, int(round(self.mask_img.width * ratio)))
            new_h = max(1, int(round(self.mask_img.height * ratio)))
            self.mask_img = self.mask_img.resize((new_w, new_h), resample=Image.NEAREST)
            self.mask_ox = int(round(self.mask_ox * ratio))
            self.mask_oy = int(round(self.mask_oy * ratio))
        self._prepare_images()
        self._refresh_draw_preview()
