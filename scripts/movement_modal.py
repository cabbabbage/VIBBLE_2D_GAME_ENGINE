#!/usr/bin/env python3
from __future__ import annotations
from typing import List, Callable

import tkinter as tk
from tkinter import ttk


class MovementModal(tk.Toplevel):
    """Modal dialog to edit per-frame movement vectors."""

    def __init__(self, parent: tk.Widget, movement: List[List[int]], frames_count: int,
                 on_save: Callable[[List[List[int]]], None], title: str = "Edit Movement"):
        super().__init__(parent)
        self.title(title)
        self.transient(parent)
        self.resizable(True, True)

        self._frames_count = max(1, int(frames_count))
        self._movement = self._coerce(movement, self._frames_count)
        self._on_save = on_save

        frm = ttk.Frame(self); frm.pack(fill="both", expand=True, padx=8, pady=8)
        top = ttk.LabelFrame(frm, text="Movement [dx, dy] (first frame forced to 0,0)")
        top.pack(fill="both", expand=True)

        self.mv_list = tk.Listbox(top, height=10); self.mv_list.pack(side="left", fill="both", expand=True, padx=4, pady=4)

        side = ttk.Frame(top); side.pack(side="right", fill="y", padx=4, pady=4)
        ttk.Button(side, text="Sync to Frames", command=self._sync_len).pack(fill="x", pady=2)
        ttk.Separator(side, orient="horizontal").pack(fill="x", pady=4)
        self.dx_var = tk.IntVar(value=0); self.dy_var = tk.IntVar(value=0)
        ttk.Label(side, text="dx").pack(anchor="w"); ttk.Entry(side, textvariable=self.dx_var, width=8).pack(anchor="w")
        ttk.Label(side, text="dy").pack(anchor="w"); ttk.Entry(side, textvariable=self.dy_var, width=8).pack(anchor="w")
        ttk.Button(side, text="Apply to Selected", command=self._apply_mv).pack(fill="x", pady=4)
        ttk.Button(side, text="Zero All", command=self._zero_all_mv).pack(fill="x", pady=2)

        btns = ttk.Frame(frm); btns.pack(fill="x", pady=(8, 0))
        ttk.Button(btns, text="OK", command=self._on_ok).pack(side="right", padx=4)
        ttk.Button(btns, text="Cancel", command=self._on_cancel).pack(side="right")

        self._refresh_mv()
        self.grab_set()
        self.protocol("WM_DELETE_WINDOW", self._on_cancel)
        self.mv_list.focus_set()

    @staticmethod
    def _coerce(movement: List[List[int]], frames_count: int) -> List[List[int]]:
        if not isinstance(movement, list):
            movement = []
        mv = [[int(dx), int(dy)] for dx, dy in movement] if movement else []
        if len(mv) < frames_count:
            mv.extend([[0, 0] for _ in range(frames_count - len(mv))])
        elif len(mv) > frames_count:
            mv = mv[:frames_count]
        mv[0] = [0, 0]
        return mv

    def _refresh_mv(self):
        self.mv_list.delete(0, tk.END)
        for i, (dx, dy) in enumerate(self._movement):
            self.mv_list.insert(tk.END, f"{i:02d}: [{dx}, {dy}]")

    def _sync_len(self):
        self._movement = self._coerce(self._movement, self._frames_count)
        self._refresh_mv()

    def _apply_mv(self):
        sel = self.mv_list.curselection()
        if not sel:
            return
        idx = sel[0]
        if 0 <= idx < len(self._movement):
            self._movement[idx] = [int(self.dx_var.get()), int(self.dy_var.get())]
            if idx == 0:
                self._movement[0] = [0, 0]
            self._refresh_mv()

    def _zero_all_mv(self):
        self._movement = [[0, 0] for _ in range(self._frames_count)]
        self._movement[0] = [0, 0]
        self._refresh_mv()

    def _on_ok(self):
        self._sync_len()
        self._on_save(self._movement)
        self.grab_release()
        self.destroy()

    def _on_cancel(self):
        self.grab_release()
        self.destroy()
