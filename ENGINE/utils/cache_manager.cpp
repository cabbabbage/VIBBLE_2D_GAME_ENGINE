#include "cache_manager.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>

namespace CacheManager {

bool load_surface_sequence(const std::string& folder, int frame_count, std::vector<SDL_Surface*>& surfaces) {
    surfaces.clear();
    surfaces.reserve(frame_count);

    std::cout << "[CacheManager] load_surface_sequence: folder=" << folder
              << " frame_count=" << frame_count << std::endl;

    for (int i = 0; i < frame_count; ++i) {
        std::string frame_path = folder + "/" + std::to_string(i) + ".png";
        SDL_Surface* surface = IMG_Load(frame_path.c_str());
        if (!surface) {
            std::cerr << "[CacheManager] Failed to load surface from: " << frame_path
                      << " (IMG_Error: " << IMG_GetError() << ")" << std::endl;

            for (SDL_Surface* surf : surfaces) {
                if (surf) SDL_FreeSurface(surf);
            }
            surfaces.clear();
            return false;
        }
        surfaces.push_back(surface);
    }

    std::cout << "[CacheManager] Successfully loaded " << surfaces.size() << " surfaces from " << folder << std::endl;
    return true;
}

bool save_surface_sequence(const std::string& folder, const std::vector<SDL_Surface*>& surfaces) {
    std::cerr << "CacheManager::save_surface_sequence called - this should not happen in new architecture!" << std::endl;
    std::cerr << "Folder: " << folder << ", surfaces: " << surfaces.size() << std::endl;
    return false;
}

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

}
