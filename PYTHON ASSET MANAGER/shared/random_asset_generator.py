import os
import json
import tkinter as tk
from tkinter import ttk, messagebox
import random
from collections import Counter

SRC_DIR = "SRC"

class RandomAssetGenerator(tk.Toplevel):
    def __init__(self, parent, current_assets, callback):
        super().__init__(parent)
        self.title("Random Asset Generator")
        self.geometry("600x500")
        self.current_assets = current_assets
        self.callback = callback

        container = ttk.Frame(self)
        container.pack(fill=tk.BOTH, expand=True)

        canvas = tk.Canvas(container)
        scrollbar = ttk.Scrollbar(container, orient="vertical", command=canvas.yview)
        scrollable_frame = ttk.Frame(canvas)

        scrollable_frame.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )

        canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)

        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        self.keep_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(scrollable_frame, text="Keep current assets", variable=self.keep_var).pack(anchor="w", padx=10, pady=5)

        form = ttk.Frame(scrollable_frame)
        form.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

        ttk.Label(form, text="Total number of assets to return:").pack(anchor="w")
        self.num_assets_entry = ttk.Entry(form)
        self.num_assets_entry.insert(0, "10")
        self.num_assets_entry.pack(fill=tk.X, pady=2)

        self._create_tag_input_section(form, "Tags to prioritize (comma separated):", is_priority=True)
        self._create_tag_input_section(form, "Tags to exclude (comma separated):", is_priority=False)

        ttk.Label(form, text="Priority strength (0 = random, 1 = strict):").pack(anchor="w", pady=(10, 0))
        self.priority_strength = tk.DoubleVar(value=0.5)
        ttk.Scale(form, from_=0, to=1, orient="horizontal", variable=self.priority_strength).pack(fill=tk.X, pady=2)

        ttk.Button(scrollable_frame, text="Generate List", command=self._generate).pack(pady=10)

    def _create_tag_input_section(self, parent, label, is_priority):
        frame = ttk.Frame(parent)
        frame.pack(fill=tk.BOTH, expand=True, pady=(10, 0))

        ttk.Label(frame, text=label).pack(anchor="w")
        inner_frame = ttk.Frame(frame)
        inner_frame.pack(fill=tk.BOTH, expand=True)

        text_box = tk.Text(inner_frame, height=2, width=50)
        text_box.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        list_frame = ttk.Frame(inner_frame)
        list_frame.pack(side=tk.LEFT, fill=tk.Y, padx=(5, 0))

        tags = self._gather_sorted_tags()
        ttk.Label(list_frame, text="Common Tags").pack()
        for tag, _ in tags:
            btn = ttk.Button(list_frame, text=tag, width=20,
                            command=lambda t=tag, tb=text_box: self._insert_tag(tb, t))
            btn.pack(anchor="w", pady=1)

        if is_priority:
            self.priority_tags_text = text_box
        else:
            self.exclude_tags_text = text_box

    def _insert_tag(self, textbox, tag):
        existing = textbox.get("1.0", tk.END).strip()
        tags = [t.strip() for t in existing.split(",") if t.strip()]
        if tag not in tags:
            tags.append(tag)
        textbox.delete("1.0", tk.END)
        textbox.insert("1.0", ", ".join(tags))

    def _gather_sorted_tags(self):
        tag_counter = Counter()
        for asset in os.listdir(SRC_DIR):
            info_path = os.path.join(SRC_DIR, asset, "info.json")
            if not os.path.isfile(info_path):
                continue
            try:
                with open(info_path) as f:
                    info = json.load(f)
                for tag in info.get("tags", []):
                    tag_counter[tag.strip().lower()] += 1
            except:
                continue
        return tag_counter.most_common()

    def _generate(self):
        try:
            total_count = int(self.num_assets_entry.get())
        except ValueError:
            messagebox.showerror("Invalid Input", "Please enter a valid number of assets.")
            return

        keep_existing = self.keep_var.get()
        existing_names = [a["name"] for a in self.current_assets] if keep_existing else []
        needed = max(0, total_count - len(existing_names))

        priority_tags = [t.strip().lower() for t in self.priority_tags_text.get("1.0", tk.END).split(",") if t.strip()]
        exclude_tags = [t.strip().lower() for t in self.exclude_tags_text.get("1.0", tk.END).split(",") if t.strip()]
        strength = self.priority_strength.get()

        all_assets = [name for name in os.listdir(SRC_DIR)
                      if os.path.isdir(os.path.join(SRC_DIR, name)) and name not in existing_names]

        valid_assets = []
        for asset in all_assets:
            info_path = os.path.join(SRC_DIR, asset, "info.json")
            if not os.path.exists(info_path):
                continue
            try:
                with open(info_path) as f:
                    info = json.load(f)
                if info.get("asset_type") == "Player":
                    continue
                tags = [t.lower() for t in info.get("tags", [])]
                if any(tag in exclude_tags for tag in tags):
                    continue
                score = sum(1 for tag in tags if tag in priority_tags)
                valid_assets.append((asset, score))
            except:
                continue

        valid_assets.sort(key=lambda x: x[1], reverse=True)

        cutoff = int(needed * strength)
        chosen = []
        seen = set(existing_names)

        for name, _ in valid_assets[:cutoff]:
            if name not in seen:
                chosen.append(name)
                seen.add(name)

        remaining = [name for name, _ in valid_assets[cutoff:] if name not in seen]
        random.shuffle(remaining)

        for name in remaining:
            if len(chosen) >= needed:
                break
            chosen.append(name)
            seen.add(name)

        final_assets = []
        existing_lookup = {a["name"]: a for a in self.current_assets}

        for name in existing_names:
            if name in existing_lookup and name not in {a["name"] for a in final_assets}:
                final_assets.append(existing_lookup[name])

        spawn_min, spawn_max = 200, 800
        json_path = os.path.join("MAPS", "map_info.json")
        try:
            with open(json_path) as f:
                info = json.load(f)
                spawn_min = info.get("rnd_spawn_min")
                spawn_max = info.get("rnd_spawn_max")
        except:
            pass

        for name in chosen:
            if name not in {a["name"] for a in final_assets}:
                info_path = os.path.join(SRC_DIR, name, "info.json")
                try:
                    with open(info_path) as f:
                        info = json.load(f)
                        spawn_min = info.get("rnd_spawn_min", 200)
                        spawn_max = info.get("rnd_spawn_max", 800)
                except:
                    spawn_min, spawn_max = 200, 800

                while True:
                    min_number = random.randint(spawn_min, spawn_max)
                    max_number = random.randint(spawn_min, spawn_max)
                    if min_number <= max_number:
                        break

                final_assets.append({
                    "name": name,
                    "min_number": min_number,
                    "max_number": max_number,
                    "position": None,
                    "exact_position": None
                })

        self.callback(final_assets)
        self.destroy()

