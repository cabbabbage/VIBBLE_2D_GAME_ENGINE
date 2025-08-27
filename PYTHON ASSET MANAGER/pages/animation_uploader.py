# === File: animation_uploader.py ===
import os
import shutil
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from PIL import Image, ImageEnhance, ImageTk, ImageSequence

class AnimationUploader(tk.Toplevel):
    def __init__(self, asset_folder, trigger_name):
        super().__init__()
        self.title("Animation Uploader")
        self.asset_folder = asset_folder
        self.trigger_name = trigger_name
        self.output_path = os.path.join(self.asset_folder, self.trigger_name)
        self.uploaded = False

        self.original_image = None
        self.modified_image = None
        self.preview_label = None

        self.settings = {
            "brightness": tk.DoubleVar(value=1.0),
            "contrast": tk.DoubleVar(value=1.0),
            "saturation": tk.DoubleVar(value=1.0),
            "sharpness": tk.DoubleVar(value=1.0),
            "tint_r": tk.DoubleVar(value=1.0),
            "tint_g": tk.DoubleVar(value=1.0),
            "tint_b": tk.DoubleVar(value=1.0),
        }

        self.upload_frame = ttk.Frame(self)
        self.upload_frame.pack(fill="both", expand=True)
        self._build_upload_ui()

    def _build_upload_ui(self):
        label = ttk.Label(self.upload_frame, text="Upload Animation", font=("Segoe UI", 14, "bold"))
        label.pack(pady=10)

        btn_folder = ttk.Button(self.upload_frame, text="Select Folder", command=self._handle_folder_upload)
        btn_folder.pack(pady=6)

        btn_gif = ttk.Button(self.upload_frame, text="Select GIF", command=self._handle_gif_upload)
        btn_gif.pack(pady=6)

        btn_gif = ttk.Button(self.upload_frame, text="Select PNG", command=self._handle_png_upload)
        btn_gif.pack(pady=6)

        btn_edit = ttk.Button(self.upload_frame, text="Edit Existing", command=self._handle_edit_existing)
        btn_edit.pack(pady=6)

    def _clear_existing(self):
        if os.path.isdir(self.output_path):
            for file in os.listdir(self.output_path):
                full_path = os.path.join(self.output_path, file)
                if os.path.isfile(full_path) and file.endswith(".png"):
                    os.remove(full_path)
        else:
            os.makedirs(self.output_path, exist_ok=True)

    def _handle_folder_upload(self):
        folder = filedialog.askdirectory()
        if not folder:
            return

        png_files = [f for f in sorted(os.listdir(folder)) if f.lower().endswith(".png")]
        if not png_files:
            messagebox.showerror("No Images", "No PNG images found in selected folder.")
            return

        self._clear_existing()
        for i, fname in enumerate(png_files):
            src = os.path.join(folder, fname)
            dst = os.path.join(self.output_path, f"{i}.png")
            shutil.copy2(src, dst)

        self.uploaded = True
        self._load_edit_page()

    def _handle_gif_upload(self):
        file = filedialog.askopenfilename(filetypes=[("GIF files", "*.gif")])
        if not file:
            return

        try:
            gif = Image.open(file)
        except Exception as e:
            messagebox.showerror("GIF Error", f"Failed to load GIF: {e}")
            return

        self._clear_existing()
        for i, frame in enumerate(ImageSequence.Iterator(gif)):
            frame = frame.convert("RGBA")
            frame.save(os.path.join(self.output_path, f"{i}.png"))

        self.uploaded = True
        self._load_edit_page()

    def _handle_png_upload(self):
        file = filedialog.askopenfilename(filetypes=[("PNG files", "*.png")])
        if not file:
            return

        try:
            image = Image.open(file).convert("RGBA")
        except Exception as e:
            messagebox.showerror("PNG Error", f"Failed to load PNG: {e}")
            return

        self._clear_existing()
        image.save(os.path.join(self.output_path, "0.png"))

        self.uploaded = True
        self._load_edit_page()


    def _handle_edit_existing(self):
        if not os.path.isdir(self.output_path):
            messagebox.showerror("No Existing Data", f"No existing animation folder: {self.output_path}")
            return
        self._load_edit_page()

    def _load_edit_page(self):
        self.upload_frame.destroy()
        self.edit_frame = ttk.Frame(self)
        self.edit_frame.pack(fill="both", expand=True)

        preview_path = os.path.join(self.output_path, "0.png")
        if not os.path.isfile(preview_path):
            messagebox.showerror("Missing Image", f"No base image found at {preview_path}")
            return
        self.original_image = Image.open(preview_path).convert("RGBA")

        ttk.Label(self.edit_frame, text="Adjustments", font=("Segoe UI", 14)).pack(pady=6)

        for key, var in self.settings.items():
            row = ttk.Frame(self.edit_frame)
            row.pack(fill="x", padx=10, pady=2)
            ttk.Label(row, text=key, width=12).pack(side="left")
            ttk.Scale(row, from_=0.0, to=2.0, variable=var, orient="horizontal", command=lambda e: self._update_preview()).pack(side="left", fill="x", expand=True)

        self.preview_label = ttk.Label(self.edit_frame)
        self.preview_label.pack(pady=10)
        self._update_preview()

        ttk.Button(self.edit_frame, text="Interpolate", command=self.handle_interpolation).pack(pady=4)
        ttk.Button(self.edit_frame, text="Save", command=self._apply_to_all).pack(pady=8)

    def _apply_modifications(self, img: Image.Image):
        img = ImageEnhance.Brightness(img).enhance(self.settings["brightness"].get())
        img = ImageEnhance.Contrast(img).enhance(self.settings["contrast"].get())
        img = ImageEnhance.Color(img).enhance(self.settings["saturation"].get())
        img = ImageEnhance.Sharpness(img).enhance(self.settings["sharpness"].get())

        r, g, b, a = img.split()
        r = r.point(lambda p: min(255, int(p * self.settings["tint_r"].get())))
        g = g.point(lambda p: min(255, int(p * self.settings["tint_g"].get())))
        b = b.point(lambda p: min(255, int(p * self.settings["tint_b"].get())))
        return Image.merge("RGBA", (r, g, b, a))

    def _update_preview(self):
        if not self.original_image:
            return
        modified = self._apply_modifications(self.original_image.copy())
        modified.thumbnail((320, 240), Image.LANCZOS)
        self.modified_image = ImageTk.PhotoImage(modified)
        self.preview_label.configure(image=self.modified_image)

    def _apply_to_all(self):
        files = sorted(f for f in os.listdir(self.output_path) if f.endswith(".png"))
        out_imgs = []

        for fname in files:
            path = os.path.join(self.output_path, fname)
            img = Image.open(path).convert("RGBA")
            modified = self._apply_modifications(img)
            modified.save(path)
            out_imgs.append(modified)

        # Generate preview.gif
        preview_gif_path = os.path.join(self.output_path, "preview.gif")
        gif_ready = []
        for img in out_imgs:
            rgb = Image.new("RGB", img.size, (0, 0, 0))
            rgb.paste(img, mask=img.split()[3])  # Use alpha as mask
            gif_ready.append(rgb.convert("P", palette=Image.ADAPTIVE))

        gif_ready[0].save(
            preview_gif_path,
            save_all=True,
            append_images=gif_ready[1:],
            loop=0,
            duration=int(1000 / 24),
            disposal=2
        )

        messagebox.showinfo("Saved", f"Applied modifications to {len(out_imgs)} frames.")
        self.destroy()



    def interpolate(self, img1: Image.Image, img2: Image.Image) -> Image.Image:
        return Image.blend(img1, img2, alpha=0.5)

    def handle_interpolation(self):
        png_files = sorted([f for f in os.listdir(self.output_path) if f.endswith(".png")], key=lambda x: int(x.split(".")[0]))
        if len(png_files) < 2:
            messagebox.showwarning("Not enough frames", "You need at least two frames to interpolate.")
            return

        new_images = []
        images = [Image.open(os.path.join(self.output_path, f)).convert("RGBA") for f in png_files]

        for i in range(len(images) - 1):
            new_images.append(images[i])
            interp = self.interpolate(images[i], images[i + 1])
            new_images.append(interp)

        new_images.append(images[-1])
        final_interp = self.interpolate(images[-1], images[0])
        new_images.append(final_interp)

        for f in os.listdir(self.output_path):
            if f.endswith(".png"):
                os.remove(os.path.join(self.output_path, f))

        for idx, img in enumerate(new_images):
            img.save(os.path.join(self.output_path, f"{idx}.png"))

    def run(self):
        self.grab_set()
        self.wait_window()
        return self.trigger_name if self.uploaded else None
