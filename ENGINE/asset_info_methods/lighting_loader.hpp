#pragma once

#include <SDL.h>
#include <nlohmann/json.hpp>
class AssetInfo;

class LightingLoader {
public:
    static void load(AssetInfo& info, const nlohmann::json& data);
    static void generate_textures(AssetInfo& info, SDL_Renderer* renderer);
};
