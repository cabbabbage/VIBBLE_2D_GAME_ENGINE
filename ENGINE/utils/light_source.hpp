#pragma once

#include <SDL.h>

struct LightSource {
        int         intensity        = 255;
        int         radius           = 64;
        int         fall_off         = 50;
        int         flare            = 0;
        int         flicker_speed    = 0;
        int         flicker_smoothness = 100;
        int         offset_x         = 0;
        int         offset_y         = 0;
        SDL_Color   color            = {255, 255, 255, 255};

        bool        in_front            = false;
        bool        behind              = false;
        bool        render_to_dark_mask = false;
        bool        render_front_and_back_to_asset_alpha_mask = false;
        int         cached_w         = 0;
        int         cached_h         = 0;
        SDL_Texture* texture         = nullptr;
};
