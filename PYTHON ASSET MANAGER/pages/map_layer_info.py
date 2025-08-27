# === File: pages/map_layer_info.py ===
import os
import tkinter as tk
from tkinter import ttk
from colorsys import hsv_to_rgb
from pages.search_rooms import SearchRoomsFrame
from pages.room_info  import RoomInfo

def level_to_hex_color(level):
    hue = (level * 0.13) % 1.0
    r, g, b = hsv_to_rgb(hue, 0.6, 1.0)
    return '#{:02x}{:02x}{:02x}'.format(int(r*255), int(g*255), int(b*255))

class MapLayerInfo(ttk.Frame):
    def __init__(self, parent, level, rooms_dir,
                 name="", radius=0, rooms_data=None,
                 save_callback=None, delete_callback=None):
        self.level      = level
        self.rooms_dir  = rooms_dir
        self.save       = save_callback or (lambda: None)
        self.delete     = delete_callback or (lambda _: None)
        self.rooms      = []
        self.rooms_data = rooms_data or []

        outline = level_to_hex_color(level)
        # colored border container
        self.bg_frame = tk.Frame(parent,
                                 bg='#2a2a2a',
                                 highlightbackground=outline,
                                 highlightthickness=3, bd=0)
        self.bg_frame.pack(side="left", fill="both", expand=True, padx=6, pady=6)

        super().__init__(self.bg_frame, style='TFrame', padding=6)
        self.pack(fill="both", expand=True)

        # vars
        self.name_var      = tk.StringVar(value=name)
        self.radius_var    = tk.IntVar(value=radius)
        self.min_rooms_var = tk.IntVar(value=0)
        self.max_rooms_var = tk.IntVar(value=0)

        self._build_ui()
        self._load_rooms_data(self.rooms_data)

    def _build_ui(self):
        # —— header row ——
        top = ttk.Frame(self); top.pack(fill="x", pady=2)
        ttk.Label(top, text="Level:", font=("Segoe UI",13,"bold")).pack(side="left")
        self.level_label = ttk.Label(top, text=str(self.level),
                                     font=("Segoe UI",13,"bold"))
        self.level_label.pack(side="left", padx=4)
        ttk.Button(top, text="✕", width=2, command=self._on_delete,
                   style='TButton').pack(side="right")

        # —— name —— 
        nf = ttk.Frame(self); nf.pack(fill="x", pady=2)
        ttk.Label(nf, text="Name:").pack(side="left")
        ent = ttk.Entry(nf, textvariable=self.name_var, style='TEntry')
        ent.pack(side="left", fill="x", expand=True, padx=4)
        self.name_var.trace_add('write', lambda *a: self.save())

        # —— radius (read-only) ——
        rf = ttk.Frame(self); rf.pack(fill="x", pady=2)
        ttk.Label(rf, text="Radius:").pack(side="left")
        self.radius_label = ttk.Label(rf, text=str(self.radius_var.get()))
        self.radius_label.pack(side="left", padx=4)

        # —— min/max rooms —— 
        lf = ttk.Frame(self); lf.pack(fill="x", pady=2)
        ttk.Label(lf, text="Min Rooms:").pack(side="left")
        self.min_spin = tk.Spinbox(
            lf, from_=0, to=9999,
            textvariable=self.min_rooms_var,
            width=5,
            bg='#1e1e1e', fg='white', insertbackground='white',
            command=self._on_min_rooms_changed
        )
        self.min_spin.pack(side="left", padx=4)
        # re-validate on focus out / Enter
        for ev in ("<FocusOut>","<Return>"):
            self.min_spin.bind(ev, lambda e: self._on_min_rooms_changed())

        ttk.Label(lf, text="Max Rooms:").pack(side="left", padx=(10,0))
        self.max_spin = tk.Spinbox(
            lf, from_=0, to=9999,
            textvariable=self.max_rooms_var,
            width=5,
            bg='#1e1e1e', fg='white', insertbackground='white',
            command=self._on_max_rooms_changed
        )
        self.max_spin.pack(side="left", padx=4)
        for ev in ("<FocusOut>","<Return>"):
            self.max_spin.bind(ev, lambda e: self._on_max_rooms_changed())

        # —— add room ——
        bf = ttk.Frame(self); bf.pack(fill="x", pady=(6,4))
        ttk.Button(bf, text="Add Room", command=self._on_add_room,
                   style='TButton').pack(side="left")

        # —— room list ——
        rooms_box = ttk.LabelFrame(self, text="Rooms", style='TFrame')
        rooms_box.pack(fill="both", expand=True, pady=4)
        self.rooms_container = rooms_box

    def _on_delete(self):
        self.bg_frame.destroy()
        self.delete(self)

    def _on_add_room(self):
        top = tk.Toplevel(self.bg_frame)
        top.title("Search Rooms")
        top.configure(bg='#1e1e1e')
        sr = SearchRoomsFrame(
            top,
            rooms_dir=self.rooms_dir,
            on_select=lambda nm, top=top: self._add_room_callback(nm, top)
        )
        sr.pack(fill="both", expand=True)

    def _add_room_callback(self, room_name, top=None):
        w = RoomInfo(
            self.rooms_container,
            room_name,
            min_instances=0, max_instances=0,
            required_children=[]
        )
        w.pack(fill="x", pady=2, padx=4)
        w.save = self._on_room_value_change
        w.min_var.trace_add("write", lambda *a: w.save())
        w.max_var.trace_add("write", lambda *a: w.save())
        self.rooms.append(w)
        self._on_room_value_change()
        if top: top.destroy()

    def _on_room_value_change(self):
        total_min = sum(r.min_var.get() for r in self.rooms)
        total_max = sum(r.max_var.get() for r in self.rooms)

        # only bump min up if children require it
        cur_min = self.min_rooms_var.get()
        if total_min > cur_min:
            self.min_rooms_var.set(total_min)

        # clamp max between current min and sum(max)
        cur_max = self.max_rooms_var.get()
        new_max = max(self.min_rooms_var.get(), min(cur_max, total_max))
        self.max_rooms_var.set(new_max)

        # adjust spinbox ranges
        self.max_spin.config(from_=self.min_rooms_var.get(), to=total_max)

        self.save()

    def _on_min_rooms_changed(self):
        # user typed a new min; re-clamp against children
        self._on_room_value_change()

    def _on_max_rooms_changed(self):
        # user typed a new max; re-clamp
        self._on_room_value_change()

    def set_radius(self, radius):
        self.radius_var.set(radius)
        self.radius_label.config(text=str(radius))

    def set_level(self, new_level):
        self.level = new_level
        self.level_label.config(text=str(new_level))
        self.bg_frame.config(highlightbackground=level_to_hex_color(new_level))

    def _load_rooms_data(self, data):
        # clear out old
        for r in list(self.rooms):
            r.destroy()
        self.rooms.clear()

        # add each saved
        for rd in data:
            self._add_room_callback(rd.get("name",""), top=None)
            last = self.rooms[-1]
            last.min_var.set(rd.get("min_instances",0))
            last.max_var.set(rd.get("max_instances",0))
            last.required_children = rd.get("required_children",[])
            last._refresh_required_child_list()

    def get_data(self):
        return {
            "level":     self.level,
            "name":      self.name_var.get(),
            "radius":    self.radius_var.get(),
            "min_rooms": self.min_rooms_var.get(),
            "max_rooms": self.max_rooms_var.get(),
            "rooms":     [r.get_data() for r in self.rooms]
        }
