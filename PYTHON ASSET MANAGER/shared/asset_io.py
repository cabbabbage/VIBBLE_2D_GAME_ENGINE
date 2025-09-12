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
        # Already inline definition (preferred)
        if isinstance(item, dict) and ("assets" in item or "area" in item) and not item.get("json_path"):
            children.append(item)
            continue
        # Legacy: load from referenced json_path and inline assets
        path = None
        if isinstance(item, dict):
            path = item.get("json_path")
            base_entry = {k: v for k, v in item.items() if k != "json_path"}
        else:
            base_entry = {}
            path = item
        if not path:
            if base_entry:
                children.append(base_entry)
            continue
        full = os.path.join(base_dir, path)
        try:
            with open(full, 'r') as cf:
                child = json.load(cf)
            assets = child.get("assets", [])
            entry = base_entry
            entry["assets"] = assets
            # Drop json_path to normalize to inline format
            children.append(entry)
        except Exception:
            # If file missing, keep whatever fields were present (minus json_path)
            children.append(base_entry)

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

