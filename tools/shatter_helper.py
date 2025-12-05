import os
import math
import random
from dataclasses import dataclass
from typing import Dict, List, Tuple

import tkinter as tk
from tkinter import filedialog

from PIL import Image


# ------------------------------------------------------------
# Config defaults
# ------------------------------------------------------------

FRAME_COUNT_DEFAULT = 40
ALPHA_THRESHOLD = 1
MAX_SPEED = 5.0
GRAVITY = 0.5
PARTICLE_COUNT = 80
PARTICLE_GRAVITY = 0.15


@dataclass
class Pixel:
    x: int
    y: int
    color: Tuple[int, int, int, int]
    shard_id: int


@dataclass
class ShardMotion:
    vx: float
    vy: float
    t_hit: int


@dataclass
class Particle:
    x: float
    y: float
    vx: float
    vy: float
    life: int
    max_life: int


# ------------------------------------------------------------
# Image and shard helpers
# ------------------------------------------------------------

def extract_pixels_and_shards(
    img: Image.Image,
    approx_shards: int,
) -> Tuple[List[Pixel], Dict[int, Tuple[float, float]], Dict[int, int]]:
    """
    Extract non transparent pixels, assign them to shards using a grid whose
    cell size is derived from approx_shards, and compute shard centers and
    bottom y for each shard.
    """
    img = img.convert("RGBA")
    w, h = img.size
    pixels_raw = img.load()

    # First pass: find bounding box of solid pixels
    min_x, min_y = w, h
    max_x, max_y = -1, -1

    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels_raw[x, y]
            if a <= ALPHA_THRESHOLD:
                continue
            if x < min_x:
                min_x = x
            if y < min_y:
                min_y = y
            if x > max_x:
                max_x = x
            if y > max_y:
                max_y = y

    if max_x < min_x or max_y < min_y:
        # no solid pixels
        return [], {}, {}

    eff_w = max_x - min_x + 1
    eff_h = max_y - min_y + 1

    approx_shards = max(1, approx_shards)
    # approximate cell area to get about approx_shards cells over non transparent region
    cell_area = max(1.0, (eff_w * eff_h) / float(approx_shards))
    cell_size = max(1, int(round(math.sqrt(cell_area))))

    num_cells_x = max(1, math.ceil(eff_w / cell_size))
    num_cells_y = max(1, math.ceil(eff_h / cell_size))

    pixels: List[Pixel] = []
    shard_accum: Dict[int, Tuple[float, float, int]] = {}
    shard_bottoms: Dict[int, int] = {}

    # Second pass: assign shards and collect stats
    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels_raw[x, y]
            if a <= ALPHA_THRESHOLD:
                continue

            cell_x = (x - min_x) // cell_size
            cell_y = (y - min_y) // cell_size
            if cell_x < 0:
                cell_x = 0
            if cell_y < 0:
                cell_y = 0
            if cell_x >= num_cells_x:
                cell_x = num_cells_x - 1
            if cell_y >= num_cells_y:
                cell_y = num_cells_y - 1

            shard_id = cell_y * num_cells_x + cell_x

            pixels.append(Pixel(x=x, y=y, color=(r, g, b, a), shard_id=shard_id))

            sx, sy, c = shard_accum.get(shard_id, (0.0, 0.0, 0))
            shard_accum[shard_id] = (sx + x, sy + y, c + 1)

            bottom = shard_bottoms.get(shard_id, y)
            if y > bottom:
                shard_bottoms[shard_id] = y
            else:
                shard_bottoms.setdefault(shard_id, y)

    # compute shard centers
    shard_centers: Dict[int, Tuple[float, float]] = {}
    for sid, (sx, sy, c) in shard_accum.items():
        if c > 0:
            shard_centers[sid] = (sx / c, sy / c)

    return pixels, shard_centers, shard_bottoms


