import tkinter as tk
from tkinter import ttk

class Range(ttk.Frame):
    def __init__(self, parent, min_bound=0, max_bound=100, set_min=None, set_max=None, force_fixed=False, label=None):
        super().__init__(parent)
        self.configure(style="Dark.TFrame")

        self.min_bound = min_bound
        self.max_bound = max_bound
        self.force_fixed = force_fixed
        self.label = label

        self.var_random = tk.BooleanVar()
        self.var_min = tk.IntVar()
        self.var_max = tk.IntVar()
        self.sliders = []
        self.check = None
        self.on_change = lambda *_: None  # external hook (e.g. autosave)

        style = ttk.Style(self)
        style.configure("Dark.TFrame", background="#2a2a2a")
        style.configure("Dark.TLabel", background="#2a2a2a", foreground="#FFFFFF", font=("Segoe UI", 12))
        style.configure("DarkHeader.TLabel", background="#2a2a2a", foreground="#DDDDDD", font=("Segoe UI", 13, "bold"))
        style.configure("Dark.TCheckbutton", background="#2a2a2a", foreground="#FFFFFF", font=("Segoe UI", 12))
        style.configure("Horizontal.TScale", background="#2a2a2a")

        if self.label:
            ttk.Label(self, text=self.label, style="DarkHeader.TLabel").pack(anchor="w", pady=(10, 4), padx=8)

        if not self.force_fixed:
            self.check = ttk.Checkbutton(
                self, text="Random Range", variable=self.var_random, command=self._update_mode,
                style="Dark.TCheckbutton"
            )
            self.check.pack(anchor="w", padx=8, pady=(0, 8))

        self.slider_frame = ttk.Frame(self, style="Dark.TFrame")
        self.slider_frame.pack(fill=tk.X, expand=True)

        if set_min is not None and set_max is not None:
            self.set(set_min, set_max)

    def _clear_sliders(self):
        for child in self.slider_frame.winfo_children():
            child.destroy()
        self.sliders = []

    def set_fixed(self):
        self.force_fixed = True

    def _draw_sliders(self):
        self._clear_sliders()
        if self.var_random.get():
            self._add_slider("Min:", self.var_min, self.min_bound, self.max_bound)
            self._add_slider("Max:", self.var_max, self.min_bound, self.max_bound)
        else:
            self._add_slider("Value:", self.var_max, self.min_bound, self.max_bound)

    def _add_slider(self, label_text, var, from_, to_):
        row = ttk.Frame(self.slider_frame, style="Dark.TFrame")
        row.pack(fill=tk.X, pady=4, padx=10)

        ttk.Label(row, text=label_text, style="Dark.TLabel").pack(side=tk.LEFT)

        value_label = ttk.Label(row, text=str(var.get()), width=5, style="Dark.TLabel")
        value_label.pack(side=tk.RIGHT, padx=(4, 0))

        def update(val):
            val_int = int(float(val))
            var.set(val_int)
            value_label.config(text=str(val_int))
            self.on_change()

        slider = ttk.Scale(row, from_=from_, to=to_, orient=tk.HORIZONTAL, command=update, style="Horizontal.TScale")
        slider.set(var.get())
        slider.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(8, 4))

        self.sliders.append(slider)

        def make_editable(event):
            value_label.pack_forget()
            entry = ttk.Entry(row, width=5, font=("Segoe UI", 12))
            entry.insert(0, str(var.get()))
            entry.pack(side=tk.RIGHT)
            entry.focus()

            def commit(event=None):
                try:
                    val = int(entry.get())
                    if from_ <= val <= to_:
                        var.set(val)
                        slider.set(val)
                        self.on_change()
                except ValueError:
                    pass
                entry.destroy()
                value_label.config(text=str(var.get()))
                value_label.pack(side=tk.RIGHT)

            entry.bind("<Return>", commit)
            entry.bind("<FocusOut>", commit)

        value_label.bind("<Double-Button-1>", make_editable)

        var.trace_add("write", lambda *args: self.on_change())

    def _update_mode(self):
        if self.force_fixed:
            return
        if self.var_random.get():
            self.var_min.set(self.var_max.get())
        else:
            self.var_max.set(self.var_max.get())
        self._draw_sliders()
        self.on_change()

    def get(self):
        if self.var_random.get():
            return int(self.var_min.get()), int(self.var_max.get())
        else:
            v = int(self.var_max.get())
            return v, v

    def get_min(self):
        return self.get()[0]

    def get_max(self):
        return self.get()[1]

    def set(self, min_val, max_val):
        min_val = max(self.min_bound, min(min_val, self.max_bound))
        max_val = max(self.min_bound, min(max_val, self.max_bound))

        self.var_min.set(min_val)
        self.var_max.set(max_val)

        self.var_random.set(min_val != max_val and not self.force_fixed)
        self._draw_sliders()

    def disable(self):
        if self.check:
            self.check.configure(state=tk.DISABLED)
        for slider in self.sliders:
            slider.configure(state=tk.DISABLED)

    def enable(self):
        if self.check:
            self.check.configure(state=tk.NORMAL)
        for slider in self.sliders:
            slider.configure(state=tk.NORMAL)

