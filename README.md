# VIBBLE - 2D Game Engine

VIBBLE is a lightweight, SDL2-powered 2D engine designed around data-driven content, controller-first AI, and expressive animation systems. Every map, asset, and animation lives outside the executable, making it easy to iterate on gameplay while keeping a tight main loop.

---

## Game Loop & Loading Pipeline

### Content Flow
1. **Select a map** – the engine boots into a main menu that enumerates maps from the project manifest. Once you choose a map, `AssetLoader` resolves the map descriptor, reads its `content_root`, and prepares rooms, trails, and asset definitions before the playable scene is created.
2. **Materialize assets** – `InitializeAssets` consumes the shared `AssetInfo` definitions and spawns runtime `Asset` instances, wiring each to an `AssetController` and an `AnimationUpdate` driver. Definitions stay immutable and shared (`std::shared_ptr`) across instances, while each runtime asset tracks its own state, children, and per-frame animation cursor.
3. **Cull and stage** – the `ActiveAssetsManager` keeps a rolling window of assets near the player, handles room transitions, and sorts the render list by depth so only relevant entities update each frame.

### Main Loop Responsibilities
* SDL events are pumped every frame, letting `Input` consume keyboard/mouse state and forwarding the same events to every active asset for per-entity handling.
* `Assets::update` advances controllers, animations, collision residency, and dev tooling hooks before handing off to the renderer. Frame pacing targets 60 FPS by measuring `SDL_GetPerformanceCounter` deltas and sleeping when work finishes early.
* Rendering runs out of `SceneRenderer`, which respects the active asset set, applies per-frame movement derived from animations, and blends lighting effects defined in asset metadata.

### Running the Engine
```bash
./run.bat
```
*The helper script configures CMake (RelWithDebInfo), builds, and launches the engine from the correct working directory so relative asset paths resolve. You can also configure the build manually with CMake + Visual Studio 2022 or the toolchain of your choice.*

Project structure highlights:
- `ENGINE/` – runtime code (assets, controllers, renderer, UI, dev tooling)
- `SRC/` – asset folders with `info.json`, animation sprites, areas, and lighting data
- `MAPS/` – map layouts and spawn definitions referenced by the manifest
- `loading/` – tarot splash art and copy for the loading screen

---

## Dev Mode Toolkit & Usage

### Enabling Dev Mode
- Press **Ctrl + D** during play to toggle Dev Mode from the in-engine pause menu. Dev Mode disables the pause overlay after switching so you can immediately test edits.
- Dev Mode also activates automatically when the selected map lacks a player-class asset, ensuring you can still move the camera and inspect the scene.

### What Dev Mode Unlocks
- Rendering quality is temporarily lowered for responsiveness (`SDL_HINT_RENDER_SCALE_QUALITY = "0"`), and all animations are forced into memory so the editor can preview any asset on demand.
- `DevControls` exposes a multi-mode editor with room editing, map-wide tooling, asset filtering, lighting panels, and spawn management. The system listens to pointer and keyboard input, opens dedicated panels (asset library, room config, map light panel, camera controls), and supports manifest-backed transactions for safe writes.
- Settings such as asset filters, lighting panel locks, and UI layout persist inside `dev_mode_settings.json`, allowing you to keep a preferred workspace between sessions.

### Using the Toolkit
1. Press **Esc** to open the pause menu, then choose **Toggle Dev Mode** (or hit Ctrl + D) to enable the overlay and disable render suppression.
2. Hover over assets to inspect them; click to open the asset library or asset info editor, adjust room geometry, or tweak lighting curves directly on the map canvas.
3. Save changes through the provided panels—each commit flows through the manifest store so map and asset JSON stay in sync with disk.

---

## Custom Controller Workflow

### File Layout
Custom controllers live under `ENGINE/custom_controllers/`. Each controller typically has a header/implementation pair and may share helpers for paths and visitation thresholds.

### Registering a Controller
1. Implement your controller class (derive from `AssetController`) and place the files in `ENGINE/custom_controllers/`.
2. Include the header and add a branch to `ControllerFactory::create_by_key` so the factory can instantiate the controller when requested by name.
3. Rebuild; the factory will now resolve any asset whose metadata references your controller key.

### Hooking Controllers from Content
- Add `"custom_controller_key": "YourController"` to the asset’s `info.json` (or assign it through the Dev Mode asset editor). The runtime merger copies this key into the `AssetInfo` instance so the factory can honour it during spawn.
- When editing through Dev Mode, the `CustomControllerService` can scaffold headers, update the factory includes, and write manifest metadata so assets and animations stay paired with the correct controller logic.

With these hooks in place you can prototype new AI behaviours, retarget animations, and iterate on gameplay loops without recompiling unrelated systems.
