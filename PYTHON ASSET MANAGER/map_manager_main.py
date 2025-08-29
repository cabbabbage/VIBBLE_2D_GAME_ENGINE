import os
import json
import tkinter as tk
from tkinter import ttk, simpledialog, messagebox

from map_pages.map_info_page     import MapInfoPage
from map_pages.rooms_page        import RoomsPage
from map_pages.trails_page       import TrailsPage
from map_pages.boundary_page     import BoundaryPage
from map_pages.map_assets_page   import MapAssetsPage
from map_pages.map_light_page    import MapLightPage

MAPS_DIR = "MAPS"
TABS = {
    "Map Info": MapInfoPage,
    "Rooms": RoomsPage,
    "Trails": TrailsPage,
    "Boundary": BoundaryPage,
    "Map-Wide Assets": MapAssetsPage,
    "Map Lighting": MapLightPage
}

class MapManagerApp(tk.Toplevel):
    def __init__(self):
        super().__init__()
        self.title("Map Manager")
        try:
            self.state('zoomed')
        except tk.TclError:
            self.geometry('1000x700')

        # ─── Styles ────────────────────────────────────────────────────────────
        style = ttk.Style(self)
        style.theme_use('clam')
        style.configure('TFrame', background='#1e1e1e')
        style.configure('TLabel', background='#1e1e1e', foreground='white')
        style.configure('TButton',
                        background='#007BFF', foreground='white',
                        relief='flat', borderwidth=0,
                        font=('Segoe UI',11,'bold'), padding=6)
        style.map('TButton', background=[('active','#0056b3')])
        # hide native scrollbars
        style.layout('Vertical.TScrollbar', [])
        style.layout('Horizontal.TScrollbar', [])
        # notebook tabs
        style.configure('TNotebook', background='#1e1e1e', borderwidth=0)
        style.configure('TNotebook.Tab',
                        background='#2a2a2a', foreground='white',
                        font=('Segoe UI',11,'bold'), padding=[10,5])
        style.map('TNotebook.Tab',
                  background=[('selected','#007BFF')],
                  foreground=[('selected','white')])

        self.configure(background='#1e1e1e')
        self.maps = []
        self.current_map = None
        self.pages = {}
        self.map_buttons = {}

        # ─── Split pane ───────────────────────────────────────────────────────
        paned = ttk.Panedwindow(self, orient=tk.HORIZONTAL)
        paned.pack(fill=tk.BOTH, expand=True)

        # ─── Left Pane ─────────────────────────────────────────────────────────
        left = ttk.Frame(paned, width=200)
        left.pack_propagate(False)
        paned.add(left, weight=0)

        # Top fixed buttons
        btn_frame = tk.Frame(left, bg='#1e1e1e')
        btn_frame.pack(fill=tk.X, padx=8, pady=(8,4))
        switch_btn = tk.Button(btn_frame, text="Switch to PYTHON ASSET MANAGER",
                               bg="#007BFF", fg="white",
                               font=("Segoe UI",11,"bold"),
                               relief='flat', borderwidth=0,
                               command=self._open_asset_manager)
        switch_btn.pack(fill=tk.X, pady=(0,4))
        new_btn = tk.Button(btn_frame, text="New Map",
                            bg="#007BFF", fg="white",
                            font=("Segoe UI",11,"bold"),
                            relief='flat', borderwidth=0,
                            command=self._new_map)
        new_btn.pack(fill=tk.X)

        # Scrollable map list (mouse-wheel only)
        list_container = tk.Frame(left, bg='#1e1e1e')
        list_container.pack(fill=tk.BOTH, expand=True, padx=8, pady=(0,8))
        self.list_canvas = tk.Canvas(list_container, bg='#2a2a2a', highlightthickness=0)
        self.list_canvas.pack(fill=tk.BOTH, expand=True)
        self.list_canvas.bind('<Enter>', lambda e: self.list_canvas.bind_all('<MouseWheel>', self._on_map_scroll))
        self.list_canvas.bind('<Leave>', lambda e: self.list_canvas.unbind_all('<MouseWheel>'))

        self.list_inner = tk.Frame(self.list_canvas, bg='#2a2a2a')
        inner_id = self.list_canvas.create_window((0,0), window=self.list_inner, anchor='nw')
        self.list_inner.bind('<Configure>',
                             lambda e: self.list_canvas.configure(scrollregion=self.list_canvas.bbox('all')))
        self.list_canvas.bind('<Configure>',
                              lambda e: self.list_canvas.itemconfig(inner_id, width=e.width))

        # ─── Right Pane ───────────────────────────────────────────────────────
        right = ttk.Frame(paned)
        paned.add(right, weight=1)

        self.content_canvas = tk.Canvas(right, bg='#1e1e1e', highlightthickness=0)
        self.content_canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.content_canvas.bind(
            '<Enter>',
            lambda e: self.content_canvas.bind_all('<MouseWheel>', self._on_content_scroll)
        )
        self.content_canvas.bind(
            '<Leave>',
            lambda e: self.content_canvas.unbind_all('<MouseWheel>')
        )

        content_inner = ttk.Frame(self.content_canvas, style='TFrame')
        win2 = self.content_canvas.create_window((0, 0), window=content_inner, anchor='nw')

        # keep the canvas scrollregion and also force the inner window
        # to always match both width *and* height of the canvas
        self.content_canvas.bind(
            '<Configure>',
            lambda e: (
                self.content_canvas.configure(scrollregion=self.content_canvas.bbox('all')),
                self.content_canvas.itemconfig(win2, width=e.width, height=e.height)
            )
        )


        # ─── Notebook ─────────────────────────────────────────────────────────
        self.notebook = ttk.Notebook(content_inner)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=12, pady=12)

        for title, cls in TABS.items():
            page = cls(self.notebook)
            self.notebook.add(page, text=title)
            self.pages[title] = page

        self._refresh_map_list()
        if self.map_buttons:
            next(iter(self.map_buttons.values())).invoke()


    def _on_map_scroll(self, event):
        self.list_canvas.yview_scroll(int(-1 * (event.delta / 120)), 'units')

    def _on_content_scroll(self, event):
        self.content_canvas.yview_scroll(int(-1 * (event.delta / 120)), 'units')

    def _scan_maps(self):
        if not os.path.isdir(MAPS_DIR):
            os.makedirs(MAPS_DIR)
        return [d for d in sorted(os.listdir(MAPS_DIR))
                if os.path.isdir(os.path.join(MAPS_DIR, d))]

    def _refresh_map_list(self):
        self.maps = self._scan_maps()
        for w in self.list_inner.winfo_children():
            w.destroy()
        self.map_buttons.clear()

        for name in self.maps:
            btn = tk.Button(self.list_inner, text=name,
                            bg='#2a2a2a', fg='white', anchor='w',
                            font=('Segoe UI',10), relief='flat',
                            command=lambda n=name: self._select_map(n))
            btn.pack(fill=tk.X, padx=4, pady=2)
            self.map_buttons[name] = btn

    def _select_map(self, name):
        self.current_map = name
        for btn in self.map_buttons.values():
            btn.configure(bg='#2a2a2a')
        self.map_buttons[name].configure(bg='#005f73')

        folder = os.path.join(MAPS_DIR, name)
        for title, page in self.pages.items():
            path = os.path.join(folder, page.get_json_filename())
            data = {}
            if os.path.exists(path):
                with open(path) as f:
                    data = json.load(f)
            try:
                page.load_data(data, path)
            except Exception as e:
                messagebox.showerror("Load Failed", f"Could not load {page.get_json_filename()}:\n{e}")

    def _new_map(self):
        while True:
            raw = simpledialog.askstring("New Map", "Enter map name:")
            if raw is None:
                return
            safe = "".join(c for c in raw if c.isalnum() or c in ("_","-")).strip()
            if not safe:
                messagebox.showerror("Invalid Name", "Letters, numbers, -, _ only.")
                continue
            folder = os.path.join(MAPS_DIR, safe)
            if os.path.exists(folder):
                messagebox.showerror("Exists", f"Map '{safe}' already exists.")
                continue
            os.makedirs(folder, exist_ok=True)
            for cls in TABS.values():
                p = os.path.join(folder, cls.get_json_filename())
                if not os.path.exists(p):
                    with open(p, "w") as f:
                        json.dump({}, f)
            break
        self._refresh_map_list()
        self.map_buttons[safe].invoke()

    def _open_asset_manager(self):
        from asset_manager_main import AssetOrganizerApp
        AssetOrganizerApp()
        self.destroy()


if __name__ == '__main__':
    MapManagerApp().mainloop()
