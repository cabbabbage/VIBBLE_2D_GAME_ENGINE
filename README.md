# VIBBLE - 2D Game Engine

VIBBLE is an SDL2-based 2D engine with external data files for maps, assets, and animations. The engine keeps game content out of the executable so builds stay small and iteration does not require recompilation.

---

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
```bash
./run.bat
```
The script configures a RelWithDebInfo build, runs CMake, and launches the engine from the correct working directory. Manual CMake builds work as well.

Project layout:
- `ENGINE/`: runtime code for assets, controllers, rendering, UI, and developer tools.
- `SRC/`: asset folders with `info.json`, sprites, areas, and lighting data.
- `MAPS/`: map layouts and spawn definitions referenced by the manifest.
- `loading/`: splash art and copy for the loading screen.

---

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

---

## Custom Controllers

### File Layout
Store controller headers and source files under `ENGINE/custom_controllers/`.

### Registering a Controller
1. Implement the controller class (derived from `AssetController`).
2. Include the header and add a branch to `ControllerFactory::create_by_key` for the new type.
3. Rebuild the project.

### Linking Controllers to Content
- Add `"custom_controller_key": "YourController"` to an asset `info.json` file or assign it through the Dev Mode editor. The value is copied into `AssetInfo` for use at spawn time.
- The Dev Mode `CustomControllerService` can scaffold files, update factory includes, and adjust manifest metadata.

Custom controllers allow new behaviour without modifying unrelated engine systems.
