#pragma once

#include <SDL.h>
class AssetInfo;
class nlohmann_json_fwd; // unused forward

class LightingLoader {
public:
    static void load(AssetInfo& info, const nlohmann::json& data);
    static void generate_textures(AssetInfo& info, SDL_Renderer* renderer);
};

