# pages/image_overlay.py

import os
import json
import shutil
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from PIL import Image, ImageTk, ImageChops, ImageFilter, ImageDraw
import math

class ScrollableFrame(ttk.Frame):
    def __init__(self, container, *args, **kwargs):
        super().__init__(container, *args, **kwargs)
        canvas = tk.Canvas(self)
        scrollbar = ttk.Scrollbar(self, orient="vertical", command=canvas.yview)
        self.scrollable_frame = ttk.Frame(canvas)
        self.scrollable_frame.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )
        canvas.create_window((0, 0), window=self.scrollable_frame, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)
        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

class ImageOverlayPage(ttk.Frame):
    def __init__(self, parent):
        super().__init__(parent)

        # Theme & fonts
        FONT      = ('Segoe UI', 14)
        FONT_BOLD = ('Segoe UI', 18, 'bold')
        MAIN_COLOR      = "#005f73"
        SECONDARY_COLOR = "#ee9b00"

        # dynamic preview size: half the screen
        sw = self.winfo_screenwidth() // 2
        sh = self.winfo_screenheight() // 2
        self.preview_w = sw
        self.preview_h = sh

        self.asset_path = None
        self.overlays   = []

        style = ttk.Style(self)
        style.configure('Main.TButton', font=FONT, padding=6,
                        background=MAIN_COLOR, foreground='black')
        style.map('Main.TButton', background=[('active', SECONDARY_COLOR)])
        style.configure('Secondary.TButton', font=FONT, padding=6,
                        background=SECONDARY_COLOR, foreground='black')
        style.map('Secondary.TButton', background=[('active', MAIN_COLOR)])
        style.configure('Large.TLabel', font=FONT)
        style.configure('LargeBold.TLabel', font=FONT_BOLD,
                        foreground=SECONDARY_COLOR)

        ttk.Label(self, text="Image Overlay", style='LargeBold.TLabel')\
            .pack(pady=(10,5))
        ttk.Button(self, text="Add Overlay Folder",
                   style='Main.TButton',
                   command=self.add_overlay)\
            .pack(pady=5)

        self.scroll_frame = ScrollableFrame(self)
        self.scroll_frame.pack(fill='both', expand=True, padx=10, pady=5)

        ttk.Button(self, text="Save Overlays",
                   style='Secondary.TButton',
                   command=self.save)\
            .pack(pady=10)

    def add_overlay(self, cfg=None):
        frame = ttk.Labelframe(self.scroll_frame.scrollable_frame,
                               text=f"Overlay {len(self.overlays)+1}",
                               padding=10)
        frame.pack(fill='x', pady=5)

        state = {
            'src_folder': None,
            'frames': [],
            'offset_x': tk.IntVar(value=0),
            'offset_y': tk.IntVar(value=0),
            'scale_pct': tk.DoubleVar(value=100),
            'rotation': tk.IntVar(value=0),
            'mode': tk.StringVar(value='normal'),
            'alpha_pct': tk.IntVar(value=100),
            'blur_radius': tk.DoubleVar(value=0),
            'fade_radius': tk.DoubleVar(value=0),
            'canvas': None,
            'label': None,
            'preview_img': None
        }
        self.overlays.append(state)

        # Row 0: folder select
        btn = ttk.Button(frame, text="Select Folder", style='Main.TButton',
                         command=lambda s=state: self._choose_folder(s))
        btn.grid(row=0, column=0, sticky='w')
        lbl = ttk.Label(frame, text="(none)", style='Large.TLabel')
        lbl.grid(row=0, column=1, columnspan=3, sticky='w')
        state['label'] = lbl

        # Offsets (wider range)
        for i, (txt, var) in enumerate([
            ('Offset X:', state['offset_x']),
            ('Offset Y:', state['offset_y'])
        ], start=1):
            ttk.Label(frame, text=txt, style='Large.TLabel')\
                .grid(row=i, column=0)
            ttk.Scale(frame,
                      from_=-500, to=500,
                      variable=var,
                      orient='horizontal',
                      length=350,
                      command=lambda _e, s=state: self._update_preview(s))\
                .grid(row=i, column=1, columnspan=3, sticky='we', padx=5)

        # Scale & Rotation
        ttk.Label(frame, text="Scale (%):", style='Large.TLabel')\
            .grid(row=3, column=0)
        ttk.Scale(frame, from_=10, to=300, variable=state['scale_pct'],
                  orient='horizontal',
                  command=lambda _e, s=state: self._update_preview(s))\
            .grid(row=3, column=1)

        ttk.Label(frame, text="Rotation (°):", style='Large.TLabel')\
            .grid(row=3, column=2)
        ttk.Scale(frame, from_=0, to=360, variable=state['rotation'],
                  orient='horizontal',
                  command=lambda _e, s=state: self._update_preview(s))\
            .grid(row=3, column=3)

        # Mode & Transparency
        ttk.Label(frame, text="Mode:", style='Large.TLabel')\
            .grid(row=4, column=0)
        modes = ['normal','multiply','add']
        ttk.OptionMenu(frame, state['mode'], modes[0], *modes,
                       command=lambda _v, s=state: self._update_preview(s))\
            .grid(row=4, column=1)

        ttk.Label(frame, text="Alpha %:", style='Large.TLabel')\
            .grid(row=4, column=2)
        ttk.Scale(frame, from_=0, to=100, variable=state['alpha_pct'],
                  orient='horizontal',
                  command=lambda _e, s=state: self._update_preview(s))\
            .grid(row=4, column=3)

        # Gaussian blur
        ttk.Label(frame, text="Blur Radius:", style='Large.TLabel')\
            .grid(row=5, column=0)
        ttk.Scale(frame, from_=0, to=20, variable=state['blur_radius'],
                  orient='horizontal',
                  command=lambda _e, s=state: self._update_preview(s))\
            .grid(row=5, column=1, columnspan=3, sticky='we', padx=5)

        # Radial fade
        ttk.Label(frame, text="Fade Radius:", style='Large.TLabel')\
            .grid(row=6, column=0)
        ttk.Scale(frame, from_=0, to=max(self.preview_w,self.preview_h)//2,
                  variable=state['fade_radius'],
                  orient='horizontal',
                  length=350,
                  command=lambda _e, s=state: self._update_preview(s))\
            .grid(row=6, column=1, columnspan=3, sticky='we', padx=5)

        # Dynamic preview canvas
        cv = tk.Canvas(frame, width=self.preview_w, height=self.preview_h, bg='black')
        cv.grid(row=7, column=0, columnspan=4, pady=5)
        state['canvas'] = cv

    def _choose_folder(self, state):
        folder = filedialog.askdirectory()
        if not folder:
            return
        state['src_folder'] = folder
        state['frames'] = sorted(f for f in os.listdir(folder) if f.lower().endswith('.png'))
        state['label'].config(text=os.path.basename(folder))
        self._update_preview(state)

    def _load_base(self):
        """Load the asset's default/0.png and scale it to the preview size."""
        asset_dir = os.path.dirname(self.asset_path)
        p = os.path.join(asset_dir, 'default', '0.png')
        if not os.path.isfile(p):
            return None

        img = Image.open(p).convert('RGBA')
        w, h = img.size
        # remember full-resolution center
        self.center = (w // 2, h // 2)

        # fit into preview canvas exactly
        return img.resize((self.preview_w, self.preview_h), Image.LANCZOS)


    def _apply_radial_fade(self, img, radius):
        w,h = img.size
        cx,cy = w/2,h/2
        # create radial gradient mask
        mask = Image.new('L', (w,h), 255)
        draw = ImageDraw.Draw(mask)
        for y in range(h):
            for x in range(w):
                d = math.hypot(x-cx, y-cy)
                if d>radius:
                    alpha = 0
                else:
                    alpha = int(255 * (1 - (d/radius)))
                draw.point((x,y), fill=alpha)
        img.putalpha(mask)
        return img

    def _update_preview(self, state):
        """Draw base (scaled to preview size) then overlay at transformed position."""
        base = self._load_base()
        if base is None or not state['frames']:
            return
        comp = base.copy()

        # load the first frame from the folder
        src = os.path.join(state['src_folder'], state['frames'][0])
        ov = Image.open(src).convert('RGBA')

        # apply scale + rotate + alpha
        scale = state['scale_pct'].get() / 100.0
        ov2 = ov.resize((int(ov.width * scale), int(ov.height * scale)),
                        Image.LANCZOS)
        ov2 = ov2.rotate(-state['rotation'].get(), expand=True, resample=Image.BICUBIC)
        alpha = state['alpha_pct'].get() / 100.0
        mask = ov2.split()[3].point(lambda v: int(v * alpha))
        ov2.putalpha(mask)

        # compute overlay offset in preview coords
        orig_w, orig_h = self.center[0]*2, self.center[1]*2
        px = self.preview_w / (orig_w or 1)
        py = self.preview_h / (orig_h or 1)
        cx_p = int(self.center[0] * px)
        cy_p = int(self.center[1] * py)
        dx = int(cx_p - ov2.width // 2 + state['offset_x'].get())
        dy = int(cy_p - ov2.height // 2 + state['offset_y'].get())

        # composite
        layer = Image.new('RGBA', comp.size)
        layer.paste(ov2, (dx, dy), ov2)
        mode = state['mode'].get()
        if mode == 'normal':
            comp = Image.alpha_composite(comp, layer)
        elif mode == 'multiply':
            comp = ImageChops.multiply(comp, layer)
        else:
            comp = ImageChops.add(comp, layer)

        # draw to canvas
        tkimg = ImageTk.PhotoImage(comp)
        state['canvas'].delete("all")
        state['canvas'].config(width=self.preview_w, height=self.preview_h)
        state['canvas'].create_image(0, 0, anchor='nw', image=tkimg)
        state['preview_img'] = tkimg  # keep reference alive


    def load(self, info_path):
        self.asset_path = info_path
        if not info_path:
            return
        if not os.path.exists(info_path):
            with open(info_path, 'w'): pass
        data = json.load(open(info_path, 'r'))
        for cfg in data.get('overlays', []):
            self.add_overlay(cfg)
            # TODO: populate each state's vars from cfg

    def save(self):
        if not self.asset_path:
            messagebox.showerror("Error", "No asset selected.")
            return

        asset_dir = os.path.dirname(self.asset_path)
        n = 0
        while True:
            outdir = os.path.join(asset_dir, f"overlay_{n}")
            if not os.path.exists(outdir):
                os.makedirs(outdir)
                break
            n += 1

        configs = []
        for state in self.overlays:
            src_folder = state['src_folder']
            if not src_folder:
                continue

            for fn in state['frames']:
                src = os.path.join(src_folder, fn)
                img = Image.open(src).convert('RGBA')

                # apply same pipeline as preview
                scale = state['scale_pct'].get()/100
                img2 = img.resize((int(img.width*scale), int(img.height*scale)), Image.LANCZOS)
                img2 = img2.rotate(-state['rotation'].get(), expand=True, resample=Image.BICUBIC)

                br = state['blur_radius'].get()
                if br>0:
                    img2 = img2.filter(ImageFilter.GaussianBlur(radius=br))

                fr = state['fade_radius'].get()
                if fr>0:
                    img2 = self._apply_radial_fade(img2, fr)

                alpha = state['alpha_pct'].get()/100
                a = img2.split()[3].point(lambda v: int(v*alpha))
                img2.putalpha(a)

                img2.save(os.path.join(outdir, fn))

            configs.append({
                'folder': os.path.basename(outdir),
                'offset_x':  state['offset_x'].get(),
                'offset_y':  state['offset_y'].get(),
                'scale_pct': state['scale_pct'].get(),
                'rotation':  state['rotation'].get(),
                'mode':      state['mode'].get(),
                'alpha_pct': state['alpha_pct'].get(),
                'blur_radius': state['blur_radius'].get(),
                'fade_radius': state['fade_radius'].get()
            })

        data = json.load(open(self.asset_path,'r'))
        data['overlays'] = configs
        with open(self.asset_path,'w') as f:
            json.dump(data, f, indent=4)

        messagebox.showinfo("Saved", "Overlay folders created and settings saved.")

