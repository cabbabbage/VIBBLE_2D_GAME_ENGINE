# Legacy Code Removal Tasks

This document lists all identified legacy, unused, and deprecated code in the VIBBLE 2D game engine that can be safely removed without breaking functionality. Each item includes file paths, line ranges, and explanations. All suggested removals are based on code comments, lack of references, and architectural changes (e.g., moved functionality to Python). Items are checked when removal is complete.

## 1. Cache Manager Deprecation
The entire CacheManager namespace was marked as deprecated when cache generation was moved to Python. All functions are either stubs, log errors when called, or handle legacy scenarios no longer needed in the current architecture. No references to CacheManager have been found in the codebase.

- [ ] Remove entire namespace `CacheManager` in `ENGINE/utils/cache_manager.hpp` (entire file, lines 1-end)
- [ ] Remove entire namespace `CacheManager` in `ENGINE/utils/cache_manager.cpp` (entire file, lines 1-end)

## 2. Legacy Asset Registration Overload
The raw pointer overload of `register_asset` is marked as a "legacy helper" that transfers ownership, violating RAII principles. The unique_ptr version should be used instead.

- [ ] Remove function declaration `Asset* register_asset(Asset* a)` in `ENGINE/world/world_grid.hpp` (line 29)
- [ ] Remove function implementation `Asset* WorldGrid::register_asset(Asset* a)` in `ENGINE/world/world_grid.cpp` (lines 125-127)

## 3. Legacy Loading Content Root Functions
These functions provide fallback support for the older directory structure ("SRC/loading_screen_content") when the newer "SRC/LOADING CONTENT" is empty. They maintain backwards compatibility but can be removed if content has migrated to the new paths.

- [ ] Remove function `legacy_loading_content_root()` in `ENGINE/ui/main_menu.cpp` (lines 120-122)
- [ ] Remove function `legacy_loading_content_root()` in `ENGINE/ui/loading_screen.cpp` (lines 29-31)
- [ ] Remove related fallback logic that calls `legacy_loading_content_root()` in `ENGINE/ui/main_menu.cpp` (around line 357)
- [ ] Remove related fallback logic that calls `legacy_loading_content_root()` in `ENGINE/ui/loading_screen.cpp` (around lines 139-142)

## 4. Incomplete Image Effect Code
The `set_global_state` function contains a TODO indicating it's not implemented and only suppresses unused parameter warnings. This functionality may no longer be needed or was incomplete.

- [ ] Remove function `set_global_state()` in `ENGINE/render/image_effect_settings.cpp` (lines 31-35, including the TODO comment and namespace)

## 5. Deprecated PNG Saving Functions
While part of the CacheManager deprecation above, these specific functions are explicitly marked as deprecated since PNG handling moved to Python.

- [ ] Covered under CacheManager removal: `save_png()`, `save_png_from_pixels()`, `save_surface_as_png()` in `ENGINE/utils/cache_manager.cpp` (lines 64-98)

## 6. Unused Surface Sequence Functions
These functions were for loading/saving sequences from cached files, now obsolete since cache moved to Python.

- [ ] Covered under CacheManager removal: `load_surface_sequence()`, `save_surface_sequence()` in `ENGINE/utils/cache_manager.cpp` (lines 8-18, 20-26)

## 7. Unused Metadata Functions
If the simpler overloads are not called elsewhere (confirmed via search), these can be removed. The header file's overloads may depend on implementation.

- [ ] Remove `load_metadata(const std::string& file_path, nlohmann::json& metadata)` and `save_metadata()` if not referenced in `ENGINE/utils/cache_manager.hpp`
- [ ] Covered under CacheManager removal if applicable
