# VIBBLE - Custom 2D Game Engine
<p align="center">
  <img src="https://github.com/cabbabbage/VIBBLE_2D_GAME_ENGINE/blob/main/MISC_CONTENT/promo2.png?raw=true" alt="Logo Title Text 2">
</p>

## 1. Engine Core & Runtime Loop
The runtime (see `ENGINE/engine.cpp`, `main.cpp`) initializes SDL, loads the map and assets, then runs the game loop.

### Core Flow
- **Initialization**
  - `AssetLoader` reads `map_info.json`, constructs rooms (`GenerateRooms`) and trails (`GenerateTrails`), loads all asset metadata (`AssetInfo`), then returns an `Assets` manager plus a generated **minimap**.
  - `SceneRenderer` is initialized with the SDL renderer, the `Assets` manager, and `RenderUtils`, and sets up the **global map light**.
- **Game Loop**
  - Polls input (`MouseInput`).
  - Updates UI (`MenuUI`, `MainMenu`).
  - Updates world via `Assets` (movement, animation, shading, lights).
  - Renders the frame through `SceneRenderer`.

---

## 2. Procedural Generation (Rooms, Trails, Boundary)
Procedural layout is driven by JSON map definitions.

- **Layers & Sectors**
  - Each layer has a **radius** and **min/max rooms**. `GenerateRooms` samples counts, assigns children to **angular sectors**, and prevents overlap.
  - Layer 0 is spawn; deeper layers inherit parent sectors and subdivide.
  - Parents can specify **required children**, which are injected before random picks.
- **Room Geometry**
  - Each `Room` owns an `Area` polygon (circle, square, or JSON-loaded). Areas provide bounds, center, size, collision, and can generate SDL textures.
- **Trail Generation** (`GenerateTrails`, `TrailGeometry`)
  - Connects parent–child and cluster pairs with trail assets.
  - Trails are ribbons extruded from polylines, respecting intersection limits.
  - Includes passes to **reconnect isolated groups** and optionally add **circular connections** around the outer layer.
- **Boundary Spawning**
  - If `map_info.json` specifies a boundary file, `AssetSpawner` spawns boundary assets into a circular boundary `Area` and assigns them to the closest room.

---

## 3. Asset System

### `AssetInfo`
- Loads **metadata** from `info.json`: type, tags, z-index, child depth, duplication rules.
- Loads **animations** (`Animation`) with scaling, blend mode, loop rules.
- Loads **collision/spacing/interaction/attack** `Area`s from JSON or generates fallbacks.
- Loads **lighting** definitions: static, orbital, intensity, fall-off, flicker, flare.
- Stores and generates cached textures for area visualization and light sprites.

### `Asset`
- Runtime instance with:
  - Position, z-index, current animation, flip state, alpha.
  - Children, lights, and areas.
  - Exposes `get_current_frame()` and placement helpers.

### `Assets` (Manager)
- Holds all `Asset`s and tracks the player.
- Updates movement, collision, animation, light/shading states.
- Provides accessors for room-related queries (via `find_current_room`).

---

## 4. Active Asset Culling
`ActiveAssetsManager` keeps only relevant assets active:
- Uses player/camera proximity.
- Recursively activates children of active assets.
- Optimizes update/render passes.

---

## 5. Rendering Pipeline (`SceneRenderer`)

- **Global Map Light (`Global_Light_Source`)**
  - Loaded from `map_light.json`.
  - Has base color, falloff, radius, orbit radius, intensity, and **key colors** by angle.
  - Updates orbit angle and interpolates color by horizon angle, producing day/night cycles.
  - Generates a radial SDL texture via `GenerateLight`.

- **Dynamic & Orbital Lights**
  - Assets with lights attach static or orbital lights.
  - Orbital lights move elliptically with respect to parent and global light angle.
  - Lights flicker, fade, and jitter per frame if configured.

- **Shading**
  - Assets with `has_shading` get **mask textures** that blend static, dynamic, and global lights.
  - Lights rendered with additive/multiplicative blend modes (simulating shaders in SDL).

- **Z-layered Rendering**
  - Assets are sorted by z-index.
  - Assets render first, then lights are composited above or below based on z-order.

- **Minimap**
  - `AssetLoader::createMinimap()` draws room bounds (red) and trails (green) to a high-res texture, then downsamples for HUD display.

---

## 6. Geometry System (`Area`)
- **Construction**
  - Supports circle and square generators with configurable roughness.
  - Can load polygons from JSON (scaled, aligned bottom-center).
- **Capabilities**
  - Bounds (`get_bounds`), center, area size, collision detection (`contains_point`, `intersects`).
  - Random point sampling and union with other areas.
  - Horizontal flipping (used for mirrored assets).
  - Debug texture generation (`create_area_texture`).

---

## 7. Lighting Details
- **GenerateLight**
  - Builds textures with radial fall-off and optional flare.
  - Alpha fades 255 → 0 from center to edge, scaled by `fall_off`.
- **Static vs. Dynamic**
  - Static lights generated once from asset JSON.
  - Orbital/dynamic lights update position/brightness each frame.
- **Global Tint**
  - `Global_Light_Source` computes horizon tint and brightness; tint modulates shaded assets.

---

## 8. Input & Developer Tools
- **Input**
  - `MouseInput` handles SDL mouse events, hover, and click checks.
- **Dev Controls**
  - `DevMouseControls` supports debug highlighting and logging asset interactions.
- **Menus & UI**
  - `MenuUI` and `MainMenu` provide UI layers.
  - `ui_overlay` adds in-game HUD elements.

---

## 9. Utilities
- **RenderUtils**
  - Parallax mapping, trapezoid projection, camera shake, minimap rendering.
  - Light distortion effects with scale/rotation jitter based on edge position.
- **CacheManager**
  - Saves/loads animation frames and area surfaces with metadata to disk.
- **Misc**
  - Helpers: `blur_util`, `fade_textures`, `view`, `zoom_control`.
  - File dialogs via `tinyfiledialogs`.

---

## 10. Python Tool – *VIBBLE Manager*
A Tkinter-based editor for asset and map JSON:
- Edit `info.json` (animations, lights, tags, areas).
- Visualize and export polygon areas for `Area`.
- Manage animations and scale factors.
- Edit procedural `map_info.json` (layer configs, radii, rooms).
- Validate alignment and asset previews (bottom-center anchor rule).

---

## 11. Special Effects
- Implemented in pure SDL2 without GLSL:
  - **Blend modes** (`SDL_BLENDMODE_ADD`, `MOD`, `MUL`) simulate lighting/shadows.
  - **Procedural textures** (lights, masks, gradients) replace shaders.
  - **Orbital lights** act like dynamic spotlights using software blending.

---
