# === File: pages/trails_page.py ===
import os
import json
import tkinter as tk
from tkinter import ttk, messagebox, simpledialog
from shared.range import Range
from shared.assets_editor import AssetEditor
from shared.batch_asset_editor import BatchAssetEditor

# Default structure for batch assets
DEFAULT_BATCH_ASSETS = {
    "has_batch_assets": False,
    "grid_spacing_min": 100,
    "grid_spacing_max": 100,
    "jitter_min": 0,
    "jitter_max": 0,
    "batch_assets": []
}

class TrailsPage(tk.Frame):
    def __init__(self, parent):
        super().__init__(parent, bg='#1e1e1e')
        self.current_trail_path = None
        self.trail_data = None
        self.trails_dir = None
        self._suspend_save = False

        # Header
        header = tk.Frame(self, bg='#1e1e1e')
        header.pack(fill=tk.X, padx=12, pady=(10, 20))
        tk.Label(header, text="Trails",
                 font=("Segoe UI", 20, "bold"),
                 fg="#005f73", bg='#1e1e1e').pack(side=tk.LEFT)
        tk.Button(header, text="Add New Trail",
                  bg="#28a745", fg="white",
                  font=("Segoe UI", 13, "bold"), width=18,
                  command=self._add_trail).pack(side=tk.RIGHT)

        # Main container: left list, right scrollable editor
        container = tk.Frame(self, bg='#2a2a2a')
        container.pack(fill=tk.BOTH, expand=True)

        # Left pane (fixed width)
        left = tk.Frame(container, bg='#2a2a2a', width=200)
        left.pack(side=tk.LEFT, fill=tk.Y, padx=12, pady=10)
        left.pack_propagate(False)
        self.trail_list = tk.Listbox(left,
                                     bg='#1e1e1e', fg='#FFFFFF',
                                     font=("Segoe UI", 12),
                                     selectbackground='#005f73')
        self.trail_list.pack(fill=tk.BOTH, expand=True)
        self.trail_list.bind('<<ListboxSelect>>', self._on_select_trail)

        # Right pane scrollable
        right = tk.Frame(container, bg='#2a2a2a')
        right.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        canvas = tk.Canvas(right, bg='#2a2a2a', highlightthickness=0)
        vsb = tk.Scrollbar(right, orient='vertical', command=canvas.yview)
        vsb.pack(side=tk.RIGHT, fill=tk.Y)
        canvas.configure(yscrollcommand=vsb.set)
        scroll_frame = tk.Frame(canvas, bg='#2a2a2a')
        win = canvas.create_window((0,0), window=scroll_frame, anchor='nw')
        scroll_frame.bind('<Configure>',
                          lambda e: canvas.configure(scrollregion=canvas.bbox('all')))
        canvas.bind('<Configure>',
                    lambda e: canvas.itemconfig(win, width=e.width))
        def _scroll(e):
            canvas.yview_scroll(int(-1*(e.delta/120)), 'units')
        canvas.bind('<Enter>', lambda e: canvas.bind_all('<MouseWheel>', _scroll))
        canvas.bind('<Leave>', lambda e: canvas.unbind_all('<MouseWheel>'))
        scroll_frame.bind('<Enter>', lambda e: canvas.bind_all('<MouseWheel>', _scroll))
        scroll_frame.bind('<Leave>', lambda e: canvas.unbind_all('<MouseWheel>'))
        canvas.pack(fill=tk.BOTH, expand=True)

        # Editor frame (initially hidden)
        self.editor_frame = tk.Frame(scroll_frame, bg='#2a2a2a')
        self.editor_frame.pack(fill=tk.BOTH, expand=True, padx=12, pady=10)
        self.editor_frame.pack_forget()

        self._build_editor()


    def _on_field_change(self, *args):
        if not self._suspend_save and self.current_trail_path:
            self._save_json()


    def load_data(self, data=None, json_path=None):
        if not json_path:
            return
        base = os.path.dirname(json_path)
        self.trails_dir = os.path.join(base, 'trails')
        os.makedirs(self.trails_dir, exist_ok=True)
        self._refresh_trail_list()


    def _build_editor(self):
        self.name_var = tk.StringVar()
        # Width & Curvyness container
        ctrl = tk.Frame(self.editor_frame, bg='#2a2a2a')
        ctrl.pack(fill=tk.X, pady=6)
        # Width
        self.width_range = Range(ctrl, 10, 1000, label="Width")
        self.width_range.pack(fill=tk.X, pady=6)
        self.width_range.var_min.trace_add('write', self._on_field_change)
        self.width_range.var_max.trace_add('write', self._on_field_change)
        # Curvyness
        self.curve_range = Range(ctrl, 0, 8,
                                 label="Curvyness",
                                 force_fixed=True)
        self.curve_range.pack(fill=tk.X, pady=6)
        self.curve_range.var_max.trace_add('write', self._on_field_change)

        # Side-by-side asset editors
        af = tk.Frame(self.editor_frame, bg='#2a2a2a')
        af.pack(fill=tk.BOTH, expand=True, pady=(10,0))
        # Basic Assets
        left = tk.Frame(af, bg='#2a2a2a')
        left.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(0,4))
        tk.Label(left, text="Basic Assets",
                 font=("Segoe UI",11,'bold'),
                 fg='#DDDDDD', bg='#2a2a2a')\
            .pack(anchor='w', pady=(0,4))
        self.asset_editor = AssetEditor(
            left,
            get_asset_list=lambda: self.trail_data.get('assets', []),
            set_asset_list=lambda v: self.trail_data.__setitem__('assets', v),
            save_callback=self._save_json
        )
        self.asset_editor.pack(fill=tk.BOTH, expand=True)
        # Batch Assets
        right = tk.Frame(af, bg='#2a2a2a')
        right.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(4,0))
        tk.Label(right, text="Batch Asset Editor",
                 font=("Segoe UI",11,'bold'),
                 fg='#DDDDDD', bg='#2a2a2a')\
            .pack(anchor='w', pady=(0,4))
        self.batch_editor = BatchAssetEditor(
            right, save_callback=self._save_json
        )
        self.batch_editor.pack(fill=tk.BOTH, expand=True)


    def _refresh_trail_list(self):
        self.trail_list.delete(0, tk.END)
        if not self.trails_dir:
            return
        for fn in sorted(os.listdir(self.trails_dir)):
            if fn.endswith('.json'):
                try:
                    with open(os.path.join(self.trails_dir, fn)) as f:
                        name = json.load(f).get('name', fn[:-5])
                except:
                    name = fn[:-5]
                self.trail_list.insert(tk.END, name)


    def _on_select_trail(self, event):
        sel = self.trail_list.curselection()
        if not sel or not self.trails_dir:
            return
        # Save prior
        if self.current_trail_path and self.trail_data:
            self._save_json()

        name = self.trail_list.get(sel[0])
        path = os.path.join(self.trails_dir, f"{name}.json")
        if not os.path.exists(path):
            return

        try:
            with open(path) as f:
                self.trail_data = json.load(f)
            self.current_trail_path = path
            self.trail_data.setdefault('assets', [])
            self.trail_data.setdefault('batch_assets', DEFAULT_BATCH_ASSETS.copy())
            self.trail_data.setdefault('inherits_map_assets', False)
            self._load_editor()
            self.editor_frame.pack(side=tk.LEFT, fill=tk.BOTH,
                                   expand=True, padx=12, pady=10)
        except Exception as e:
            messagebox.showerror("Error loading trail", str(e))


    def _load_editor(self):
        self._suspend_save = True

        # Batch first
        self.batch_editor.load(self.trail_data.get('batch_assets', {}))
        # Simple fields
        self.name_var.set(self.trail_data.get('name',''))
        wmin, wmax = self.trail_data.get('min_width',0), self.trail_data.get('max_width',0)
        self.width_range.set(wmin, wmax)
        cv = self.trail_data.get('curvyness',0)
        self.curve_range.set(cv, cv)

        # Basic assets
        self.asset_editor.current_path = self.current_trail_path
        self.asset_editor.inherit_state = self.trail_data.get('inherits_map_assets', False)
        self.asset_editor.inherit_var.set(self.asset_editor.inherit_state)
        self.asset_editor.load_assets()

        self._suspend_save = False


    def _add_trail(self):
        name = simpledialog.askstring("New Trail", "Enter trail name:")
        if not name:
            return
        safe = "".join(c for c in name.strip() if c.isalnum() or c in ("_","-"))
        path = os.path.join(self.trails_dir, f"{safe}.json")
        if os.path.exists(path):
            messagebox.showerror("Error", f"Trail '{safe}' already exists.")
            return
        data = {
            'name': safe,
            'min_width': 100, 'max_width': 200,
            'curvyness': 50,
            'inherits_map_assets': False,
            'assets': [], 'batch_assets': DEFAULT_BATCH_ASSETS.copy()
        }
        with open(path, 'w') as f:
            json.dump(data, f, indent=2)
        self._refresh_trail_list()
        idx = self.trail_list.get(0, tk.END).index(safe)
        self.trail_list.selection_clear(0, tk.END)
        self.trail_list.selection_set(idx)
        self._on_select_trail(None)


    def _save_json(self):
        if self._suspend_save or not self.current_trail_path:
            return
        self.trail_data['min_width'], self.trail_data['max_width'] = self.width_range.get()
        self.trail_data['curvyness'] = self.curve_range.get()[0]
        self.trail_data['inherits_map_assets'] = self.asset_editor.inherit_state
        self.trail_data['batch_assets'] = self.batch_editor.save()
        try:
            with open(self.current_trail_path, 'w') as f:
                json.dump(self.trail_data, f, indent=2)
        except Exception as e:
            messagebox.showerror("Save Failed", str(e))


    @staticmethod
    def get_json_filename():
        return 'trails.json'

