# VIBBLE - 2D Game Engine

Lightweight, data-driven 2D engine built on SDL2 with a controller-based AI and a flexible animation system.

## Project Flow & Data Model

This is the complete flow from content to pixels each frame, and how core data types relate to each other.

### Loading, Spawning, and Data Culling
- Load map: the app starts in a menu; you pick a folder under `MAPS/<MapName>`. `AssetLoader` reads `map_info.json`, builds rooms and trails, and loads asset types from `SRC/*/info.json` into shared definitions (`AssetInfo`).
- Build assets: `InitializeAssets` turns `AssetInfo` into live `Asset` instances, assigns each an `AssetController` via `ControllerFactory`, and wires an `AnimationUpdate` for movement/animation control.
- Spawning details:
  - Rooms and trails are generated from map JSON (layered layout). Boundary assets are placed when configured.
  - Static/background props may be clustered by proximity and linked as children (“child linking”). Moving assets are excluded from linking.
- Culling: `ActiveAssetsManager` maintains a list of active assets around the player and sorts them by z-index; it also tracks a small set of “closest” assets for interaction.

### Shared Definitions vs. Runtime Instances
- All runtime `Asset` instances with the same asset name share the same `std::shared_ptr<AssetInfo>` (definition). The definition is immutable at runtime and contains animations, areas, lights, and metadata.
- Each `Asset` holds its own state (position, z, current animation/frame, controller, AnimationUpdate state, children, etc.).

### Classes & Relationships
- `AssetInfo` (definition, shared across instances of the same name)
  - Identity: `name`, `type`, `start_animation`, `z_threshold`, `tags`, `flipable`, `scale_factor`.
  - Animations: `std::map<std::string, Animation>` keyed by trigger/id.
  - Geometry: `areas` (named polygons for collision/shading/passability).
  - Lights: `light_sources`, `orbital_light_sources`.
  - Children: authoring hints used by tools.
  - Controller: `custom_controller_key` (binds to a C++ controller).
  - Derived: `moving_asset` (true if any Animation indicates movement).

- `Animation` (one entry inside `AssetInfo::animations`)
  - Frame textures: `frames` (vector of `SDL_Texture*`).
  - Frame data: `frames_data` (vector of `AnimationFrame`).
  - Motion & playback: `total_dx/dy`, `movment`, `locked`, `loop`, `rnd_start`, `speed_factor`, `randomize`.
  - Mapping: `on_end_mapping` (next animation id) / `on_end_animation`.
  - Source/transform: `source`, `flipped_source`, `reverse_source`.

- `AnimationFrame` (one element per visible frame)
  - `dx/dy` per-frame movement, `z_resort` (request resort), `rgb` tint.
  - Linked list pointers (`prev/next`) and flags (`is_first/is_last`).

- `Asset` (runtime instance)
  - `info` (shared_ptr<AssetInfo>), `controller_` (unique_ptr<AssetController>), `anim_` (unique_ptr<AnimationUpdate>).
  - `current_animation` (string id) and `current_frame` (AnimationFrame*).
  - `pos`, `z_index`, `alpha`, `children`, `areas`, and light attachments.

- `AnimationUpdate` (movement and animation driver)
  - Controller-facing API: `set_idle`, `set_pursue`, `set_run`, `set_orbit`/`set_orbit_ccw`/`set_orbit_cw`, `set_patrol`, `set_serpentine`, `set_animation_now`, `set_animation_qued`, `set_mode_none`.
  - Each update: picks the best animation toward the current target, advances frames, applies per-frame movement, respects `locked`/`loop`, and safely switches animations.

### Runtime Ownership Tree
```
MainApp
└─ Assets
   ├─ ActiveAssetsManager
   ├─ SceneRenderer
   ├─ rooms: vector<Room*>
   ├─ owned_assets: deque<unique_ptr<Asset>>
   └─ all: vector<Asset*> (raw views of owned)

Asset (runtime)
├─ info: shared_ptr<AssetInfo>   ← shared per asset name
├─ controller_: unique_ptr<AssetController>
├─ anim_: unique_ptr<AnimationUpdate>
├─ current_animation: string
├─ current_frame: AnimationFrame*
├─ children: vector<Asset*>
└─ areas, lights, z, pos, alpha, etc.

AssetInfo (definition)
├─ name, type, start_animation, z_threshold, tags, flipable, scale_factor
├─ moving_asset: bool (derived)
├─ animations: map<string, Animation>
├─ areas: vector<NamedArea>
├─ light_sources, orbital_light_sources
├─ children: vector<ChildInfo>
└─ custom_controller_key

Animation
├─ frames: vector<SDL_Texture*>
├─ frames_data: vector<AnimationFrame>
├─ number_of_frames, speed_factor, locked, loop, rnd_start
├─ total_dx, total_dy, movment, on_end_mapping/on_end_animation
└─ source, flipped_source, reverse_source, randomize

AnimationFrame
└─ dx, dy, z_resort, rgb, prev/next, is_first/is_last
```

---

## Quick Start

