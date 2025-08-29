import os
import json
from pathlib import Path

def migrate_asset_folder(asset_dir: Path):
    info_path = asset_dir / 'info.json'
    if not info_path.exists():
        return False
    try:
        with open(info_path, 'r') as f:
            info = json.load(f)
    except Exception as e:
        print(f"[migrate] skip {asset_dir}: {e}")
        return False

    changed = False

    def inline_area(key):
        nonlocal changed
        val = info.get(key)
        if isinstance(val, str):
            p = asset_dir / val
            if p.exists():
                try:
                    with open(p, 'r') as af:
                        info[key] = json.load(af)
                        changed = True
                except Exception as e:
                    print(f"[migrate] area {key} failed for {asset_dir}: {e}")

    for k in ("impassable_area", "spacing_area", "collision_area", "interaction_area", "hit_area"):
        inline_area(k)

    children = []
    for item in info.get('child_assets', []):
        if isinstance(item, dict) and (item.get('area') or not item.get('json_path')):
            children.append(item)
            continue
        path = item.get('json_path') if isinstance(item, dict) else item
        if not path:
            continue
        p = asset_dir / path
        if not p.exists():
            continue
        try:
            with open(p, 'r') as cf:
                child = json.load(cf)
            entry = {k: v for k, v in child.items() if k not in ('assets',)}
            entry['json_path'] = path
            children.append(entry)
            changed = True
        except Exception as e:
            print(f"[migrate] child {path} failed for {asset_dir}: {e}")
    if children:
        info['child_assets'] = children

    if changed:
        with open(info_path, 'w') as f:
            json.dump(info, f, indent=2)
        print(f"[migrate] updated {asset_dir}")
    return changed


def migrate_all(src_root: str = 'SRC'):
    root = Path(src_root)
    if not root.exists():
        print(f"SRC not found: {src_root}")
        return
    count = 0
    for path in root.iterdir():
        if path.is_dir():
            if migrate_asset_folder(path):
                count += 1
    print(f"[migrate] complete. {count} assets updated.")

if __name__ == '__main__':
    migrate_all()
