import os
import json
from pathlib import Path

def migrate_asset_folder(asset_dir: Path):
    info_path = asset_dir / 'info.json'

    # Load or create fresh info.json
    if info_path.exists():
        try:
            with open(info_path, 'r') as f:
                info = json.load(f)
        except Exception:
            info = {}
    else:
        info = {}

    to_delete = set()

    # Inline area JSONs if they exist
    for k in ("impassable_area", "spacing_area", "collision_area", "interaction_area", "hit_area"):
        val = info.get(k)
        if isinstance(val, str):
            p = asset_dir / val
            if p.exists():
                try:
                    with open(p, 'r') as af:
                        info[k] = json.load(af)
                        to_delete.add(str(p))
                except Exception as e:
                    print(f"[migrate] area {k} failed for {asset_dir}: {e}")

    # Inline child assets
    children = []
    for item in info.get('child_assets', []):
        path = None
        if isinstance(item, dict):
            path = item.get('json_path')
        elif isinstance(item, str):
            path = item
        if path:
            p = asset_dir / path
            if p.exists():
                try:
                    with open(p, 'r') as cf:
                        child = json.load(cf)
                    entry = {k: v for k, v in child.items() if k not in ('assets',)}
                    entry['json_path'] = path
                    children.append(entry)
                    to_delete.add(str(p))
                except Exception as e:
                    print(f"[migrate] child {path} failed for {asset_dir}: {e}")
        else:
            children.append(item)
    if children:
        info['child_assets'] = children

    # Write back merged info.json
    with open(info_path, 'w') as f:
        json.dump(info, f, indent=2)

    # Delete ALL other JSON files (recursively) except info.json
    deleted = 0
    for json_file in asset_dir.rglob('*.json'):
        if json_file.resolve() != info_path.resolve():
            try:
                os.remove(json_file)
                deleted += 1
            except Exception as e:
                print(f'[migrate] warn: could not delete {json_file}: {e}')

    print(f'[migrate] cleaned {asset_dir} (deleted {deleted} files)')
    return True


def migrate_all(src_root: str = 'SRC'):
    root = Path(src_root)
    if not root.exists():
        print(f"SRC not found: {src_root}")
        return
    count = 0
    for path in root.iterdir():
        if path.is_dir():
            migrate_asset_folder(path)
            count += 1
    print(f"[migrate] complete. {count} assets processed.")


if __name__ == '__main__':
    migrate_all()