Requirements
- Windows + Visual Studio 2022 (CMake generator), CMake >= 3.16
- SDL2, SDL2_image, SDL2_mixer, SDL2_ttf (via vcpkg toolchain or system installs)

Build & Run
- From the repo root:
  - `./run.bat`  (configures, builds RelWithDebInfo, runs with correct working dir)
  - Or manually:
    - `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`
    - `cmake --build build --config RelWithDebInfo -j`
    - `./ENGINE/engine.exe`

Important
- Always run from the repo root so relative paths resolve (`MAPS/`, `SRC/`, `MISC_CONTENT/`, `loading/`).

## What's Inside

- Data-driven content: assets under `SRC/<AssetName>`, maps under `MAPS/<MapName>`.
- AnimationUpdate: one API to control both movement targeting and animation selection.
- Controllers: tiny C++ classes that set AnimationUpdate modes based on proximity/logic.
- ActiveAssetsManager: activates/sorts nearby assets for efficient updates.
- SceneRenderer: z-layered sprite rendering, software lighting and shading.

## Folder Structure

- `ENGINE/` - core engine code (assets, controllers, rendering, UI)
- `MAPS/`   - map folders with `map_info.json` and room/trail data
- `SRC/`    - assets with `info.json`, frames, areas, and lights
- `scripts/` and `PYTHON ASSET MANAGER/` - authoring tools and helpers

## Runtime Architecture (Concise)

Startup
- `engine.exe` → main menu → select `MAPS/<MapName>`
- `AssetLoader` loads map JSON, rooms/trails; `AssetLibrary::loadAllAnimations()` builds `AssetInfo` from `SRC/*/info.json`.
- Each `AssetInfo` computes `moving_asset` and caches textures.

Assets & Controllers
- `InitializeAssets` creates live `Asset` objects and assigns controllers via `ControllerFactory`.
- `ActiveAssetsManager` maintains, sorts, and exposes the active set.

Per-frame
- Input → `Assets::update()` → per-asset `controller_->update()` (sets a mode)
- `AnimationUpdate::update()` advances frames, chooses animations, and applies movement.
- `SceneRenderer` draws world, lights, overlays.

Child Linking
- Static props may be auto-clustered as children; moving assets (per `AssetInfo::moving_asset`) are excluded.

## Controllers & Movement (AnimationUpdate)

Controllers are tiny: they never advance frames; they only call AnimationUpdate.

Mode setters
- `set_idle(min_dist, max_dist, rest_ratio)`
- `set_pursue(final_target, min_dist, max_dist)`
- `set_run(threat, min_dist, max_dist)`
- `set_orbit(center, min_radius, max_radius, keep_direction_ratio)`
- `set_orbit_ccw(center, min_radius, max_radius)` / `set_orbit_cw(center, min_radius, max_radius)`
- `set_patrol(waypoints, loop, hold_frames)`
- `set_serpentine(final_target, min_stride, max_stride, sway, keep_side_ratio)`
- `set_mode_none()` - no AI targeting; just advance and follow `on_end` mapping

Animation overrides
- `set_animation_now(anim_id)` - switch immediately; suspends the current mode until done
- `set_animation_qued(anim_id)` - play next when the current animation finishes

Examples
```cpp
// Default idle (Frog-like)
if (self_->anim_) self_->anim_->set_idle(0, 20, 3);

// Chaser with close CCW orbit at ~40px (Davey)
bool near   = Range::is_in_range(self_, player, 40);
bool inView = Range::is_in_range(self_, player, 1000);
if (near)           self_->anim_->set_orbit_ccw(player, 40, 40);
else if (inView)    self_->anim_->set_pursue(player, 20, 30);
else                self_->anim_->set_idle(40, 80, 5);

// Bomber: explode near player; otherwise chase/idle
if (Range::is_in_range(self_, player, 40)) {
  self_->anim_->set_animation_now("explosion");
  self_->anim_->set_mode_none();
} else if (Range::is_in_range(self_, player, 1000)) {
  self_->anim_->set_pursue(player, 20, 30);
} else {
  self_->anim_->set_idle(40, 80, 5);
}
```

Notes
- Always guard `self_`, `self_->info`, and `self_->anim_`.
- Let `AnimationUpdate::update()` advance frames and respect `locked/loop`.
- `set_mode_none()` is useful after one-shot animations (e.g., explosion) to let `on_end` mapping resolve.

## Content Authoring

Assets (`SRC/<AssetName>/info.json`)
- `animations`: sources, movement, loop/locked, `on_end` mapping
- `areas`: collision/interaction/shading polygons
- `lighting`: static/orbital lights
- `custom_controller_key`: binds to a controller in `ENGINE/custom_controllers`
- `moving_asset` is computed automatically from animations

Maps (`MAPS/<MapName>/map_info.json`)
- Layered rooms/trails, radii, counts, and optional boundary geometry
- Auto child-linking for static props; moving assets are excluded

## Tools

- `run.bat` - one-click build + run (VS 2022, correct working directory)
- `scripts/custom_controller_manager.py` - scaffolds a Frog-style controller and registers it
- `scripts/animation_ui.py` and friends - authoring helpers for animations/areas

