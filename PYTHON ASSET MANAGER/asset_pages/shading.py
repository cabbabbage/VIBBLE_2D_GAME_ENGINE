# === File: pages/shading.py ===
import os
import json
import tkinter as tk
from tkinter import ttk, messagebox
from shared.range import Range


class ShadingPage(ttk.Frame):
    def __init__(self, parent):
        super().__init__(parent)
        self.asset_path = None

        self.FONT = ('Segoe UI', 14)
        self.BOLD = ('Segoe UI', 18, 'bold')
        self.COLOR_ACCENT = "#005f73"

        # Title header
        ttk.Style(self).configure('Dark.TFrame', background='#1e1e1e')
        self.configure(style='Dark.TFrame')
        ttk.Label(
            self, text="Shading Settings", font=self.BOLD, foreground=self.COLOR_ACCENT,
            background='#1e1e1e'
        ).pack(anchor='w', padx=12, pady=(10, 14))

        # Master toggle
        self.has_shading_var = tk.BooleanVar()
        ttk.Checkbutton(
            self, text="Has Shading", variable=self.has_shading_var,
            command=self._toggle_ui_state, style='Shading.TCheckbutton'
        ).pack(anchor='w', padx=20, pady=4)

        # Container for shading controls
        self.options_frame = ttk.Frame(self, style='Dark.TFrame')
        self.options_frame.pack(fill=tk.X, padx=12, pady=8)

        # Base Shadow
        self.base_shadow_var = tk.BooleanVar()
        ttk.Checkbutton(
            self.options_frame, text="Has Base Shadow",
            variable=self.base_shadow_var, style='Shading.TCheckbutton'
        ).pack(anchor='w', pady=4)
        self.base_intensity = Range(
            self.options_frame, min_bound=0, max_bound=100,
            label="Base Shadow Intensity"
        )
        self.base_intensity.set_fixed()
        self.base_intensity.pack(fill=tk.X, pady=4)

        # Gradient Shadow
        self.gradient_shadow_var = tk.BooleanVar()
        ttk.Checkbutton(
            self.options_frame, text="Has Gradient Shadow",
            variable=self.gradient_shadow_var, style='Shading.TCheckbutton'
        ).pack(anchor='w', pady=4)
        self.gradient_intensity = Range(
            self.options_frame, min_bound=0, max_bound=100,
            label="Gradient Shadow Intensity"
        )
        self.gradient_intensity.set_fixed()
        self.gradient_intensity.pack(fill=tk.X, pady=4)

        # Casted Shadow
        self.cast_shadow_var = tk.BooleanVar()
        ttk.Checkbutton(
            self.options_frame, text="Has Casted Shadow",
            variable=self.cast_shadow_var, style='Shading.TCheckbutton'
        ).pack(anchor='w', pady=4)
        self.cast_intensity = Range(
            self.options_frame, min_bound=0, max_bound=100,
            label="Cast Shadow Intensity"
        )
        self.cast_intensity.set_fixed()
        self.cast_intensity.pack(fill=tk.X, pady=4)

        # Save button
        ttk.Button(self, text="Save", command=self.save).pack(pady=8)

    def _toggle_ui_state(self):
        state = 'normal' if self.has_shading_var.get() else 'disabled'
        for child in self.options_frame.winfo_children():
            try:
                child.configure(state=state)
            except:
                pass
            for grand in child.winfo_children():
                try:
                    grand.configure(state=state)
                except:
                    pass

    def load(self, path):
        self.asset_path = path
        if not os.path.isfile(path):
            return

        try:
            with open(path, 'r') as f:
                data = json.load(f)
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load JSON: {e}")
            return

        shading = data.get("shading_info", {})

        # Booleans: treat None/null as False
        self.has_shading_var.set(bool(shading.get("has_shading")))
        self.base_shadow_var.set(bool(shading.get("has_base_shadow")))

        # Numeric: treat None/null as 0
        base_val = shading.get("base_shadow_intensity") or 0
        self.base_intensity.set(base_val, base_val)

        self.gradient_shadow_var.set(bool(shading.get("has_gradient_shadow")))
        grad_val = shading.get("gradient_shadow_intensity") or 0
        self.gradient_intensity.set(grad_val, grad_val)

        self.cast_shadow_var.set(bool(shading.get("has_casted_shadows")))
        cast_val = shading.get("cast_shadow_intensity") or 0
        self.cast_intensity.set(cast_val, cast_val)

        self._toggle_ui_state()

    def save(self):
        if not self.asset_path:
            messagebox.showerror("Error", "No asset selected.")
            return

        try:
            with open(self.asset_path, 'r') as f:
                data = json.load(f)
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load JSON: {e}")
            return

        if self.has_shading_var.get():
            data["shading_info"] = {
                "has_shading": True,
                "has_base_shadow": self.base_shadow_var.get(),
                "base_shadow_intensity": self.base_intensity.get_min(),
                "has_gradient_shadow": self.gradient_shadow_var.get(),
                "gradient_shadow_intensity": self.gradient_intensity.get_min(),
                "has_casted_shadows": self.cast_shadow_var.get(),
                "cast_shadow_intensity": self.cast_intensity.get_min()
            }
        else:
            data["shading_info"] = {
                "has_shading": False,
                "has_base_shadow": False,
                "base_shadow_intensity": 0,
                "has_gradient_shadow": False,
                "gradient_shadow_intensity": 0,
                "has_casted_shadows": False,
                "cast_shadow_intensity": 0
            }

        try:
            with open(self.asset_path, 'w') as f:
                json.dump(data, f, indent=4)
            messagebox.showinfo("Saved", "Shading settings saved.")
        except Exception as e:
            messagebox.showerror("Save Error", str(e))


