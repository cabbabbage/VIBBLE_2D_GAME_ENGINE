#include "cache_manager.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>

// Namespace for cache management functions
namespace CacheManager {

// Placeholder implementation for backwards compatibility
// Since we've moved cache generation to Python, these are now minimal stubs
// that should not be called in normal operation (only for legacy/test code)

// Load a surface sequence from cached PNG files
bool load_surface_sequence(const std::string& folder, int frame_count, std::vector<SDL_Surface*>& surfaces) {
    surfaces.clear();
    surfaces.reserve(frame_count);

    for (int i = 0; i < frame_count; ++i) {
        std::string frame_path = folder + "/" + std::to_string(i) + ".png";
        SDL_Surface* surface = IMG_Load(frame_path.c_str());
        if (!surface) {
            std::cerr << "[CacheManager] Failed to load surface from: " << frame_path << ": " << IMG_GetError() << std::endl;
            // Free already loaded surfaces on failure
            for (SDL_Surface* surf : surfaces) {
                if (surf) SDL_FreeSurface(surf);
            }
            surfaces.clear();
            return false;
        }
        surfaces.push_back(surface);
    }
    return true;
}

// Save a surface sequence - should not be called for new generation  
bool save_surface_sequence(const std::string& folder, const std::vector<SDL_Surface*>& surfaces) {
    std::cerr << "CacheManager::save_surface_sequence called - this should not happen in new architecture!" << std::endl;
    std::cerr << "Folder: " << folder << ", surfaces: " << surfaces.size() << std::endl;
    return false;
}

// Load metadata from cache
bool load_metadata(const std::string& file_path, nlohmann::json& metadata) {
    try {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            return false;
        }
        file >> metadata;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load metadata from " << file_path << ": " << e.what() << std::endl;
        return false;
    }
}

// Save metadata to cache
bool save_metadata(const std::string& file_path, const nlohmann::json& metadata) {
    try {
        std::filesystem::create_directories(std::filesystem::path(file_path).parent_path());
        std::ofstream file(file_path);
        if (!file.is_open()) {
            return false;
        }
        file << metadata.dump(2);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to save metadata to " << file_path << ": " << e.what() << std::endl;
        return false;
    }
}

// Load surface from file
SDL_Surface* load_surface(const std::string& file_path) {
    if (file_path.empty()) {
        return nullptr;
    }
    SDL_Surface* surface = IMG_Load(file_path.c_str());
    if (!surface) {
        std::cerr << "Failed to load surface from " << file_path << ": " << IMG_GetError() << std::endl;
    }
    return surface;
}

// Convert surface to texture
SDL_Texture* surface_to_texture(SDL_Renderer* renderer, SDL_Surface* surface) {
    if (!renderer || !surface) {
        return nullptr;
    }
    return SDL_CreateTextureFromSurface(renderer, surface);
}

std::optional<nlohmann::json> load_metadata(const std::string& meta_file) {
    nlohmann::json metadata;
    if (load_metadata(meta_file, metadata)) {
        return metadata;
    }
    return std::nullopt;
}

// PNG saving functions (deprecated - now handled by Python)
bool save_png(SDL_Surface* surface, const std::string& path) {
    if (!surface || path.empty()) {
        return false;
    }
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    int result = IMG_SavePNG(surface, path.c_str());
    if (result != 0) {
        std::cerr << "Failed to save PNG to " << path << ": " << IMG_GetError() << std::endl;
        return false;
    }
    return true;
}

bool save_png_from_pixels(const uint8_t* rgba_pixels, int width, int height, const std::string& path) {
    if (!rgba_pixels || width <= 0 || height <= 0 || path.empty()) {
        return false;
    }
    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        (void*)rgba_pixels,
        width, height, 32, width * 4,
        0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
    );
    if (!surface) {
        std::cerr << "Failed to create surface from pixels: " << SDL_GetError() << std::endl;
        return false;
    }
    bool result = save_png(surface, path);
    SDL_FreeSurface(surface);
    return result;
}

bool save_surface_as_png(SDL_Surface* surface, const std::string& path) {
    return save_png(surface, path);
}

} // namespace CacheManager
