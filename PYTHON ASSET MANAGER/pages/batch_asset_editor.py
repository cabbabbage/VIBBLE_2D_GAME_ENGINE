import os
import random
import math
import tkinter as tk
from tkinter import ttk, messagebox
from PIL import Image, ImageTk
import json
from pages.range import Range
from pages.search import AssetSearchWindow
from pages.load_existing import LoadExistingChildren

SRC_DIR = "SRC"

class BatchAssetEditor(ttk.Frame):
    def _bind_range_save(self, rw: Range):
        rw.var_min.trace_add("write", lambda *_: self._trigger_save())
        rw.var_max.trace_add("write", lambda *_: self._trigger_save())
        if hasattr(rw, "var_random"):
            rw.var_random.trace_add("write", lambda *_: self._trigger_save())

    def __init__(self, parent, save_callback=None):
        style = ttk.Style()
        style.configure('Batch.TFrame', background='#2a2a2a')
        super().__init__(parent, style='Batch.TFrame')
        self.save_callback = save_callback
        self._suspend_save = False
        self.asset_data = []
        self.sliders = []

        # ─── Outer container ────────────────────────────────────────────────
        outer = tk.Frame(self, bg="#2a2a2a", bd=1, relief="solid")
        outer.pack(fill=tk.BOTH, expand=True, padx=12, pady=10)

        # ─── Top bar with Add buttons ────────────────────────────────────────
        top_bar = tk.Frame(outer, bg="#2a2a2a")
        top_bar.pack(fill=tk.X, pady=(6,4))
        btn_cfg = {
            "bg":"#007BFF","fg":"white",
            "font":("Segoe UI",13,"bold"),
            "width":16, "relief":"flat"
        }
        tk.Button(top_bar, text="Add New Asset", command=self._add_asset, **btn_cfg).pack(side=tk.LEFT, padx=6)
        tk.Button(top_bar, text="Add Existing", command=self._add_existing_asset, **btn_cfg).pack(side=tk.LEFT)

        # ─── Section header ────────────────────────────────────────────────
        tk.Label(
            outer, text="Batch Assets",
            font=("Segoe UI",13,"bold"),
            fg="#DDDDDD", bg="#2a2a2a"
        ).pack(anchor="w", padx=10, pady=(0,4))

        # ─── Grid Spacing & Jitter ─────────────────────────────────────────
        self.grid_spacing = Range(outer, label="Grid Spacing",
                                  min_bound=0, max_bound=400,
                                  set_min=100,set_max=100)
        self.grid_spacing.pack(fill=tk.X, padx=10, pady=(4,2))
        self._bind_range_save(self.grid_spacing)

        self.jitter = Range(outer, label="Jitter",
                            min_bound=0, max_bound=200,
                            set_min=0,set_max=0)
        self.jitter.pack(fill=tk.X, padx=10, pady=(0,8))
        self._bind_range_save(self.jitter)

        # ─── Asset sliders area (no visible scrollbar) ─────────────────────
        self.asset_canvas = tk.Canvas(outer, bg="#2a2a2a", highlightthickness=0)
        self.asset_frame  = tk.Frame(self.asset_canvas, bg="#2a2a2a")
        win = self.asset_canvas.create_window((0,0), window=self.asset_frame, anchor='nw')

        # update scrollregion
        self.asset_frame.bind(
            '<Configure>',
            lambda e: self.asset_canvas.configure(scrollregion=self.asset_canvas.bbox('all'))
        )
        self.asset_canvas.bind(
            '<Configure>',
            lambda e: self.asset_canvas.itemconfig(win, width=e.width)
        )

        self.asset_canvas.pack(fill=tk.BOTH, expand=True, padx=10, pady=6)

        # mouse wheel scrolling when hovered
        def _on_mousewheel(ev):
            self.asset_canvas.yview_scroll(int(-1*(ev.delta/120)), "units")
        self.asset_frame.bind("<Enter>", lambda e: self.asset_canvas.bind_all("<MouseWheel>", _on_mousewheel))
        self.asset_frame.bind("<Leave>", lambda e: self.asset_canvas.unbind_all("<MouseWheel>"))

    def _trigger_save(self, *_):
        if not self._suspend_save and self.save_callback:
            self.save_callback()

    def _add_asset(self):
        window = AssetSearchWindow(self)
        window.wait_window()
        res = getattr(window, "selected_result", None)
        if not res: return
        kind, name = res
        is_tag = (kind == "tag")
        if any(d["name"]==name and d.get("tag")==is_tag for d in self.asset_data):
            return
        color = "#%06x" % random.randint(0, 0xFFFFFF)
        if not self.asset_data:
            self.asset_data = [
                {"name":"null","tag":False,"percent":50,"color":"#CCCCCC"},
                {"name":name,"tag":is_tag,"percent":50,"color":color}
            ]
        elif len(self.asset_data)==1 and self.asset_data[0]["name"]=="null":
            self.asset_data[0]["percent"]=50
            self.asset_data.append({"name":name,"tag":is_tag,"percent":50,"color":color})
        else:
            self.asset_data.append({"name":name,"tag":is_tag,"percent":5,"color":color})
            self._rebalance_percentages()
        self._refresh_ui()
        self._trigger_save()

    def _add_existing_asset(self):
        from pages.load_existing import open_window_and_return_data
        new_assets = open_window_and_return_data("batch_assets")
        if not new_assets or not isinstance(new_assets, list):
            return
        for asset in new_assets:
            self.asset_data.append(asset)
        self._rebalance_percentages()
        self._refresh_ui()
        self._trigger_save()

    def _remove_asset(self, idx):
        if 0 <= idx < len(self.asset_data):
            del self.asset_data[idx]
        if not self.asset_data:
            self.asset_data = [{"name":"null","tag":False,"percent":100,"color":"#CCCCCC"}]
        else:
            self._rebalance_percentages()
        self._refresh_ui()
        self._trigger_save()

    def _rebalance_percentages(self):
        total = sum(a["percent"] for a in self.asset_data)
        n = len(self.asset_data)
        if total == 0:
            for a in self.asset_data:
                a["percent"] = 100//n
        else:
            scaled = [round(a["percent"]*100/total) for a in self.asset_data]
            diff = 100 - sum(scaled)
            if diff:
                i = max(range(n), key=lambda i:scaled[i])
                scaled[i] += diff
            for i,a in enumerate(self.asset_data):
                a["percent"] = max(1,scaled[i])

    def _adjust_others(self, idx, new_val):
        n = len(self.asset_data)
        if n <= 1:
            self.asset_data[idx]["percent"] = 100
            return
        old = [a["percent"] for a in self.asset_data]
        old_total = sum(old) - old[idx]
        self.asset_data[idx]["percent"] = new_val
        remaining = 100 - new_val

        props = []
        for j in range(n):
            if j == idx: continue
            props.append(
                old[j]*remaining/old_total if old_total>0 else remaining/(n-1)
            )

        floored = [math.floor(v) for v in props]
        fracs   = [v - math.floor(v) for v in props]
        s       = sum(floored)
        diff    = remaining - s
        if diff>0:
            order = sorted(range(len(fracs)), key=lambda i:fracs[i], reverse=True)
            for k in order[:diff]: floored[k]+=1
        elif diff<0:
            order = sorted(range(len(fracs)), key=lambda i:fracs[i])
            for k in order[: -diff]: floored[k]=max(1,floored[k]-1)

        for k in range(len(floored)):
            floored[k] = max(1,floored[k])
        s = sum(floored)
        diff = remaining - s
        for _ in range(abs(diff)):
            k = random.randrange(len(floored))
            if diff>0:
                floored[k]+=1
            elif floored[k]>1:
                floored[k]-=1

        it = 0
        for j in range(n):
            if j==idx: continue
            self.asset_data[j]["percent"] = floored[it]
            it += 1

    def _refresh_ui(self):
        # clear
        for w in self.asset_frame.winfo_children():
            w.destroy()
        self.sliders.clear()

        n = len(self.asset_data)
        for i, asset in enumerate(self.asset_data):
            row = tk.Frame(self.asset_frame, bg="#2a2a2a")
            row.pack(fill=tk.X, pady=4)

            # ── left column: icon + name ───────────────────────────────
            left_col = tk.Frame(row, width=200, bg="#2a2a2a")
            left_col.pack(side=tk.LEFT, fill=tk.Y)
            left_col.pack_propagate(False)

            # thumbnail/icon (now 64×64)
            icon_lbl = tk.Label(left_col, bg="#2a2a2a")
            path = os.path.join(SRC_DIR, asset["name"], "default", "0.png")
            if os.path.exists(path):
                try:
                    img = Image.open(path)
                    img.thumbnail((64, 64), Image.Resampling.LANCZOS)
                    ph  = ImageTk.PhotoImage(img)
                    icon_lbl.configure(image=ph)
                    icon_lbl.image = ph
                except:
                    icon_lbl.configure(text="?")
            else:
                icon_lbl.configure(text="#", font=("Segoe UI",24,"bold"))
            icon_lbl.pack(side=tk.LEFT, padx=(4,4), pady=4)

            # name label
            tk.Label(
                left_col,
                text=asset["name"],
                font=("Segoe UI",12),
                fg="white", bg="#2a2a2a",
                anchor="w"
            ).pack(side=tk.LEFT, padx=(0,4), pady=4, fill=tk.X, expand=True)

            # ── slider ─────────────────────────────────────────────────
            slider = tk.Scale(
                row, from_=1, to=100, orient=tk.HORIZONTAL,
                resolution=1, length=200,
                bg=asset["color"], troughcolor=asset["color"],
                highlightthickness=0
            )
            slider.set(asset["percent"])
            slider.pack(side=tk.LEFT, padx=6, fill=tk.X, expand=True)
            slider.bind(
                "<ButtonRelease-1>",
                lambda e, idx=i, s=slider: (
                    self._adjust_others(idx, s.get()),
                    self._refresh_ui(),
                    self._trigger_save()
                )
            )

            # ── delete button ───────────────────────────────────────────
            tk.Button(
                row, text="✕", bg="#D9534F", fg="white",
                font=("Segoe UI",12,"bold"), width=3,
                relief="flat", command=lambda idx=i: self._remove_asset(idx)
            ).pack(side=tk.RIGHT, padx=(6,4))

            self.sliders.append((row, slider))


    def save(self):
        return {
            "has_batch_assets": any(a["name"]!="null" for a in self.asset_data),
            "grid_spacing_min": self.grid_spacing.get_min(),
            "grid_spacing_max": self.grid_spacing.get_max(),
            "jitter_min": self.jitter.get_min(),
            "jitter_max": self.jitter.get_max(),
            "batch_assets": [
                {"name":a["name"],"tag":a.get("tag",False),
                 "percent":a["percent"],"color":a["color"]}
                for a in self.asset_data
            ]
        }

    def load(self, data=None):
        self._suspend_save = True
        for w in self.asset_frame.winfo_children():
            w.destroy()

        if not isinstance(data, dict):
            data = {}
        self.grid_spacing.set(
            data.get("grid_spacing_min",100),
            data.get("grid_spacing_max",100)
        )
        self.jitter.set(
            data.get("jitter_min",0),
            data.get("jitter_max",0)
        )

        loaded = data.get("batch_assets", [])
        if isinstance(loaded, list) and loaded:
            self.asset_data = [
                {
                    "name":a["name"],
                    "tag":a.get("tag",False),
                    "percent":a.get("percent",0),
                    "color":a.get("color","#%06x"%random.randint(0,0xFFFFFF))
                } for a in loaded
            ]
            self._rebalance_percentages()
        else:
            self.asset_data = [{"name":"null","tag":False,"percent":100,"color":"#CCCCCC"}]

        self._refresh_ui()
        self._suspend_save = False
