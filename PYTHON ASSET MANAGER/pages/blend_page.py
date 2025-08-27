import os
import json
import tkinter as tk
from tkinter import ttk, messagebox

BLEND_MODES = [
    "SDL_BLENDMODE_NONE",
    "SDL_BLENDMODE_BLEND",
    "SDL_BLENDMODE_ADD",
    "SDL_BLENDMODE_MOD",
    "SDL_BLENDMODE_MUL"
]

class BlendModePage(ttk.Frame):
    def __init__(self, parent, on_save_callback=None):
        super().__init__(parent)
        self.config_path = None
        self.on_save = on_save_callback
        self._suppress_save = False

        font_large = ("Segoe UI", 14)
        style = ttk.Style()
        style.configure("Big.TButton", font=("Segoe UI", 14))
        style.configure("Big.TMenubutton", font=font_large)

        ttk.Label(self, text="Blend Mode:", font=font_large).grid(
            row=0, column=0, sticky="w", padx=16, pady=10
        )
        self.blend_var = tk.StringVar()
        self.blend_menu = ttk.OptionMenu(self, self.blend_var, BLEND_MODES[0], *BLEND_MODES)
        self.blend_menu.config(style="Big.TMenubutton", width=30)
        self.blend_menu.grid(row=0, column=1, sticky="w", padx=16, pady=10)

        btn_frame = ttk.Frame(self)
        btn_frame.grid(row=1, column=0, columnspan=2, pady=(10, 20))

        save_btn = tk.Button(
            btn_frame,
            text="Save",
            command=self.save,
            bg="#007BFF",
            fg="white",
            font=("Segoe UI", 12, "bold")
        )
        save_btn.pack(side=tk.LEFT, padx=(10, 5))

        load_btn = tk.Button(
            btn_frame,
            text="Load",
            command=self.load,
            bg="#28a745",
            fg="white",
            font=("Segoe UI", 12, "bold")
        )
        load_btn.pack(side=tk.LEFT, padx=(5, 10))

    def load(self, config_path=None):
        """
        Load blend mode from a JSON file. If config_path is None, open a file dialog.
        Expected JSON format:
        {
            "blend_mode": "SDL_BLENDMODE_BLEND"
        }
        """
        if config_path is None:
            from tkinter import filedialog
            path = filedialog.askopenfilename(
                title="Select Blend Mode Config",
                filetypes=[("JSON Files", "*.json")]
            )
            if not path:
                return
            self.config_path = path
        else:
            self.config_path = config_path

        if not os.path.exists(self.config_path):
            messagebox.showerror("Error", f"File not found: {self.config_path}")
            return

        try:
            with open(self.config_path, "r") as f:
                data = json.load(f)
            self._suppress_save = True
            self.blend_var.set(data.get("blend_mode", BLEND_MODES[0]))
            self._suppress_save = False
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load config: {e}")

    def save(self):
        """
        Save the selected blend mode to a JSON file. If no path specified, open a file dialog.
        """
        if self._suppress_save:
            return

        if not self.config_path:
            from tkinter import filedialog
            path = filedialog.asksaveasfilename(
                title="Save Blend Mode Config",
                defaultextension=".json",
                filetypes=[("JSON Files", "*.json")]
            )
            if not path:
                return
            self.config_path = path

        # Load existing JSON or start with empty dict
        existing_data = {}
        if os.path.exists(self.config_path):
            try:
                with open(self.config_path, "r") as f:
                    existing_data = json.load(f)
                    if not isinstance(existing_data, dict):
                        existing_data = {}
            except Exception:
                existing_data = {}

        # Update only the blend_mode key
        existing_data["blend_mode"] = self.blend_var.get()

        try:
            with open(self.config_path, "w") as f:
                json.dump(existing_data, f, indent=4)
            messagebox.showinfo("Saved", f"Blend mode saved to {self.config_path}")
            if self.on_save:
                self.on_save(self.blend_var.get())
        except Exception as e:
            messagebox.showerror("Error", f"Failed to save config: {e}")
