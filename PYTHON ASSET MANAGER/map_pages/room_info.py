# === File: pages/room_info.py ===
import os
import json
import tkinter as tk
from tkinter import ttk, messagebox
from map_pages.search_rooms import SearchRoomsFrame

class RoomInfo(tk.Frame):
    """
    UI element for configuring a single room within a map layer.
    Now supports:
      - min_instances, max_instances
      - required_child selection (supports multiple)
    """
    def __init__(self, parent, name,
                 min_instances=0, max_instances=0,
                 required_children=None):
        super().__init__(parent, bg='#2a2a2a', bd=0)
        self.room_name = name
        self.name_label_var = tk.StringVar(value=name)
        self.required_children = required_children or []

        # header with room name and delete button
        hdr = tk.Frame(self, bg='#2a2a2a')
        hdr.pack(fill='x', pady=(0,4))
        tk.Label(
            hdr, textvariable=self.name_label_var,
            font=("Segoe UI", 12, "bold"),
            fg='white', bg='#2a2a2a'
        ).pack(side='left', padx=(4,0))
        tk.Button(
            hdr, text='✕',
            bg='#D9534F', fg='white',
            relief='flat', font=("Segoe UI",11,"bold"),
            width=2, command=self._on_delete
        ).pack(side='right', padx=(0,4))

        # Min / Max instances
        row1 = tk.Frame(self, bg='#2a2a2a')
        row1.pack(fill='x', padx=4, pady=2)
        tk.Label(
            row1, text="Min Instances:",
            font=("Segoe UI",11),
            fg='white', bg='#2a2a2a'
        ).pack(side='left')
        self.min_var = tk.IntVar(value=min_instances)
        min_sb = tk.Spinbox(
            row1, from_=0, to=9999,
            textvariable=self.min_var,
            font=("Segoe UI",11),
            width=5, relief='flat',
            bg='#1e1e1e', fg='white',
            command=self._on_value_change,
            insertbackground='white'
        )
        min_sb.pack(side='left', padx=6)
        min_sb.bind("<FocusOut>", lambda e: self._on_value_change())
        min_sb.bind("<Return>",   lambda e: self._on_value_change())

        row2 = tk.Frame(self, bg='#2a2a2a')
        row2.pack(fill='x', padx=4, pady=2)
        tk.Label(
            row2, text="Max Instances:",
            font=("Segoe UI",11),
            fg='white', bg='#2a2a2a'
        ).pack(side='left')
        self.max_var = tk.IntVar(value=max_instances)
        max_sb = tk.Spinbox(
            row2, from_=0, to=9999,
            textvariable=self.max_var,
            font=("Segoe UI",11),
            width=5, relief='flat',
            bg='#1e1e1e', fg='white',
            command=self._on_value_change,
            insertbackground='white'
        )
        max_sb.pack(side='left', padx=6)
        max_sb.bind("<FocusOut>", lambda e: self._on_value_change())
        max_sb.bind("<Return>",   lambda e: self._on_value_change())

        # Required children list
        child_frame = tk.LabelFrame(
            self, text="Required Children",
            font=("Segoe UI",11,"bold"),
            fg='white', bg='#2a2a2a', labelanchor='n'
        )
        child_frame.configure(highlightbackground='#444', highlightthickness=1)
        child_frame.pack(fill='x', padx=4, pady=6)
        self.child_list = tk.Frame(child_frame, bg='#2a2a2a')
        self.child_list.pack(fill='x', padx=4, pady=(4,2))
        self._refresh_required_child_list()

        tk.Button(
            child_frame, text="Add Child",
            bg='#007BFF', fg='white',
            relief='flat', font=("Segoe UI",11,"bold"),
            command=self._on_add_required_child
        ).pack(anchor='w', padx=4, pady=(0,4))

    def _refresh_required_child_list(self):
        for w in self.child_list.winfo_children():
            w.destroy()
        for child in self.required_children:
            f = tk.Frame(self.child_list, bg='#2a2a2a')
            f.pack(fill='x', pady=1, padx=(0,4))
            tk.Label(
                f, text=child,
                font=("Segoe UI",11),
                fg='white', bg='#2a2a2a'
            ).pack(side='left')
            tk.Button(
                f, text='✕', width=2,
                bg='#D9534F', fg='white',
                relief='flat', font=("Segoe UI",10,"bold"),
                command=lambda c=child: self._remove_required_child(c)
            ).pack(side='left', padx=6)

    def _remove_required_child(self, name):
        if name in self.required_children:
            self.required_children.remove(name)
            self._refresh_required_child_list()
            self._on_value_change()

    def _on_add_required_child(self):
        top = tk.Toplevel(self)
        top.configure(bg='#1e1e1e')
        top.title("Select Required Child")
        layer = self.master.master
        sr = SearchRoomsFrame(
            top, rooms_dir=layer.rooms_dir,
            on_select=lambda nm: self._on_required_selected(nm, top)
        )
        sr.pack(fill='both', expand=True)

    def _on_required_selected(self, name, popup):
        if name not in self.required_children:
            self.required_children.append(name)
            self._refresh_required_child_list()
            self._on_value_change()
        popup.destroy()

    def _on_delete(self):
        # remove self from parent layer
        layer = self.master.master
        if hasattr(layer, 'rooms'):
            try:
                layer.rooms.remove(self)
            except ValueError:
                pass
        self.destroy()
        if hasattr(layer, 'save'):
            layer.save()

    def _on_value_change(self):
        layer = self.master.master
        if hasattr(layer, 'save'):
            layer.save()

    def get_data(self):
        return {
            "name":        self.room_name,
            "min_instances": self.min_var.get(),
            "max_instances": self.max_var.get(),
            "required_children": list(self.required_children)
        }

    def load(self, data):
        self.min_var.set(data.get("min_instances", 0))
        self.max_var.set(data.get("max_instances", 0))
        self.required_children = data.get("required_children", [])
        self._refresh_required_child_list()

