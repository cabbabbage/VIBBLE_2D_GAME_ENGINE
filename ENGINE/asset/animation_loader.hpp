#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include <SDL.h>

class Animation;
class AssetInfo;

class AnimationLoader {
public:
    struct LoadDiagnostics {
        bool cache_invalid = false;
};

    static void load(Animation& animation, const std::string& trigger, const nlohmann::json& anim_json, AssetInfo& info, const std::string& dir_path, const std::string& root_cache, float scale_factor, SDL_Renderer* renderer, SDL_Texture*& base_sprite, int& scaled_sprite_w, int& scaled_sprite_h, int& original_canvas_width, int& original_canvas_height, bool scaling_refresh_pending, LoadDiagnostics* diagnostics = nullptr);

    static bool load_child_timelines_from_json(const nlohmann::json& anim_json, Animation& animation);
};
