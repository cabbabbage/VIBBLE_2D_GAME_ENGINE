#!/usr/bin/env python3
import os

class GifCleaner:
    def __init__(self, root: str = "."):
        self.root = os.path.abspath(root)

    def delete_gifs(self):
        for dirpath, _, filenames in os.walk(self.root):
            for filename in filenames:
                if filename.lower().endswith(".bak"):
                    file_path = os.path.join(dirpath, filename)
                    try:
                        os.remove(file_path)
                        print(f"Deleted: {file_path}")
                    except Exception as e:
                        print(f"Failed to delete {file_path}: {e}")

if __name__ == "__main__":
    cleaner = GifCleaner(".")  # start in current directory
    cleaner.delete_gifs()
