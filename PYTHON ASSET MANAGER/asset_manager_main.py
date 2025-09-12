import os
import json
import shutil
import tkinter as tk
from tkinter import ttk, messagebox, simpledialog
from PIL import Image, ImageTk

from asset_pages.basic_info import BasicInfoPage
from asset_pages.size import SizePage
from asset_pages.Animations import AnimationsPage
from asset_pages.passa import PassabilityPage
from asset_pages.child import ChildAssetsPage
from asset_pages.spacing import SpacingThresholdPage
from asset_pages.tags import TagsPage
from asset_pages.edit_raw_json import EditRawJsonPage
from asset_pages.lighting import LightingPage

ASSET_DIR = "SRC"

class AssetOrganizerApp(tk.Toplevel):
    def __init__(self):
        super().__init__()
        self.title("Asset Organizer")
        try:
            self.state('zoomed')
        except tk.TclError:
            self.attributes('-zoomed', True)

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
        style.layout('Vertical.TScrollbar', [])
        style.layout('Horizontal.TScrollbar', [])
        style.configure('TNotebook', background='#1e1e1e', borderwidth=0)
        style.configure('TNotebook.Tab',
                        background='#2a2a2a', foreground='white',
                        font=('Segoe UI',11,'bold'), padding=[10,5])
        style.map('TNotebook.Tab',
                  background=[('selected','#007BFF')],
                  foreground=[('selected','white')])

        self.configure(background='#1e1e1e')
        self.assets = self._scan_assets()
        self.current_asset = None
        self.pages = {}
        self.asset_thumbnails = {}

        paned = ttk.Panedwindow(self, orient=tk.HORIZONTAL)
        paned.pack(fill=tk.BOTH, expand=True)

        # ─── Left Pane ─────────────────────────────────────────────────────────
        left = ttk.Frame(paned, width=220)
        left.pack_propagate(False)
        paned.add(left, weight=0)

        top_btns = tk.Frame(left, bg='#1e1e1e')
        top_btns.pack(fill=tk.X, pady=(8,4), padx=5)
        tk.Button(top_btns, text="Switch to Map Manager",
                  bg="#007BFF", fg="white",
                  font=("Segoe UI",11,"bold"),
                  relief='flat', borderwidth=0,
                  command=self._open_asset_manager).pack(fill=tk.X, pady=(0,4))
        tk.Button(top_btns, text="New Asset",
                  bg="#007BFF", fg="white",
                  font=("Segoe UI",11,"bold"),
                  relief='flat', borderwidth=0,
                  command=self._new_asset).pack(fill=tk.X)

        # Scrollable asset list (mouse-wheel only)
        list_container = tk.Frame(left, bg='#1e1e1e')
        list_container.pack(fill=tk.BOTH, expand=True, padx=5, pady=(4,8))
        self.asset_canvas = tk.Canvas(list_container, bg='#2a2a2a', highlightthickness=0)
        self.asset_canvas.pack(fill=tk.BOTH, expand=True)
        self.asset_canvas.bind('<Enter>', lambda e: self.asset_canvas.bind_all('<MouseWheel>', self._on_mousewheel_assets))
        self.asset_canvas.bind('<Leave>', lambda e: self.asset_canvas.unbind_all('<MouseWheel>'))

        self.asset_inner = tk.Frame(self.asset_canvas, bg='#2a2a2a')
        win = self.asset_canvas.create_window((0,0), window=self.asset_inner, anchor='nw')
        self.asset_inner.bind('<Configure>',
                              lambda e: self.asset_canvas.configure(scrollregion=self.asset_canvas.bbox('all')))
        self.asset_canvas.bind('<Configure>',
                               lambda e: self.asset_canvas.itemconfig(win, width=e.width))

        self.asset_buttons = {}

        # ─── Editor Pane ───────────────────────────────────────────────────────
        editor = ttk.Frame(paned)
        paned.add(editor, weight=1)
        # ensure this frame really fills all available space:
        editor.pack_propagate(False)

        # Put the Notebook straight into 'editor' so it always fills its height:
        self.notebook = ttk.Notebook(editor)
        self.notebook.pack(fill=tk.BOTH, expand=True)

        # add each tab exactly as before
        for cls, title in [
            (BasicInfoPage, 'Basic Info'),
            (SizePage, 'Sizing'),
            (PassabilityPage, 'Passability'),
            (SpacingThresholdPage, 'Spacing'),
            (AnimationsPage, 'Animations'),
            (ChildAssetsPage, 'Child Assets'),
            (TagsPage, 'Tags'),
            (LightingPage, 'Lighting'),
            (EditRawJsonPage, 'JSON')
        ]:
            if cls is AnimationsPage:
                page = cls(self.notebook, asset_folder=os.path.join(ASSET_DIR, self.current_asset or ''))
                self.animations_tab = page
            else:
                page = cls(self.notebook)
            self.notebook.add(page, text=title)
            self.pages[title] = page

        # finally populate the asset list, etc.
        self._refresh_asset_list()



    def _on_mousewheel_assets(self, event):
        """Scroll the asset list when the mouse wheel is used over it."""
        self.asset_canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")
        
    def _on_editor_scroll(self, event):
        self.notebook.event_generate('<MouseWheel>', delta=event.delta)

    def _scan_assets(self):
        if not os.path.isdir(ASSET_DIR):
            return []
        return [d for d in sorted(os.listdir(ASSET_DIR))
                if os.path.isdir(os.path.join(ASSET_DIR, d))]

    def _refresh_asset_list(self):
        for w in self.asset_inner.winfo_children():
            w.destroy()
        self.asset_buttons.clear()
        for name in self._scan_assets():
            img_path = os.path.join(ASSET_DIR, name, 'default', '0.png')
            photo = None
            if os.path.exists(img_path):
                try:
                    img = Image.open(img_path)
                    img.thumbnail((24,24))
                    photo = ImageTk.PhotoImage(img)
                except:
                    pass
            btn = tk.Button(self.asset_inner, text=name, image=photo, compound='left',
                            bg='#2a2a2a', fg='white', font=('Segoe UI',11),
                            relief='flat', borderwidth=0,
                            command=lambda n=name: self._select(n))
            btn.image = photo
            btn.pack(fill='x', padx=4, pady=1)
            btn.bind('<Enter>', lambda e, b=btn: b.configure(bg='#444'))
            btn.bind('<Leave>', lambda e, b=btn, n=name: b.configure(
                bg=('#17A2B8' if n==self.current_asset else '#2a2a2a')))
            self.asset_buttons[name] = btn

    def _select(self, name):
        self.current_asset = name
        for n, b in self.asset_buttons.items():
            b.configure(bg='#2a2a2a', font=('Segoe UI',11))
        self.asset_buttons[name].configure(bg='#17A2B8', font=('Segoe UI',11,'bold'))

        if hasattr(self, 'animations_tab'):
            self.animations_tab.asset_folder = os.path.join(ASSET_DIR, name)
            self.animations_tab._load_existing()

        for page in self.pages.values():
            page.load(os.path.join(ASSET_DIR, name, 'info.json'))

        self.notebook.select(self.pages['Basic Info'])

    def _new_asset(self):
        name = simpledialog.askstring('New Asset','Enter a name:')
        if not name or not name.strip():
            return
        folder = os.path.join(ASSET_DIR, name.strip())
        if os.path.exists(folder):
            messagebox.showerror('Error', f"Asset '{name}' exists.")
            return
        os.makedirs(folder)
        default = {
            "impassable_area": None,
            "asset_name": name,
            "asset_type": "Object",
            "duplicatable": False,
            "duplication_interval_min": 30,
            "duplication_interval_max": 60,
            "min_child_depth": 0,
            "max_child_depth": 2
        }
        with open(os.path.join(folder,'info.json'),'w') as f:
            json.dump(default, f, indent=4)
        self._refresh_asset_list()
        self._select(name)

    def _open_asset_manager(self):
        from map_manager_main import MapManagerApp
        MapManagerApp()
        self.destroy()


if __name__ == '__main__':
    AssetOrganizerApp().mainloop()
