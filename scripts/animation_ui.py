#!/usr/bin/env python3
from __future__ import annotations
import os, sys, json, threading, subprocess, shutil, re, copy
from pathlib import Path
from typing import Any, Dict, List, Optional, Callable, Tuple, Iterable, Set
import tkinter as tk
from tkinter import ttk, messagebox, filedialog, colorchooser
try:
    from PIL import Image, ImageTk, ImageSequence
except Exception:
    Image = ImageTk = ImageSequence = None
try:
    import simpleaudio as sa  # type: ignore
except Exception:  # pragma: no cover - optional dependency
    sa = None

# ---------- file helpers ----------
def is_numbered_png(filename: str) -> bool:
    return filename.lower().endswith(".png") and filename[:-4].isdigit()

def get_image_paths(folder: str) -> List[str]:
    files = os.listdir(folder)
    numbered_pngs = [f for f in files if is_numbered_png(f)]
    return sorted(
        [os.path.join(folder, f) for f in numbered_pngs],
        key=lambda x: int(os.path.basename(x)[:-4]),
    )

# ---------- bounds & crop ----------
def compute_union_bounds(image_paths: Iterable[str], alpha_threshold: int = 0) -> Tuple[int, int, int, int, int, int]:
    """
    Returns (top, bottom, left, right, base_w, base_h) as margins,
    computed from the union bbox across all given images.
    If no non-transparent pixels are found, returns zeros.
    """
    if Image is None:
        return (0, 0, 0, 0, 0, 0)

    union_bbox = None
    base_w = base_h = 0

    for path in image_paths:
        with Image.open(path).convert("RGBA") as img:
            if base_w == 0:
                base_w, base_h = img.size
            a = img.split()[3]
            # treat alpha <= threshold as transparent
            if alpha_threshold > 0:
                mask = a.point(lambda v: 255 if v > alpha_threshold else 0, mode="L")
            else:
                mask = a.point(lambda v: 255 if v != 0 else 0, mode="L")

            bbox = mask.getbbox()  # (L, T, R, B) or None
            if bbox is None:
                continue
            union_bbox = bbox if union_bbox is None else (
                min(union_bbox[0], bbox[0]),
                min(union_bbox[1], bbox[1]),
                max(union_bbox[2], bbox[2]),
                max(union_bbox[3], bbox[3]),
            )

    if union_bbox is None or base_w == 0:
        return (0, 0, 0, 0, 0, 0)

    L, T, R, B = union_bbox
    crop_left   = L
    crop_top    = T
    crop_right  = max(0, base_w - R)
    crop_bottom = max(0, base_h - B)
    return (crop_top, crop_bottom, crop_left, crop_right, base_w, base_h)

def crop_images_with_bounds(
    image_paths: Iterable[str],
    crop_top: int,
    crop_bottom: int,
    crop_left: int,
    crop_right: int,
) -> int:
    """Crops each image in-place using the fixed margins. Returns count cropped."""
    if Image is None:
        return 0

    count = 0
    for path in image_paths:
        with Image.open(path).convert("RGBA") as img:
            w, h = img.size
            L = crop_left
            T = crop_top
            R = w - crop_right
            B = h - crop_bottom
            if L >= R or T >= B:
                continue
            img.crop((L, T, R, B)).save(path, format="PNG", optimize=True)
            count += 1
    return count
class HistoryManager:
    """Keeps a stack of deep-copied snapshots for undo support."""

    def __init__(self, limit: int = 200):
        self._stack: list[Dict[str, Any]] = []
        self._limit = max(1, int(limit))

    def snapshot(self, data: Dict[str, Any]) -> None:
        try:
            snap = copy.deepcopy(data)
        except Exception:
            # fall back to shallow copy if deepcopy fails
            snap = dict(data or {})
        self._stack.append(snap)
        if len(self._stack) > self._limit:
            # drop oldest
            self._stack = self._stack[-self._limit :]

    def can_undo(self) -> bool:
        return len(self._stack) > 0

    def undo(self) -> Optional[Dict[str, Any]]:
        if not self._stack:
            return None
        try:
            last = self._stack.pop()
        except Exception:
            return None
        return last

