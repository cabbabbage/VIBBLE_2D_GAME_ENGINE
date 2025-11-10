#pragma once

#include <SDL.h>

struct LightSource {
        int         intensity        = 255;
        int         radius           = 64;
        int         fall_off         = 50;
        int         flare            = 0;
        int         flicker          = 20;
        int         offset_x         = 0;
        int         offset_y         = 0;
        SDL_Color   color            = {255, 255, 255, 255};
        // Rendering controls
        bool        in_front            = false; // render light texture in front of asset
        bool        behind              = false; // render light texture behind asset
        bool        render_to_dark_mask = false; // carve light out of the dynamic darkness overlay
        bool        render_front_and_back_to_asset_alpha_mask = false; // copy light textures into asset alpha mask
        int         cached_w         = 0;
        int         cached_h         = 0;
        SDL_Texture* texture         = nullptr;
};
