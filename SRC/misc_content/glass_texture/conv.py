import os
from PIL import Image

def convert_jpg_to_png(root_dir):
    for dirpath, _, filenames in os.walk(root_dir):
        for file in filenames:
            if file.lower().endswith(('.jpg', '.jpeg')):
                jpg_path = os.path.join(dirpath, file)
                png_path = os.path.splitext(jpg_path)[0] + '.png'

                try:
                    with Image.open(jpg_path) as img:
                        img.save(png_path, 'PNG')
                    os.remove(jpg_path)
                    print(f"Converted and replaced: {jpg_path}")
                except Exception as e:
                    print(f"Error converting {jpg_path}: {e}")

if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    convert_jpg_to_png(script_dir)
