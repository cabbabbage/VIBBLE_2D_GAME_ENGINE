import os
import json
import tkinter as tk
from tkinter import ttk, messagebox
from pages.batch_asset_editor import BatchAssetEditor  # Updated to use BatchAssetEditor

class BoundaryPage(ttk.Frame):
    def __init__(self, parent):
        super().__init__(parent)
        self.data = {
            "batch_assets": {
                "has_batch_assets": False,
                "grid_spacing_min": 100,
                "grid_spacing_max": 100,
                "jitter_min": 0,
                "jitter_max": 0,
                "batch_assets": []
            }
        }
        self.json_path = None

        self.editor = BatchAssetEditor(
            self,
            save_callback=self._save_json
        )
        self.editor.pack(fill=tk.BOTH, expand=True, padx=40, pady=10)

    def _save_json(self):
        if not self.json_path:
            return
        self.data["batch_assets"] = self.editor.save()  # make sure jitter values are updated here
        try:
            with open(self.json_path, "w") as f:
                json.dump(self.data, f, indent=2)
        except Exception as e:
            messagebox.showerror("Save Failed", str(e))

    def load_data(self, data, json_path=None):
        self.data = data or {
            "batch_assets": {
                "has_batch_assets": False,
                "grid_spacing_min": 100,
                "grid_spacing_max": 100,
                "jitter_min": 0,
                "jitter_max": 0,
                "batch_assets": []
            }
        }
        self.json_path = json_path
        self.editor.load(self.data.get("batch_assets", {}))

    def get_data(self):
        self.data["batch_assets"] = self.editor.save()
        return self.data

    @staticmethod
    def get_json_filename():
        return "map_boundary.json"
