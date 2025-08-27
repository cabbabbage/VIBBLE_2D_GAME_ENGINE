# === File: pages/AnimationConfig.py ===
import os
import json
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from PIL import Image, ImageTk
import pandas as pd
from pages.animation_uploader import AnimationUploader
from pages.area_ui import AreaUI
from pages.range import Range

class AnimationEditor(ttk.Frame):
    def __init__(self, parent):
        super().__init__(parent)
        # Dark boundary style
        self.configure(style="Dark.TFrame")
        # Enlarge checkboxes
        style = ttk.Style(self)
        style.configure('Big.TCheckbutton', padding=(8, 8))

        self.asset_folder = None
        self.trigger_name = None
        self.requires_area = False
        self.top_level_flag = None
        self.lock_until_done_var = tk.BooleanVar(value=False)
        self.on_change = None

        self.frames_path = tk.StringVar()
        self.on_end_var = tk.StringVar(value="")
        self.randomize_start_var = tk.BooleanVar(value=False)
        self.audio_path = tk.StringVar()
        self.volume = tk.IntVar(value=50)
        self.event_data_var = tk.StringVar()

        self.area_ui = None
        self.preview_gif = None

        self.triggers_df = pd.read_csv("PYTHON ASSET MANAGER/triggers.csv")
        self.available_triggers = self.triggers_df["trigger_name"].tolist()

        self._build_ui()
        self._bind_autosave()

    def _build_ui(self):
        for i in range(3):
            self.columnconfigure(i, weight=1)

        ttk.Label(self, text="Trigger Type:", font=("Segoe UI", 12))
        ttk.Label(self, text="Trigger Type:", font=("Segoe UI", 12)).grid(row=1, column=0, sticky='w', padx=12, pady=6)
        self.trigger_var = tk.StringVar()
        trigger_menu = ttk.OptionMenu(
            self, self.trigger_var, None,
            *self.available_triggers, command=self._on_trigger_select
        )
        trigger_menu.grid(row=1, column=1, sticky='ew', padx=12, pady=6)

        ttk.Label(self, text="Frames Folder:", font=("Segoe UI", 12)).grid(
            row=2, column=0, sticky='w', padx=12, pady=6
        )
        ttk.Entry(
            self, textvariable=self.frames_path, font=("Segoe UI", 12)
        ).grid(row=2, column=1, sticky='ew', padx=12, pady=6)
        tk.Button(
            self, text="Add Frames", command=self._use_animation_uploader,
            bg="#007BFF", fg="white",
            font=("Segoe UI", 13, "bold"), width=12
        ).grid(row=2, column=2, sticky='w', padx=12, pady=6)

        ttk.Label(self, text="On End:", font=("Segoe UI", 12)).grid(
            row=3, column=0, sticky='w', padx=12, pady=6
        )
        self.on_end_menu_var = tk.StringVar(value="")
        self.on_end_menu = ttk.OptionMenu(self, self.on_end_menu_var, "")
        self.on_end_menu.grid(row=3, column=1, sticky='ew', padx=12, pady=6)

        ttk.Label(self, text="Event Data:", font=("Segoe UI", 12)).grid(
            row=4, column=0, sticky='w', padx=12, pady=6
        )
        ttk.Entry(
            self, textvariable=self.event_data_var, font=("Segoe UI", 12)
        ).grid(row=4, column=1, sticky='ew', padx=12, pady=6)

        # Use enlarged style for checkboxes
        ttk.Checkbutton(
            self, text="Lock Until Done", variable=self.lock_until_done_var,
            style='Big.TCheckbutton'
        ).grid(row=5, column=0, columnspan=2, sticky='w', padx=12, pady=4)
        ttk.Checkbutton(
            self, text="Randomize Start Frame", variable=self.randomize_start_var,
            style='Big.TCheckbutton'
        ).grid(row=6, column=0, columnspan=2, sticky='w', padx=12, pady=4)

        ttk.Button(
            self, text="Add Audio", command=self._select_audio
        ).grid(row=7, column=2, sticky='w', padx=12, pady=4)
        # Replace volume slider with Range widget
        self.volume_range = Range(
            self, min_bound=0, max_bound=100,
            set_min=self.volume.get(), set_max=self.volume.get(),
            force_fixed=True, label="Volume"
        )
        self.volume_range.grid(row=7, column=1, sticky='ew', padx=12, pady=4)
        self.volume_range.var_max.trace_add(
            "write",
            lambda *_: (self.volume.set(self.volume_range.get()[0]), self._trigger_autosave())
        )

        self.area_ui_row = 8
        self.preview_canvas = tk.Canvas(
            self, bg="black", width=320, height=240, highlightthickness=0
        )
        self.preview_canvas.grid(row=9, column=0, columnspan=3, padx=12, pady=16)

    def _bind_autosave(self):
        vars_to_watch = [
            self.frames_path, self.on_end_menu_var,
            self.event_data_var, self.randomize_start_var,
            self.audio_path, self.volume, self.lock_until_done_var
        ]
        for var in vars_to_watch:
            var.trace_add("write", lambda *_: self._trigger_autosave())

    def _trigger_autosave(self):
        if callable(self.on_change):
            self.on_change()

    def _on_trigger_select(self, trigger_name):
        self.trigger_name = trigger_name
        row = self.triggers_df[
            self.triggers_df["trigger_name"] == trigger_name
        ]
        self.requires_area = str(
            row["requires_area"].values[0]
        ).strip().lower() in ("1", "true") if not row.empty else False
        self.top_level_flag = (
            row["top_level_flag"].values[0]
        ) if not row.empty else None

        if self.requires_area:
            if self.area_ui:
                self.area_ui.grid_forget()
            area_path = os.path.join(
                self.asset_folder,
                f"{self.trigger_name}_area.json"
            ) if self.asset_folder else f"{self.trigger_name}_area.json"
            self.area_ui = AreaUI(
                self, area_path,
                label_text="Animation Area:",
                autosave_callback=self._trigger_autosave
            )
            self.area_ui.grid(
                row=self.area_ui_row,
                column=0, columnspan=3,
                sticky="ew", padx=12, pady=8
            )
            if self.asset_folder:
                folder_name = self.frames_path.get() or self.trigger_name
                frames_folder = os.path.join(
                    self.asset_folder, folder_name
                )
                os.makedirs(frames_folder, exist_ok=True)
            else:
                frames_folder = None
            self.area_ui.frames_source = frames_folder
            self.area_ui._load_area_json()
            self.area_ui._draw_preview()
        elif self.area_ui:
            self.area_ui.grid_forget()
            self.area_ui = None

        self._update_on_end_options()
        self._trigger_autosave()

    def _update_on_end_options(self):
        if not self.asset_folder:
            return
        options = []
        info_path = os.path.join(
            self.asset_folder, "info.json"
        )
        if os.path.isfile(info_path):
            with open(info_path, 'r') as f:
                try:
                    data = json.load(f)
                    keys = list(data.get("animations", {}).keys())
                    if self.trigger_name in keys:
                        keys.remove(self.trigger_name)
                        keys.insert(0, self.trigger_name)
                    for insert_key in ["terminate", "freeze_on_last", "reverse"]:
                        if insert_key not in keys:
                            keys.insert(1, insert_key)
                    options += keys
                except Exception:
                    pass

        menu = self.on_end_menu["menu"]
        menu.delete(0, "end")
        for opt in options:
            menu.add_command(
                label=opt,
                command=lambda v=opt: self.on_end_menu_var.set(v)
            )

    def _select_audio(self):
        file = filedialog.askopenfilename(
            filetypes=[("WAV files", "*.wav")]
        )
        if file:
            self.audio_path.set(file)

    def _use_animation_uploader(self):
        if not self.asset_folder or not self.trigger_name:
            messagebox.showerror(
                "Missing Info",
                "Asset folder or trigger type not set."
            )
            return
        uploader = AnimationUploader(
            self.asset_folder, self.trigger_name
        )
        result_folder = uploader.run()
        if result_folder:
            self.frames_path.set(self.trigger_name)
            if self.area_ui:
                self.area_ui.frames_source = result_folder
                self.area_ui._draw_preview()
            self._load_gif_preview()
            self._trigger_autosave()

    def _load_gif_preview(self):
        if not self.asset_folder or not self.trigger_name:
            return

        gif_path = os.path.join(
            self.asset_folder, self.trigger_name, "preview.gif"
        )
        if not os.path.isfile(gif_path):
            return

        gif = Image.open(gif_path)
        width = 320
        height = int((width / gif.width) * gif.height)
        gif = gif.resize((width, height), Image.LANCZOS)

        self.preview_gif = ImageTk.PhotoImage(gif)
        self.preview_canvas.delete("all")
        self.preview_canvas.create_image(
            0, 0, anchor='nw', image=self.preview_gif
        )

    def load(self, trigger_name: str, anim_data: dict, asset_folder: str):
        self.lock_until_done_var.set(
            anim_data.get("lock_until_done", False)
        )

        self.asset_folder = asset_folder
        self.trigger_var.set(trigger_name)
        self._on_trigger_select(trigger_name)

        self.frames_path.set(anim_data.get("frames_path", ""))
        self.on_end_menu_var.set(anim_data.get("on_end", ""))
        self.event_data_var.set(
            ", ".join(anim_data.get("event_data", []))
        )
        self.randomize_start_var.set(
            anim_data.get("randomize", False)
        )
        self.audio_path.set(
            anim_data.get("audio_path", "")
        )
        self.volume.set(anim_data.get("volume", 50))

        if self.area_ui:
            folder_name = self.frames_path.get() or self.trigger_name
            frames_folder = os.path.join(
                self.asset_folder, folder_name
            )
            os.makedirs(frames_folder, exist_ok=True)
            self.area_ui.frames_source = frames_folder
            self.area_ui._load_area_json()
            self.area_ui._draw_preview()

        self._load_gif_preview()

    def save(self):
        if not self.trigger_name or not self.frames_path.get():
            messagebox.showerror(
                "Missing Data",
                "Required fields are missing: trigger, or frames"
            )
            return None

        if self.requires_area:
            if not self.area_ui or not self.area_ui.area_data:
                messagebox.showerror(
                    "Missing Area",
                    f"Required area not defined for trigger '{self.trigger_name}'"
                )
                return None
            self.area_ui._save_json()

        return {
            "frames_path": self.frames_path.get(),
            "on_end": self.on_end_menu_var.get(),
            "event_data": [x.strip() for x in self.event_data_var.get().split(",") if x.strip()],
            "randomize": self.randomize_start_var.get(),
            "audio_path": self.audio_path.get(),
            "volume": self.volume.get(),
            "lock_until_done": self.lock_until_done_var.get(),
            **({"area_path": f"{self.trigger_name}_area.json"} if self.requires_area else {})
        }