def build_shard_motions(
    shard_centers: Dict[int, Tuple[float, float]],
    shard_bottoms: Dict[int, int],
    img_w: int,
    img_h: int,
    origin_x: int,
    origin_y: int,
    frame_count: int,
    direction: str,
) -> Dict[int, ShardMotion]:
    """
    Create motion parameters for each shard: horizontal velocity, vertical
    velocity and the frame at which the shard hits the ground.
    All shards will be at or on the ground by the last frame.
    """
    cx = img_w / 2.0
    cy = img_h / 2.0
    ground_y = origin_y + img_h - 1

    motions: Dict[int, ShardMotion] = {}

    if frame_count <= 1:
        min_hit = max_hit = 1
    else:
        min_hit = max(1, int(frame_count * 0.6))
        max_hit = frame_count - 1 if frame_count > 1 else 1

    for sid, (sx, sy) in shard_centers.items():
        # radial direction (from sprite center)
        rx = sx - cx
        ry = sy - cy
        rlen = math.hypot(rx, ry) or 1.0
        rx /= rlen
        ry /= rlen

        # base directional bias
        if direction == "left":
            base_x, base_y = -1.0, 0.1
        elif direction == "forward":
            base_x, base_y = 0.0, 1.0
        elif direction == "backward":
            base_x, base_y = 0.6, -0.3
        else:
            base_x, base_y = 0.0, 1.0

        mix = 0.6
        dir_x = base_x * mix + rx * (1.0 - mix)
        dir_y = base_y * mix + ry * (1.0 - mix)

        dir_x += random.uniform(-0.2, 0.2)
        dir_y += random.uniform(-0.2, 0.2)

        dlen = math.hypot(dir_x, dir_y) or 1.0
        dir_x /= dlen
        dir_y /= dlen

        speed = random.uniform(MAX_SPEED * 0.4, MAX_SPEED)
        vx = dir_x * speed

        # choose when this shard hits the ground
        t_hit = random.randint(min_hit, max_hit) if max_hit >= min_hit else max_hit

        # bottom pixel of this shard in image space
        bottom_y_local = shard_bottoms.get(sid, int(round(sy)))
        y0_bottom_world = origin_y + bottom_y_local

        # solve for vy such that bottom pixel is at the ground at t_hit:
        # y0 + vy * t + 0.5 * g * t^2 = ground_y
        # => vy = (ground_y - y0 - 0.5 * g * t^2) / t
        vy = (ground_y - y0_bottom_world - 0.5 * GRAVITY * (t_hit ** 2)) / float(t_hit)

        motions[sid] = ShardMotion(vx=vx, vy=vy, t_hit=t_hit)

    return motions


# ------------------------------------------------------------
# Particle helpers
# ------------------------------------------------------------

def create_particles(
    origin_x: int,
    origin_y: int,
    img_w: int,
    img_h: int,
    frame_count: int,
    direction: str,
) -> List[Particle]:
    particles: List[Particle] = []

    cx = origin_x + img_w / 2.0
    cy = origin_y + img_h / 2.0

    for _ in range(PARTICLE_COUNT):
        px = cx + random.uniform(-img_w * 0.4, img_w * 0.4)
        py = cy + random.uniform(-img_h * 0.2, img_h * 0.2)

        if direction == "left":
            base_vx = random.uniform(-2.0, -0.2)
        elif direction == "forward":
            base_vx = random.uniform(-0.5, 0.5)
        else:
            base_vx = random.uniform(0.2, 2.0)

        base_vy = random.uniform(-1.5, 0.5)

        life = random.randint(int(frame_count * 0.4), frame_count)

        particles.append(
            Particle(
                x=px,
                y=py,
                vx=base_vx,
                vy=base_vy,
                life=life,
                max_life=life,
            )
        )

    return particles


def update_particles(particles: List[Particle]):
    for p in particles:
        if p.life <= 0:
            continue
        p.x += p.vx
        p.y += p.vy
        p.vy += PARTICLE_GRAVITY
        p.life -= 1


