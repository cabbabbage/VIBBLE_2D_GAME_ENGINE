# === File: pages/tags.py ===
import os
import json
import tkinter as tk
from tkinter import messagebox
from collections import Counter, defaultdict
from pages.range import Range

SRC_DIR = "SRC"

class TagsPage(tk.Frame):
    def __init__(self, parent):
        super().__init__(parent, bg='#1e1e1e')
        self.json_path = None
        self.data = {}
        self.asset_tag_map = {}
        self.tag_usage = {}
        self.recommended_buttons = []
        self.anti_recommended_buttons = []
        self._loaded = False

        # Title header
        title = tk.Label(
            self, text="Tags", font=("Segoe UI", 20, "bold"),
            fg="#005f73", bg='#1e1e1e'
        )
        title.pack(fill=tk.X, pady=(10, 20))

        # Scrollable content area
        self.canvas = tk.Canvas(self, bg='#2a2a2a', highlightthickness=0)
        self.scroll_frame = tk.Frame(self.canvas, bg='#2a2a2a')
        window_id = self.canvas.create_window((0, 0), window=self.scroll_frame, anchor='nw')
        # update scrollregion
        self.scroll_frame.bind(
            '<Configure>',
            lambda e: self.canvas.configure(scrollregion=self.canvas.bbox('all'))
        )
        # make scroll_frame span full width
        self.canvas.bind(
            '<Configure>',
            lambda e: self.canvas.itemconfig(window_id, width=e.width)
        )
        # mouse-wheel scrolling
        self.scroll_frame.bind(
            '<Enter>', lambda e: self.canvas.bind_all('<MouseWheel>', self._on_mousewheel)
        )
        self.scroll_frame.bind(
            '<Leave>', lambda e: self.canvas.unbind_all('<MouseWheel>')
        )
        self.canvas.pack(fill=tk.BOTH, expand=True)

        # Section: Tags input
        hdr1 = tk.Label(
            self.scroll_frame, text="Tags (comma separated):",
            font=("Segoe UI", 13, "bold"), fg="#DDDDDD", bg='#2a2a2a'
        )
        hdr1.pack(anchor='w', padx=12, pady=(10, 4))
        self.text = tk.Text(
            self.scroll_frame, height=4, wrap='word',
            font=("Segoe UI", 12), bg='#2a2a2a', fg='#FFFFFF'
        )
        self.text.pack(fill=tk.X, padx=12, pady=(0, 10))
        self.text.bind('<KeyRelease>', lambda e: self._autosave())

        # Section: Anti-Tags input
        hdr2 = tk.Label(
            self.scroll_frame, text="Anti-Tags (comma separated):",
            font=("Segoe UI", 13, "bold"), fg="#DDDDDD", bg='#2a2a2a'
        )
        hdr2.pack(anchor='w', padx=12, pady=(10, 4))
        self.anti_text = tk.Text(
            self.scroll_frame, height=2, wrap='word',
            font=("Segoe UI", 12), bg='#2a2a2a', fg='#FFFFFF'
        )
        self.anti_text.pack(fill=tk.X, padx=12, pady=(0, 10))
        self.anti_text.bind('<KeyRelease>', lambda e: self._autosave())

        # Section: Recommended Tags
        hdr3 = tk.Label(
            self.scroll_frame, text="Recommended Tags:",
            font=("Segoe UI", 13, "bold"), fg="#DDDDDD", bg='#2a2a2a'
        )
        hdr3.pack(anchor='w', padx=12, pady=(10, 4))
        self.common_canvas = tk.Canvas(
            self.scroll_frame, height=300, bg='#2a2a2a', highlightthickness=0
        )
        self.common_inner = tk.Frame(self.common_canvas, bg='#2a2a2a')
        common_id = self.common_canvas.create_window((0, 0), window=self.common_inner, anchor='nw')
        self.common_inner.bind(
            '<Configure>',
            lambda e: self.common_canvas.configure(scrollregion=self.common_canvas.bbox('all'))
        )
        self.common_canvas.bind(
            '<Configure>',
            lambda e: self.common_canvas.itemconfig(common_id, width=e.width)
        )
        self.common_inner.bind('<Enter>', lambda e: self.common_canvas.bind_all('<MouseWheel>', self._on_mousewheel_inner))
        self.common_inner.bind('<Leave>', lambda e: self.common_canvas.unbind_all('<MouseWheel>'))
        self.common_canvas.pack(fill=tk.X, padx=12, pady=(0, 10))

        # Section: Recommended Anti-Tags
        hdr4 = tk.Label(
            self.scroll_frame, text="Recommended Anti-Tags:",
            font=("Segoe UI", 13, "bold"), fg="#DDDDDD", bg='#2a2a2a'
        )
        hdr4.pack(anchor='w', padx=12, pady=(10, 4))
        self.anti_frame = tk.Frame(self.scroll_frame, bg='#2a2a2a')
        self.anti_frame.pack(fill=tk.X, padx=12, pady=(0, 10))

        # Section: Spawn Range
        hdr5 = tk.Label(
            self.scroll_frame, text="Spawn Range", 
            font=("Segoe UI", 13, "bold"), fg="#DDDDDD", bg='#2a2a2a'
        )
        hdr5.pack(anchor='w', padx=12, pady=(10, 4))
        self.spawn_range = Range(
            self.scroll_frame, min_bound=0, max_bound=1000,
            set_min=500, set_max=500, label="Spawn Range"
        )
        self.spawn_range.pack(fill=tk.X, padx=12, pady=(0, 10))
        for v in (self.spawn_range.var_min, self.spawn_range.var_max, getattr(self.spawn_range, 'var_random', None)):
            if v:
                v.trace_add('write', lambda *_: self._autosave())

        self._loaded = True

    def _on_mousewheel(self, event):
        self.canvas.yview_scroll(int(-1 * (event.delta / 120)), 'units')

    def _on_mousewheel_inner(self, event):
        self.common_canvas.yview_scroll(int(-1 * (event.delta / 120)), 'units')

    def load(self, json_path):
        self.json_path = json_path
        if not os.path.exists(json_path):
            return
        with open(json_path) as f:
            self.data = json.load(f)

        self.text.delete('1.0', tk.END)
        self.text.insert(tk.END, ', '.join(self.data.get('tags', [])))
        self.anti_text.delete('1.0', tk.END)
        self.anti_text.insert(tk.END, ', '.join(self.data.get('anti_tags', [])))

        self.spawn_range.set(
            self.data.get('rnd_spawn_min', 300),
            self.data.get('rnd_spawn_max', 400)
        )

        self._scan_asset_tags()
        self._refresh_recommended_tags()
        self._loaded = True

    def _autosave(self):
        if not self._loaded or not self.json_path:
            return
        self.data['tags'] = [t.strip() for t in self.text.get('1.0', tk.END).split(',') if t.strip()]
        self.data['anti_tags'] = [t.strip() for t in self.anti_text.get('1.0', tk.END).split(',') if t.strip()]
        self.data['rnd_spawn_min'] = self.spawn_range.get_min()
        self.data['rnd_spawn_max'] = self.spawn_range.get_max()
        try:
            with open(self.json_path, 'w') as f:
                json.dump(self.data, f, indent=2)
        except Exception as e:
            messagebox.showerror('Save Failed', str(e))

    def _scan_asset_tags(self):
        self.asset_tag_map.clear()
        for folder in os.listdir(SRC_DIR):
            folder_path = os.path.join(SRC_DIR, folder)
            if not os.path.isdir(folder_path):
                continue
            info_path = os.path.join(folder_path, 'info.json')
            if os.path.exists(info_path):
                try:
                    with open(info_path) as f:
                        info = json.load(f)
                        tags = set(tag.strip().lower() for tag in info.get('tags', []) if isinstance(tag, str))
                        if tags:
                            self.asset_tag_map[folder] = tags
                except:
                    continue
        counter = Counter()
        for tags in self.asset_tag_map.values():
            counter.update(tags)
        self.tag_usage = dict(counter)

    def _get_current_tags(self):
        return set(tag.strip().lower() for tag in self.text.get('1.0', tk.END).split(',') if tag.strip())

    def _get_current_anti_tags(self):
        return set(tag.strip().lower() for tag in self.anti_text.get('1.0', tk.END).split(',') if tag.strip())

    def _find_similar_recommendations(self, limit=30, exclude=None):
        if exclude is None:
            exclude = set()
        current = self._get_current_tags()
        scores = defaultdict(int)
        for tags in self.asset_tag_map.values():
            shared = current & tags
            if shared:
                for tag in tags:
                    if tag not in current and tag not in exclude:
                        scores[tag] += len(shared)
        prioritized = sorted(scores.items(), key=lambda x: (-x[1], -self.tag_usage.get(x[0], 0)))
        recs = [tag for tag, _ in prioritized]
        used = set(recs) | current | exclude
        fill = [tag for tag, _ in sorted(self.tag_usage.items(), key=lambda x: -x[1]) if tag not in used]
        return (recs + fill)[:limit]

    def _find_similar_anti_recommendations(self, limit=10, exclude=None):
        if exclude is None:
            exclude = set()
        current_anti = self._get_current_anti_tags()
        current = self._get_current_tags()
        full_ex = current | current_anti | exclude
        scores = defaultdict(int)
        for folder in os.listdir(SRC_DIR):
            info_path = os.path.join(folder, 'info.json')
            try:
                with open(info_path) as f:
                    info = json.load(f)
                    antis = set(tag.strip().lower() for tag in info.get('anti_tags', []) if isinstance(tag, str))
                    overlap = current_anti & antis
                    if overlap:
                        for tag in antis:
                            if tag not in full_ex:
                                scores[tag] += len(overlap)
            except:
                continue
        prioritized = sorted(scores.items(), key=lambda x: (-x[1], -self.tag_usage.get(x[0], 0)))
        recs = [tag for tag, _ in prioritized]
        used = set(recs) | full_ex
        fill = [tag for tag, _ in sorted(self.tag_usage.items(), key=lambda x: -x[1]) if tag not in used]
        return (recs + fill)[:limit]

    def _refresh_recommended_tags(self):
        for b in self.recommended_buttons:
            b.destroy()
        self.recommended_buttons.clear()
        for b in self.anti_recommended_buttons:
            b.destroy()
        self.anti_recommended_buttons.clear()

        current = self._get_current_tags()
        anti_current = self._get_current_anti_tags()
        tag_recs = self._find_similar_recommendations(exclude=anti_current)
        anti_recs = self._find_similar_anti_recommendations(exclude=current)

        for idx, tag in enumerate(tag_recs):
            btn = tk.Button(
                self.common_inner, text=tag, width=20, height=2,
                font=("Segoe UI",10,"bold"),
                command=lambda t=tag: self._add_tag(t)
            )
            btn.grid(row=idx//5, column=idx%5, padx=4, pady=4, sticky='w')
            self.recommended_buttons.append(btn)

        for idx, tag in enumerate(anti_recs):
            btn = tk.Button(
                self.anti_frame, text=tag, width=20, height=2,
                font=("Segoe UI",10,"bold"),
                command=lambda t=tag: self._add_anti_tag(t)
            )
            btn.grid(row=idx//5, column=idx%5, padx=4, pady=4, sticky='w')
            self.anti_recommended_buttons.append(btn)

    def _add_tag(self, tag):
        tags = self._get_current_tags()
        if tag not in tags:
            tags.add(tag)
            self.text.delete('1.0', tk.END)
            self.text.insert('1.0', ', '.join(sorted(tags)))
            self._refresh_recommended_tags()
            self._autosave()

    def _add_anti_tag(self, tag):
        tags = self._get_current_anti_tags()
        if tag not in tags:
            tags.add(tag)
            self.anti_text.delete('1.0', tk.END)
            self.anti_text.insert('1.0', ', '.join(sorted(tags)))
            self._refresh_recommended_tags()
            self._autosave()
