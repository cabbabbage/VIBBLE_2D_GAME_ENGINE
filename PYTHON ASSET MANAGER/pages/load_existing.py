import os
import json
import tkinter as tk
from tkinter import ttk, messagebox
from pages.search import AssetSearchWindow

_returned_data = None


def open_window_and_return_data(key):
    global _returned_data
    _returned_data = None

    root = tk.Toplevel()
    root.title("Load Existing Data by Key")
    root.geometry("500x550")
    app = LoadExistingChildren(root, key)
    root.grab_set()
    root.wait_window()
    return _returned_data


class LoadExistingChildren(ttk.Frame):
    def __init__(self, parent, key):
        super().__init__(parent)
        self.pack(fill="both", expand=True)
        self.parent = parent
        self.key = key
        self.check_vars = []

        self.file_frame = ttk.Frame(self)
        self.file_frame.pack(fill="both", expand=True, padx=10, pady=10)

        self.button_frame = ttk.Frame(self)
        self.button_frame.pack(pady=8)

        add_btn = ttk.Button(self.button_frame, text="Add", command=self._gather_selected)
        add_btn.pack()

        self.after(100, self._launch_search)

    def _launch_search(self):
        win = AssetSearchWindow(self)
        win.wait_window()
        result = win.selected_result

        if not result or result[0] != "asset":
            self.parent.destroy()
            return

        folder_name = result[1]
        full_path = os.path.join("SRC", folder_name)
        if not os.path.isdir(full_path):
            messagebox.showerror("Error", f"Invalid folder: {full_path}")
            self.parent.destroy()
            return

        self._load_matching_jsons(full_path)

    def _load_matching_jsons(self, folder):
        for widget in self.file_frame.winfo_children():
            widget.destroy()

        canvas = tk.Canvas(self.file_frame)
        scroll_frame = ttk.Frame(canvas)
        scrollbar = ttk.Scrollbar(self.file_frame, orient="vertical", command=canvas.yview)
        canvas.configure(yscrollcommand=scrollbar.set)

        canvas.create_window((0, 0), window=scroll_frame, anchor="nw")
        scroll_frame.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))

        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        self.check_vars.clear()
        json_files = sorted([
            f for f in os.listdir(folder)
            if f.startswith("child_assets_") and f.endswith(".json")
        ])

        if not json_files:
            ttk.Label(scroll_frame, text="No child_assets_#.json files found").pack()
            return

        for fname in json_files:
            path = os.path.join(folder, fname)
            try:
                with open(path) as f:
                    data = json.load(f)
                    if self.key not in data:
                        continue
                var = tk.BooleanVar()
                cb = ttk.Checkbutton(scroll_frame, text=path, variable=var)
                cb.pack(anchor="w", pady=2, padx=5)
                self.check_vars.append((var, path))
            except Exception as e:
                print(f"[LoadExisting] Skipped invalid file: {path} ({e})")

        if not self.check_vars:
            ttk.Label(scroll_frame, text=f"No files found with key: '{self.key}'").pack()

    def _gather_selected(self):
        global _returned_data
        combined = []

        for var, path in self.check_vars:
            if var.get():
                try:
                    with open(path) as f:
                        data = json.load(f)
                        values = data.get(self.key)
                        if isinstance(values, list):
                            combined.extend(values)
                        elif values:
                            combined.append(values)
                except Exception as e:
                    print(f"[LoadExisting] Failed to read {path}: {e}")

        _returned_data = combined
        self.parent.destroy()
