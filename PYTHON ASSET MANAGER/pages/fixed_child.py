# pages/fixed_child_assets.py

import os
import json
import tkinter as tk
from tkinter import ttk, messagebox, filedialog
from PIL import Image, ImageTk

ASSET_DIR = "SRC"

class ScrollableFrame(ttk.Frame):
    def __init__(self, container, *args, **kwargs):
        super().__init__(container, *args, **kwargs)
        self.canvas = tk.Canvas(self)
        vsb = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.scrollable_frame = ttk.Frame(self.canvas)
        self.scrollable_frame.bind(
            "<Configure>",
            lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all"))
        )
        self.canvas.create_window((0,0), window=self.scrollable_frame, anchor='nw')
        self.canvas.configure(yscrollcommand=vsb.set)
        self.canvas.pack(side='left', fill='both', expand=True)
        vsb.pack(side='right', fill='y')

class FixedChildAssetsPage(ttk.Frame):
    def __init__(self, parent):
        super().__init__(parent)

        # — Theme & fonts —
        self.FONT            = ('Segoe UI', 14)
        self.FONT_BOLD       = ('Segoe UI', 18, 'bold')
        self.MAIN_COLOR      = "#005f73"
        self.SECONDARY_COLOR = "#ee9b00"

        # — State —
        self.asset_path    = None
        self.fixed_frames  = []
        self.asset_names   = []

        # — Styles —
        style = ttk.Style(self)
        style.configure('Main.TButton', font=self.FONT, padding=6,
                        background=self.MAIN_COLOR, foreground='black')
        style.map('Main.TButton',
                  background=[('active', self.SECONDARY_COLOR)])
        style.configure('LargeBold.TLabel', font=self.FONT_BOLD,
                        foreground=self.SECONDARY_COLOR)
        style.configure('Large.TLabel', font=self.FONT)

        # — Layout —
        ttk.Label(self, text="Fixed-Spawn Children",
                  style='LargeBold.TLabel')\
            .grid(row=0, column=0, columnspan=4,
                  pady=(10,20), padx=12)

        # scrollable container for rows
        self.container = ScrollableFrame(self)
        self.container.grid(row=1, column=0, columnspan=4,
                            sticky="nsew", padx=12, pady=(0,12))
        self.columnconfigure(1, weight=1)

        # Add button
        ttk.Button(self, text="Add Child Fixed",
                   style='Main.TButton',
                   command=self._add_row)\
            .grid(row=2, column=0, columnspan=4, pady=(0,12))

        # Save button
        ttk.Button(self, text="Save",
                   style='Main.TButton',
                   command=self.save)\
            .grid(row=3, column=0, columnspan=4, pady=(0,12))

    def _add_row(self, data=None):
        idx = len(self.fixed_frames)
        frm = ttk.Frame(self.container.scrollable_frame,
                        relief='ridge', padding=10)
        frm.grid(row=idx, column=0, sticky="ew", pady=6)
        frm.columnconfigure(1, weight=1)

        # Child Asset dropdown
        ttk.Label(frm, text="Child Asset:", style='Large.TLabel')\
            .grid(row=0, column=0, sticky="w")
        asset_var = tk.StringVar(value=(data or {}).get('asset',''))
        om = ttk.OptionMenu(frm, asset_var, asset_var.get(), *self.asset_names,
                            command=lambda _v, i=idx: self._update_preview(i))
        om.grid(row=0, column=1, sticky="we", padx=6)

        # Z Offset
        ttk.Label(frm, text="Z Offset:", style='Large.TLabel')\
            .grid(row=1, column=0, sticky="w", pady=4)
        z_var = tk.IntVar(value=(data or {}).get('z_offset',0))
        ttk.Spinbox(frm, from_=-9999, to=9999, textvariable=z_var, width=8)\
            .grid(row=1, column=1, sticky="w", padx=6)

        # Offset X slider
        ttk.Label(frm, text="Offset X:", style='Large.TLabel')\
            .grid(row=2, column=0, sticky="w", pady=4)
        x_var = tk.IntVar(value=(data or {}).get('offset_x',0))
        ttk.Scale(frm,
                  from_=-500, to=500,   # wider range
                  variable=x_var,
                  orient='horizontal',
                  length=350,
                  command=lambda _e, i=idx: self._update_preview(i))\
            .grid(row=2, column=1, sticky="we", padx=6)

        # Offset Y slider
        ttk.Label(frm, text="Offset Y:", style='Large.TLabel')\
            .grid(row=3, column=0, sticky="w", pady=4)
        y_var = tk.IntVar(value=(data or {}).get('offset_y',0))
        ttk.Scale(frm,
                  from_=-500, to=500,
                  variable=y_var,
                  orient='horizontal',
                  length=350,
                  command=lambda _e, i=idx: self._update_preview(i))\
            .grid(row=3, column=1, sticky="we", padx=6)

        # Preview canvas (bigger)
        cv = tk.Canvas(frm, width=400, height=400, bg='black')
        cv.grid(row=0, column=2, rowspan=4, padx=10)

        ttk.Button(frm, text="Remove", style='Main.TButton',
                   command=lambda i=idx: self._remove_row(i))\
            .grid(row=0, column=3, sticky="e")

        self.fixed_frames.append({
            'frame': frm,
            'vars': {
                'asset':    asset_var,
                'z_offset': z_var,
                'offset_x': x_var,
                'offset_y': y_var
            },
            'canvas': cv,
            'tkimg': None
        })

        # Initial preview
        self._update_preview(idx)

    def _remove_row(self, idx):
        entry = self.fixed_frames.pop(idx)
        entry['frame'].destroy()
        for i, e in enumerate(self.fixed_frames):
            e['frame'].grid_configure(row=i)

    def _update_preview(self, idx):
        entry = self.fixed_frames[idx]
        vars_ = entry['vars']
        cv    = entry['canvas']
        asset = vars_['asset'].get()

        # load parent default
        p0 = os.path.join(os.path.dirname(self.asset_path),
                          'default','0.png')
        if not os.path.isfile(p0):
            return
        pimg = Image.open(p0).convert('RGBA')

        # load child default + center from its JSON
        cdir = os.path.join(ASSET_DIR, asset)
        c0   = os.path.join(cdir,'default','0.png')
        info = os.path.join(cdir,'info.json')
        if not (os.path.isfile(c0) and os.path.isfile(info)):
            return
        cimg = Image.open(c0).convert('RGBA')
        with open(info,'r') as f:
            ci = json.load(f)
        cc = ci.get('center', {})
        cx_c = cc.get('x', cimg.width//2)
        cy_c = cc.get('y', cimg.height//2)

        # parent center
        with open(self.asset_path,'r') as f:
            pd = json.load(f)
        pc = pd.get('center', {})
        cx_p = pc.get('x', pimg.width//2)
        cy_p = pc.get('y', pimg.height//2)

        dx = cx_p - cx_c + vars_['offset_x'].get()
        dy = cy_p - cy_c + vars_['offset_y'].get()

        comp = pimg.copy()
        comp.paste(cimg, (dx, dy), cimg)

        # scale down to fit canvas
        w,h = comp.size
        s = min(400/w, 400/h)
        disp = comp.resize((int(w*s), int(h*s)), Image.LANCZOS)

        tkimg = ImageTk.PhotoImage(disp)
        cv.delete('all')
        cv.config(width=disp.width, height=disp.height)
        cv.create_image(0,0,anchor='nw',image=tkimg)
        entry['tkimg'] = tkimg

    def load(self, info_path):
        self.asset_path = info_path
        if not info_path:
            return
        # build asset_names of child_only
        current = os.path.basename(os.path.dirname(info_path))
        self.asset_names = []
        for n in sorted(os.listdir(ASSET_DIR)):
            if n == current: continue
            inf = os.path.join(ASSET_DIR,n,'info.json')
            if os.path.isfile(inf):
                j = json.load(open(inf,'r'))
                if j.get('child_only',False):
                    self.asset_names.append(n)

        # clear existing rows
        for e in self.fixed_frames:
            e['frame'].destroy()
        self.fixed_frames.clear()

        # re-create rows from JSON
        data = json.load(open(info_path,'r'))
        for cfg in data.get('fixed_children', []):
            self._add_row(cfg)

    def save(self):
        if not self.asset_path:
            messagebox.showerror("Error","No asset selected.")
            return

        j = json.load(open(self.asset_path,'r'))
        out = []
        for e in self.fixed_frames:
            v = e['vars']
            out.append({
                'asset':    v['asset'].get(),
                'z_offset': v['z_offset'].get(),
                'offset_x': v['offset_x'].get(),
                'offset_y': v['offset_y'].get()
            })
        j['fixed_children'] = out
        with open(self.asset_path,'w') as f:
            json.dump(j, f, indent=2)

        messagebox.showinfo("Saved","Fixed children saved.")
