#pragma once

#include <SDL.h>
#include <SDL_image.h>

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Minimal stub for CacheManager class that has been migrated to Python.
// This header only exists to satisfy #includes during the build,
// but the actual implementation is in tools/cache_manager.py

namespace CacheManager {

    // Surface loading/caching (now handled by Python)
    bool load_surface_sequence(const std::string& folder, int frame_count, std::vector<SDL_Surface*>& loaded);
    bool save_surface_sequence(const std::string& folder, const std::vector<SDL_Surface*>& images);

    // Loading methods for asset_info.cpp
    SDL_Surface* load_surface(const std::string& path);

    // Metadata handling
    std::optional<json> load_metadata(const std::string& meta_file);
    bool load_metadata(const std::string& meta_file, json& out_json);
    bool save_metadata(const std::string& meta_file, const json& meta);

    // Texture conversion
    SDL_Texture* surface_to_texture(SDL_Renderer* renderer, SDL_Surface* surface);

} // namespace CacheManager
