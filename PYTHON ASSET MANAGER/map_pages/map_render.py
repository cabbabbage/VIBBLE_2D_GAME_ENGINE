# file: map_renderer.py

import os
import json
import math
import random
from colorsys import hsv_to_rgb


class MapRenderer:
    def __init__(self, rooms_dir, preview_canvas, preview_size, layer_widgets, factor):
        self.rooms_dir = rooms_dir
        self.preview_canvas = preview_canvas
        self.PREVIEW_SIZE = preview_size
        self.layer_widgets = layer_widgets
        self.factor = factor
        self._suspend_save = False
        self.layer_radii = []  # ✅ FIX: Always define this


    def calculate_radii(self):
        self._suspend_save = True
        try:
            areas_meta = []
            largest_sizes = []
            all_rooms = []
            global_room_lookup = {}

            def load_room_area(name):
                if name in global_room_lookup:
                    return global_room_lookup[name]

                p = os.path.join(self.rooms_dir, f"{name}.json")
                if not os.path.exists(p):
                    return None
                try:
                    with open(p, 'r') as f:
                        info = json.load(f)
                except Exception:
                    return None

                geo = info.get("geometry", "square").lower()
                w, h = info.get("max_width", 0), info.get("max_height", 0)

                area = {
                    "room_name": name,
                    "type": "circle" if geo == "circle" else "square",
                    "diameter": w if geo == "circle" else None,
                    "width": w if geo != "circle" else None,
                    "height": h if geo != "circle" else None,
                    "min_instances": 0,
                    "max_instances": 0,
                    "required_children": []
                }
                global_room_lookup[name] = area
                return area

            # First pass: collect rooms from each layer widget
            for layer in self.layer_widgets:
                for room_widget in layer.rooms:
                    name = room_widget.room_name
                    area = load_room_area(name)
                    if not area:
                        continue

                    area["min_instances"] = room_widget.min_var.get()
                    area["max_instances"] = room_widget.max_var.get()
                    area["required_children"] = getattr(room_widget, "required_children", [])

                    all_rooms.append(area)

                            # Second pass: resolve required children for each area
                for area in all_rooms:
                    resolved = []
                    for child in area.get("required_children", []):
                        if isinstance(child, dict):
                            resolved.append(child)
                        else:
                            child_area = load_room_area(child)
                            if child_area:
                                resolved.append(child_area)
                    area["required_children"] = resolved




                

            # Build areas_meta by layer and find max size for radius calculation (including required children)
            for layer in self.layer_widgets:
                layer_meta = []
                max_span = 0
                required_areas = []
                for room_widget in layer.rooms:
                    area = global_room_lookup.get(room_widget.room_name)
                    if not area:
                        continue

                    # Add base area
                    layer_meta.append(area)

                    # Track max span
                    if area["type"] == "circle":
                        span = area["diameter"]
                    else:
                        span = math.hypot(area["width"], area["height"])
                    max_span = max(max_span, span)

                    # Include required children in next layer's size consideration
                    for child in area.get("required_children", []):
                        if child["type"] == "circle":
                            c_span = child["diameter"]
                        else:
                            c_span = math.hypot(child["width"], child["height"])
                        max_span = max(max_span, c_span)

                largest_sizes.append(max_span)
                areas_meta.append(layer_meta)


            # Debug print
            for idx, layer in enumerate(areas_meta):
                for room in layer:
                    print(f"[DEBUG] Layer {idx} Room '{room['room_name']}' required_children: {[c['room_name'] for c in room.get('required_children', [])]}")

            if not largest_sizes or not areas_meta or all(s == 0 for s in largest_sizes):
                print("No valid room geometry found for radius calculation.")
                return

            radii = []
            prev_r = 0
            for i, size in enumerate(largest_sizes):
                if i == 0:
                    r = 0
                else:
                    r = prev_r + 1.2 * ((size / 2.0) + (largest_sizes[i - 1] / 2.0))

                radii.append(r)

                prev_r = r

            self.layer_radii = radii
            for i, layer in enumerate(self.layer_widgets):
                if i < len(radii):
                    layer.set_radius(radii[i])

            # We need roominfo_lookup for required_children info, assume from widgets:
            roominfo_lookup = {}
            for layer in self.layer_widgets:
                for room_widget in layer.rooms:
                    roominfo_lookup[room_widget.room_name] = room_widget
            map_radius = ((radii[-1] + (largest_sizes[-1] / 2.0) + 1500))
            self.render_preview(radii, areas_meta, map_radius, global_room_lookup, roominfo_lookup)

        finally:
            self._suspend_save = False


    def room_type_to_color(self, name):
        hue = (hash(name) % 360) / 360.0
        r, g, b = hsv_to_rgb(hue, 0.4, 0.95)
        return '#{:02x}{:02x}{:02x}'.format(int(r*255), int(g*255), int(b*255))

    def render_preview(self, radii, areas_meta, map_radius_actual, global_room_lookup, roominfo_lookup):
        print("Generating Map Preview:")
        C = self.PREVIEW_SIZE // 2
        self.preview_canvas.delete("all")
        scale = C / map_radius_actual
        self.map_radius_actual = map_radius_actual

        self.preview_canvas.create_oval(0, 0, self.PREVIEW_SIZE, self.PREVIEW_SIZE, outline="black", width=3)
        self.preview_canvas.create_text(C, self.PREVIEW_SIZE - 20, text=f"Map radius: {int(map_radius_actual)}", font=("Segoe UI", 12, "bold"))

        # Root layer, single root room
        if not areas_meta or not areas_meta[0]:
            print("[ERROR] No root room found for preview rendering.")
            return

        root_area = areas_meta[0][0]
        parents = [{
            "pos": (C, C),
            "layer": 0,
            "area": root_area,
            "sector_start": 0.0,
            "sector_size": 2 * math.pi
        }]
        placed_areas = [parents[0]]
        self._draw_area(C, C, root_area, self.room_type_to_color(root_area["room_name"]), self._ring_color(0), scale)

        for i in range(1, len(radii)):
            ring_px = radii[i] * scale
            col = self._ring_color(i)
            next_meta = areas_meta[i]
            self.preview_canvas.create_oval(C - ring_px, C - ring_px, C + ring_px, C + ring_px, outline=col, width=2)

            layer_widget = self.layer_widgets[i]
            min_rooms = layer_widget.min_rooms_var.get()
            max_rooms = layer_widget.max_rooms_var.get()
            room_pool = self.get_children_from_layer(next_meta, min_rooms, max_rooms)

            new_parents = []

            if i == 1:
                # Special case: evenly space all children individually on layer 1
                combined_rooms = []

                for parent in parents:
                    parent_name = parent["area"]["room_name"]
                    roominfo = roominfo_lookup.get(parent_name)
                    if roominfo:
                        for name in getattr(roominfo, "required_children", []):
                            area = global_room_lookup.get(name)
                            if area:
                                combined_rooms.append(area)

                combined_rooms.extend(room_pool)
                random.shuffle(combined_rooms)
                if not combined_rooms:
                    continue

                slice_size = 2 * math.pi / len(combined_rooms)
                for idx, room in enumerate(combined_rooms):
                    angle = idx * slice_size
                    cx = C + ring_px * math.cos(angle)
                    cy = C + ring_px * math.sin(angle)

                    fill = self.room_type_to_color(room["room_name"])
                    self._draw_area(cx, cy, room, fill, col, scale)
                    self.preview_canvas.create_line(C, C, cx, cy, fill="black")

                    new_parents.append({
                        "pos": (cx, cy),
                        "layer": i,
                        "area": room,
                        "sector_start": angle - slice_size / 2,
                        "sector_size": slice_size
                    })
                    placed_areas.append({"pos": (cx, cy), "area": room})
            else:
                # General case for deeper layers
                parent_to_children = {}
                for parent in parents:
                    required = parent["area"].get("required_children", [])
                    parent_to_children[parent["pos"]] = required[:]


                # Distribute optional children fairly
                assignments = room_pool[:]
                parent_counts = {pos: len(children) for pos, children in parent_to_children.items()}

                while assignments:
                    min_count = min(parent_counts.values())
                    eligible = [pos for pos in parent_to_children if parent_counts[pos] == min_count]
                    for pos in eligible:
                        if not assignments:
                            break
                        room = assignments.pop()
                        parent_to_children[pos].append(room)
                        parent_counts[pos] += 1

                # Spawn children per parent
                for parent in parents:
                    p_x, p_y = parent["pos"]
                    sector_start = parent["sector_start"]
                    sector_size = parent["sector_size"]

                    children = parent_to_children[parent["pos"]]
                    random.shuffle(children)
                    print(f"[DEBUG] Parent '{parent['area']['room_name']}' has children: {[r['room_name'] for r in children]}")

                    if not children:
                        continue

                    slice_size = sector_size / len(children)
                    buffer_angle = slice_size * 0.05

                    for idx, room in enumerate(children):
                        fill = self.room_type_to_color(room["room_name"])
                        child_sector_start = sector_start + idx * slice_size + buffer_angle
                        child_sector_size = slice_size - 2 * buffer_angle

                        placed = False
                        for attempt in range(100):
                            angle = child_sector_start + random.random() * child_sector_size
                            cx = C + ring_px * math.cos(angle)
                            cy = C + ring_px * math.sin(angle)
                            if not self._does_overlap(cx, cy, room, placed_areas, scale):
                                placed = True
                                break
                        if not placed:
                            print(f"[DEBUG] Failed to place room '{room['room_name']}' on layer {i} due to overlap.")
                            continue

                        self._draw_area(cx, cy, room, fill, col, scale)
                        self.preview_canvas.create_line(p_x, p_y, cx, cy, fill="black")
                        new_parents.append({
                            "pos": (cx, cy),
                            "layer": i,
                            "area": room,
                            "sector_start": child_sector_start,
                            "sector_size": child_sector_size
                        })
                        placed_areas.append({"pos": (cx, cy), "area": room})

            parents = new_parents

        # Draw Color Legend
        legend_items = {}
        for placed in placed_areas:
            name = placed["area"]["room_name"]
            legend_items[name] = self.room_type_to_color(name)

        sorted_items = sorted(legend_items.items())
        key_y = self.PREVIEW_SIZE - 45
        key_x = 20
        box_size = 12
        spacing = 100

        for name, color in sorted_items:
            if key_x + spacing > self.PREVIEW_SIZE - 100:
                key_x = 20
                key_y -= 20
            self.preview_canvas.create_rectangle(key_x, key_y, key_x + box_size, key_y + box_size, fill=color, outline="black")
            self.preview_canvas.create_text(key_x + box_size + 5, key_y + box_size // 2, text=name, anchor="w", font=("Segoe UI", 9))
            key_x += spacing



    def get_children_from_layer(self, next_meta, min_rooms, max_rooms):
        # Calculate total number of rooms to generate
        target = random.randint(min_rooms, max_rooms)

        # Start with minimum required rooms
        pool = []
        expandable = []
        for room in next_meta:
            pool.extend([room] * room["min_instances"])
            extra = room["max_instances"] - room["min_instances"]
            expandable.extend([room] * extra)

        # Add more from expandable pool until target met
        while len(pool) < target and expandable:
            choice = random.choice(expandable)
            pool.append(choice)
            expandable.remove(choice)

        random.shuffle(pool)
        return pool


    def _draw_area(self, x, y, area, color_fill, color_outline, scale):
        if area["type"] == "circle":
            d = area["diameter"] * scale
            self.preview_canvas.create_oval(x - d / 2, y - d / 2, x + d / 2, y + d / 2, fill=color_fill, outline=color_outline)
        else:
            w = area["width"] * scale
            h = area["height"] * scale
            self.preview_canvas.create_rectangle(x - w / 2, y - h / 2, x + w / 2, y + h / 2, fill=color_fill, outline=color_outline)

    def _does_overlap(self, x, y, area, placed, scale):
        buffer_radius = 300 * scale
        r1 = area["diameter"] * scale / 2 if area["type"] == "circle" else math.hypot(area["width"], area["height"]) * scale / 2
        for other in placed:
            ox, oy = other["pos"]
            oarea = other["area"]
            r2 = oarea["diameter"] * scale / 2 if oarea["type"] == "circle" else math.hypot(oarea["width"], oarea["height"]) * scale / 2
            if math.hypot(x - ox, y - oy) < r1 + r2 + buffer_radius:
                return True
        return False

    def _ring_color(self, lvl):
        hue = (lvl * 0.13) % 1.0
        r, g, b = hsv_to_rgb(hue, 0.6, 1.0)
        return f'#{int(r*255):02x}{int(g*255):02x}{int(b*255):02x}'

