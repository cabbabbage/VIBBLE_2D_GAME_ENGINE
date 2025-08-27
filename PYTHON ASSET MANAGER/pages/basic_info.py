import os
import json
import shutil
import tkinter as tk
from tkinter import ttk, messagebox, simpledialog
from PIL import Image, ImageTk
from pages.range import Range

ASSET_DIR = "SRC"
HARD_CODED_TYPES = ["Player", "Object", "boundary", "Texture", "Enemy", "Map Boundary", "MAP"]

class BasicInfoPage(ttk.Frame):
    def __init__(self, parent, on_rename_callback=None):
        super().__init__(parent)
        self.asset_path = None
        self.on_rename = on_rename_callback
        self._loaded = False

        self.name_var = tk.StringVar()
        self.type_var = tk.StringVar()
        self.can_invert_var = tk.BooleanVar()

        self.name_var.trace_add("write", self._auto_save)
        self.type_var.trace_add("write", self._auto_save)
        self.can_invert_var.trace_add("write", self._auto_save)

        container = ttk.Frame(self)
        container.pack(fill="both", expand=True)

        canvas = tk.Canvas(container, bg="#1e1e1e")
        scrollbar = ttk.Scrollbar(container, orient="vertical", command=canvas.yview)
        canvas.configure(yscrollcommand=scrollbar.set)

        scrollable_frame = tk.Frame(canvas, bg="#1e1e1e")
        scrollable_frame.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")
        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        scrollable_frame.bind("<Enter>", lambda e: canvas.bind_all("<MouseWheel>", lambda event: canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")))
        scrollable_frame.bind("<Leave>", lambda e: canvas.unbind_all("<MouseWheel>"))

        ttk.Style().configure("Dark.TFrame", background="#1e1e1e")
        ttk.Style().configure("Dark.TLabel", background="#1e1e1e", foreground="#FFFFFF", font=("Segoe UI", 12))
        ttk.Style().configure("Dark.TButton", font=("Segoe UI", 13, "bold"))
        ttk.Style().configure("DarkHeader.TLabel", background="#1e1e1e", foreground="#DDDDDD", font=("Segoe UI", 20, "bold"))

        # Title
        ttk.Label(scrollable_frame, text="Basic Info", style="DarkHeader.TLabel")\
            .pack(anchor="w", pady=(10, 20), padx=12)

        # Asset Name
        ttk.Label(scrollable_frame, text="Asset Name:", style="Dark.TLabel")\
            .pack(anchor="w", padx=12, pady=(4, 4))
        self.name_display = ttk.Label(scrollable_frame, textvariable=self.name_var, style="Dark.TLabel")
        self.name_display.pack(anchor="w", padx=12, pady=(0, 8))

        # Asset Type
        ttk.Label(scrollable_frame, text="Asset Type:", style="Dark.TLabel")\
            .pack(anchor="w", padx=12, pady=(10, 4))
        self.type_menu = ttk.OptionMenu(scrollable_frame, self.type_var, HARD_CODED_TYPES[0], *HARD_CODED_TYPES)
        self.type_menu.config(width=30)
        self.type_menu.pack(anchor="w", padx=12, pady=(0, 8))

        # Update Radius
        ttk.Label(scrollable_frame, text="Update Radius:", style="Dark.TLabel")\
            .pack(anchor="w", padx=12, pady=(10, 0))
        self.update_radius_range = Range(scrollable_frame, label=None, min_bound=0, max_bound=10000)
        self.update_radius_range.set_fixed()
        self.update_radius_range.var_max.trace_add("write", lambda *_: self._auto_save())
        self.update_radius_range.pack(fill="x", padx=12, pady=(0, 8))

        # Render Radius
        ttk.Label(scrollable_frame, text="Render Radius:", style="Dark.TLabel")\
            .pack(anchor="w", padx=12, pady=(0, 0))
        self.render_radius_range = Range(scrollable_frame, label=None, min_bound=0, max_bound=10000)
        self.render_radius_range.set_fixed()
        self.render_radius_range.var_max.trace_add("write", lambda *_: self._auto_save())
        self.render_radius_range.pack(fill="x", padx=12, pady=(0, 10))

        ttk.Label(scrollable_frame, text="Can Invert:", style="Dark.TLabel")\
            .pack(anchor="w", padx=12, pady=(10, 0))
        self.can_invert_check = tk.Checkbutton(
            scrollable_frame,
            text="Enable asset flipping",
            variable=self.can_invert_var,
            bg="#1e1e1e", fg="#FFFFFF", selectcolor="#1e1e1e",
            font=("Segoe UI", 12), activebackground="#1e1e1e", activeforeground="#FFFFFF"
        )
        self.can_invert_check.pack(anchor="w", padx=18, pady=(0, 8))


        # Preview Image
        self.preview_label = ttk.Label(scrollable_frame, background="#1e1e1e")
        self.preview_label.pack(anchor="center", pady=(10, 20))

        # Actions
        btn_frame = ttk.Frame(scrollable_frame, style="Dark.TFrame")
        btn_frame.pack(pady=(10, 0))

        tk.Button(btn_frame, text="Delete Asset", command=self._delete_asset,
                  bg="#D9534F", fg="white", font=("Segoe UI", 13, "bold"), width=15).pack(side=tk.LEFT, padx=(10, 5))
        tk.Button(btn_frame, text="Create Duplicate", command=self._duplicate_asset,
                  bg="#28a745", fg="white", font=("Segoe UI", 13, "bold"), width=18).pack(side=tk.LEFT, padx=(5, 10))

    def _auto_save(self, *args):
        if self._loaded:
            self.save()

    def load(self, info_path):
        self.asset_path = info_path
        if not info_path or not os.path.exists(info_path):
            return

        try:
            with open(info_path, "r") as f:
                data = json.load(f)
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load JSON:\n{e}")
            return

        self._loaded = False
        self.name_var.set(data.get("asset_name", ""))
        self.type_var.set(data.get("asset_type", HARD_CODED_TYPES[0]))
        self.can_invert_var.set(data.get("can_invert", False))

        self.update_radius_range.set(data.get("update_radius", 1000), data.get("update_radius", 1000))
        self.render_radius_range.set(data.get("render_radius", 1000), data.get("render_radius", 1000))

        preview_path = os.path.join(os.path.dirname(info_path), "default", "0.png")
        if os.path.exists(preview_path):
            try:
                img = Image.open(preview_path)
                img.thumbnail((200, 200))
                self._preview_photo = ImageTk.PhotoImage(img)
                self.preview_label.config(image=self._preview_photo, text="")
            except:
                self.preview_label.config(text="Preview failed", image="")
        else:
            self.preview_label.config(text="No preview available", image="")

        self._loaded = True

    def save(self):
        if not self.asset_path:
            return

        try:
            with open(self.asset_path, "r") as f:
                data = json.load(f)
        except Exception as e:
            print(f"Failed to read for save: {e}")
            return

        data["asset_name"] = self.name_var.get().strip()
        data["asset_type"] = self.type_var.get()
        data["can_invert"] = self.can_invert_var.get()
        data["update_radius"] = self.update_radius_range.get_max()
        data["render_radius"] = self.render_radius_range.get_max()

        try:
            with open(self.asset_path, "w") as f:
                json.dump(data, f, indent=4)
        except Exception as e:
            print(f"Failed to write asset data: {e}")

    def _delete_asset(self):
        if not self.asset_path:
            return
        asset_folder = os.path.dirname(self.asset_path)
        name = os.path.basename(asset_folder)
        if not messagebox.askyesno("Confirm Delete", f"Delete '{name}'?"):
            return
        try:
            shutil.rmtree(asset_folder)
            root = self.winfo_toplevel()
            if hasattr(root, '_refresh_asset_list'):
                root._refresh_asset_list()
            if hasattr(root, 'asset_buttons'):
                for btn in root.asset_buttons.values():
                    btn.configure(bg="#f0f0f0")
        except Exception as e:
            messagebox.showerror("Error", f"Could not delete asset:\n{e}")

    def _duplicate_asset(self):
        if not self.asset_path:
            return
        src_folder = os.path.dirname(self.asset_path)
        base = os.path.basename(src_folder)
        new_name = simpledialog.askstring("Duplicate Asset", f"Name for copy of '{base}':", initialvalue=f"{base}_copy")
        if not new_name or not new_name.strip():
            return

        dst = os.path.join(os.path.dirname(src_folder), new_name.strip())
        if os.path.exists(dst):
            messagebox.showerror("Error", f"'{new_name}' already exists.")
            return

        try:
            shutil.copytree(src_folder, dst)
            info = os.path.join(dst, "info.json")
            if os.path.exists(info):
                with open(info, "r+") as f:
                    data = json.load(f)
                    data["asset_name"] = new_name.strip()
                    f.seek(0); f.truncate()
                    json.dump(data, f, indent=4)
            root = self.winfo_toplevel()
            if hasattr(root, '_refresh_asset_list'):
                root._refresh_asset_list()
            if hasattr(root, '_select_asset_by_name'):
                root._select_asset_by_name(new_name.strip())
        except Exception as e:
            messagebox.showerror("Error", f"Duplicate failed:{e}")
