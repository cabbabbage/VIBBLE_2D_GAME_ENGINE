# VIBBLE - 2D Game Engine (Departed Affairs and Co.)

## Links
- Engine and Dev Mode design overview video:<br>
  <a href="https://youtu.be/QaqOHtNQzZs" target="_blank">
    <img src="https://img.youtube.com/vi/QaqOHtNQzZs/0.jpg" alt="Watch the design overview on YouTube" width="560">
  </a>
- Take a look at the flow chart of the game structure:
  ![Game structure flow chart](SRC/misc_content/engine.drawio.svg)
  [flow chart](https://app.diagrams.net/#Hcabbabbage%2FVIBBLE_2D_GAME_ENGINE%2Fplayer_fix%2Fengine.drawio.xml#%7B%22pageId%22%3A%224_JYmeCipBGX9XNcjFr7%22%7D)


## Installation
### Quick start (Windows)
1. Clone the repo.
2. Run `setup.bat` from the project root if you wish to build and compile compile.

The script installs build tools (Git, MSVC build tools, CMake, Ninja, vcpkg), fetches dependencies, configures a RelWithDebInfo build, compiles, and launches the engine. Requires Windows 10/11, internet, and admin rights are recommended for tool installs.


## Overview
- SDL2-based 2D engine; content lives in external JSON-driven files for maps, assets, lighting, and animations.
- Manifest-driven loading keeps game data out of the executable so asset changes do not require recompiles.
- Dev Mode ships in-engine for editing rooms, lighting, assets, and spawns with immediate write-through to content files.
- Asset tools regenerate animation and lighting caches from manifest flags to keep generated art in sync.

## Project Layout
- `ENGINE/`: Runtime source for assets, controllers, rendering, UI, and dev tools.
- `SRC/`: Source art and `info.json` definitions; includes loading screen content.
- `MAPS/`: Map layouts, rooms, and spawn data referenced by the manifest.
- `content/`: Runtime content packs (e.g., `content/test`, `content/forrest`).
- `tools/`: Helper scripts like `set_rebuild_values.py`, `asset_tool.py`, and `light_tool.py`.
- `TESTS/`: Unit tests for core systems.
- `vcpkg/`, `external/`: Dependency management and bundled libs.

## Running
- Preferred: run `run.bat` to configure, build, and start the engine.
- To build fresh run `compile_and_run.bat`; it launches `release\engine.exe` produced by the build.
- Repeat runs reuse the configured build; rerun `compile_and_run.bat` after pulling dependency changes.

## Dev Mode
- Toggle with `Ctrl+D` or through the pause menu (`Esc`), or it auto-enables when a map lacks a player.
- Reduces render quality for responsiveness and exposes editors for maps, assets, lighting, and spawns. Settings persist in `dev_mode_settings.json`.

## Custom Controllers
- Add new controllers under `ENGINE/animation_update/custom_controllers/` and register them in `ControllerFactory::create_by_key`.
- Link from content with `"custom_controller_key": "YourController"` in an asset `info.json` or via the Dev Mode editors.

## Testing
- Build tests: `cmake --build --preset windows-vcpkg-release --target engine_tests`
- Run tests: `ENGINE/engine_tests.exe`

## License

MIT License - see `LICENSE`.
