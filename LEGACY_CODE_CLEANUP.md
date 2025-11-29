# Legacy Code Cleanup Tasks

This document outlines 10 identified legacy, fallback, or deprecated code elements in the ENGINE directory that can be removed to clean up the codebase. These are based on code comments, pattern analysis, and functionality that has been superseded.

**Important Notes:**
- Always create a backup or commit changes before removing code.
- Test builds and functionality after each removal.
- Some items may affect compatibility; check for dependencies in caller code.
- Use git for tracking changes: `git status`, `git diff`, etc.

## 1. Duplicate Include of Manifest Loader Header

**Description:** The header `core/manifest/manifest_loader.hpp` is included twice in `main.cpp` (likely lines 3 and 17+).

**File(s):** `ENGINE/main.cpp`

**Removal Steps:**
- Locate the duplicate `#include "core/manifest/manifest_loader.hpp"`
- Remove the redundant line, keeping only one.

**Warnings:** Ensure the include is actually used elsewhere in the file; search for `manifest::` usages.

## 2. Legacy register_asset Function Stub

**Description:** The `register_asset` function is marked as a "legacy helper; transfers ownership" with minimal stubs due to cache generation moved to Python.

**File(s):** `ENGINE/world/world_grid.hpp`

**Removal Steps:**
- Find the `register_asset` function declaration and implementation.
- If it's only a stub and not used, remove it entirely.
- Search the codebase for usages: `grep -r "register_asset"` and update callers.

**Warnings:** Might be used in legacy test code; verify no active calls.

## 3. Deprecated PNG Saving Functions

**Description:** PNG saving functions in `cache_manager.cpp` are explicitly marked "deprecated - now handled by Python".

**File(s):** `ENGINE/utils/cache_manager.cpp`

**Removal Steps:**
- Locate `save_png` function(s).
- Remove the function definition(s) and any related code.
- Search for usages and remove or replace callers.

**Warnings:** Ensure Python-based asset tools handle all PNG saving needs.

## 4. Legacy Loading Content Root Functions

**Description:** Functions like `legacy_loading_content_root()` in UI modules are fallback paths for old content directories (e.g., `SRC/loading_screen_content`).

**File(s):**
- `ENGINE/ui/loading_screen.hpp` and `ENGINE/ui/loading_screen.cpp`
- `ENGINE/ui/main_menu.hpp` and `ENGINE/ui/main_menu.cpp`

**Removal Steps:**
- Remove `legacy_loading_content_root()` functions and related logic.
- Simplify content loading to use manifest-driven paths.
- Update any code that checks for fallback to old directory.

**Warnings:** Ensure new content roots are properly configured in manifests; this might break old setups.

## 5. Legacy Spawning Behavior Fallback

**Description:** Fallback to spawning all areas as "legacy behavior" when no area selections are made in `asset_spawner.cpp`.

**File(s):** `ENGINE/spawn/asset_spawner.cpp`

**Removal Steps:**
- Locate the conditional causing fallback: if area_selection_counts is empty, select all areas.
- Remove the legacy branch, enforcing selective spawning.
- Update any dependent spawn group logic.

**Warnings:** This changes behavior; test that selective spawning works without fallback.

## 6. Legacy Serialization Field

**Description:** `grid_depth_offset_px` is kept for "legacy serialization" while runtime derives from other sources in `warped_screen_grid.hpp`.

**File(s):** `ENGINE/render/warped_screen_grid.hpp`

**Removal Steps:**
- Remove the field from the class and serialization code.
- Ensure runtime calculations don't depend on it.

**Warnings:** Check serialization/deserialization for compatibility; might break old save files.


## 8. Legacy Area Type Handling

**Description:** Comments in `dev_controls.cpp` mention ignoring "legacy area type".

**File(s):** `ENGINE/dev_mode/dev_controls.cpp`

**Removal Steps:**
- Remove code that reads/ignores legacy area types.
- Update serialization to modern formats only.

**Warnings:** Ensure dev mode UI still functions without legacy types.

## 9. Deprecated speed_factor Transition

**Description:** Multiple files mention transitioning from legacy 'speed_factor' (multiplier of base 24) to explicit FPS in animation modules.

**File(s):**
- `ENGINE/dev_mode/asset_sections/animation_editor_window/AnimationDocument.cpp`
- `ENGINE/dev_mode/asset_sections/animation_editor_window/AnimationInspectorPanel.cpp`
- `ENGINE/asset/asset_info.cpp`
- Others with speed_factor references.

**Removal Steps:**
- Replace 'speed_factor' parsing with FPS.
- Remove legacy math (e.g., fps = 24 * speed_factor).
- Update JSON handling to no longer check for speed_factor.

**Warnings:** Animation playback; ensure FPS conversion is accurate.

## 10. Legacy UI Speed Slider

**Description:** `speed_slider_` in `PlaybackSettingsPanel.hpp` is marked "// legacy UI, no longer used".

**File(s):**
- `ENGINE/dev_mode/asset_sections/animation_editor_window/PlaybackSettingsPanel.hpp`
- Likely the corresponding .cpp file.

**Removal Steps:**
- Remove the `speed_slider_` member variable and related GUI code.
- If replaced by FPS dropdown, ensure that's working.

**Warnings:** UI must still provide speed control via current elements.
