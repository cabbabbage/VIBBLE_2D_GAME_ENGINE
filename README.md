# VIBBLE - 2D Game Engine

VIBBLE is an SDL2-based 2D game engine with external data files for maps, assets, and animations. The engine keeps game content out of the executable so builds stay small and iteration does not require recompilation.

Built with CMake and managed via vcpkg for dependency resolution.

## Game Loop and Loading Pipeline

### Content Flow
1. Select a map from the manifest. `AssetLoader` reads the map descriptor, resolves the `content_root`, and loads room, trail, and asset definitions.
2. `InitializeAssets` builds runtime `Asset` instances from shared `AssetInfo` definitions. Each instance owns its state, children, and animation cursor.
3. `ActiveAssetsManager` tracks nearby assets, handles room transitions, and keeps the render list sorted by depth.

### Main Loop Responsibilities
* SDL events feed the `Input` system, which provides the same events to each active asset.
* `Assets::update` advances controllers, animations, and collision state, then hands off to rendering.
* `SceneRenderer` draws the active assets, applies animation movement, and uses lighting data from asset metadata.

### Running the Engine
On Windows, use the provided `run.bat` script:

```cmd
run.bat
```

This script automatically:
- Installs prerequisites (Git, Visual Studio Build Tools, CMake, Ninja, vcpkg)
- Sets up dependencies via vcpkg
- Configures a RelWithDebInfo build via CMake
- Compiles the project
- Launches the engine from the correct working directory

## Project Layout

- `ENGINE/`: Runtime source code for assets, controllers, rendering, UI, developer tools, and game logic.
- `SRC/`: Asset directories with `info.json` files, sprites, areas, and lighting data.
  - `SRC/assets/`: Game assets (characters, objects, etc.)
  - `SRC/LOADING CONTENT/`: Loading screen images and copy.
  - `SRC/loading_screen_content/`: Additional loading resources.
- `MAPS/`: Map layouts, room definitions, and spawn configurations referenced by the manifest.
- `content/`: Dynamic content folders (e.g., `content/test`, `content/forrest`) containing map-specific assets and music.
- `manifest.json`: Central configuration file defining assets, maps, image effects, and game settings.
- `vcpkg/`: Local vcpkg installation for dependency management.
- `external/`: Bundled external libraries (e.g., nlohmann_json).
- `TESTS/`: Unit tests for core engine systems.
- `CMakeLists.txt`, `CMakePresets.json`: Build configuration files.
- `run.bat`: Automated Windows build and run script.
- `LICENSE`: MIT License.

The manifest system separates content from code, allowing for rapid iteration without recompilation.

## Prerequisites

### Windows (Primary Platform)
- Windows 10/11
- Internet connection (for automatic dependency downloads)
- Administrator privileges (recommended for system-wide tool installs)

The `run.bat` script handles all prerequisite setup automatically on first run.

### Manual Setup (Optional)
If preferred, install manually:
- Git
- Visual Studio Build Tools (or full VS 2022) with MSVC, CMake, and Windows SDK
- CMake 3.16+
- vcpkg

## Dependencies

Dependencies are managed via vcpkg and specified in `vcpkg.json`. Key libraries include:

- **SDL2**: Core multimedia library
- **SDL2_image**: Image loading support
- **SDL2_mixer**: Audio playback
- **SDL2_ttf**: Font rendering
- **GLAD**: OpenGL loader
- **nlohmann_json**: JSON parsing (bundled in `external/` if not available via vcpkg)

## Developer Mode

### Enabling Dev Mode
- Press **Ctrl+D** or select **Toggle Dev Mode** from the pause menu.
- Dev Mode enables automatically if the active map does not define a player asset.

### Dev Mode Features
- Rendering quality drops to improve responsiveness (`SDL_HINT_RENDER_SCALE_QUALITY = "0"`).
- `DevControls` provides editors for rooms, maps, assets, lighting, and spawns. Panels respond to pointer and keyboard input and write through the manifest system.
- Settings such as filters and UI layout persist in `dev_mode_settings.json`.

### Using the Toolkit
1. Open the pause menu with **Esc** and enable Dev Mode (or use **Ctrl+D**).
2. Interact with assets to open editors, adjust room geometry, or modify lighting.
3. Save changes through the panels so updates reach the content files.

## Custom Controllers

### File Layout
Store controller headers and source files under `ENGINE/animation_update/custom_controllers/`.

### Registering a Controller
1. Implement the controller class (derived from `AssetController`).
2. Include the header and add a branch to `ControllerFactory::create_by_key` for the new type.
3. Rebuild the project.

