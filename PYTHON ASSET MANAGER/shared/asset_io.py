import os
import json


def load_info(info_path):
    """
    Load an asset's single info.json and normalize legacy references:
    - If area keys (e.g., spacing_area) are strings -> load that JSON and inline as {points: ...}.
    - If child_assets contains strings or objects with json_path -> load and inline area info.
    Returns a dict with inline 'areas' and 'child_assets' entries.
    """
    with open(info_path, 'r') as f:
        info = json.load(f)
    base_dir = os.path.dirname(info_path)

    def _inline_area(key):
        val = info.get(key)
        if isinstance(val, str):
            p = os.path.join(base_dir, val)
            try:
                with open(p, 'r') as af:
                    info[key] = json.load(af)
            except Exception:
                pass

    for k in ("impassable_area", "spacing_area", "collision_area", "interaction_area", "hit_area"):
        _inline_area(k)

    children = []
    for item in info.get("child_assets", []):
        if isinstance(item, dict) and ("area" in item or not item.get("json_path")):
            children.append(item)
            continue
        path = item.get("json_path") if isinstance(item, dict) else item
        if not path:
            continue
        full = os.path.join(base_dir, path)
        try:
            with open(full, 'r') as cf:
                child = json.load(cf)
            entry = {k: v for k, v in child.items() if k not in ("assets",)}
            entry["json_path"] = path
            children.append(entry)
        except Exception:
            continue

    if children:
        info["child_assets"] = children
    return info


def save_info(info_path, info):
    """
    Save the in-memory info dict back to a single info.json with inline areas and children.
    Images and animation folders remain untouched in the directory.
    """
    os.makedirs(os.path.dirname(info_path), exist_ok=True)
    with open(info_path, 'w') as f:
        json.dump(info, f, indent=2)

