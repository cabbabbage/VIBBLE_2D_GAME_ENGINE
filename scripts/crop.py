import os
from PIL import Image

def is_numbered_png(filename):
    return filename.endswith('.png') and filename[:-4].isdigit()

def get_image_paths(folder):
    files = os.listdir(folder)
    numbered_pngs = [f for f in files if is_numbered_png(f)]
    return sorted(
        [os.path.join(folder, f) for f in numbered_pngs],
        key=lambda x: int(os.path.basename(x)[:-4])
    )

def get_crop_bounds(image_paths):
    """Compute crop bounds that preserve all non-transparent pixels across all images."""
    images = []
    for path in image_paths:
        img = Image.open(path).convert("RGBA")
        images.append(img)

    width, height = images[0].size

    top_crop = 0
    bottom_crop = 0
    left_crop = 0
    right_crop = 0

    # initialize crop counters
    top_crop = bottom_crop = left_crop = right_crop = 0

    # === TOP ===
    for y in range(height):
        if all(all(img.getpixel((x, y))[3] == 0 for x in range(width)) for img in images):
            top_crop += 1
        else:
            break

    # === BOTTOM ===
    for y in range(height - 1, -1, -1):
        if all(all(img.getpixel((x, y))[3] == 0 for x in range(width)) for img in images):
            bottom_crop += 1
        else:
            break

    # === LEFT ===
    for x in range(width):
        if all(all(img.getpixel((x, y))[3] == 0 for y in range(height)) for img in images):
            left_crop += 1
        else:
            break

    # === RIGHT ===
    for x in range(width - 1, -1, -1):
        if all(all(img.getpixel((x, y))[3] == 0 for y in range(height)) for img in images):
            right_crop += 1
        else:
            break


    for img in images:
        img.close()

    return top_crop, bottom_crop, left_crop, right_crop

def crop_and_save_images(image_paths, crop_top, crop_bottom, crop_left, crop_right):
    for path in image_paths:
        with Image.open(path).convert("RGBA") as img:
            width, height = img.size
            left = crop_left
            top = crop_top
            right = width - crop_right
            bottom = height - crop_bottom

            if left >= right or top >= bottom:
                print(f"[!] Skipping {path}: crop dimensions result in empty image")
                continue

            cropped = img.crop((left, top, right, bottom))
            cropped.save(path)
            print(f"[✓] Cropped and saved: {path}")

def process_folder(folder):
    image_paths = get_image_paths(folder)
    if not image_paths:
        return

    print(f"\n[📁] Processing folder: {folder}")
    crop_top, crop_bottom, crop_left, crop_right = get_crop_bounds(image_paths)
    print(f"    Crop (T:{crop_top}, B:{crop_bottom}, L:{crop_left}, R:{crop_right})")

    if crop_top == 0 and crop_bottom == 0 and crop_left == 0 and crop_right == 0:
        print("    [→] No cropping needed.")
        return

    crop_and_save_images(image_paths, crop_top, crop_bottom, crop_left, crop_right)

def recursive_search(start_dir):
    for root, _, files in os.walk(start_dir):
        if any(is_numbered_png(f) for f in files):
            process_folder(root)

if __name__ == "__main__":
    base_dir = os.path.dirname(os.path.abspath(__file__))
    print(f"[🚀] Starting recursive crop from: {base_dir}")
    recursive_search(base_dir)
    print("\n[✅] Cropping complete.")
