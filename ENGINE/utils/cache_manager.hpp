#pragma once

#include <SDL.h>
#include <SDL_image.h>

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace CacheManager {

    bool load_surface_sequence(const std::string& folder, int frame_count, std::vector<SDL_Surface*>& loaded);
    bool save_surface_sequence(const std::string& folder, const std::vector<SDL_Surface*>& images);

    SDL_Surface* load_surface(const std::string& path);

    std::optional<json> load_metadata(const std::string& meta_file);
    bool load_metadata(const std::string& meta_file, json& out_json);
    bool save_metadata(const std::string& meta_file, const json& meta);

    SDL_Texture* surface_to_texture(SDL_Renderer* renderer, SDL_Surface* surface);

}