class ViewStateManager:
    """Capture and restore window/canvas view state (geometry, zoom, pan)."""

    def capture(self, win, canvas) -> Dict[str, Any]:
        try:
            geom = win.geometry()
        except Exception:
            geom = None
        try:
            zoom = float(getattr(canvas, "_zoom", 1.0))
        except Exception:
            zoom = 1.0
        try:
            xv = float(canvas.xview()[0])
            yv = float(canvas.yview()[0])
        except Exception:
            xv = 0.0
            yv = 0.0
        return {
            "geometry": geom,
            "zoom": zoom,
            "xview": xv,
            "yview": yv,
        }

    def apply(self, win, canvas, state: Dict[str, Any]) -> None:
        if not isinstance(state, dict):
            return
        try:
            geom = state.get("geometry")
            if geom:
                win.geometry(str(geom))
        except Exception:
            pass

        try:
            target_zoom = float(state.get("zoom", 1.0))
        except Exception:
            target_zoom = 1.0

        try:
            current_zoom = float(getattr(canvas, "_zoom", 1.0))
        except Exception:
            current_zoom = 1.0

        try:
            # adjust zoom at canvas center
            if target_zoom > 0 and abs(target_zoom - current_zoom) > 1e-6:
                w = max(1, int(canvas.winfo_width()))
                h = max(1, int(canvas.winfo_height()))
                factor = target_zoom / current_zoom
                # Call the canvas zoom method if available
                zoom_at = getattr(canvas, "_zoom_at", None)
                if callable(zoom_at):
                    zoom_at(factor, w // 2, h // 2)
                else:
                    setattr(canvas, "_zoom", target_zoom)
        except Exception:
            pass

        try:
            xv = float(state.get("xview", 0.0))
            yv = float(state.get("yview", 0.0))
            xv = min(max(0.0, xv), 1.0)
            yv = min(max(0.0, yv), 1.0)
            canvas.xview_moveto(xv)
            canvas.yview_moveto(yv)
        except Exception:
            pass

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
class AnimationsPanel:
    def __init__(
        self,
        parent: tk.Widget,
        anim_id: str,
        payload: Dict[str, Any],
        *,
        on_changed: Optional[Callable[[str, Dict[str, Any]], None]] = None,
        on_renamed: Optional[Callable[[str, str], None]] = None,
        on_delete: Optional[Callable[[str], None]] = None,
        preview_provider: Any = None,
        asset_folder: Optional[str] = None,
        list_animation_names: Optional[Callable[[], List[str]]] = None,
        resolve_animation_payload: Optional[Callable[[str], Optional[Dict[str, Any]]]] = None,
    ) -> None:
        self.parent = parent
        self.node_id = str(anim_id)
        self.on_changed = on_changed
        self.on_renamed = on_renamed
        self.on_delete = on_delete
        self.preview_provider = preview_provider
        self.asset_folder = asset_folder
        self.list_animation_names = list_animation_names or (lambda: [])
        self.resolve_animation_payload = resolve_animation_payload

        self.payload = self._coerce_payload(self.node_id, payload)
        self._sync_frames_from_source()

        
        self.frame = tk.LabelFrame(parent, text=self.node_id, bd=6, relief=tk.GROOVE)
        self._build_ui()
        self._refresh_preview()

    
    def _build_ui(self) -> None:
        header = ttk.Frame(self.frame)
        header.grid(row=0, column=0, sticky="ew", padx=8, pady=(6, 8))
        self.frame.columnconfigure(0, weight=1)

        id_font = ("Segoe UI", 14, "bold")
        ttk.Label(header, text="ID:", font=id_font).pack(side="left")
        self.id_var = tk.StringVar(value=self.node_id)
        
        id_entry = tk.Entry(header, textvariable=self.id_var, width=24, font=id_font)
        id_entry.pack(side="left", padx=(4, 10))
        id_entry.bind("<FocusOut>", self._commit_rename)
        id_entry.bind("<Return>", self._commit_rename)

        ttk.Button(header, text="Delete", command=self._do_delete).pack(side="right")

        self._preview_label = tk.Label(header, bd=0, highlightthickness=0, background="#2d3436")
        self._preview_label.pack(side="right", padx=6)
        self._preview_img = None

        body = ttk.Frame(self.frame)
        body.grid(row=1, column=0, sticky="nsew", padx=8, pady=(0, 8))
        self.frame.rowconfigure(1, weight=1)

        
        self.sources_panel = SourcesElementPanel(
            body,
            self.payload.get("source", {}),
            on_changed=self._apply_changes,
            asset_folder=self.asset_folder,
            get_current_name=lambda: self.node_id,
            list_animation_names=self.list_animation_names,
        )
        self.sources_panel.get_frame().pack(fill="x", pady=4)

        
        pf = ttk.LabelFrame(body, text="Playback")
        pf.pack(fill="x", pady=4)
        self.flipped_var = tk.BooleanVar(value=bool(self.payload.get("flipped_source", False)))
        self.reversed_var = tk.BooleanVar(value=bool(self.payload.get("reverse_source", False)))
        self.locked_var = tk.BooleanVar(value=bool(self.payload.get("locked", False)))
        self.loop_var = tk.BooleanVar(value=bool(self.payload.get("loop", False)))  
        
        self.rnd_start_var = tk.BooleanVar(value=bool(self.payload.get("rnd_start", False)))

        ttk.Checkbutton(pf, text="flipped",  variable=self.flipped_var,  command=self._apply_changes).grid(row=0, column=0, sticky="w")
        ttk.Checkbutton(pf, text="reverse",  variable=self.reversed_var, command=self._apply_changes).grid(row=0, column=1, sticky="w")
        ttk.Checkbutton(pf, text="locked",   variable=self.locked_var,   command=self._apply_changes).grid(row=0, column=2, sticky="w")
        ttk.Checkbutton(pf, text="rnd start",variable=self.rnd_start_var,command=self._apply_changes).grid(row=0, column=3, sticky="w")
        ttk.Checkbutton(pf, text="loop",     variable=self.loop_var,     command=self._apply_changes).grid(row=0, column=4, sticky="w")  

        ttk.Label(pf, text="speed").grid(row=1, column=0, sticky="e")
        self.speed_var = tk.IntVar(value=int(self.payload.get("speed_factor", 1)))
        spin_speed = ttk.Spinbox(pf, from_=-20, to=20, textvariable=self.speed_var, width=8, command=self._apply_changes)
        spin_speed.grid(row=1, column=1, sticky="w", padx=4)
        spin_speed.bind("<FocusOut>", lambda _e: self._apply_changes())

        
        ttk.Label(pf, text="frames").grid(row=2, column=0, sticky="e")
        self.frames_var = tk.IntVar(value=int(self.payload.get("number_of_frames", 1)))
        frames_entry = ttk.Entry(pf, textvariable=self.frames_var, width=8, state="readonly")
        frames_entry.grid(row=2, column=1, sticky="w", padx=4)

        
        mvf = ttk.LabelFrame(body, text="Movement")
        mvf.pack(fill="x", pady=4)
        ttk.Label(mvf, text="Edit per-frame movement vectors").grid(row=0, column=0, sticky="w", padx=4)
        ttk.Button(mvf, text="Edit Movement...", command=self._open_movement_modal).grid(row=0, column=1, sticky="e", padx=4)

        
        oef = ttk.LabelFrame(body, text="On End")
        oef.pack(fill="x", pady=4)
        self.on_end_var = tk.StringVar(value=str(self.payload.get("on_end", "default") or "default"))
        self.on_end_combo = ttk.Combobox(oef, textvariable=self.on_end_var, state="readonly", width=36)
        self.on_end_combo.pack(side="left", padx=6, pady=4)
        self.on_end_combo.bind("<<ComboboxSelected>>", lambda _e: self._apply_changes())
        self._refresh_on_end_options()

        # Audio section
        self.audio_frame = ttk.LabelFrame(body, text="Audio")
        self.audio_frame.pack(fill="x", pady=4)
        self.audio_name: Optional[str] = None
        self.audio_volume_var = tk.IntVar(value=100)
        self.audio_effects_var = tk.BooleanVar(value=False)
        aud = self.payload.get("audio")
        if isinstance(aud, dict) and aud.get("name"):
            self.audio_name = str(aud.get("name"))
            self.audio_volume_var.set(int(aud.get("volume", 100)))
            self.audio_effects_var.set(bool(aud.get("effects", False)))
            self._build_audio_controls(self.audio_frame)
        else:
            ttk.Button(
                self.audio_frame,
                text="Select Clip Audio",
                command=self._select_audio,
            ).pack(padx=4, pady=4, anchor="w")

    
    def get_frame(self) -> ttk.Frame:
        return self.frame

    
    def _count_frame_files(self, path: str) -> int:
        """Count frame files in a folder, excluding GIFs and non-files."""
        if not path:
            return 1
        try:
            count = 0
            with os.scandir(path) as it:
                for entry in it:
                    if not entry.is_file():
                        continue
                    name = entry.name.lower()
                    
                    if name.endswith((".png", ".jpg", ".jpeg", ".bmp", ".webp")):
                        count += 1
            return max(1, count)
        except Exception:
            return 1

    def _compute_frames_from_source(self, src: Dict[str, Any]) -> int:
        """Compute number_of_frames by following source chains until a real frame source is found."""
        visited: Set[str] = set()
        current = dict(src or {})

        while True:
            kind = (current or {}).get("kind", "folder")

            if kind == "folder":
                base = self.asset_folder or ""
                rel = (current.get("path") or "").strip()
                path = os.path.join(base, rel) if rel else base
                return self._count_frame_files(path)

            if kind == "spritesheet":
                
                cols = int((current.get("cols") or 0))
                rows = int((current.get("rows") or 0))
                frames = int((current.get("frames") or 0))
                if cols > 0 and rows > 0:
                    return max(1, cols * rows)
                if frames > 0:
                    return frames
                
                return 1

            if kind == "animation":
                
                
                target = (current.get("name") or current.get("path") or "").strip()
                if not target:
                    return 1
                
                if target in visited:
                    return 1
                visited.add(target)

                
                resolved_payload: Optional[Dict[str, Any]] = None
                if callable(self.resolve_animation_payload):
                    try:
                        resolved_payload = self.resolve_animation_payload(target)
                    except Exception:
                        resolved_payload = None

                if not isinstance(resolved_payload, dict):
                    
                    return 1

                
                current = dict(resolved_payload.get("source") or {})
                continue

            
            return 1

    def _sync_frames_from_source(self) -> None:
        n = self._compute_frames_from_source(self.payload.get("source", {}))
        self.payload["number_of_frames"] = n
        
        if hasattr(self, "frames_var"):
            self.frames_var.set(n)

    def set_payload(self, new_payload: Dict[str, Any]) -> None:
        self.payload = self._coerce_payload(self.node_id, new_payload)
        self._sync_frames_from_source()
        
        self.sources_panel.set_values(self.payload.get("source", {}))

        self.flipped_var.set(bool(self.payload.get("flipped_source", False)))
        self.reversed_var.set(bool(self.payload.get("reverse_source", False)))
        self.locked_var.set(bool(self.payload.get("locked", False)))
        self.loop_var.set(bool(self.payload.get("loop", False)))
        try:
            self.rnd_start_var.set(bool(self.payload.get("rnd_start", False)))
        except Exception:
            pass
        self.speed_var.set(int(self.payload.get("speed_factor", 1)))
        self.frames_var.set(int(self.payload.get("number_of_frames", 1)))
        self.on_end_var.set(str(self.payload.get("on_end", "default") or "default"))
        self._refresh_on_end_options()
        aud = self.payload.get("audio")
        self.audio_name = None
        if isinstance(aud, dict) and aud.get("name"):
            self.audio_name = str(aud.get("name"))
            self.audio_volume_var.set(int(aud.get("volume", 100)))
            self.audio_effects_var.set(bool(aud.get("effects", False)))
            self._build_audio_controls(self.audio_frame)
        else:
            self._clear_audio_frame()

        self._refresh_preview()
        if self.on_changed:
            self.on_changed(self.node_id, self.payload)

    
    def _commit_rename(self, _evt=None):
        new_id = self.id_var.get().strip()
        if not new_id or new_id == self.node_id:
            self.id_var.set(self.node_id)
            return
        old = self.node_id
        self.node_id = new_id
        try:
            self.frame.configure(text=new_id)
        except Exception:
            pass
        
        try:
            self._refresh_on_end_options()
        except Exception:
            pass
        if self.on_renamed:
            self.on_renamed(old, new_id)

    def _do_delete(self):
        if self.on_delete:
            self.on_delete(self.node_id)

    def _open_movement_modal(self):
        parent = self.frame.winfo_toplevel()
        n = self._compute_frames_from_source(self.payload.get("source", {}))
        MovementModal(
            parent=parent,
            movement=self.payload.get("movement", []),
            frames_count=n,
            on_save=self._on_movement_saved,
            title=f"Movement: {self.node_id}",
        )

    def _on_movement_saved(self, new_movement: List[List[int]]):
        self.payload["movement"] = new_movement

        
        tot_dx = sum(int(it[0]) for it in new_movement)  
        tot_dy = sum(int(it[1]) for it in new_movement)
        self.payload["movement_total"] = {"dx": int(tot_dx), "dy": int(tot_dy)}

        if self.on_changed:
            self.on_changed(self.node_id, self.payload)
        self._refresh_preview()

    def _apply_changes(self):
        payload = self.payload
        payload["source"] = self.sources_panel.read_values()
        payload["flipped_source"] = bool(self.flipped_var.get())
        payload["reverse_source"] = bool(self.reversed_var.get())
        payload["locked"] = bool(self.locked_var.get())
        payload["loop"] = bool(self.loop_var.get())  
        payload["rnd_start"] = bool(self.rnd_start_var.get())

        v = int(self.speed_var.get())
        if v == 0:
            v = 1
        v = max(-20, min(20, v))
        payload["speed_factor"] = v
        payload["on_end"] = str(self.on_end_var.get() or "default")

        
        payload["number_of_frames"] = self._compute_frames_from_source(payload.get("source", {}))
        self.frames_var.set(payload["number_of_frames"])

        
        mv = payload.get("movement", [])
        if not isinstance(mv, list):
            mv = []
        n = payload["number_of_frames"]
        if len(mv) < n:
            mv.extend([[0, 0] for _ in range(n - len(mv))])
        elif len(mv) > n:
            mv = mv[:n]
        if n >= 1:
            mv[0] = [0, 0]
        payload["movement"] = mv

        self._apply_audio_changes(notify=False)

        if self.on_changed:
            self.on_changed(self.node_id, payload)
        self._refresh_preview()

    def _refresh_preview(self):
        provider = self.preview_provider
        if not provider:
            return
        try:
            img = provider.get_preview(self.node_id, self.payload)
            self._preview_img = img
            if img is not None:
                self._preview_label.configure(image=img)
        except Exception:
            pass


    def _clear_audio_frame(self) -> None:
        for ch in list(self.audio_frame.winfo_children()):
            try:
                ch.destroy()
            except Exception:
                pass
        ttk.Button(
            self.audio_frame,
            text="Select Clip Audio",
            command=self._select_audio,
        ).pack(padx=4, pady=4, anchor="w")

    def _build_audio_controls(self, frame: tk.Widget) -> None:
        for ch in list(frame.winfo_children()):
            try:
                ch.destroy()
            except Exception:
                pass
        frame.columnconfigure(1, weight=1)
        ttk.Label(frame, text=f"Clip: {self.audio_name}").grid(row=0, column=0, sticky="w", padx=4)
        ttk.Button(frame, text="Play", command=self._play_audio).grid(row=0, column=1, sticky="w")
        ttk.Label(frame, text="Volume").grid(row=1, column=0, sticky="e", padx=4)
        ttk.Scale(
            frame,
            from_=0,
            to=100,
            variable=self.audio_volume_var,
            orient="horizontal",
            command=lambda _v: self._apply_audio_changes(),
        ).grid(row=1, column=1, sticky="ew", padx=4)
        ttk.Checkbutton(
            frame,
            text="Apply Effects",
            variable=self.audio_effects_var,
            command=self._apply_audio_changes,
        ).grid(row=2, column=0, columnspan=2, sticky="w", padx=4)
        btns = ttk.Frame(frame)
        btns.grid(row=3, column=0, columnspan=2, sticky="w", padx=4, pady=(4, 0))
        ttk.Button(btns, text="Replace Audio", command=self._replace_audio).pack(side="left")
        ttk.Button(btns, text="Delete Audio", command=self._delete_audio).pack(side="left", padx=4)

    def _select_audio(self):
        file = filedialog.askopenfilename(filetypes=[("Audio", "*.wav *.mp3")])
        if not file:
            return
        self._import_audio(file)

    def _import_audio(self, src_path: str) -> None:
        base = os.path.basename(src_path)
        name, ext = os.path.splitext(base)
        out_dir = self.asset_folder or ""
        try:
            os.makedirs(out_dir, exist_ok=True)
            dest = os.path.join(out_dir, name + ".wav")
            if ext.lower() == ".wav":
                shutil.copy2(src_path, dest)
            else:
                try:
                    subprocess.run(
                        ["ffmpeg", "-y", "-i", src_path, dest],
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                        check=True,
                    )
                except Exception:
                    shutil.copy2(src_path, dest)
        except Exception as e:
            messagebox.showerror("Audio Import", f"Failed to import audio: {e}")
            return
        self.audio_name = name
        self.audio_volume_var.set(100)
        self.audio_effects_var.set(False)
        self._apply_audio_changes(notify=False)
        self._build_audio_controls(self.audio_frame)
        if self.on_changed:
            self.on_changed(self.node_id, self.payload)

    def _play_audio(self):
        if not self.audio_name:
            return
        path = os.path.join(self.asset_folder or "", self.audio_name + ".wav")
        try:
            if sa:
                sa.WaveObject.from_wave_file(path).play()
            else:  # fallback to ffplay/aplay
                subprocess.Popen(
                    ["ffplay", "-nodisp", "-autoexit", path],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
        except Exception as e:
            messagebox.showerror("Play Audio", f"Failed to play audio: {e}")

    def _replace_audio(self):
        self._select_audio()

    def _delete_audio(self):
        self.audio_name = None
        self.payload.pop("audio", None)
        self._clear_audio_frame()
        if self.on_changed:
            self.on_changed(self.node_id, self.payload)

    def _apply_audio_changes(self, *_args, notify: bool = True) -> None:
        if self.audio_name:
            vol = int(self.audio_volume_var.get())
            vol = max(0, min(100, vol))
            eff = bool(self.audio_effects_var.get())
            self.payload["audio"] = {
                "name": self.audio_name,
                "volume": vol,
                "effects": eff,
            }
        else:
            self.payload.pop("audio", None)
        if notify and self.on_changed:
            self.on_changed(self.node_id, self.payload)

    @staticmethod
    def _coerce_payload(anim_name: str, p: Dict[str, Any]) -> Dict[str, Any]:
        p = dict(p or {})
        src = dict(p.get("source") or {})
        _kind = src.get("kind", "folder")
        p["source"] = {
            "kind": _kind,
            "path": src.get("path", anim_name if _kind == "folder" else ""),
            "name": src.get("name", None if _kind == "folder" else ""),
        }
        p.setdefault("flipped_source", False)
        p.setdefault("reverse_source", False)
        p.setdefault("locked", False)
        p.setdefault("loop", bool(p.get("loop", False)))  
        p.setdefault("rnd_start", bool(p.get("rnd_start", False)))
        try:
            _raw = int(str(p.get("speed_factor", 1)).strip())
        except Exception:
            _raw = 1
        if _raw == 0:
            _raw = 1
        _raw = max(-20, min(20, _raw))
        p.setdefault("speed_factor", _raw)

        
        try:
            p.setdefault("number_of_frames", max(1, int(p.get("number_of_frames", 1))))
        except Exception:
            p.setdefault("number_of_frames", 1)

        
        mv = p.get("movement")
        n = p["number_of_frames"]
        if not isinstance(mv, list) or len(mv) < 1:
            mv = [[0, 0] for _ in range(n)]
        if len(mv) != n:
            if len(mv) < n:
                mv.extend([[0, 0] for _ in range(n - len(mv))])
            else:
                mv = mv[:n]
        mv[0] = [0, 0]
        p["movement"] = mv

        val = p.get("on_end")
        if not val:
            val = "default"
        p["on_end"] = str(val)

        aud = p.get("audio")
        if isinstance(aud, dict):
            name = str(aud.get("name", "")).strip()
            try:
                vol = int(aud.get("volume", 100))
            except Exception:
                vol = 100
            vol = max(0, min(100, vol))
            eff = bool(aud.get("effects", False))
            if name:
                p["audio"] = {"name": name, "volume": vol, "effects": eff}
            else:
                p.pop("audio", None)
        else:
            p.pop("audio", None)

        return p

    def _refresh_on_end_options(self):
        try:
            names = sorted(self.list_animation_names())
        except Exception:
            names = []
        
        base = ["default", "reverse", "end"]
        
        seen = set()
        values = []
        for x in base + names:
            if x in seen:
                continue
            seen.add(x)
            values.append(x)
        try:
            self.on_end_combo["values"] = values
        except Exception:
            pass
        
        cur = str(self.on_end_var.get() or "default")
        if cur not in values:
            self.on_end_var.set("default")
class CustomControllerManager:
    """Helper to create/open an asset-level custom controller (C++).

    Files live in ENGINE/custom_controllers/<asset>_controller.hpp/.cpp
    The key equals the file base name (e.g., "player_controller").

    Also patches ENGINE/asset/controller_factory.cpp to include and register
    the newly created controller in create_by_key(...).
    """

    def __init__(self, info_path: Path) -> None:
        self.info_path = info_path
        self.asset_name = info_path.parent.name
        self.repo_root = self._find_repo_root_with_engine(info_path)
        self.engine_dir = (
            self.repo_root / "ENGINE"
            if self.repo_root
            else info_path.parents[2] / "ENGINE"
        )
        self.ctrl_dir = self.engine_dir / "custom_controllers"
        self.base_name = f"{self.asset_name}_controller"
        self.hpp = self.ctrl_dir / f"{self.base_name}.hpp"
        self.cpp = self.ctrl_dir / f"{self.base_name}.cpp"

        # controller factory assumed to live with other asset code
        self.factory_dir = self.engine_dir / "asset"
        self.factory_cpp = self.factory_dir / "controller_factory.cpp"
        self.factory_hpp = self.factory_dir / "controller_factory.hpp"

    @staticmethod
    def _find_repo_root_with_engine(start: Path) -> Optional[Path]:
        for p in [start] + list(start.parents):
            if (p / "ENGINE").exists() and (p / "ENGINE").is_dir():
                return p
        return None

    def exists(self) -> bool:
        return self.hpp.exists() and self.cpp.exists()

    def key(self) -> str:
        return self.base_name

    # ---------- public API ----------

    def create(self) -> Tuple[Path, Path]:
        """Create controller files and register them in the controller factory."""
        self.ctrl_dir.mkdir(parents=True, exist_ok=True)
        class_name = self._class_name()

        # --- generate files in expected controller format ---
        guard = re.sub(r"[^0-9A-Za-z]", "_", self.base_name).upper() + "_HPP"
        hpp = f"""#ifndef {guard}
#define {guard}

#include "asset/asset_controller.hpp"

class Asset;

class {class_name} : public AssetController {{

public:
    {class_name}(Asset* self);
    ~{class_name}() override = default;
    void update(const Input& in) override;

private:
    Asset* self_ = nullptr;
}};

#endif
"""
        cpp = f"""#include "{self.base_name}.hpp"

#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"

{class_name}::{class_name}(Asset* self)
    : self_(self) {{}}

void {class_name}::update(const Input& /*in*/) {{

}}
"""
        self.hpp.write_text(hpp, encoding="utf-8")
        self.cpp.write_text(cpp, encoding="utf-8")

        # --- register in controller factory ---
        self._register_in_controller_factory(class_name)

        return self.hpp, self.cpp

    def open_in_ide(self) -> None:
        path = self.hpp if self.hpp.exists() else self.cpp
        if not path:
            return
        try:
            if sys.platform.startswith("win"):
                os.startfile(str(path))  # type: ignore[attr-defined]
            elif sys.platform == "darwin":
                subprocess.Popen(["open", str(path)])
            else:
                subprocess.Popen(["xdg-open", str(path)])
        except Exception:
            pass

    # ---------- internals ----------

    def _class_name(self) -> str:
        # Convert file base (snake/other) to PascalCase
        parts = [p for p in self.base_name.replace("-", "_").split("_") if p]
        return "".join(s[:1].upper() + s[1:] for s in parts)

    # controller factory patching
    def _register_in_controller_factory(self, class_name: str) -> None:
        """Ensure controller_factory.cpp includes and returns this controller."""
        if not self.factory_cpp.exists():
            # fail softly; user can wire manually if project layout differs
            return

        text = self.factory_cpp.read_text(encoding="utf-8")

        include_line = f'#include "custom_controllers/{self.base_name}.hpp"\n'
        if include_line not in text:
            # insert after last custom_controllers include if present, else after other includes
            lines = text.splitlines(True)
            insert_at = 0
            last_inc = 0
            for i, ln in enumerate(lines):
                if ln.lstrip().startswith("#include"):
                    last_inc = i + 1
                # prefer clustering with other custom_controllers includes
                if 'custom_controllers/' in ln:
                    insert_at = i + 1
            if insert_at == 0:
                insert_at = last_inc
            lines.insert(insert_at, include_line)
            text = "".join(lines)

        # add branch in create_by_key
        key_branch = (
            f' if (key == "{self.base_name}")\n'
            f'  return std::make_unique<{class_name}>(assets_, self);\n'
        )

        # find create_by_key(...) body
        m = re.search(
            r"(std::unique_ptr<\s*AssetController\s*>\s*ControllerFactory::create_by_key\s*\([^)]*\)\s*{\s*)(.*?)(\n})",
            text,
            flags=re.DOTALL,
        )
        if m:
            head, body, tail = m.group(1), m.group(2), m.group(3)

            if key_branch not in body:
                # insert before the first closing '}' of the try block if present,
                # else just before the final 'return nullptr;'
                # try to locate the try { ... } block
                try_block = re.search(r"try\s*{\s*(.*?)}\s*catch", body, flags=re.DOTALL)
                if try_block:
                    tb_head, tb_content, tb_tail = (
                        body[: try_block.start(0)],
                        try_block.group(1),
                        body[try_block.end(1) :],
                    )
                    if key_branch not in tb_content:
                        tb_content = tb_content + key_branch
                    body = tb_head + "try {\n" + tb_content + "}" + tb_tail
                else:
                    # fallback: place before final return nullptr;
                    body = body.replace("return nullptr;", key_branch + " return nullptr;")

            text = head + body + tail

        self.factory_cpp.write_text(text, encoding="utf-8")
class PreviewProvider:
    """
    Small helper to load a preview image (first frame) for animations.
    - Resolves folder source: base_dir / source.path
    - Resolves animation source: looks up referenced animation payload
    - Applies horizontal flip if flipped_source is set on either level
    - Returns a Tk PhotoImage ready for display
    """

    SUPPORTED_EXTS = (".png", ".jpg", ".jpeg", ".gif", ".bmp")

    def __init__(self, base_dir: Path, animation_lookup: Callable[[str], Optional[Dict[str, Any]]],
                 size: Tuple[int, int] = (72, 72)):
        self.base_dir = Path(base_dir)
        self.lookup = animation_lookup
        self.size = size

    def _find_first_image(self, folder: Path) -> Optional[Path]:
        try:
            if not folder.exists():
                return None
            files = sorted([p for p in folder.iterdir() if p.suffix.lower() in self.SUPPORTED_EXTS])
            return files[0] if files else None
        except Exception:
            return None

    def get_preview(self, anim_name: str, payload: Dict[str, Any]):
        if Image is None or ImageTk is None:
            return None  # PIL not available; caller should handle
        flip = bool(payload.get("flipped_source", False))
        src = payload.get("source") or {}
        kind = src.get("kind", "folder")
        img_path: Optional[Path] = None

        if kind == "folder":
            rel = src.get("path") or anim_name
            folder = (self.base_dir / rel).resolve()
            img_path = self._find_first_image(folder)
        elif kind == "animation":
            ref_name = src.get("name") or anim_name
            other = self.lookup(ref_name) or {}
            flip = flip or bool(other.get("flipped_source", False))
            other_src = (other.get("source") or {})
            if other_src.get("kind") == "folder":
                rel = other_src.get("path") or ref_name
                folder = (self.base_dir / rel).resolve()
                img_path = self._find_first_image(folder)
            else:
                # one level only
                rel = ref_name
                folder = (self.base_dir / rel).resolve()
                img_path = self._find_first_image(folder)
        else:
            return None

        if not img_path or not img_path.exists():
            return None

        try:
            im = Image.open(img_path)
            # scale to fit
            target_w, target_h = self.size
            im = im.convert("RGBA")
            im.thumbnail((target_w, target_h), Image.LANCZOS)
            if flip:
                im = im.transpose(Image.FLIP_LEFT_RIGHT)
            return ImageTk.PhotoImage(im)
        except Exception:
            return None

# ---------- json helpers ----------
def read_json(p: Path) -> Dict[str, Any]:
    with p.open("r", encoding="utf-8") as f:
        return json.load(f)

def write_json(p: Path, data: Dict[str, Any]) -> None:
    with p.open("w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)

def ensure_sections(d: Dict[str, Any]) -> None:
    if "animations" not in d or not isinstance(d["animations"], dict):
        d["animations"] = {}
    # keep for compatibility; not used by this UI
    if "mappings" not in d or not isinstance(d["mappings"], dict):
        d["mappings"] = {}
    if "layout" not in d or not isinstance(d["layout"], dict):
        d["layout"] = {}
    # optional start field
    if "start" not in d:
        d["start"] = ""

# ---------- app (list-based UI) ----------
class AnimationConfiguratorAppSingle:
    def __init__(self, info_path: Path):
        self.info_path = info_path
        self.data: Optional[Dict[str, Any]] = None

        self.win = tk.Tk()
        self.win.title("Animation Configurator")
        self.win.geometry("1100x780")

        try:
            self.data = read_json(self.info_path)
        except Exception as e:
            messagebox.showerror("Load error", f"Failed to read {self.info_path}:\n{e}")
            self.win.destroy()
            raise

        ensure_sections(self.data)

        # managers: undo + view persistence
        self.history = HistoryManager(limit=200)
        self.view_state = ViewStateManager()
        self.history.snapshot(self.data)

        # Actions bar
        act = ttk.Frame(self.win)
        act.pack(side="top", fill="x", padx=8, pady=6)

        # Custom controller button (asset-level)
        self.cc_manager = CustomControllerManager(self.info_path)
        self.custom_ctrl_btn = ttk.Button(
            act,
            text=self._custom_ctrl_button_text(),
            command=self._on_custom_ctrl,
        )
        self.custom_ctrl_btn.pack(side="left", padx=(2, 8))

        # Proactively ensure engine sources include all existing controllers
        try:
            self._ensure_all_custom_includes()
        except Exception:
            pass

        ttk.Button(act, text="New Animation", command=self.create_animation).pack(side="left", padx=2)

        # Crop All (Global Bounds) button
        ttk.Button(
            act,
            text="Crop All (Global Bounds)",
            command=self._crop_all_global,
        ).pack(side="left", padx=2)

        ttk.Label(act, text="Start:").pack(side="left", padx=(16, 4))
        self.start_var = tk.StringVar(value=str(self.data.get("start", "")))
        self.start_cb = ttk.Combobox(act, textvariable=self.start_var, width=24, state="readonly")
        self.start_cb.pack(side="left")
        self.start_cb.bind("<<ComboboxSelected>>", self._on_start_changed)

        # Scrollable list area
        list_wrap = ttk.Frame(self.win)
        list_wrap.pack(fill="both", expand=True)
        self.scroll_canvas = tk.Canvas(list_wrap, bg="#2d3436", highlightthickness=0)
        vsb = ttk.Scrollbar(list_wrap, orient="vertical", command=self.scroll_canvas.yview)
        self.scroll_canvas.configure(yscrollcommand=vsb.set)
        vsb.pack(side="right", fill="y")
        self.scroll_canvas.pack(side="left", fill="both", expand=True)
        self.inner = ttk.Frame(self.scroll_canvas)
        self.scroll_window = self.scroll_canvas.create_window((0, 0), window=self.inner, anchor="nw")
        self.inner.bind("<Configure>", lambda _e: self.scroll_canvas.configure(scrollregion=self.scroll_canvas.bbox("all")))
        # keep inner frame width matching canvas width
        self.scroll_canvas.bind("<Configure>", lambda e: self.scroll_canvas.itemconfigure(self.scroll_window, width=e.width))
        # grid config for multi-column layout
        self.grid_cols = 2  # number of columns in the panel grid
        for c in range(self.grid_cols):
            self.inner.columnconfigure(c, weight=1, uniform="panels")

        self.status = tk.StringVar(value=f"Loaded {self.info_path}")
        ttk.Label(self.win, textvariable=self.status, relief=tk.SUNKEN, anchor="w").pack(side="bottom", fill="x")

        # keyboard + close
        try:
            self.win.bind_all("<Control-z>", self._undo_last_change)
            self.win.bind_all("<Control-Z>", self._undo_last_change)
        except Exception:
            pass
        try:
            self.win.protocol("WM_DELETE_WINDOW", self._on_close)
        except Exception:
            pass

        # panels registry
        self.panels: Dict[str, AnimationsPanel] = {}

        # preview provider (optional)
        try:
            self.preview_provider = PreviewProvider(
                base_dir=self.info_path.parent,
                animation_lookup=lambda name: self.data.get("animations", {}).get(name),
                size=(72, 72),
            )
        except Exception:
            self.preview_provider = None

        # apply geometry if saved
        try:
            view = self.data.get("layout", {}).get("__view", {})
            if isinstance(view, dict):
                geom = view.get("geometry")
                if geom:
                    self.win.geometry(str(geom))
        except Exception:
            pass

        self.rebuild_list()
        self._restore_view_state()

    # ----- helpers for cropping -----
    def _anim_source_folder(self, anim_payload: Dict[str, Any]) -> Optional[Path]:
        try:
            src = anim_payload.get("source", {})
            if not isinstance(src, dict):
                return None
            if src.get("kind") != "folder":
                return None
            rel = src.get("path")
            if not rel:
                return None
            return (self.info_path.parent / rel).resolve()
        except Exception:
            return None

    def _run_in_thread(self, fn, on_done=None):
        def _wrap():
            try:
                result = fn()
            except Exception as e:
                result = e
            finally:
                if on_done:
                    self.win.after(0, lambda r=result: on_done(r))
        threading.Thread(target=_wrap, daemon=True).start()

    def _crop_all_global(self):
        if not (self.data and isinstance(self.data.get("animations"), dict)):
            messagebox.showinfo("Crop All", "No animations found.")
            return

        # 1) Collect all folders and image paths
        anims = list(self.data["animations"].items())
        images_by_folder: Dict[Path, List[str]] = {}

        for _name, payload in anims:
            folder = self._anim_source_folder(payload)
            if folder and folder.exists():
                try:
                    images_by_folder[folder] = get_image_paths(str(folder))
                except Exception:
                    images_by_folder[folder] = []

        all_image_paths: List[str] = []
        for lst in images_by_folder.values():
            all_image_paths.extend(lst)

        if not all_image_paths:
            messagebox.showinfo("Crop All", "No numbered PNG frames found.")
            return

        # 2) Background work: compute global bounds, then crop everything with those bounds
        def work():
            # Slightly tolerant threshold to ignore faint halos
            top, bottom, left, right, _w, _h = compute_union_bounds(all_image_paths, alpha_threshold=2)
            if top == bottom == left == right == 0:
                return {"bounds": (0, 0, 0, 0), "total": 0, "per_folder": []}

            total_cropped = 0
            per_folder = []
            for folder, imgs in images_by_folder.items():
                if not imgs:
                    per_folder.append((str(folder), 0))
                    continue
                n = crop_images_with_bounds(imgs, top, bottom, left, right)
                total_cropped += n
                per_folder.append((str(folder), n))
            return {"bounds": (top, bottom, left, right), "total": total_cropped, "per_folder": per_folder}

        def done(res):
            if isinstance(res, Exception):
                messagebox.showerror("Crop All", f"Error: {res}")
                return
            bounds = res.get("bounds", (0, 0, 0, 0))
            total = res.get("total", 0)
            per_folder = res.get("per_folder", [])
            t, b, l, r = bounds
            if total == 0:
                messagebox.showinfo("Crop All", "No cropping needed (global bounds empty).")
            else:
                lines = [f"Global crop (T:{t}, B:{b}, L:{l}, R:{r})", f"Total frames cropped: {total}", ""]
                for folder, n in per_folder:
                    lines.append(f"{folder}: {n} frames")
                messagebox.showinfo("Crop All", "\n".join(lines))
            # refresh previews and save
            try:
                self.save_current()
                self.rebuild_list()
            except Exception:
                pass

        self._run_in_thread(work, on_done=done)

    # ----- autosave -----
    def save_current(self):
        if not (self.data and self.info_path):
            return
        try:
            # persist view state
            self._save_view_state_to_data()
            write_json(self.info_path, self.data)
            self.status.set(f"Saved {self.info_path}")
        except Exception as e:
            messagebox.showerror("Save error", f"Failed to save {self.info_path}:\n{e}")

    # ----- list build -----
    def rebuild_list(self):
        for w in self.inner.winfo_children():
            w.destroy()
        self.panels.clear()
        anims = list(self.data.get("animations", {}).keys())
        anims.sort()
        # place panels in a grid with spacing
        cmax = max(1, int(getattr(self, "grid_cols", 2)))
        r = 0
        c = 0
        for a in anims:
            p = AnimationsPanel(
                self.inner,
                a,
                self.data["animations"][a],
                on_changed=self._on_panel_changed,
                on_renamed=self._on_panel_renamed,
                on_delete=self._on_panel_delete,
                preview_provider=self.preview_provider,
                asset_folder=str(self.info_path.parent),
                list_animation_names=lambda: list(self.data.get("animations", {}).keys()),
                resolve_animation_payload=lambda name: self.data.get("animations", {}).get(str(name), None),
            )
            fr = p.get_frame()
            fr.grid(row=r, column=c, sticky="nsew", padx=12, pady=12)
            self.panels[a] = p
            c += 1
            if c >= cmax:
                c = 0
                r += 1
        self._refresh_start_selector()

    # ----- panel callbacks -----
    def _on_panel_changed(self, node_id: str, payload: Dict[str, Any]):
        if not (self.data and self.info_path):
            return
        self._snapshot()
        self.data.setdefault("animations", {})[node_id] = payload
        self.save_current()

    def _on_panel_renamed(self, old_id: str, new_id: str):
        if not (self.data and self.info_path):
            return
        new_id = str(new_id).strip()
        if not new_id:
            return
        if new_id in self.data.get("animations", {}) and new_id != old_id:
            messagebox.showerror("Rename", f"'{new_id}' already exists.")
            # rebuild to reset UI to old id
            self.rebuild_list()
            return
        self._snapshot()
        anims = self.data.setdefault("animations", {})
        anims[new_id] = anims.pop(old_id)
        # update start if matches
        if str(self.data.get("start", "")) == old_id:
            self.data["start"] = new_id
        self.save_current()
        self.rebuild_list()

    def _custom_ctrl_button_text(self) -> str:
        return "Open Custom Controller in IDE" if self.cc_manager.exists() else "Create Custom Controller"

    def _on_custom_ctrl(self):
        if self.cc_manager.exists():
            self.cc_manager.open_in_ide()
            return
        # create files and update JSON with key
        try:
            self.cc_manager.create()
        except Exception as e:
            messagebox.showerror("Create error", f"Failed to create custom controller files:\n{e}")
            return
        # store key at top-level
        try:
            self.data["custom_controller_key"] = self.cc_manager.key()
        except Exception:
            pass
        self.save_current()
        try:
            self.custom_ctrl_btn.configure(text=self._custom_ctrl_button_text())
        except Exception:
            pass

    def _ensure_all_custom_includes(self):
        # Scan ENGINE/custom_controllers for headers and ensure includes exist in key engine files
        engine_dir = self.info_path.parents[2] / "ENGINE"
        cc_dir = engine_dir / "custom_controllers"
        if not cc_dir.exists():
            return
        targets = [
            engine_dir / "asset_info_methods" / "animation_loader.cpp",
            engine_dir / "asset_info_methods" / "animation_loader.hpp",
            engine_dir / "ui" / "asset_info_ui.cpp",
        ]
        headers = [p for p in cc_dir.glob("*.hpp")]
        for hdr in headers:
            base = hdr.stem  # without extension
            inc = f"#include \"custom_controllers/{base}.hpp\"\n"
            for t in targets:
                try:
                    if not t.exists():
                        continue
                    txt = t.read_text(encoding="utf-8")
                    if inc.strip() in txt:
                        continue
                    lines = txt.splitlines(True)
                    ins = 0
                    for i, ln in enumerate(lines[:100]):
                        if ln.lstrip().startswith('#include'):
                            ins = i + 1
                    lines.insert(ins, inc)
                    t.write_text("".join(lines), encoding="utf-8")
                except Exception:
                    pass

    def _on_panel_delete(self, node_id: str):
        if not (self.data and self.info_path):
            return
        self._snapshot()
        self.data.setdefault("animations", {}).pop(node_id, None)
        if str(self.data.get("start", "")) == node_id:
            self.data["start"] = ""
        self.save_current()
        self.rebuild_list()

    # ----- start selector -----
    def _refresh_start_selector(self):
        node_ids = list(self.data.get("animations", {}).keys())
        node_ids.sort()
        self.start_cb["values"] = node_ids
        cur = str(self.data.get("start", ""))
        if cur and cur not in node_ids:
            cur = ""
            self.data["start"] = ""
        self.start_var.set(cur)

    def _on_start_changed(self, _evt=None):
        if not (self.data and self.info_path):
            return
        self._snapshot()
        self.data["start"] = self.start_var.get()
        self.save_current()

    # ----- create -----
    def create_animation(self):
        if not (self.data and self.info_path):
            return
        self._snapshot()
        base = "new_anim"
        name, i = base, 1
        while name in self.data["animations"]:
            name = f"{base}_{i}"
            i += 1
        self.data["animations"][name] = {
            "source": {"kind": "folder", "path": name, "name": None},
            "flipped_source": False,
            "reverse_source": False,
            "locked": False,
            "rnd_start": False,
            "speed_factor": 1,
            "number_of_frames": 1,
            "movement": [[0, 0]],
            "on_end": "",
        }
        self.save_current()
        self.rebuild_list()

    # ----- view persistence + undo -----
    def _save_view_state_to_data(self):
        try:
            layout = self.data.setdefault("layout", {})
            layout["__view"] = self.view_state.capture(self.win, self.scroll_canvas)
        except Exception:
            pass

    def _restore_view_state(self):
        try:
            view = self.data.get("layout", {}).get("__view", {})
            if isinstance(view, dict):
                self.win.after(50, lambda v=view: self.view_state.apply(self.win, self.scroll_canvas, v))
        except Exception:
            pass

    def _snapshot(self):
        try:
            if self.data is not None:
                self.history.snapshot(self.data)
        except Exception:
            pass

    def _undo_last_change(self, _evt=None):
        try:
            snap = self.history.undo()
            if snap is None:
                return
            self.data = snap
            write_json(self.info_path, self.data)
            self.rebuild_list()
            self._restore_view_state()
            self.status.set("Undid last change")
        except Exception:
            pass

    def _on_close(self):
        try:
            self._save_view_state_to_data()
            self.save_current()
        finally:
            try:
                self.win.destroy()
            except Exception:
                pass

    def run(self):
        self.win.mainloop()

# ---------- entry ----------
def _choose_info_json() -> Optional[Path]:
    root = tk.Tk()
    root.withdraw()
    root.update_idletasks()
    path_str = filedialog.askopenfilename(
        title="Select info.json",
        filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
    )
    root.destroy()
    if not path_str:
        return None
    p = Path(path_str).expanduser().resolve()
    return p if p.exists() else None

def main(argv: List[str]) -> int:
    if len(argv) == 1:
        info_path = _choose_info_json()
        if not info_path:
            print("No file selected. Exiting.")
            return 1
        app = AnimationConfiguratorAppSingle(info_path)
        app.run()
        return 0

    if len(argv) == 2:
        info_path = Path(argv[1]).expanduser().resolve()
        if not info_path.exists():
            print(f"Error: {info_path} does not exist")
            return 1
        app = AnimationConfiguratorAppSingle(info_path)
        app.run()
        return 0

    print("Usage:\n  animation_ui.py                # pick JSON via dialog\n  animation_ui.py path/to/info.json")
    return 1

if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
