#pragma once

#include <SDL.h>

class Asset;

namespace runtime_lighting {

struct AssetLight {
    Asset*   asset            = nullptr;
    SDL_Rect asset_rect{0, 0, 0, 0};
    int      base_width       = 0;
    int      base_height      = 0;
    bool     flipped          = false;
    float    asset_base_scale = 1.0f;
    bool     has_dark_mask_lights = false;
    bool     has_front_lights     = false;
    bool     has_back_lights      = false;
};

}

