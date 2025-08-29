# === File: pages/animations.py ===
import os
import json
import tkinter as tk
from tkinter import messagebox
from asset_pages.AnimationConfig import AnimationEditor
import pandas as pd

class AnimationsPage(tk.Frame):
    def __init__(self, parent, asset_folder):
        super().__init__(parent)
        self.asset_folder = asset_folder
        self.anim_configs = {}
        self._loaded = False

        # Page boundary
        self.configure(bg='#1e1e1e')

        # Header with title and Add button
        header = tk.Frame(self, bg='#1e1e1e')
        header.pack(fill=tk.X, pady=(10, 20))
        title = tk.Label(
            header, text="Animations",
            font=("Segoe UI", 20, "bold"),
            fg="#005f73", bg='#1e1e1e'
        )
        title.pack(side=tk.LEFT, padx=12)
        add_btn = tk.Button(
            header, text="Add New Animation",
            bg="#28a745", fg="white",
            font=("Segoe UI", 13, "bold"),
            width=18, padx=8, pady=4,
            command=self._add_empty_editor
        )
        add_btn.pack(side=tk.RIGHT, padx=12)

        # Scrollable content area
        self.canvas = tk.Canvas(self, bg='#2a2a2a', highlightthickness=0)
        self.scroll_frame = tk.Frame(self.canvas, bg='#2a2a2a')
        window_id = self.canvas.create_window((0, 0), window=self.scroll_frame, anchor='nw')
        # update scrollregion when content changes
        self.scroll_frame.bind(
            '<Configure>',
            lambda e: self.canvas.configure(scrollregion=self.canvas.bbox('all'))
        )
        # make scroll_frame span full canvas width
        self.canvas.bind(
            '<Configure>',
            lambda e: self.canvas.itemconfig(window_id, width=e.width)
        )
        # mouse-wheel scrolling on hover
        def _on_mousewheel(evt):
            self.canvas.yview_scroll(int(-1*(evt.delta/120)), 'units')
        self.scroll_frame.bind('<Enter>', lambda e: self.canvas.bind_all('<MouseWheel>', _on_mousewheel))
        self.scroll_frame.bind('<Leave>', lambda e: self.canvas.unbind_all('<MouseWheel>'))
        self.canvas.pack(fill=tk.BOTH, expand=True)

        # Load trigger definitions
        self.triggers_df = pd.read_csv("PYTHON ASSET MANAGER/triggers.csv")

        # Load existing animations
        self._load_existing()
        self._loaded = True

    def _load_existing(self):
        # Clear existing editor frames
        for child in list(self.scroll_frame.winfo_children()):
            child.destroy()
        self.anim_configs.clear()

        # Load or initialize animations data
        info_path = os.path.join(self.asset_folder, 'info.json')
        data = {}
        if os.path.isfile(info_path):
            try:
                with open(info_path, 'r') as f:
                    data = json.load(f)
            except json.JSONDecodeError:
                data = {}
        animations = data.get('animations', {})
        if not isinstance(animations, dict) or 'default' not in animations:
            animations = {'default': {'path': None, 'end_path': None}}
            data['animations'] = animations
            try:
                with open(info_path, 'w') as f:
                    json.dump(data, f, indent=4)
            except Exception as e:
                messagebox.showerror("Error", f"Could not initialize animations: {e}")

        # Create editor for each animation
        for trigger, anim_data in animations.items():
            if not isinstance(anim_data, dict):
                anim_data = {'path': None, 'end_path': None}
            self._add_editor(trigger, anim_data)

    def _add_empty_editor(self):
        self._add_editor('', {})

    def _add_editor(self, trigger, anim_data):
        key = trigger or f"__temp_{len(self.anim_configs)}"
        if key in self.anim_configs:
            return

        wrapper = tk.Frame(self.scroll_frame, bg='#2a2a2a')
        wrapper.pack(fill=tk.X, padx=12, pady=6)

        # Animation editor
        editor = AnimationEditor(wrapper)
        editor.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 6))
        editor.load(trigger or 'default', anim_data, self.asset_folder)
        editor.on_change = self._autosave

        # Delete button
        del_btn = tk.Button(
            wrapper, text="X",
            command=lambda w=wrapper, k=key: self._delete_editor(w, k),
            bg="#D9534F", fg="white",
            font=("Segoe UI", 12, "bold"), width=3, padx=4
        )
        del_btn.pack(side=tk.RIGHT)

        self.anim_configs[key] = editor

    def _delete_editor(self, wrapper, key):
        wrapper.destroy()
        self.anim_configs.pop(key, None)
        self._autosave()

    def _autosave(self):
        if not self._loaded:
            return
        info_path = os.path.join(self.asset_folder, 'info.json')
        data = {}
        if os.path.isfile(info_path):
            try:
                with open(info_path, 'r') as f:
                    data = json.load(f)
            except json.JSONDecodeError:
                data = {}

        animations = {}
        for key, editor in self.anim_configs.items():
            saved = editor.save()
            trigger = editor.trigger_name.strip() or 'default'
            if isinstance(saved, dict):
                animations[trigger] = saved

        if 'default' not in animations:
            animations['default'] = {'path': None, 'end_path': None}
        data['animations'] = animations

        # Sync tags
        tags = data.get('tags', [])
        for tr in animations:
            if tr not in tags:
                tags.append(tr)
        data['tags'] = tags

        try:
            with open(info_path, 'w') as f:
                json.dump(data, f, indent=4)
        except Exception as e:
            messagebox.showerror("Save Failed", str(e))

    def load(self, info_path):
        self.asset_folder = os.path.dirname(info_path)
        self._load_existing()

