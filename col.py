#!/usr/bin/env python3
import json
import os

ROOT = r"C:\USERS\CALMI\DOCUMENTS\GITHUB\VIBBLE_2D_GAME_ENGINE\MAPS\FORREST"
OUT_FILE = os.path.join(ROOT, "mapinfo_merged.json")

def load_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)

def main():
    # Load base map_info (already contains map_light_data since legacy files were merged)
    map_info = load_json(os.path.join(ROOT, "map_info.json"))

    # Merge in map_assets
    map_info["map_assets_data"] = load_json(os.path.join(ROOT, "map_assets.json"))

    # Merge in map_boundary
    map_info["map_boundary_data"] = load_json(os.path.join(ROOT, "map_boundary.json"))

    # Merge rooms
    rooms_dir = os.path.join(ROOT, "rooms")
    rooms_data = {}
    if os.path.isdir(rooms_dir):
        for fn in os.listdir(rooms_dir):
            if fn.endswith(".json"):
                name = os.path.splitext(fn)[0]
                rooms_data[name] = load_json(os.path.join(rooms_dir, fn))
    map_info["rooms_data"] = rooms_data

    # Merge trails
    trails_dir = os.path.join(ROOT, "trails")
    trails_data = {}
    if os.path.isdir(trails_dir):
        for fn in os.listdir(trails_dir):
            if fn.endswith(".json"):
                name = os.path.splitext(fn)[0]
                trails_data[name] = load_json(os.path.join(trails_dir, fn))
    map_info["trails_data"] = trails_data

    # Save merged file
    with open(OUT_FILE, "w", encoding="utf-8") as f:
        json.dump(map_info, f, indent=2)

    print(f"Merged map saved to {OUT_FILE}")

if __name__ == "__main__":
    main()
