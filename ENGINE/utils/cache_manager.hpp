#pragma once

#include <string>
#include <vector>
#include <SDL.h>
#include <nlohmann/json.hpp>

class CacheManager {
public:
    static bool load_metadata(const std::string& meta_file, nlohmann::json& out_meta);
    static bool save_metadata(const std::string& meta_file, const nlohmann::json& meta);
    static SDL_Surface* load_surface(const std::string& path);
    static bool save_surface_as_png(SDL_Surface* surface, const std::string& path);
    static bool load_surface_sequence(const std::string& folder, int frame_count, std::vector<SDL_Surface*>& surfaces);
    static bool save_surface_sequence(const std::string& folder, const std::vector<SDL_Surface*>& surfaces);
    static SDL_Surface* load_and_scale_surface(const std::string& path, float scale, int& out_w, int& out_h);
    static SDL_Texture* surface_to_texture(SDL_Renderer* renderer, SDL_Surface* surface);
    static std::vector<SDL_Texture*> surfaces_to_textures(SDL_Renderer* renderer, const std::vector<SDL_Surface*>& surfaces);
};
