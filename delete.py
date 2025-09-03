import os

def delete_bak_files(root_dir="."):
    for dirpath, dirnames, filenames in os.walk(root_dir):
        for filename in filenames:
            if filename.lower().endswith(".bak"):
                file_path = os.path.join(dirpath, filename)
                try:
                    os.remove(file_path)
                    print(f"Deleted: {file_path}")
                except Exception as e:
                    print(f"Failed to delete {file_path}: {e}")

if __name__ == "__main__":
    start_dir = os.path.dirname(os.path.abspath(__file__))  # script's directory
    delete_bak_files(start_dir)