### Linking Controllers to Content
- Add `"custom_controller_key": "YourController"` to an asset `info.json` file or assign it through the Dev Mode editor. The value is copied into `AssetInfo` for use at spawn time.
- The Dev Mode `CustomControllerService` can scaffold files, update factory includes, and adjust manifest metadata.

Custom controllers allow new behaviour without modifying unrelated engine systems.

## Testing

Unit tests are located in the `TESTS/` directory. Build and run tests with:

```cmd
# From project root (after running run.bat once to set up environment)
cmake --build --preset windows-vcpkg-release --target engine_tests
ENGINE/engine_tests.exe
```

## Animation Rebuild Workflow

- Each animation frame in `manifest.json` now carries a `needs_rebuild` flag under the animation's `frames` array. The flag covers all variants (normal/foreground/background/mask) for that frame.
- Use `tools/set_rebuild_values.py` to mark work:
  - `python tools/set_rebuild_values.py all` marks every frame for rebuild.
  - `python tools/set_rebuild_values.py asset <asset>` marks all frames for one asset.
  - `python tools/set_rebuild_values.py animation <asset> <animation>` marks all frames in one animation.
  - `python tools/set_rebuild_values.py frame <asset> <animation> <index>` marks a single frame.
- Run `python tools/asset_tool.py` to rebuild all frames flagged in the manifest; the tool clears `needs_rebuild` back to `false` after successful generation.
- C++ call sites now toggle `needs_rebuild` via the helper script and then invoke `asset_tool.py`; there is no rebuild queue or per-animation cache fingerprinting anymore.

## Lighting Rebuild Workflow

- Each light definition in `manifest.json` now carries a `needs_rebuild` flag under its entry in the asset's `lighting_info` array.
- Use `python tools/set_rebuild_values.py lighting_all` to mark every light in the manifest, `lighting_asset <asset>` to mark every light for one asset, or `lighting_light <asset> <index>` to flag a specific light definition by index.
- Run `python tools/light_tool.py` to regenerate the `.png` cache for all assets that still expose `needs_rebuild` entries; successful runs reset those flags so future runs skip unchanged lights.
- Dev Mode now marks the right light definitions automatically whenever you change intensity, radius, falloff, or color, or when you add/remove lights, so manual flagging is rare. Flicker, offset, and rendering-layer tweaks no longer trigger cache regenerations.

Tests cover core systems like asset loading, manifest parsing, and rendering prerequisites.

## License

MIT License - see [LICENSE](LICENSE) for details.

## AnimationUpdate Public API Documentation

The `AnimationUpdate` class provides the restricted public interface for custom controllers. Access is limited to core planning, movement, and animation setting methods to maintain internal engine state isolation.

### Constructors
- **AnimationUpdate(Asset* self, Assets* assets)**  
  Creates an `AnimationUpdate` instance tied to a specific asset and assets manager.

### Debug Control
- **void set_debug_enabled(bool enabled)**  
  Enables or disables debug logging for movement planning and pathing. Useful for development and troubleshooting AI behaviors.

### Path Planning
- **void auto_move(const std::vector<SDL_Point>& rel_checkpoints, int visited_thresh_px, std::optional<int> checkpoint_resolution, bool override_non_locked)**  
  Plans a multi-point path relative to the asset's current position. Checkpoints are visited in order; `visited_thresh_px` is the pixel radius considered "reached" (e.g., 5). `checkpoint_resolution` can override grid resolution for planning (default uses pixel-level precision). `override_non_locked` allows movement during locked animations if true.

- **void auto_move(SDL_Point rel_checkpoint, int visited_thresh_px, std::optional<int> checkpoint_resolution, bool override_non_locked)**  
  Plans a single-point move to a relative checkpoint. Otherwise same as above.

- **void auto_move(Asset* target_asset, int visited_thresh_px, bool override_non_locked)**  
  Plans a path to follow and catch up to a moving target asset (e.g., enemy pursuing player). Target is checked at planning time only—use in update loop for continuous pursuit.

### Plan Inspection
- **int visit_threshold_px() const**  
  Returns the current visit threshold in pixels used for path planning (how close the asset needs to get to consider a checkpoint reached).

### Immediate Movement
- **void move(SDL_Point delta, const std::string& animation, bool resort_z, bool override_non_locked)**  
  Requests an immediate one-shot movement by `delta` (pixel offset) and switches to the specified animation. `resort_z` re-sorts the asset in depth order if true. Useful for instant jumps or teleports.

### Animation Control
- **void set_animation(const std::string& animation_id)**  
  Looks up and sets the animation from the asset's info by ID; playback now always runs at the engine's fixed 24 fps timing.

---

This engine is designed for 2D game development with an emphasis on content-driven workflows and rapid prototyping.
