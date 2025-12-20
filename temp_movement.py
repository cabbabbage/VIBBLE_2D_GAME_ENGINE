import json
from pathlib import Path
from typing import Dict, Any

with open('SRC/assets/spider/manifest.json') as f:
    data = json.load(f)

anims = data.get('animations', {})

def resolve_source(payload: Dict[str, Any]):
    source = payload.get('source')
    if not isinstance(source, dict):
        return None
    kind = source.get('kind')
    if kind != 'animation':
        return None
    name = source.get('name') or source.get('path')
    if not name:
        return None
    return name

cache = {}

def resolve_movement(anim_id: str, depth: int = 0):
    if depth > 10:
        return 0, 0, 'depth'
    key = (anim_id, depth)
    if key in cache:
        return cache[key]
    payload = anims.get(anim_id)
    if not isinstance(payload, dict):
        cache[key] = (0, 0, 'missing')
        return cache[key]
    source = resolve_source(payload)
    if source:
        inherit = payload.get('inherit_source_movement', True)
        if inherit:
            dx, dy, sig = resolve_movement(source, depth + 1)
            mods = payload.get('derived_modifiers', {})
            if mods.get('flipMovementX'):
                dx = -dx
            if mods.get('flipMovementY'):
                dy = -dy
            cache[key] = (dx, dy, f"child({sig})")
            return cache[key]
    movement = payload.get('movement')
    dx = 0
    dy = 0
    if isinstance(movement, list):
        for entry in movement[1:]:
            if isinstance(entry, list):
                if entry and isinstance(entry[0], (int, float)):
                    dx += int(entry[0])
                if len(entry) > 1 and isinstance(entry[1], (int, float)):
                    dy += int(entry[1])
            elif isinstance(entry, dict):
                if 'dx' in entry and isinstance(entry['dx'], (int, float)):
                    dx += int(entry['dx'])
                if 'dy' in entry and isinstance(entry['dy'], (int, float)):
                    dy += int(entry['dy'])
    cache[key] = (dx, dy, 'own')
    return cache[key]

for anim in ['left', 'right', 'down_right']:
    dx, dy, sig = resolve_movement(anim)
    print(anim, '->', dx, dy, sig)
