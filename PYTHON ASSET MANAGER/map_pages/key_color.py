import tkinter as tk
from tkinter import colorchooser
import math

class ColorKeyCircleEditor(tk.Canvas):
    def __init__(self, parent, on_change=None, width=320, height=320, **kwargs):
        super().__init__(parent, width=width, height=height, bg="#d6f0ff", highlightthickness=0, **kwargs)
        self.radius = min(width, height) // 2 - 190  # extra padding for labels
        self.center = (width // 2, height // 2)
        self.keys = []
        self.fixed_angles = {0.0, 180.0}
        self.selected_key = None
        self.hover_key = None
        self.on_change_callback = on_change

        self.bind("<Double-Button-1>", self._on_double_click)
        self.bind("<Button-1>", self._on_click)
        self.bind("<B1-Motion>", self._on_drag)
        self.bind("<ButtonRelease-1>", self._on_release)
        self.bind("<Motion>", self._on_motion)
        self.focus_set()

        self._build_initial_keys()
        self._draw()

    def _build_initial_keys(self):
        self.keys = [
            {"angle": 0.0,   "color": (255, 255, 255, 255)},
            {"angle": 180.0, "color": (0, 0, 0, 255)}
        ]

    def _ensure_fixed_keys_exist(self):
        present = {round(k["angle"] % 360, 2) for k in self.keys}
        if 0.0 not in present:
            self.keys.append({"angle": 0.0, "color": (255, 255, 255, 255)})
        if 180.0 not in present:
            self.keys.append({"angle": 180.0, "color": (0, 0, 0, 255)})

    def _mouse_to_angle(self, x, y):
        cx, cy = self.center
        dx, dy = x - cx, y - cy
        return (math.degrees(math.atan2(dx, -dy)) + 360) % 360

    def _find_nearest_key(self, angle, tol=6):
        best, md = None, tol
        for k in self.keys:
            d = abs((k["angle"] - angle + 360) % 360)
            if d < md:
                best, md = k, d
        return best

    def _on_double_click(self, e):
        angle = self._mouse_to_angle(e.x, e.y)
        key = self._find_nearest_key(angle)

        if key:
            if round(key["angle"] % 360, 2) in (0.0, 180.0):
                self._change_color(key)
            else:
                self._open_context_menu(key)
        else:
            self._add_key_pair(angle)

        self._draw()
        self._save_if_needed()

    def _on_click(self, e):
        angle = self._mouse_to_angle(e.x, e.y)
        key = self._find_nearest_key(angle)
        self.selected_key = key if key and key["angle"] not in self.fixed_angles else None
        self._draw()

    def _on_drag(self, e):
        if not self.selected_key or self.selected_key["angle"] in self.fixed_angles:
            return
        old_angle = self.selected_key["angle"]
        old_sym = self._get_symmetric_angle(old_angle)
        new_angle = self._mouse_to_angle(e.x, e.y)
        self.selected_key["angle"] = new_angle
        new_sym = self._get_symmetric_angle(new_angle)
        for k in self.keys:
            if abs((k["angle"] - old_sym) % 360) < 0.01:
                k["angle"] = new_sym
        self._draw()
        self._save_if_needed()

    def _on_release(self, e):
        self.selected_key = None

    def _on_motion(self, e):
        angle = self._mouse_to_angle(e.x, e.y)
        self.hover_key = self._find_nearest_key(angle)
        self._draw()

    def _get_symmetric_angle(self, angle):
        return (360 - angle) % 360

    def _sync_symmetric_color(self, key):
        target = self._get_symmetric_angle(key["angle"])
        for k in self.keys:
            if abs((k["angle"] - target) % 360) < 0.01:
                k["color"] = key["color"]

    def _interpolate_color(self, c1, c2, t):
        return tuple(int(c1[i] + (c2[i] - c1[i]) * t) for i in range(4))

    def _get_neighbors(self, angle):
        s = sorted(self.keys, key=lambda k: k["angle"])
        for i in range(len(s)):
            a1, a2 = s[i]["angle"], s[(i + 1) % len(s)]["angle"]
            if a1 < angle < a2 or (a2 < a1 and (angle > a1 or angle < a2)):
                return s[i], s[(i + 1) % len(s)]
        return s[-1], s[0]

    def _add_key_pair(self, angle):
        before, after = self._get_neighbors(angle)
        c = self._interpolate_color(before["color"], after["color"], 0.5)
        key = {"angle": angle, "color": c}
        sym = {"angle": self._get_symmetric_angle(angle), "color": c}
        self.keys.extend([key, sym])

    def _open_context_menu(self, key):
        if key["angle"] in self.fixed_angles:
            return
        menu = tk.Menu(self, tearoff=0)
        menu.add_command(label="Change Color", command=lambda k=key: self._change_color(k))
        menu.add_command(label="Delete", command=lambda k=key: self._delete_key_pair(k))
        menu.tk_popup(self.winfo_pointerx(), self.winfo_pointery())
        menu.grab_release()

    def _change_color(self, key):
        from tkinter import Toplevel, Label, Button, Entry, StringVar, IntVar
        from tkinter.colorchooser import askcolor

        rgb = list(key["color"][:3])
        alpha = key["color"][3]
        rgba_var = StringVar()
        alpha_var = IntVar(value=alpha)

        def update_preview():
            rgba_label = f"RGBA: {rgb[0]}, {rgb[1]}, {rgb[2]}, {alpha_var.get()}"
            rgba_var.set(rgba_label)
            color_box.configure(bg=f'#{rgb[0]:02x}{rgb[1]:02x}{rgb[2]:02x}')

        def choose_color():
            nonlocal rgb
            color = askcolor(color=f'#{rgb[0]:02x}{rgb[1]:02x}{rgb[2]:02x}')
            if color and color[0]:
                rgb = list(map(int, color[0]))
                update_preview()

        def apply_changes():
            key["color"] = (rgb[0], rgb[1], rgb[2], alpha_var.get())
            self._sync_symmetric_color(key)
            self._draw()
            self._save_if_needed()
            win.destroy()

        win = Toplevel(self)
        win.title("Adjust RGBA")
        win.transient(self)
        win.grab_set()
        win.configure(padx=12, pady=10)

        # Color preview and label
        color_box = Label(win, width=10, height=2, relief="ridge")
        color_box.grid(row=0, column=0, padx=4, pady=4)
        Label(win, textvariable=rgba_var, font=("Segoe UI", 10)).grid(row=0, column=1, padx=4, sticky="w")

        # Alpha control
        Label(win, text="Alpha (0-255):").grid(row=1, column=0, sticky="e", pady=6)
        alpha_entry = Entry(win, textvariable=alpha_var, width=5)
        alpha_entry.grid(row=1, column=1, sticky="w")

        # Color picker button
        Button(win, text="Pick Color", command=choose_color).grid(row=2, column=0, columnspan=2, pady=8)

        # Action buttons
        Button(win, text="OK", command=apply_changes, width=10).grid(row=3, column=0, pady=6)
        Button(win, text="Cancel", command=win.destroy, width=10).grid(row=3, column=1, pady=6)

        update_preview()
        win.wait_window()


    def _delete_key_pair(self, key):
        if key["angle"] in self.fixed_angles:
            return
        tgt = self._get_symmetric_angle(key["angle"])
        angs = {round(key["angle"], 2), round(tgt, 2)}
        self.keys = [k for k in self.keys if round(k["angle"], 2) not in angs]
        self._draw()
        self._save_if_needed()

    def _draw(self):
        self.delete("all")
        w, h = int(self['width']), int(self['height'])

        # Shrink radius to make room for labels
        self.radius = min(w, h) // 2 - 130
        self.center = (w // 2, h // 2)
        cx, cy = self.center

        label_offset = 30  # space between circle and label

        self.create_oval(cx - self.radius, cy - self.radius,
                        cx + self.radius, cy + self.radius, outline="black")

        for k in self.keys:
            a = k["angle"]
            r = math.radians(a)
            x1, y1 = cx, cy
            x2 = cx + self.radius * math.sin(r)
            y2 = cy - self.radius * math.cos(r)
            col = "#%02x%02x%02x" % k["color"][:3]

            hl = False
            if self.hover_key and (
                abs((a - self.hover_key["angle"]) % 360) < 0.01 or
                abs((a - self._get_symmetric_angle(self.hover_key["angle"])) % 360) < 0.01):
                hl = True
            if self.selected_key and (
                abs((a - self.selected_key["angle"]) % 360) < 0.01 or
                abs((a - self._get_symmetric_angle(self.selected_key["angle"])) % 360) < 0.01):
                hl = True

            self.create_line(x1, y1, x2, y2,
                            fill="yellow" if hl else col,
                            width=10 if hl else 6)

            # Label position
            lx = cx + (self.radius + label_offset) * math.sin(r)
            ly = cy - (self.radius + label_offset) * math.cos(r)

            # Clamp to inside the canvas (for safety)
            lx = max(6, min(w - 6, lx))
            ly = max(6, min(h - 6, ly))

            # Anchor based on position
            if 45 <= a <= 135:
                anchor = 'w'
            elif 225 <= a <= 315:
                anchor = 'e'
            elif 135 < a < 225:
                anchor = 'n'
            else:
                anchor = 's'

            rgba_label = f"{k['color'][0]},{k['color'][1]},{k['color'][2]},{k['color'][3]}"
            self.create_text(lx, ly, text=rgba_label,
                            fill="white", font=("Segoe UI", 14, "bold"), anchor=anchor)


    def load_keys(self, key_list):
        self.keys, seen = [], set()
        for k in key_list:
            if isinstance(k, list) and len(k) == 2:
                a, rgba = k
                a %= 360
                if round(a, 2) not in seen:
                    self.keys.append({"angle": a, "color": tuple(rgba)})
                    seen.add(round(a, 2))
        self._ensure_fixed_keys_exist()
        self._draw()
        self._save_if_needed()

    def get_keys(self):
        return [[round(k["angle"], 2), list(k["color"])]
                for k in sorted(self.keys, key=lambda x: x["angle"])]

    def _save_if_needed(self):
        if callable(self.on_change_callback):
            self.on_change_callback(self.get_keys())