def draw_particles(frame: Image.Image, particles: List[Particle]):
    draw_pixels = frame.load()
    w, h = frame.size

    for p in particles:
        if p.life <= 0:
            continue
        ix = int(round(p.x))
        iy = int(round(p.y))
        if ix < 0 or iy < 0 or iy >= h or ix >= w:
            continue

        alpha = int(255 * (p.life / p.max_life))
        r, g, b = 255, 255, 255
        dr, dg, db, da = draw_pixels[ix, iy]
        out_a = alpha + da * (255 - alpha) // 255
        if out_a == 0:
            draw_pixels[ix, iy] = (0, 0, 0, 0)
        else:
            out_r = (r * alpha + dr * da * (255 - alpha) // 255) // max(out_a, 1)
            out_g = (g * alpha + dg * da * (255 - alpha) // 255) // max(out_a, 1)
            out_b = (b * alpha + db * da * (255 - alpha) // 255) // max(out_a, 1)
            draw_pixels[ix, iy] = (int(out_r), int(out_g), int(out_b), int(out_a))


# ------------------------------------------------------------
# Animation rendering
# ------------------------------------------------------------

def compute_canvas_size(img_w: int, img_h: int, frame_count: int) -> Tuple[int, int, int, int]:
    """
    Compute a canvas size that expands left, right and up but not down.
    The bottom of the original image is treated as the ground and stays
    at the bottom edge of the canvas.
    Returns (canvas_w, canvas_h, origin_x, origin_y).
    """
    max_horizontal_disp = MAX_SPEED * frame_count
    margin_x = int(max_horizontal_disp + img_w * 0.5)

    # generous room above for shards to fly upward
    max_vertical_up = 0.5 * GRAVITY * (frame_count ** 2) + img_h
    margin_top = int(max_vertical_up)

    canvas_w = img_w + 2 * margin_x
    canvas_h = img_h + margin_top

    origin_x = margin_x
    origin_y = margin_top  # bottom of image sits on bottom of canvas

    return canvas_w, canvas_h, origin_x, origin_y


def render_animation(
    pixels: List[Pixel],
    shard_motions: Dict[int, ShardMotion],
    img_w: int,
    img_h: int,
    canvas_w: int,
    canvas_h: int,
    origin_x: int,
    origin_y: int,
    frame_count: int,
    direction: str,
    out_dir: str,
):
    os.makedirs(out_dir, exist_ok=True)

    particles = create_particles(origin_x, origin_y, img_w, img_h, frame_count, direction)

    for frame_idx in range(frame_count):
        frame = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))
        frame_pixels = frame.load()

        t = frame_idx

        for p in pixels:
            motion = shard_motions.get(p.shard_id)
            if motion is None:
                continue

            # shard falls until t_hit, then stays on the ground
            t_eff = min(t, motion.t_hit)
            dx = motion.vx * t
            dy = motion.vy * t_eff + 0.5 * GRAVITY * (t_eff ** 2)

            nx = int(round(origin_x + p.x + dx))
            ny = int(round(origin_y + p.y + dy))

            if 0 <= nx < canvas_w and 0 <= ny < canvas_h:
                frame_pixels[nx, ny] = p.color

        update_particles(particles)
        draw_particles(frame, particles)

        frame_name = f"frame_{frame_idx:03d}.png"
        frame_path = os.path.join(out_dir, frame_name)
        frame.save(frame_path)


# ------------------------------------------------------------
# Simple Tk settings dialog
# ------------------------------------------------------------

def ask_animation_settings(root: tk.Tk) -> Tuple[List[str], int, int]:
    """
    Show a minimal UI to select directions, shard count and frame count.
    Returns (directions, shard_count, frame_count).
    """
    config = {"directions": [], "shard_count": 0, "frame_count": 0}

    win = tk.Toplevel(root)
    win.title("Breaking animation settings")
    win.resizable(False, False)

    left_var = tk.BooleanVar(value=True)
    forward_var = tk.BooleanVar(value=True)
    backward_var = tk.BooleanVar(value=False)

    shard_var = tk.StringVar(value="120")
    frames_var = tk.StringVar(value=str(FRAME_COUNT_DEFAULT))

    status_var = tk.StringVar(value="")

    row = 0
    tk.Label(win, text="Directions:").grid(row=row, column=0, sticky="w", padx=8, pady=(8, 2))
    row += 1

    tk.Checkbutton(win, text="Left", variable=left_var).grid(row=row, column=0, sticky="w", padx=16)
    row += 1
    tk.Checkbutton(win, text="Forward", variable=forward_var).grid(row=row, column=0, sticky="w", padx=16)
    row += 1
    tk.Checkbutton(win, text="Backward", variable=backward_var).grid(row=row, column=0, sticky="w", padx=16)
    row += 1

    tk.Label(win, text="Approx shard count:").grid(row=row, column=0, sticky="w", padx=8, pady=(8, 2))
    row += 1
    tk.Entry(win, textvariable=shard_var, width=10).grid(row=row, column=0, sticky="w", padx=16)
    row += 1

    tk.Label(win, text="Frames per animation:").grid(row=row, column=0, sticky="w", padx=8, pady=(8, 2))
    row += 1
    tk.Entry(win, textvariable=frames_var, width=10).grid(row=row, column=0, sticky="w", padx=16)
    row += 1

    status_label = tk.Label(win, textvariable=status_var, fg="red")
    status_label.grid(row=row, column=0, sticky="w", padx=8, pady=(4, 4))
    row += 1

    def on_ok():
        dirs: List[str] = []
        if left_var.get():
            dirs.append("left")
        if forward_var.get():
            dirs.append("forward")
        if backward_var.get():
            dirs.append("backward")

        if not dirs:
            status_var.set("Select at least one direction.")
            return

        try:
            shard_count = int(shard_var.get())
            frame_count = int(frames_var.get())
            if shard_count <= 0 or frame_count <= 0:
                raise ValueError
        except ValueError:
            status_var.set("Shard count and frames must be positive integers.")
            return

        config["directions"] = dirs
        config["shard_count"] = shard_count
        config["frame_count"] = frame_count

        win.destroy()
        root.quit()

    tk.Button(win, text="Start", command=on_ok).grid(row=row, column=0, sticky="e", padx=8, pady=(4, 8))

    # center dialog on screen
    win.update_idletasks()
    w = win.winfo_width()
    h = win.winfo_height()
    sw = win.winfo_screenwidth()
    sh = win.winfo_screenheight()
    x = int((sw - w) / 2)
    y = int((sh - h) / 2)
    win.geometry(f"+{x}+{y}")

    root.deiconify()
    root.mainloop()

    if not config["directions"] or config["shard_count"] <= 0 or config["frame_count"] <= 0:
        raise RuntimeError("Settings dialog was closed without valid input.")

    return config["directions"], config["shard_count"], config["frame_count"]


# ------------------------------------------------------------
# Main script flow
# ------------------------------------------------------------

def main():
    root = tk.Tk()
    root.withdraw()

    input_path = filedialog.askopenfilename(
        title="Select source PNG",
        filetypes=[("PNG images", "*.png")]
    )
    if not input_path:
        print("No input image selected, aborting.")
        root.destroy()
        return

    output_root = filedialog.askdirectory(
        title="Select output directory for breaking animations"
    )
    if not output_root:
        print("No output directory selected, aborting.")
        root.destroy()
        return

    img = Image.open(input_path).convert("RGBA")
    img_w, img_h = img.size
    print(f"Loaded image: {input_path} ({img_w} x {img_h})")

    try:
        directions, shard_count, frame_count = ask_animation_settings(root)
    except RuntimeError as e:
        print(str(e))
        root.destroy()
        return

    root.destroy()

    pixels, shard_centers, shard_bottoms = extract_pixels_and_shards(img, shard_count)
    if not pixels:
        print("Image has no non transparent pixels, nothing to animate.")
        return

    canvas_w, canvas_h, origin_x, origin_y = compute_canvas_size(img_w, img_h, frame_count)
    print(f"Canvas size: {canvas_w} x {canvas_h}")
    print(f"Shard count target: {shard_count}, actual shards: {len(shard_centers)}")

    base_name = os.path.splitext(os.path.basename(input_path))[0]

    dir_folder_names = {
        "left": "break_left",
        "forward": "break_forward",
        "backward": "break_backward",
    }

    for dir_key in directions:
        folder_suffix = dir_folder_names.get(dir_key, dir_key)
        print(f"Generating animation: {dir_key}")
        motions = build_shard_motions(
            shard_centers=shard_centers,
            shard_bottoms=shard_bottoms,
            img_w=img_w,
            img_h=img_h,
            origin_x=origin_x,
            origin_y=origin_y,
            frame_count=frame_count,
            direction=dir_key,
        )
        out_dir = os.path.join(output_root, f"{base_name}_{folder_suffix}")
        render_animation(
            pixels=pixels,
            shard_motions=motions,
            img_w=img_w,
            img_h=img_h,
            canvas_w=canvas_w,
            canvas_h=canvas_h,
            origin_x=origin_x,
            origin_y=origin_y,
            frame_count=frame_count,
            direction=dir_key,
            out_dir=out_dir,
        )
        print(f"Saved frames to: {out_dir}")

    print("Done.")


if __name__ == "__main__":
    main()
