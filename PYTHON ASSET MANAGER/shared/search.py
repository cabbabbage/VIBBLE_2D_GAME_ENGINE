import os
import json
import tkinter as tk
from tkinter import ttk
from PIL import Image, ImageTk

SRC_DIR = "SRC"

class AssetSearchWindow(tk.Toplevel):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.title("Search Assets or Tags")
        self.geometry("500x600")
        self.configure(bg='#1e1e1e')

        self.selected_result = None  # (type, name)
        self.thumb_size = (64, 64)

        # ─── Styles ─────────────────────────────────────────────────────
        style = ttk.Style(self)
        style.theme_use('clam')
        style.configure('TFrame', background='#2a2a2a')
        style.configure('TLabel', background='#2a2a2a', foreground='white')
        style.configure('TEntry', fieldbackground='#1e1e1e', foreground='white', padding=4)
        style.configure('TButton', background='#007BFF', foreground='white',
                        font=('Segoe UI',11,'bold'), relief='flat', padding=6)
        style.map('TButton', background=[('active','#0056b3')])
        # hide scrollbars
        style.layout('Vertical.TScrollbar', [])
        style.layout('Horizontal.TScrollbar', [])

        # ─── Search bar ─────────────────────────────────────────────────
        entry_frame = ttk.Frame(self)
        entry_frame.pack(fill=tk.X, padx=10, pady=10)
        ttk.Label(entry_frame, text="Search:").pack(side=tk.LEFT, padx=(0,5))
        self.query_var = tk.StringVar()
        entry = ttk.Entry(entry_frame, textvariable=self.query_var, style='TEntry')
        entry.pack(side=tk.LEFT, fill=tk.X, expand=True)
        entry.bind("<KeyRelease>", self.update_results)

        # ─── Scrollable result list (no visible scrollbar) ─────────────
        canvas_frame = ttk.Frame(self)
        canvas_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=(0,10))

        self.canvas = tk.Canvas(
            canvas_frame,
            bg='#2a2a2a',
            bd=0,
            highlightthickness=0
        )
        self.canvas.pack(fill=tk.BOTH, expand=True)

        # result_frame inside canvas
        self.result_frame = ttk.Frame(self.canvas)
        self.window_id = self.canvas.create_window((0, 0), window=self.result_frame, anchor='nw')

        # configure scrolling region & wheel
        self.result_frame.bind(
            "<Configure>",
            lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all"))
        )
        self.canvas.bind(
            "<Configure>",
            lambda e: self.canvas.itemconfig(self.window_id, width=e.width)
        )
        self.canvas.bind(
            "<Enter>",
            lambda e: self.canvas.bind_all("<MouseWheel>", self._on_mousewheel)
        )
        self.canvas.bind(
            "<Leave>",
            lambda e: self.canvas.unbind_all("<MouseWheel>")
        )

        # ─── Load & show results ─────────────────────────────────────────
        self.assets = self._load_assets()
        self.update_results()

    def _on_mousewheel(self, event):
        self.canvas.yview_scroll(int(-1*(event.delta/120)), "units")

    def _load_assets(self):
        results = []
        if not os.path.isdir(SRC_DIR):
            return results
        for name in sorted(os.listdir(SRC_DIR)):
            path = os.path.join(SRC_DIR, name)
            info_p = os.path.join(path, "info.json")
            if os.path.isdir(path) and os.path.isfile(info_p):
                try:
                    info = json.load(open(info_p, "r"))
                    results.append({
                        "name": info.get("asset_name", name),
                        "tags": info.get("tags", []),
                        "path": path
                    })
                except:
                    pass
        return results

    def update_results(self, event=None):
        q = self.query_var.get().lower()
        # clear
        for w in self.result_frame.winfo_children():
            w.destroy()

        matches, tags = [], set()
        for asset in self.assets:
            if q in asset["name"].lower():
                matches.append(("asset", asset))
            for t in asset["tags"]:
                if q in t.lower():
                    tags.add(t)

        matches.sort(key=lambda x: x[1]["name"])
        for _, asset in matches:
            self._add_asset_widget(asset)
        for tag in sorted(tags):
            self._add_tag_widget(tag)

    def _add_asset_widget(self, asset):
        row = tk.Frame(self.result_frame, bg='#2a2a2a', bd=1, highlightthickness=1, highlightbackground='#444')
        row.pack(fill=tk.X, pady=4, padx=2)
        row.bind("<Button-1>", lambda e, n=asset["name"]: self._on_select("asset", n))

        # thumbnail
        lbl = tk.Label(row, bg='#2a2a2a')
        img_p = os.path.join(asset["path"], "default", "0.png")
        if os.path.isfile(img_p):
            try:
                img = Image.open(img_p)
                img.thumbnail(self.thumb_size, Image.Resampling.LANCZOS)
                ph = ImageTk.PhotoImage(img)
                lbl.configure(image=ph)
                lbl.image = ph
            except:
                lbl.configure(text="?")
        else:
            lbl.configure(text="#", font=("Segoe UI",24,"bold"))
        lbl.pack(side=tk.LEFT, padx=8, pady=4)
        lbl.bind("<Button-1>", lambda e, n=asset["name"]: self._on_select("asset", n))

        # name
        name_lbl = tk.Label(row, text=asset["name"],
                            font=("Segoe UI",12), fg="white", bg="#2a2a2a", anchor="w")
        name_lbl.pack(side=tk.LEFT, padx=(0,10), fill=tk.X, expand=True)
        name_lbl.bind("<Button-1>", lambda e, n=asset["name"]: self._on_select("asset", n))

    def _add_tag_widget(self, tag):
        row = tk.Frame(self.result_frame, bg='#2a2a2a', bd=1, highlightthickness=1, highlightbackground='#444')
        row.pack(fill=tk.X, pady=4, padx=2)
        row.bind("<Button-1>", lambda e, t=tag: self._on_select("tag", t))

        lbl = tk.Label(
            row, text=f"# {tag}",
            font=("Segoe UI",11,"italic"),
            fg="#bbb", bg="#2a2a2a", anchor="w"
        )
        lbl.pack(side=tk.LEFT, padx=10, pady=6)
        lbl.bind("<Button-1>", lambda e, t=tag: self._on_select("tag", t))

    def _on_select(self, item_type, value):
        self.selected_result = (item_type, value)
        self.destroy()


# Optional launcher for standalone testing
def launch_search():
    root = tk.Tk(); root.withdraw()
    w = AssetSearchWindow()
    w.wait_window()
    return getattr(w, "selected_result", None)

if __name__ == "__main__":
    print("Returned:", launch_search())

