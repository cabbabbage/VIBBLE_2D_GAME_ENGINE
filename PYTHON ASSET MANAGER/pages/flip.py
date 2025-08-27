import os
from PIL import Image, ImageOps

src_folder = r"C:\Users\cal_m\OneDrive\Documents\GitHub\tarot_game\Tarot Game\Davey\Davet Run Right\Frames"
dst_folder = os.path.join(os.path.dirname(src_folder), "left")
os.makedirs(dst_folder, exist_ok=True)

for filename in os.listdir(src_folder):
    if filename.lower().endswith(".png"):
        src_path = os.path.join(src_folder, filename)
        dst_path = os.path.join(dst_folder, filename)

        img = Image.open(src_path).convert("RGBA")
        mirrored = ImageOps.mirror(img)
        mirrored.save(dst_path)

print(f"Mirrored images saved to: {dst_folder}")
