#pragma once

#include <SDL.h>

class AssetInfo;

class AnimationLoader {
public:
    static void load(AssetInfo& info, SDL_Renderer* renderer);
    static void get_area_textures(AssetInfo& info, SDL_Renderer* renderer);
};

