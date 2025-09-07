#!/usr/bin/env python3
from __future__ import annotations
from typing import Any, Dict, Optional, Callable, List

import tkinter as tk
from tkinter import ttk
from tkinter import filedialog, messagebox
import os
import shutil
import re
try:
    from PIL import Image, ImageSequence
except Exception:
    Image = None
    ImageSequence = None



class SourcesElementPanel:
    """
    A reusable sub-panel that edits an animation's source configuration.
    Fields:
      - kind: folder | animation
      - path: if kind == folder
      - name: if kind == animation

    Usage:
      p = SourcesElementPanel(parent, source_dict, on_changed=callback)
      frame = p.get_frame()
      normalized_source = p.read_values()
      p.set_values(source_dict)
    """

    def __init__(
        self,
        parent: tk.Widget,
        source: Dict[str, Any],
        *,
        on_changed: Optional[Callable[[], None]] = None,
        asset_folder: Optional[str] = None,
        get_current_name: Optional[Callable[[], str]] = None,
        list_animation_names: Optional[Callable[[], List[str]]] = None,
    ) -> None:
        self.parent = parent
        self._on_changed = on_changed
        self.asset_folder = asset_folder
        self._get_current_name = get_current_name or (lambda: "")
        self._list_animation_names = list_animation_names or (lambda: [])

        src = dict(source or {})
        kind = src.get("kind", "folder")
        path = src.get("path", "")  # relative folder name under asset folder
        name = src.get("name", None)  # referenced animation name

        self.frame = ttk.LabelFrame(parent, text="Source")
        self.frame.columnconfigure(1, weight=1)

        self.kind_var = tk.StringVar(value=kind)
        ttk.Radiobutton(self.frame, text="Folder", value="folder", variable=self.kind_var, command=self._on_kind_changed).grid(
            row=0, column=0, sticky="w", padx=4
        )
        ttk.Radiobutton(self.frame, text="Animation", value="animation", variable=self.kind_var, command=self._on_kind_changed).grid(
            row=0, column=1, sticky="w"
        )

        # Folder row
        self._row_folder = ttk.Frame(self.frame)
        self._row_folder.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(6, 2))
        self._row_folder.columnconfigure(1, weight=1)
        ttk.Label(self._row_folder, text="Selected folder:").grid(row=0, column=0, sticky="w", padx=4)
        self.folder_rel_var = tk.StringVar(value=str(path or ""))
        ttk.Label(self._row_folder, textvariable=self.folder_rel_var).grid(row=0, column=1, sticky="w", padx=6)
        ttk.Button(self._row_folder, text="Select New Frames", command=self._select_new_frames).grid(row=0, column=2, sticky="e", padx=6)

        # Animation row
        self._row_anim = ttk.Frame(self.frame)
        self._row_anim.grid(row=2, column=0, columnspan=2, sticky="ew", pady=(6, 2))
        ttk.Label(self._row_anim, text="Animation:").grid(row=0, column=0, sticky="w", padx=4)
        self.name_var = tk.StringVar(value=("" if name is None else str(name)))
        self.anim_combo = ttk.Combobox(self._row_anim, textvariable=self.name_var, state="readonly", width=28)
        self.anim_combo.grid(row=0, column=1, sticky="w", padx=6)
        self.anim_combo.bind("<<ComboboxSelected>>", lambda _e: self._notify())
        self._refresh_anim_list()

        self._apply_kind_visibility()

    def get_frame(self) -> ttk.Frame:
        return self.frame

    def set_values(self, source: Dict[str, Any]) -> None:
        src = dict(source or {})
        self.kind_var.set(src.get("kind", "folder"))
        self.folder_rel_var.set(str(src.get("path", "") or ""))
        name = src.get("name", None)
        self.name_var.set("" if name is None else str(name))
        self._refresh_anim_list()
        self._apply_kind_visibility()

    def read_values(self) -> Dict[str, Any]:
        kind = "animation" if self.kind_var.get() == "animation" else "folder"
        if kind == "folder":
            return {"kind": kind, "path": self.folder_rel_var.get().strip(), "name": None}
        else:
            nm = self.name_var.get().strip()
            return {"kind": kind, "path": "", "name": (nm or None)}

    def _notify(self):
        if self._on_changed:
            try:
                self._on_changed()
            except Exception:
                pass

    # ----- internal helpers -----
    def _on_kind_changed(self):
        self._apply_kind_visibility()
        self._notify()

    def _apply_kind_visibility(self):
        kind = self.kind_var.get()
        try:
            if kind == "animation":
                self._row_folder.grid_remove()
                self._row_anim.grid()
            else:
                self._row_anim.grid_remove()
                self._row_folder.grid()
        except Exception:
            pass

    def _refresh_anim_list(self):
        try:
            names = sorted(self._list_animation_names())
        except Exception:
            names = []
        try:
            self.anim_combo["values"] = names
        except Exception:
            pass

    # ----- frames import flow -----
    def _select_new_frames(self):
        if not self.asset_folder:
            messagebox.showerror("No Asset Folder", "Asset folder not configured.")
            return
        # pop a simple chooser dialog (buttons like the existing uploader)
        dlg = tk.Toplevel(self.frame)
        dlg.title("Select New Frames")
        dlg.transient(self.frame.winfo_toplevel())
        ttk.Label(dlg, text="Import from:").pack(padx=12, pady=(10, 6))
        ttk.Button(dlg, text="Folder of PNGs", command=lambda: (dlg.destroy(), self._import_from_folder())).pack(padx=12, pady=4)
        ttk.Button(dlg, text="GIF File", command=lambda: (dlg.destroy(), self._import_from_gif())).pack(padx=12, pady=4)
        ttk.Button(dlg, text="Single PNG", command=lambda: (dlg.destroy(), self._import_from_png())).pack(padx=12, pady=4)
        ttk.Button(dlg, text="Cancel", command=dlg.destroy).pack(padx=12, pady=(8, 12))
        try:
            dlg.grab_set()
            dlg.wait_window()
        except Exception:
            pass

    def _ensure_output_dir(self) -> Optional[str]:
        name = self._get_current_name() or ""
        if not name:
            messagebox.showerror("Missing Name", "Set an ID/name before importing frames.")
            return None
        out_dir = os.path.join(self.asset_folder, name)
        os.makedirs(out_dir, exist_ok=True)
        # clear existing .png files
        try:
            for f in os.listdir(out_dir):
                if f.lower().endswith(".png"):
                    try:
                        os.remove(os.path.join(out_dir, f))
                    except Exception:
                        pass
        except Exception:
            pass
        return out_dir

    def _finish_import(self, out_dir: str):
        # set folder relative name and notify
        rel = os.path.basename(out_dir)
        self.folder_rel_var.set(rel)
        self.kind_var.set("folder")

        # optionally write preview.gif if PIL present
        if Image is not None:
            try:
                files = [f for f in sorted(os.listdir(out_dir)) if f.lower().endswith('.png')]
                if files:
                    frames = []
                    for f in files:
                        p = os.path.join(out_dir, f)
                        im = Image.open(p).convert('RGBA')
                        rgb = Image.new('RGB', im.size, (0,0,0))
                        rgb.paste(im, mask=im.split()[3])
                        frames.append(rgb.convert('P'))
                    if frames:
                        frames[0].save(os.path.join(out_dir, 'preview.gif'), save_all=True, append_images=frames[1:], loop=0, duration=1000//24, disposal=2)
            except Exception:
                pass
        self._apply_kind_visibility()
        self._notify()

    def _import_from_folder(self):
        folder = filedialog.askdirectory()
        if not folder:
            return
        out_dir = self._ensure_output_dir()
        if not out_dir:
            return
        # Collect and sort files by their original numeric order if present.
        # This preserves the OG numbering (e.g., 0.png, 1.png, 10.png, 11.png, 2.png -> 0,1,2,10,11).
        def _num_key(name: str):
            stem = os.path.splitext(name)[0]
            # Prefer full-stem integer when possible
            try:
                return (0, int(stem), stem.lower())
            except Exception:
                # Fallback: extract first integer substring
                m = re.search(r"\d+", stem)
                if m:
                    try:
                        return (0, int(m.group(0)), stem.lower())
                    except Exception:
                        pass
                return (1, stem.lower())
        try:
            entries = [f for f in os.listdir(folder) if f.lower().endswith('.png')]
            png_files = sorted(entries, key=_num_key)
        except Exception:
            png_files = [f for f in sorted(os.listdir(folder)) if f.lower().endswith('.png')]
        if not png_files:
            messagebox.showerror("No Images", "No PNG images found in selected folder.")
            return
        for i, fname in enumerate(png_files):
            src = os.path.join(folder, fname)
            dst = os.path.join(out_dir, f"{i}.png")
            try:
                shutil.copy2(src, dst)
            except Exception:
                pass
        self._finish_import(out_dir)

    def _import_from_gif(self):
        if Image is None:
            messagebox.showerror("PIL Missing", "Pillow is required to import GIF files.")
            return
        file = filedialog.askopenfilename(filetypes=[("GIF files", "*.gif")])
        if not file:
            return
        out_dir = self._ensure_output_dir()
        if not out_dir:
            return
        try:
            gif = Image.open(file)
        except Exception as e:
            messagebox.showerror("GIF Error", f"Failed to load GIF: {e}")
            return
        for i, frame in enumerate(ImageSequence.Iterator(gif)):
            try:
                frame = frame.convert('RGBA')
                frame.save(os.path.join(out_dir, f"{i}.png"))
            except Exception:
                pass
        self._finish_import(out_dir)

    def _import_from_png(self):
        if Image is None:
            messagebox.showerror("PIL Missing", "Pillow is required to import PNG files.")
            return
        file = filedialog.askopenfilename(filetypes=[("PNG files", "*.png")])
        if not file:
            return
        out_dir = self._ensure_output_dir()
        if not out_dir:
            return
        try:
            img = Image.open(file).convert('RGBA')
            img.save(os.path.join(out_dir, '0.png'))
        except Exception:
            pass
        self._finish_import(out_dir)
