#!/usr/bin/env python3
from __future__ import annotations
from typing import List, Callable, Tuple, Any, Optional

import tkinter as tk
from tkinter import ttk
from tkinter import colorchooser


class MovementModal(tk.Toplevel):
   """Edit per-frame movement with click-to-place positions, zoom & pan, and editable totals.

   Interaction:
     - Left click: set current frame position (frames 1..N-2 only).
     - Middle (or Right) drag: pan the view.
     - Mouse wheel: zoom at cursor.
     - Arrow keys ◀ ▶ switch current frame.
     - Frame 0 fixed at (0,0); last frame fixed at (ΔX,ΔY) (change via totals).
   """

   CANVAS_W = 420
   CANVAS_H = 300
   DOT_R    = 5

   SCALE_MIN = 2.0
   SCALE_MAX = 64.0
   ZOOM_STEP = 1.1

   def __init__(self, parent: tk.Widget, movement: List[List[Any]], frames_count: int,
               on_save: Callable[[List[List[Any]]], None], title: str = "Edit Movement"):
      super().__init__(parent)
      self.title(title)
      self.transient(parent)
      self.resizable(True, True)

      # data
      self._frames_count = max(1, int(frames_count))
      self._movement = self._coerce_movement(movement, self._frames_count)
      self._positions = self._movement_to_positions(self._movement)
      self._idx = 0
      self._on_save = on_save

      # viewport
      self._scale = 0.0
      self._center_x = 0.0
      self._center_y = 0.0

      # totals trace suppression (prevents clicks from being undone)
      self._suspend_totals_trace = False

      # panning state
      self._panning = False
      self._pan_last: Tuple[int, int] = (0, 0)

      # ui
      root = ttk.Frame(self)
      root.pack(fill="both", expand=True, padx=10, pady=10)

      # totals + frame nav
      top = ttk.Frame(root); top.pack(fill="x", pady=(0, 8))

      ttk.Label(top, text="Total ΔX:").pack(side="left")
      self._total_dx_var = tk.StringVar()
      self._total_dx_entry = ttk.Entry(top, textvariable=self._total_dx_var, width=8)
      self._total_dx_entry.pack(side="left", padx=(2, 12))

      ttk.Label(top, text="Total ΔY:").pack(side="left")
      self._total_dy_var = tk.StringVar()
      self._total_dy_entry = ttk.Entry(top, textvariable=self._total_dy_var, width=8)
      self._total_dy_entry.pack(side="left", padx=(2, 12))
      self._editing_totals_widgets = (self._total_dx_entry, self._total_dy_entry)

      nav = ttk.Frame(top); nav.pack(side="right")
      ttk.Button(nav, text="◀", width=3, command=self._prev_frame).pack(side="left", padx=4)
      self._frame_label = ttk.Label(nav, text=self._frame_label_text())
      self._frame_label.pack(side="left", padx=4)
      ttk.Button(nav, text="▶", width=3, command=self._next_frame).pack(side="left", padx=4)

      # editor
      # per-frame properties (resort_z + tint)
      props = ttk.Frame(root); props.pack(fill="x", pady=(0, 8))
      self._resort_var = tk.BooleanVar(value=True)
      ttk.Checkbutton(props, text="Resort Z", variable=self._resort_var,
                      command=self._on_resort_toggle).pack(side="left")

      ttk.Label(props, text="Tint:").pack(side="left", padx=(12, 4))
      # small color swatch to visualize current frame tint
      self._tint_swatch = tk.Label(props, width=4, relief="sunken", bg="#FFFFFF")
      self._tint_swatch.pack(side="left")
      ttk.Button(props, text="Pick…", command=self._on_pick_tint).pack(side="left", padx=6)

      editor = ttk.LabelFrame(root, text="Positions (absolute, relative to origin 0,0)")
      editor.pack(fill="both", expand=True)
      self._canvas = tk.Canvas(editor, bg="#1e1f24", highlightthickness=0,
                              width=self.CANVAS_W, height=self.CANVAS_H)
      self._canvas.pack(fill="both", expand=True, padx=6, pady=6)

      # buttons
      btns = ttk.Frame(root); btns.pack(fill="x", pady=(8, 0))
      ttk.Button(btns, text="Zero All", command=self._zero_all).pack(side="left")
      ttk.Button(btns, text="OK", command=self._on_ok).pack(side="right", padx=4)
      ttk.Button(btns, text="Cancel", command=self._on_cancel).pack(side="right")

      # events
      self._canvas.bind("<Configure>", self._on_canvas_resize)

      # left-click to set a point (only middle frames)
      self._canvas.bind("<Button-1>", self._on_canvas_click)

      # pan with middle or right mouse button
      self._canvas.bind("<Button-2>", self._on_pan_start)
      self._canvas.bind("<B2-Motion>", self._on_pan_move)
      self._canvas.bind("<ButtonRelease-2>", self._on_pan_end)

      self._canvas.bind("<Button-3>", self._on_pan_start)
      self._canvas.bind("<B3-Motion>", self._on_pan_move)
      self._canvas.bind("<ButtonRelease-3>", self._on_pan_end)

      # zoom
      self._canvas.bind("<MouseWheel>", self._on_mouse_wheel)     # Windows/macOS
      self._canvas.bind("<Button-4>", lambda e: self._on_wheel_steps(e, +1))  # X11
      self._canvas.bind("<Button-5>", lambda e: self._on_wheel_steps(e, -1))  # X11

      self.bind("<Left>",  lambda _e: self._prev_frame())
      self.bind("<Right>", lambda _e: self._next_frame())

      # totals fields change → recompute linear distribution
      self._totals_trace_ids = (
         self._total_dx_var.trace_add("write", lambda *_: self._on_totals_changed()),
         self._total_dy_var.trace_add("write", lambda *_: self._on_totals_changed()),
      )

      # commit totals on enter / leaving the field
      self._total_dx_entry.bind("<Return>",   lambda e: self._on_totals_changed())
      self._total_dy_entry.bind("<Return>",   lambda e: self._on_totals_changed())
      self._total_dx_entry.bind("<KP_Enter>", lambda e: self._on_totals_changed())
      self._total_dy_entry.bind("<KP_Enter>", lambda e: self._on_totals_changed())
      self._total_dx_entry.bind("<FocusOut>", lambda e: self._on_totals_changed())
      self._total_dy_entry.bind("<FocusOut>", lambda e: self._on_totals_changed())


      # finalize
      self._set_totals_from_positions()
      self.grab_set()
      self.protocol("WM_DELETE_WINDOW", self._on_cancel)
      self._canvas.focus_set()
      self._auto_fit_view()
      self._redraw()
      self._update_frame_props_ui()

   # ---------------- data coercion / conversion ----------------
   @staticmethod
   def _coerce_movement(movement: List[List[Any]], frames_count: int) -> List[List[Any]]:
      if not isinstance(movement, list):
         movement = []
      mv: List[List[Any]] = []
      for i, item in enumerate(movement[:frames_count]):
         try:
            dx, dy = int(item[0]), int(item[1])
         except Exception:
            dx, dy = 0, 0
         # extras: resort_z (bool) and tint (rgb list)
         resort_z = True
         tint = [255, 255, 255]
         try:
            if isinstance(item, (list, tuple)) and len(item) >= 3:
               resort_z = bool(item[2])
            if isinstance(item, (list, tuple)) and len(item) >= 4:
               tint = MovementModal._coerce_tint_value(item[3])
         except Exception:
            resort_z = True
            tint = [255, 255, 255]
         if i == 0:
            dx, dy = 0, 0
         mv.append([dx, dy, resort_z, tint])
      if len(mv) < frames_count:
         mv.extend([[0, 0, True, [255, 255, 255]] for _ in range(frames_count - len(mv))])
      if mv:
         # keep extras on first frame but force zero delta
         resort0 = mv[0][2] if len(mv[0]) >= 3 else True
         tint0 = mv[0][3] if len(mv[0]) >= 4 else [255, 255, 255]
         mv[0] = [0, 0, bool(resort0), MovementModal._coerce_tint_value(tint0)]
      return mv

   @staticmethod
   def _movement_to_positions(mv: List[List[Any]]) -> List[List[int]]:
      pos: List[List[int]] = []
      x, y = 0, 0
      for i, item in enumerate(mv):
         try:
            dx, dy = int(item[0]), int(item[1])
         except Exception:
            dx, dy = 0, 0
         if i == 0:
            x, y = 0, 0
         else:
            x += int(dx); y += int(dy)
         pos.append([x, y])
      return pos

   def _positions_to_movement(self, pos: List[List[int]]) -> List[List[Any]]:
      out: List[List[Any]] = []
      for i, (x, y) in enumerate(pos):
         if i == 0:
            dx, dy = 0, 0
         else:
            px, py = pos[i - 1]
            dx, dy = int(x - px), int(y - py)
         # preserve extras if present, else defaults
         resort_z = True
         tint = [255, 255, 255]
         if isinstance(self._movement, list) and i < len(self._movement):
            old = self._movement[i]
            if isinstance(old, (list, tuple)):
               if len(old) >= 3:
                  try:
                     resort_z = bool(old[2])
                  except Exception:
                     resort_z = True
               if len(old) >= 4:
                  try:
                     tint = MovementModal._coerce_tint_value(old[3])
                  except Exception:
                     tint = [255, 255, 255]
         out.append([dx, dy, resort_z, tint])
      # ensure frame 0 deltas zero
      if out:
         out[0][0] = 0; out[0][1] = 0
      return out

   # ---------------- extras helpers --------------------------
   @staticmethod
   def _coerce_tint_value(val: Any) -> List[int]:
      # Accept [r,g,b], (r,g,b), or hex string; default to white
      def clamp8(n: Any) -> int:
         try:
            n = int(round(float(n)))
         except Exception:
            n = 255
         return max(0, min(255, n))

      if isinstance(val, (list, tuple)) and len(val) >= 3:
         r, g, b = clamp8(val[0]), clamp8(val[1]), clamp8(val[2])
         return [r, g, b]
      if isinstance(val, str):
         s = val.strip()
         if s.startswith('#') and (len(s) == 7 or len(s) == 4):
            if len(s) == 4:
               # short form #rgb
               s = '#' + ''.join(ch*2 for ch in s[1:4])
            try:
               r = int(s[1:3], 16); g = int(s[3:5], 16); b = int(s[5:7], 16)
               return [r, g, b]
            except Exception:
               return [255, 255, 255]
      return [255, 255, 255]

   @staticmethod
   def _rgb_to_hex(rgb: List[int]) -> str:
      r = max(0, min(255, int(rgb[0] if len(rgb) > 0 else 255)))
      g = max(0, min(255, int(rgb[1] if len(rgb) > 1 else 255)))
      b = max(0, min(255, int(rgb[2] if len(rgb) > 2 else 255)))
      return f"#{r:02X}{g:02X}{b:02X}"

   @staticmethod
   def _parse_int_or_none(val: Any) -> Optional[int]:
      try:
         s = str(val).strip()
      except Exception:
         return None
      if s in ("", "-", "+"):
         return None
      try:
         return int(s)
      except Exception:
         return None

   # ---------------- frame nav / totals ------------------------
   def _frame_label_text(self) -> str:
      return f"Frame {self._idx} / {self._frames_count - 1}"

   def _set_frame(self, idx: int):
      self._idx = max(0, min(self._frames_count - 1, idx))
      self._frame_label.configure(text=self._frame_label_text())
      self._update_frame_props_ui()
      self._redraw()

   def _prev_frame(self):
      self._set_frame(self._idx - 1)

   def _next_frame(self):
      self._set_frame(self._idx + 1)

   def _set_totals_from_positions(self):
      # Avoid clobbering user input while typing in totals entries
      try:
         focus_widget = self.focus_get()
      except Exception:
         focus_widget = None
      if focus_widget in getattr(self, "_editing_totals_widgets", (None,)):
         return
      lastx, lasty = self._positions[-1] if self._positions else (0, 0)
      self._suspend_totals_trace = True
      try:
         self._total_dx_var.set(str(int(lastx)))
         self._total_dy_var.set(str(int(lasty)))
      finally:
         self._suspend_totals_trace = False

   def _on_totals_changed(self, *_):
      if self._suspend_totals_trace:
         return
      dx = self._parse_int_or_none(self._total_dx_var.get())
      dy = self._parse_int_or_none(self._total_dy_var.get())
      if dx is None or dy is None:
         return
      self._redistribute_positions_linear(dx, dy)
      self._movement = self._positions_to_movement(self._positions)
      self._update_frame_props_ui()
      self._auto_fit_view()
      self._redraw()


   def _redistribute_positions_linear(self, dx: int, dy: int):
      n = self._frames_count
      if n <= 1:
         self._positions = [[0, 0]]
         return
      self._positions = []
      for i in range(n):
         t = i / (n - 1)
         x = round(dx * t)
         y = round(dy * t)
         self._positions.append([x, y])
      self._positions[0]  = [0, 0]
      self._positions[-1] = [dx, dy]

   # ---------------- viewport / transforms ---------------------
   def _auto_fit_view(self):
      xs = [0] + [p[0] for p in self._positions]
      ys = [0] + [p[1] for p in self._positions]
      xmin, xmax = min(xs), max(xs)
      ymin, ymax = min(ys), max(ys)
      if xmin == xmax:
         xmin -= 10; xmax += 10
      if ymin == ymax:
         ymin -= 10; ymax += 10
      dx = (xmax - xmin) * 0.15
      dy = (ymax - ymin) * 0.15
      xmin -= dx; xmax += dx
      ymin -= dy; ymax += dy
      self._center_x = (xmin + xmax) / 2.0
      self._center_y = (ymin + ymax) / 2.0
      cw = max(1, self._canvas.winfo_width())
      ch = max(1, self._canvas.winfo_height())
      sx = cw / max(1e-6, (xmax - xmin))
      sy = ch / max(1e-6, (ymax - ymin))
      self._scale = max(self.SCALE_MIN, min(self.SCALE_MAX, min(sx, sy)))

   def _world_to_canvas(self, x: float, y: float) -> Tuple[int, int]:
      cw = max(1, self._canvas.winfo_width())
      ch = max(1, self._canvas.winfo_height())
      cx = int((x - self._center_x) * self._scale + cw / 2.0)
      cy = int((y - self._center_y) * self._scale + ch / 2.0)  # SDL: +y goes down
      return cx, cy

   def _canvas_to_world(self, cx: int, cy: int) -> Tuple[float, float]:
      cw = max(1, self._canvas.winfo_width())
      ch = max(1, self._canvas.winfo_height())
      x = (cx - cw / 2.0) / max(1e-6, self._scale) + self._center_x
      y = (cy - ch / 2.0) / max(1e-6, self._scale) + self._center_y  # SDL: +y goes down
      return x, y


   def _zoom_at(self, canvas_x: int, canvas_y: int, steps: int):
      if steps == 0:
         return
      wx, wy = self._canvas_to_world(canvas_x, canvas_y)
      for _ in range(abs(steps)):
         if steps > 0:
            self._scale *= self.ZOOM_STEP
         else:
            self._scale /= self.ZOOM_STEP
         self._scale = max(self.SCALE_MIN, min(self.SCALE_MAX, self._scale))
         cw = max(1, self._canvas.winfo_width())
         ch = max(1, self._canvas.winfo_height())
         self._center_x = wx - (canvas_x - cw / 2.0) / self._scale
         self._center_y = wy - (canvas_y - ch / 2.0) / self._scale  # SDL: +y down
         self._redraw()


   # ---------------- drawing & interaction --------------------
   def _on_canvas_resize(self, _evt=None):
      if self._scale <= 0.0:
         self._auto_fit_view()
      self._redraw()

   def _redraw(self, _evt=None):
      c = self._canvas
      c.delete("all")

      cw = max(1, c.winfo_width())
      ch = max(1, c.winfo_height())

      # axes
      ox, oy = self._world_to_canvas(0, 0)
      c.create_line(0, oy, cw, oy, fill="#3a3f46")
      c.create_line(ox, 0, ox, ch, fill="#3a3f46")
      c.create_oval(ox-2, oy-2, ox+2, oy+2, fill="#7f8fa6", outline="")

      # path and dots
      prev = (0, 0)
      for i, (x, y) in enumerate(self._positions):
         cx, cy = self._world_to_canvas(x, y)
         px, py = self._world_to_canvas(prev[0], prev[1])
         c.create_line(px, py, cx, cy, fill="#9aa0a6")
         prev = (x, y)
         color = "#e84118" if i == self._idx else "#44bd32"
         c.create_oval(cx - self.DOT_R, cy - self.DOT_R, cx + self.DOT_R, cy + self.DOT_R,
                       fill=color, outline="#111")

      # sync totals display with last point without retriggering trace
      self._set_totals_from_positions()

   def _on_canvas_click(self, evt):
      # only allow editing middle frames; frame 0 and last are locked
      if self._idx == 0 or self._idx == self._frames_count - 1:
         return
      x, y = self._canvas_to_world(evt.x, evt.y)
      self._positions[self._idx] = [int(round(x)), int(round(y))]
      self._movement = self._positions_to_movement(self._positions)
      self._update_frame_props_ui()
      self._redraw()

   # --- panning (middle or right mouse) ---
   def _on_pan_start(self, evt):
      self._panning = True
      self._pan_last = (evt.x, evt.y)

   def _on_pan_move(self, evt):
      if not self._panning:
         return
      lx, ly = self._pan_last
      dx_pix = evt.x - lx
      dy_pix = evt.y - ly
      self._pan_last = (evt.x, evt.y)
      # pixel delta -> world delta (SDL: +y down)
      self._center_x -= dx_pix / self._scale
      self._center_y -= dy_pix / self._scale
      self._redraw()


   def _on_pan_end(self, _evt):
      self._panning = False

   # --- zoom ---
   def _on_mouse_wheel(self, evt):
      steps = 1 if evt.delta > 0 else -1
      self._zoom_at(evt.x, evt.y, steps)

   def _on_wheel_steps(self, evt, steps: int):
      self._zoom_at(evt.x, evt.y, steps)

   # ---------------- bulk actions / save ----------------------
   def _zero_all(self):
      self._redistribute_positions_linear(0, 0)
      self._movement = self._positions_to_movement(self._positions)
      self._update_frame_props_ui()
      self._set_frame(0)

   def _on_ok(self):
      self._movement = self._positions_to_movement(self._positions)
      # force frame 0 delta zero but keep extras
      if self._movement:
         resort0 = True
         tint0 = [255, 255, 255]
         try:
            if len(self._movement[0]) >= 3:
               resort0 = bool(self._movement[0][2])
            if len(self._movement[0]) >= 4:
               tint0 = MovementModal._coerce_tint_value(self._movement[0][3])
         except Exception:
            resort0, tint0 = True, [255, 255, 255]
         self._movement[0] = [0, 0, resort0, tint0]
      self._on_save(self._movement)
      self.grab_release()
      self.destroy()

   def _on_cancel(self):
      self.grab_release()
      self.destroy()

   # ---------------- frame props UI handlers ------------------
   def _update_frame_props_ui(self):
      # guard for empty movement
      if not isinstance(self._movement, list) or not self._movement:
         self._resort_var.set(True)
         self._tint_swatch.configure(bg="#FFFFFF")
         return
      i = max(0, min(self._idx, len(self._movement) - 1))
      item = self._movement[i]
      resort_z = True
      tint = [255, 255, 255]
      if isinstance(item, (list, tuple)):
         if len(item) >= 3:
            try:
               resort_z = bool(item[2])
            except Exception:
               resort_z = True
         if len(item) >= 4:
            try:
               tint = MovementModal._coerce_tint_value(item[3])
            except Exception:
               tint = [255, 255, 255]
      self._resort_var.set(resort_z)
      self._tint_swatch.configure(bg=MovementModal._rgb_to_hex(tint))

   def _on_resort_toggle(self):
      if not isinstance(self._movement, list) or not self._movement:
         return
      i = max(0, min(self._idx, len(self._movement) - 1))
      item = list(self._movement[i]) if isinstance(self._movement[i], (list, tuple)) else [0, 0, True, [255, 255, 255]]
      # ensure length
      while len(item) < 4:
         if len(item) == 0:
            item = [0, 0, True, [255, 255, 255]]
            break
         elif len(item) == 1:
            item.append(0)
         elif len(item) == 2:
            item.extend([True, [255, 255, 255]])
         elif len(item) == 3:
            item.append([255, 255, 255])
      item[2] = bool(self._resort_var.get())
      self._movement[i] = item

   def _on_pick_tint(self):
      if not isinstance(self._movement, list) or not self._movement:
         return
      i = max(0, min(self._idx, len(self._movement) - 1))
      # initial color from current value
      current = [255, 255, 255]
      try:
         if isinstance(self._movement[i], (list, tuple)) and len(self._movement[i]) >= 4:
            current = MovementModal._coerce_tint_value(self._movement[i][3])
      except Exception:
         current = [255, 255, 255]
      initial_hex = MovementModal._rgb_to_hex(current)
      res = colorchooser.askcolor(color=initial_hex, parent=self)
      if not res or res[1] is None:
         return
      try:
         rgb = MovementModal._coerce_tint_value(res[0])
      except Exception:
         rgb = current
      # update data and UI
      item = list(self._movement[i]) if isinstance(self._movement[i], (list, tuple)) else [0, 0, True, [255, 255, 255]]
      while len(item) < 4:
         if len(item) == 0:
            item = [0, 0, True, [255, 255, 255]]
            break
         elif len(item) == 1:
            item.append(0)
         elif len(item) == 2:
            item.extend([True, [255, 255, 255]])
         elif len(item) == 3:
            item.append([255, 255, 255])
      item[3] = rgb
      self._movement[i] = item
      self._tint_swatch.configure(bg=MovementModal._rgb_to_hex(rgb))
