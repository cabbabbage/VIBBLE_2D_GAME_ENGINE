import os
import json

def boost_scale_percentage(root_dir):
    for dirpath, dirnames, filenames in os.walk(root_dir):
        if "info.json" in filenames:
            info_path = os.path.join(dirpath, "info.json")
            try:
                with open(info_path, 'r') as f:
                    data = json.load(f)

                updated = False
                if "size_settings" in data and isinstance(data["size_settings"], dict):
                    size_settings = data["size_settings"]
                    if "scale_percentage" in size_settings and isinstance(size_settings["scale_percentage"], (int, float)):
                        original = size_settings["scale_percentage"]
                        boosted = round(original * 0.9)
                        if boosted != original:
                            data["size_settings"]["scale_percentage"] = boosted
                            updated = True

                if updated:
                    with open(info_path, 'w') as f:
                        json.dump(data, f, indent=2)
                    print(f"[Updated] Boosted scale to {boosted} in {info_path}")

            except Exception as e:
                print(f"[Error] Failed to process {info_path}: {e}")

if __name__ == "__main__":
    start_path = r"C:\Users\cal_m\OneDrive\Documents\GitHub\tarot_game\SRC"
    boost_scale_percentage(start_path)
