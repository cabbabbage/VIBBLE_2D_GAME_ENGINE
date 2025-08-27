import tkinter as tk
from tkinter import ttk, messagebox
import os
import time


class EditRawJsonPage(ttk.Frame):
    def __init__(self, parent):
        super().__init__(parent)

        self.json_path = None
        self._debounce_after_id = None
        self._last_mod_time = None
        self._last_user_input = 0

        self.text = tk.Text(self, wrap="none", undo=True, maxundo=1000, font=("Consolas", 11))
        self.text.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)

        self.scroll_y = ttk.Scrollbar(self, orient="vertical", command=self.text.yview)
        self.scroll_y.pack(side=tk.RIGHT, fill=tk.Y)
        self.text.configure(yscrollcommand=self.scroll_y.set)

        self.scroll_x = ttk.Scrollbar(self, orient="horizontal", command=self.text.xview)
        self.scroll_x.pack(side=tk.BOTTOM, fill=tk.X)
        self.text.configure(xscrollcommand=self.scroll_x.set)

        self.text.bind("<Key>", self._on_keypress)
        self.text.bind("<Control-z>", self._undo)
        self.text.bind("<Control-Z>", self._undo)
        self.text.bind("<Control-Shift-Z>", self._redo)
        self.text.bind("<Control-y>", self._redo)

        self._style_editor()
        self.after(1500, self._poll_external_change)

    def _style_editor(self):
        self.text.configure(
            bg="#1e1e1e",
            fg="#d4d4d4",
            insertbackground="#ffffff",
            selectbackground="#264f78",
            selectforeground="#ffffff"
        )

    def _undo(self, event=None):
        try:
            self.text.edit_undo()
        except tk.TclError:
            pass
        return "break"

    def _redo(self, event=None):
        try:
            self.text.edit_redo()
        except tk.TclError:
            pass
        return "break"

    def _on_keypress(self, event=None):
        # record last user input time and debounce save
        self._last_user_input = time.time()
        if self._debounce_after_id:
            self.after_cancel(self._debounce_after_id)
        self._debounce_after_id = self.after(800, self._auto_save)

    def _auto_save(self):
        """Write out the raw contents immediately, no JSON parsing required."""
        if not self.json_path:
            return
        raw = self.text.get("1.0", "end-1c")
        try:
            with open(self.json_path, "w", encoding="utf-8") as f:
                f.write(raw)
            self._last_mod_time = os.path.getmtime(self.json_path)
            print("[EditRawJsonPage] Autosaved")
        except Exception as e:
            print(f"[EditRawJsonPage] Save error: {e}")

    def _poll_external_change(self):
        # Only reload if the user hasn't typed in the last 3s
        if self.json_path:
            now = time.time()
            if (now - self._last_user_input) > 3 and os.path.isfile(self.json_path):
                try:
                    mtime = os.path.getmtime(self.json_path)
                    if self._last_mod_time and mtime != self._last_mod_time:
                        with open(self.json_path, "r", encoding="utf-8") as f:
                            contents = f.read()
                        self.text.delete("1.0", "end")
                        self.text.insert("1.0", contents)
                        self.text.edit_reset()
                        self._last_mod_time = mtime
                        print("[EditRawJsonPage] Reloaded external change.")
                except Exception as e:
                    print(f"[EditRawJsonPage] Poll error: {e}")
        self.after(1500, self._poll_external_change)

    def load(self, path):
        """Load the raw contents of info.json directly from disk."""
        self.json_path = path
        print(f"[EditRawJsonPage] Loading: {path}")
        if not os.path.isfile(path):
            self.text.delete("1.0", "end")
            self.text.insert("1.0", "{\n\n}")
            self._last_mod_time = None
            return
        try:
            with open(path, "r", encoding="utf-8") as f:
                contents = f.read()
            self.text.delete("1.0", "end")
            self.text.insert("1.0", contents)
            self._last_mod_time = os.path.getmtime(path)
        except Exception as e:
            messagebox.showerror("Error", f"Could not load JSON:\n{e}")
