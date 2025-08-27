import os
import json
import tkinter as tk
from tkinter import messagebox
from pages.search import AssetSearchWindow

ASSET_DIR = "SRC"

class ApplyPageSettings(tk.Frame):
    def __init__(self, parent, page_data, label="Apply To Another Asset"):
        super().__init__(parent)
        self.page_data_fn = page_data  # Rename for clarity: it's a function

        self.button = tk.Button(
            self,
            text=label,
            command=self._open_search,
            bg="#007BFF", fg="white",
            font=("Segoe UI", 13, "bold"),
            padx=8, pady=4,
            width=24
        )
        self.button.pack(pady=(10, 10))

    def _open_search(self):
        window = AssetSearchWindow(self)
        window.wait_window()

        result = window.selected_result
        if not result:
            return

        asset_type, asset_name = result
        info_path = os.path.join(ASSET_DIR, asset_name, "info.json")

        if not os.path.isfile(info_path):
            messagebox.showerror("Error", f"info.json not found for '{asset_name}'")
            return

        try:
            with open(info_path, "r") as f:
                data = json.load(f)
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load JSON: {e}")
            return

        # ✅ Fix: Call the function to get actual data
        for key, value in self.page_data_fn().items():
            data[key] = value

        try:
            with open(info_path, "w") as f:
                json.dump(data, f, indent=4)
        except Exception as e:
            messagebox.showerror("Error", f"Failed to save updated JSON: {e}")
            return

        messagebox.showinfo("Success", f"Settings applied to '{asset_name}'")
