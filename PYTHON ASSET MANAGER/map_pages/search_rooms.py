# search_rooms.py

import os
import json
import tkinter as tk
from tkinter import ttk

class SearchRoomsFrame(ttk.Frame):
    """
    A frame that lets the user search for room JSON files by their "name" field.
    Instantiate with a parent widget, the directory to scan, and an optional callback:
        def on_room_selected(room_name): ...
        frame = SearchRoomsFrame(parent, rooms_dir, on_room_selected)
    """
    def __init__(self, parent, rooms_dir, on_select=None):
        super().__init__(parent)
        self.rooms_dir = rooms_dir
        self.on_select = on_select
        self.all_rooms = []          # list of all room names
        self.filtered_rooms = []     # list of rooms matching current search
        self._load_rooms()
        self._build_ui()

    def _load_rooms(self):
        """Scan self.rooms_dir for .json files, read each, and collect its "name"."""
        names = []
        if not os.path.isdir(self.rooms_dir):
            return
        for fname in os.listdir(self.rooms_dir):
            if not fname.lower().endswith('.json'):
                continue
            path = os.path.join(self.rooms_dir, fname)
            try:
                with open(path, 'r') as f:
                    data = json.load(f)
                name = data.get("name")
                if isinstance(name, str):
                    names.append(name)
            except Exception:
                # skip invalid JSON
                continue
        self.all_rooms = sorted(names, key=str.lower)
        self.filtered_rooms = list(self.all_rooms)

    def _build_ui(self):
        """Create search entry and listbox of rooms."""
        # Search bar
        self.search_var = tk.StringVar()
        search_frame = ttk.Frame(self)
        search_frame.pack(fill="x", padx=5, pady=(5, 0))
        ttk.Label(search_frame, text="Search:").pack(side="left")
        entry = ttk.Entry(search_frame, textvariable=self.search_var)
        entry.pack(side="left", fill="x", expand=True, padx=(4,0))
        entry.bind("<KeyRelease>", lambda e: self._on_search())

        # Listbox + scrollbar
        list_frame = ttk.Frame(self)
        list_frame.pack(fill="both", expand=True, padx=5, pady=5)
        self.listbox = tk.Listbox(list_frame, activestyle="dotbox")
        self.listbox.pack(side="left", fill="both", expand=True)
        scrollbar = ttk.Scrollbar(list_frame, orient="vertical", command=self.listbox.yview)
        scrollbar.pack(side="right", fill="y")
        self.listbox.config(yscrollcommand=scrollbar.set)

        # Bind double-click or Enter to selection
        self.listbox.bind("<Double-Button-1>", lambda e: self._select_current())
        self.listbox.bind("<Return>", lambda e: self._select_current())

        # Populate initial list
        self._update_listbox()

    def _on_search(self):
        """Filter self.all_rooms by the search_var and update listbox."""
        term = self.search_var.get().strip().lower()
        if term == "":
            self.filtered_rooms = list(self.all_rooms)
        else:
            self.filtered_rooms = [r for r in self.all_rooms if term in r.lower()]
        self._update_listbox()

    def _update_listbox(self):
        """Refresh the listbox contents from self.filtered_rooms."""
        self.listbox.delete(0, tk.END)
        for name in self.filtered_rooms:
            self.listbox.insert(tk.END, name)

    def _select_current(self):
        """Handle user selecting a room: call callback with the room name."""
        idx = self.listbox.curselection()
        if not idx:
            return
        room_name = self.filtered_rooms[idx[0]]
        if callable(self.on_select):
            self.on_select(room_name)

    def refresh(self):
        """Re-scan the directory and refresh UI (e.g., if files changed)."""
        self._load_rooms()
        self._on_search()

