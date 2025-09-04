#!/usr/bin/env python3
from __future__ import annotations
from typing import Optional, Tuple, Callable, Dict, Any

import tkinter as tk
from tkinter import ttk


class BaseNode:
    """
    Shared draggable node for graph editors.

    Features
      - header with collapse arrow (▾/▸) and title
      - content area (self.content) for subclass controls
      - left input port (GREEN) and right output port (RED)
      - precise, dynamic port positioning based on live canvas/window geometry
      - drag-to-move via header
      - callbacks:
          on_begin_connect(node_id)     when user clicks the output port bubble
          on_moved(node_id, x, y)       when node is dropped after drag
          on_changed(node_id, payload)  optional – subclasses can use
    """

    # appearance
    PORT_RADIUS = 8
    PORT_MARGIN = 6
    COLOR_INPUT = "#44bd32"   # green (left)
    COLOR_OUTPUT = "#e84118"  # red   (right)
    COLOR_PORT_OUTLINE = "#2f3640"

    def __init__(
        self,
        canvas: tk.Canvas,
        node_id: str,
        title: str,
        x: int = 60,
        y: int = 40,
    ):
        self.canvas = canvas
        self.node_id = node_id

        # external callbacks (set by host/graph)
        self.on_begin_connect: Optional[Callable[[str], None]] = None
        self.on_moved: Optional[Callable[[str, int, int], None]] = None
        self.on_changed: Optional[Callable[[str, Dict[str, Any]], None]] = None
        self.on_renamed: Optional[Callable[[str, str], None]] = None

        # --- widget structure ---
        self.frame = ttk.Frame(canvas, relief=tk.RIDGE, borderwidth=2)

        # header (drag handle)
        self.header_frame = ttk.Frame(self.frame)
        self.header_frame.pack(fill="x")

        # collapse state + toggle arrow
        self._collapsed = False
        self._toggle_btn = ttk.Label(self.header_frame, text="▾", width=2, anchor="center")
        self._toggle_btn.pack(side="left", padx=(2, 0))
        self._toggle_btn.bind("<Button-1>", self._on_toggle)

        self._title_var = tk.StringVar(value=title)
        self.title_label = ttk.Label(
            self.header_frame,
            textvariable=self._title_var,
            background="#1e272e",
            foreground="white",
            anchor="w",
        )
        self.title_label.pack(side="left", fill="x", expand=True)
        self.title_label.bind("<Double-Button-1>", self._start_rename)

        # content for subclass UI
        self.content = ttk.Frame(self.frame)
        self.content.pack(fill="both", expand=True)

        # embed in canvas
        self._win = canvas.create_window(x, y, window=self.frame, anchor="nw")

        # canvas ports
        self._port_in = None
        self._port_out = None

        # drag handling via header
        self._dragging = False
        self._drag_off: Tuple[int, int] = (0, 0)
        for target in (self.header_frame, self.title_label):
            target.bind("<Button-1>", self._on_drag_start)
            target.bind("<B1-Motion>", self._on_drag_move)
            target.bind("<ButtonRelease-1>", self._on_drag_end)

        # keep ports in sync with canvas/layout changes
        self.canvas.bind("<Configure>", lambda _e: self._update_size_and_ports())
        self.frame.bind("<Configure>", lambda _e: self._update_size_and_ports())

        # cached geom
        self.x = x
        self.y = y
        self.w = self.frame.winfo_reqwidth()
        self.h = self.frame.winfo_reqheight()

        # initial ports
        self._update_size_and_ports()

    # ---------- public API for subclasses ----------
    def get_content_frame(self) -> ttk.Frame:
        """Where subclasses should pack/place their controls."""
        return self.content

    def request_layout(self):
        """Call this after modifying content controls to ensure size + ports refresh."""
        self._update_size_and_ports()

    def set_title(self, text: str):
        self._title_var.set(text)
        self.request_layout()

    def set_collapsed(self, collapsed: bool):
        if collapsed != self._collapsed:
            self._toggle_body(force_state=collapsed)

    # centers for edge routing
    def input_port_center(self) -> Tuple[int, int]:
        x, y, w, h = self._current_win_geometry()
        return (x + self.PORT_MARGIN + self.PORT_RADIUS, y + h // 2)

    def output_port_center(self) -> Tuple[int, int]:
        x, y, w, h = self._current_win_geometry()
        return (x + w - (self.PORT_MARGIN + self.PORT_RADIUS), y + h // 2)

    # ---------- internal: geometry ----------
    def _current_win_geometry(self) -> Tuple[int, int, int, int]:
        self.frame.update_idletasks()
        coords = self.canvas.coords(self._win) or [self.x, self.y]
        x = int(coords[0])
        y = int(coords[1])
        w = int(self.frame.winfo_width())
        h = int(self.frame.winfo_height())
        # cache
        self.x, self.y, self.w, self.h = x, y, w, h
        return x, y, w, h

    def _update_size_and_ports(self):
        # recalc widget size and redraw ports
        self._draw_ports()

    # ---------- internal: ports ----------
    def _draw_ports(self):
        x, y, w, h = self._current_win_geometry()
        r = self.PORT_RADIUS
        m = self.PORT_MARGIN
        cy = y + h // 2

        # left input (GREEN)
        if self._port_in:
            self.canvas.delete(self._port_in)
        cx_in = x + m + r
        self._port_in = self.canvas.create_oval(
            cx_in - r, cy - r, cx_in + r, cy + r,
            fill=self.COLOR_INPUT, outline=self.COLOR_PORT_OUTLINE, width=2
        )

        # right output (RED)
        if self._port_out:
            self.canvas.delete(self._port_out)
        cx_out = x + w - (m + r)
        self._port_out = self.canvas.create_oval(
            cx_out - r, cy - r, cx_out + r, cy + r,
            fill=self.COLOR_OUTPUT, outline=self.COLOR_PORT_OUTLINE, width=2
        )
        # clicking output starts a connection
        self.canvas.tag_bind(self._port_out, "<Button-1>", self._on_begin_connect)

        # keep above other canvas items
        self.canvas.tag_raise(self._port_in)
        self.canvas.tag_raise(self._port_out)

    def _on_begin_connect(self, _evt=None):
        if self.on_begin_connect:
            self.on_begin_connect(self.node_id)

    # ---------- internal: rename ----------
    def _start_rename(self, _evt=None):
        if hasattr(self, "_title_entry"):
            return
        self._title_entry = ttk.Entry(self.header_frame, textvariable=self._title_var)
        self.title_label.forget()
        self._title_entry.pack(side="left", fill="x", expand=True)
        self._title_entry.focus_set()
        self._title_entry.select_range(0, tk.END)
        self._title_entry.bind("<Return>", self._commit_rename)
        self._title_entry.bind("<Escape>", self._cancel_rename)
        self._title_entry.bind("<FocusOut>", self._commit_rename)

    def _finish_rename(self, commit: bool):
        entry = getattr(self, "_title_entry", None)
        if not entry:
            return
        new_name = entry.get().strip()
        entry.destroy()
        delattr(self, "_title_entry")
        self.title_label.pack(side="left", fill="x", expand=True)
        if commit and new_name:
            old_id = self.node_id
            if new_name != old_id:
                self.node_id = new_name
                self._title_var.set(new_name)
                if self.on_renamed:
                    self.on_renamed(old_id, new_name)
        else:
            self._title_var.set(self.node_id)
        self.request_layout()

    def _commit_rename(self, _evt=None):
        self._finish_rename(True)

    def _cancel_rename(self, _evt=None):
        self._finish_rename(False)

    # ---------- internal: collapse ----------
    def _on_toggle(self, _evt=None):
        self._toggle_body(force_state=not self._collapsed)

    def _toggle_body(self, force_state: bool):
        self._collapsed = force_state
        self._toggle_btn.configure(text="▸" if self._collapsed else "▾")
        if self._collapsed:
            # hide content to save space
            self.content.forget()
        else:
            # show content
            self.content.pack(fill="both", expand=True)
        self._update_size_and_ports()
        # report move to force edge reflow in host
        if self.on_moved:
            x, y, _, _ = self._current_win_geometry()
            self.on_moved(self.node_id, x, y)

    # ---------- internal: drag ----------
    def _on_drag_start(self, evt):
        self._dragging = True
        self._drag_off = (evt.x, evt.y)

    def _on_drag_move(self, evt):
        if not self._dragging:
            return
        cx = self.canvas.canvasx(evt.x_root - self.canvas.winfo_rootx())
        cy = self.canvas.canvasy(evt.y_root - self.canvas.winfo_rooty())
        self.canvas.coords(self._win, int(cx - self._drag_off[0]), int(cy - self._drag_off[1]))
        self._draw_ports()

    def _on_drag_end(self, _evt):
        if not self._dragging:
            return
        self._dragging = False
        if self.on_moved:
            x, y, _, _ = self._current_win_geometry()
            self.on_moved(self.node_id, x, y)
