# === File: pages/size.py ===
import os
import json
import tkinter as tk
from tkinter import ttk, messagebox
from PIL import Image, ImageTk
from shared.range import Range
from shared.apply_page_settings import ApplyPageSettings

class SizePage(tk.Frame):
    def __init__(self, parent):
        super().__init__(parent)
        self.asset_path = None
        self._orig_img = None
        self._fit_img = None
        self._loaded = False

        # Page boundary
        self.configure(bg='#1e1e1e')

        # Title header
        title = tk.Label(
            self, text="Size Settings",
            font=("Segoe UI", 20, "bold"),
            fg="#005f73", bg='#1e1e1e'
        )
        title.pack(fill=tk.X, pady=(10, 20))

        # Scrollable area
        canvas = tk.Canvas(self, bg='#2a2a2a', highlightthickness=0)
        scroll_frame = tk.Frame(canvas, bg='#2a2a2a')
        window_id = canvas.create_window((0, 0), window=scroll_frame, anchor='nw')
        scroll_frame.bind(
            '<Configure>',
            lambda e: canvas.configure(scrollregion=canvas.bbox('all'))
        )
        canvas.bind(
            '<Configure>',
            lambda e: canvas.itemconfig(window_id, width=e.width)
        )
        def _scroll(ev):
            canvas.yview_scroll(int(-1*(ev.delta/120)), 'units')
        scroll_frame.bind('<Enter>', lambda e: canvas.bind_all('<MouseWheel>', _scroll))
        scroll_frame.bind('<Leave>', lambda e: canvas.unbind_all('<MouseWheel>'))
        canvas.pack(fill=tk.BOTH, expand=True)

        # -- Image Preview --
        hdr1 = tk.Label(
            scroll_frame, text="Image Preview",
            font=("Segoe UI", 13, "bold"), fg="#DDDDDD", bg='#2a2a2a'
        )
        hdr1.pack(anchor='w', padx=12, pady=(10, 4))

        self.CANVAS_W, self.CANVAS_H = 480, 270
        self.preview = tk.Canvas(
            scroll_frame, width=self.CANVAS_W, height=self.CANVAS_H,
            bg='black', bd=2, relief='sunken'
        )
        self.preview.pack(padx=12, pady=(0, 8))

        # -- Size Controls --
        hdr2 = tk.Label(
            scroll_frame, text="Size Controls",
            font=("Segoe UI", 13, "bold"), fg="#DDDDDD", bg='#2a2a2a'
        )
        hdr2.pack(anchor='w', padx=12, pady=(10, 4))

        self.scale_range = Range(
            scroll_frame,
            min_bound=1, max_bound=150,
            set_min=100, set_max=100,
            force_fixed=True,
            label="Scale (%)"
        )
        self.threshold_range = Range(
            scroll_frame,
            min_bound=-100, max_bound=200,
            set_min=0, set_max=0,
            force_fixed=True,
            label="Z Threshold (px)"
        )
        for rw in (self.scale_range, self.threshold_range):
            rw.pack(fill='x', padx=12, pady=6)
            rw.var_min.trace_add("write", lambda *_: [self._rescale(), self._autosave()])
            rw.var_max.trace_add("write", lambda *_: [self._rescale(), self._autosave()])

        # -- Options --
        hdr3 = tk.Label(
            scroll_frame, text="Options",
            font=("Segoe UI", 13, "bold"), fg="#DDDDDD", bg='#2a2a2a'
        )
        hdr3.pack(anchor='w', padx=12, pady=(10, 4))
        ttk.Style().configure('W.TCheckbutton', font=("Segoe UI", 12), background='#2a2a2a', foreground='#FFFFFF')

        # ✅ Apply Page Settings Button
        apply_btn = ApplyPageSettings(
            scroll_frame,
            page_data=lambda: {
                "z_threshold": self.threshold_range.get()[1],
                "size_settings": {"scale_percentage": self.scale_range.get()[1]}
            },
            label="Apply Size Settings to Another Asset"
        )
        apply_btn.pack(pady=(6, 12))

        self._loaded = True

    def load(self, info_path):
        self.asset_path = info_path
        if not info_path:
            return
        if not os.path.exists(info_path):
            with open(info_path, 'w') as f:
                json.dump({}, f)
        with open(info_path, 'r') as f:
            data = json.load(f)
        ss = data.get("size_settings", {})
        self.scale_range.set(
            ss.get("scale_percentage", 100),
            ss.get("scale_percentage", 100)
        )
        zt = data.get('z_threshold', 0)
        self.threshold_range.set(zt, zt)
        base_dir = os.path.dirname(info_path)
        img_p = os.path.join(base_dir, "default", "0.png")
        self._orig_img = Image.open(img_p).convert('RGBA') if os.path.isfile(img_p) else None
        if self._orig_img:
            bw, bh = self._orig_img.size
            fit_scale = min(self.CANVAS_W/bw, self.CANVAS_H/bh)
            self._fit_img = self._orig_img.resize((int(bw*fit_scale), int(bh*fit_scale)), Image.LANCZOS)
        else:
            self._fit_img = None
        self._rescale()
        self._loaded = True

    def _rescale(self):
        self.preview.delete('all')
        if not self._fit_img:
            return
        base_pct = self.scale_range.get()[1]
        self._draw(self.preview, base_pct/100.0)
        thr = self.threshold_range.get()[1]
        orig_h = getattr(self._orig_img, 'height', 0)
        if orig_h:
            disp_h = self._fit_img.height * (base_pct/100.0)
            yoff = (self.CANVAS_H - disp_h)/2
            frac = 1.0 - thr/orig_h
            y = yoff + disp_h * frac
            self.preview.create_line(0, y, self.CANVAS_W, y, fill='red', width=2)

    def _draw(self, cvs, scale):
        w, h = self._fit_img.size
        sw, sh = int(w*scale), int(h*scale)
        if sw <= 0 or sh <= 0:
            return
        img = self._fit_img.resize((sw, sh), Image.LANCZOS)
        tk_img = ImageTk.PhotoImage(img)
        x = (int(cvs['width']) - sw) // 2
        y = (int(cvs['height']) - sh) // 2
        cvs.create_image(x, y, anchor='nw', image=tk_img)
        setattr(self, f'_img_{id(cvs)}', tk_img)

    def _autosave(self):
        if self._loaded:
            self.save()

    def save(self):
        if not self.asset_path:
            return
        try:
            with open(self.asset_path, 'r') as f:
                data = json.load(f)
        except Exception:
            data = {}
        data['z_threshold'] = self.threshold_range.get()[1]
        data['size_settings'] = { 'scale_percentage': self.scale_range.get()[1] }
        with open(self.asset_path, 'w') as f:
            json.dump(data, f, indent=4)

