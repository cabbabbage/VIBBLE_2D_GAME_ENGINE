# === File: pages/rooms_page.py ===
import os
import json
import tkinter as tk
from tkinter import ttk, messagebox, simpledialog
from pages.range import Range
from pages.assets_editor import AssetEditor
from pages.batch_asset_editor import BatchAssetEditor

# Default batch assets structure
DEFAULT_BATCH_ASSETS = {
    "has_batch_assets": False,
    "grid_spacing_min": 100,
    "grid_spacing_max": 100,
    "jitter_min": 0,
    "jitter_max": 0,
    "batch_assets": []
}

class RoomsPage(tk.Frame):
    def __init__(self, parent):
        super().__init__(parent, bg='#1e1e1e')
        self.current_room_path = None
        self.room_data = None
        self.rooms_dir = None
        self._suspend_save = False

        # Header
        header = tk.Frame(self, bg='#1e1e1e')
        header.pack(fill=tk.X, padx=12, pady=(10, 20))
        tk.Label(header, text="Rooms",
                 font=("Segoe UI", 20, "bold"),
                 fg="#005f73", bg='#1e1e1e')\
            .pack(side=tk.LEFT)
        tk.Button(header, text="Add New Room",
                  bg="#28a745", fg="white",
                  font=("Segoe UI", 13, "bold"), width=18,
                  command=self._add_room)\
            .pack(side=tk.RIGHT)

        # Main: left list + right scrollable editor
        container = tk.Frame(self, bg='#2a2a2a')
        container.pack(fill=tk.BOTH, expand=True)

        # Left pane (fixed width)
        left = tk.Frame(container, bg='#2a2a2a', width=200)
        left.pack(side=tk.LEFT, fill=tk.Y, padx=12, pady=10)
        left.pack_propagate(False)
        self.room_list = tk.Listbox(left,
                                    bg='#1e1e1e', fg='#FFFFFF',
                                    font=("Segoe UI",12),
                                    selectbackground='#005f73')
        self.room_list.pack(fill=tk.BOTH, expand=True)
        self.room_list.bind('<<ListboxSelect>>', self._on_select_room)

        # Right pane (scrollable)
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

        # Editor frame
        self.editor_frame = tk.Frame(scroll_frame, bg='#2a2a2a')
        self.editor_frame.pack(fill=tk.BOTH, expand=True, padx=12, pady=10)
        self.editor_frame.pack_forget()

        self._build_editor()


    # autosave hook for all fields
    def _on_field_change(self, *args):
        if not self._suspend_save and self.current_room_path:
            self._save_json()


    def load_data(self, data=None, json_path=None):
        if not json_path:
            return
        base = os.path.dirname(json_path)
        self.rooms_dir = os.path.join(base, "rooms")
        os.makedirs(self.rooms_dir, exist_ok=True)
        self._refresh_room_list()


    def _build_editor(self):
        # Room Name
        self.name_var = tk.StringVar()

        # Controls container
        ctrl = tk.Frame(self.editor_frame, bg='#2a2a2a')
        ctrl.pack(fill=tk.X, pady=6)
        # Width
        self.width_range = Range(ctrl, 700, 250000, label="Room Width")
        self.width_range.pack(fill=tk.X, pady=6)
        self.width_range.var_min.trace_add('write', self._on_field_change)
        self.width_range.var_max.trace_add('write', self._on_field_change)
        # Height
        self.height_range = Range(ctrl, 700, 250000, label="Room Height")
        self.height_range.pack(fill=tk.X, pady=6)
        self.height_range.var_min.trace_add('write', self._on_field_change)
        self.height_range.var_max.trace_add('write', self._on_field_change)
        # Edge Smoothness
        self.edge_smoothness = Range(ctrl, 0, 100,
                                     label="Edge Smoothness",
                                     force_fixed=True)
        self.edge_smoothness.pack(fill=tk.X, pady=6)
        self.edge_smoothness.var_min.trace_add('write', self._on_field_change)
        self.edge_smoothness.var_max.trace_add('write', self._on_field_change)

        # Geometry
        tk.Label(self.editor_frame, text="Geometry:",
                 font=("Segoe UI",12), fg='#FFFFFF', bg='#2a2a2a')\
            .pack(anchor='w', pady=(8,0))
        self.geometry_var = tk.StringVar()
        geom = ttk.Combobox(self.editor_frame,
                            textvariable=self.geometry_var,
                            state='readonly',
                            values=["Circle","Square"])
        geom.pack(fill=tk.X, pady=2)
        geom.bind('<<ComboboxSelected>>',
                  lambda e: (self._on_field_change(), self._toggle_height_state()))

        # Spawn/Boss
        tb = tk.Frame(self.editor_frame, bg='#2a2a2a')
        tb.pack(fill=tk.X, pady=6)
        self.spawn_var = tk.BooleanVar()
        self.boss_var = tk.BooleanVar()
        ttk.Checkbutton(tb, text="Is Spawn", variable=self.spawn_var,
                        command=lambda: (self._checkbox_logic('spawn'),
                                         self._on_field_change()))\
            .pack(side=tk.LEFT, padx=(0,10))
        ttk.Checkbutton(tb, text="Is Boss", variable=self.boss_var,
                        command=lambda: (self._checkbox_logic('boss'),
                                         self._on_field_change()))\
            .pack(side=tk.LEFT)

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
            get_asset_list=lambda: self.room_data.get('assets', []),
            set_asset_list=lambda v: self.room_data.__setitem__('assets', v),
            save_callback=self._save_json,
            positioning=True,
            current_path=self.current_room_path or ""
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


    def _refresh_room_list(self):
        self.room_list.delete(0, tk.END)
        if not self.rooms_dir:
            return
        os.makedirs(self.rooms_dir, exist_ok=True)
        for fn in sorted(os.listdir(self.rooms_dir)):
            if fn.endswith('.json'):
                try:
                    with open(os.path.join(self.rooms_dir, fn)) as f:
                        name = json.load(f).get('name', fn[:-5])
                except:
                    name = fn[:-5]
                self.room_list.insert(tk.END, name)


    def _on_select_room(self, event):
        sel = self.room_list.curselection()
        if not sel or not self.rooms_dir:
            return
        # save previous
        if self.current_room_path and self.room_data:
            self._save_json()

        name = self.room_list.get(sel[0])
        path = os.path.join(self.rooms_dir, f"{name}.json")
        if not os.path.exists(path):
            return

        try:
            with open(path) as f:
                self.room_data = json.load(f)
            self.current_room_path = path
            self.room_data.setdefault('assets', [])
            self.room_data.setdefault('batch_assets', DEFAULT_BATCH_ASSETS.copy())
            self.room_data.setdefault('inherits_map_assets', False)
            self._load_editor()
            self.editor_frame.pack(side=tk.LEFT, fill=tk.BOTH,
                                   expand=True, padx=12, pady=10)
        except Exception as e:
            messagebox.showerror("Error loading room", str(e))


    def _load_editor(self):
        self._suspend_save = True

        # batch first
        self.batch_editor.load(self.room_data.get('batch_assets', {}))
        # simple fields
        self.name_var.set(self.room_data.get('name',''))
        mw, xw = self.room_data.get('min_width',0), self.room_data.get('max_width',0)
        self.width_range.set(mw, xw)
        mh, xh = self.room_data.get('min_height',0), self.room_data.get('max_height',0)
        self.height_range.set(mh, xh)
        es = self.room_data.get('edge_smoothness',0)
        self.edge_smoothness.set(es, es)
        self.geometry_var.set(self.room_data.get('geometry','Random'))
        self.spawn_var.set(self.room_data.get('is_spawn', False))
        self.boss_var.set(self.room_data.get('is_boss', False))

        # basic assets
        self.asset_editor.current_path = self.current_room_path
        self.asset_editor.inherit_state = self.room_data.get('inherits_map_assets', False)
        self.asset_editor.inherit_var.set(self.asset_editor.inherit_state)
        self.asset_editor.load_assets()

        self._suspend_save = False


    def _toggle_height_state(self):
        if self.geometry_var.get() == "Circle":
            self.height_range.disable()
        else:
            self.height_range.enable()


    def _checkbox_logic(self, changed):
        if self.spawn_var.get() and self.boss_var.get():
            if changed == 'spawn':
                self.boss_var.set(False)
            else:
                self.spawn_var.set(False)


    def _add_room(self):
        name = simpledialog.askstring("New Room", "Enter room name:")
        if not name:
            return
        safe = "".join(c for c in name.strip() if c.isalnum() or c in ("_","-"))
        path = os.path.join(self.rooms_dir, f"{safe}.json")
        if os.path.exists(path):
            messagebox.showerror("Error", f"Room '{safe}' already exists.")
            return
        data = {
            'name': safe,
            'min_width': 100, 'max_width': 200,
            'min_height': 100, 'max_height': 200,
            'edge_smoothness': 0,
            'geometry': 'Random',
            'is_spawn': False, 'is_boss': False,
            'inherits_map_assets': False,
            'assets': [], 'batch_assets': DEFAULT_BATCH_ASSETS.copy()
        }
        with open(path,'w') as f:
            json.dump(data, f, indent=2)
        self._refresh_room_list()
        idx = self.room_list.get(0, tk.END).index(safe)
        self.room_list.selection_clear(0, tk.END)
        self.room_list.selection_set(idx)
        self._on_select_room(None)


    def _save_json(self):
        if not self.current_room_path or not self.room_data:
            return
        self.room_data['min_width'], self.room_data['max_width'] = self.width_range.get()
        self.room_data['min_height'], self.room_data['max_height'] = self.height_range.get()
        self.room_data['edge_smoothness'] = self.edge_smoothness.get()[0]
        self.room_data['geometry'] = self.geometry_var.get()
        self.room_data['is_spawn'] = self.spawn_var.get()
        self.room_data['is_boss'] = self.boss_var.get()
        self.room_data['inherits_map_assets'] = self.asset_editor.inherit_state
        self.room_data['batch_assets'] = self.batch_editor.save()
        try:
            with open(self.current_room_path,'w') as f:
                json.dump(self.room_data,f,indent=2)
        except Exception as e:
            messagebox.showerror("Save Failed", str(e))


    @staticmethod
    def get_json_filename():
        return "rooms.json"
