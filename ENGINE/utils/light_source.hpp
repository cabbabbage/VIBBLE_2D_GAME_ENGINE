// === File: light_source.hpp ===

#pragma once

#include <SDL.h>
#include <vector>

struct LightSource {
    int intensity = 255;
    int radius = 64;
    int fall_off = 50;
    int flare = 0;
    int flicker = 20;
    int offset_x = 0;
    int offset_y = 0;
    int x_radius = 0;
    int y_radius = 0;
    int cached_w = 0;
    int cached_h = 0;
    SDL_Color color = {255, 255, 255, 255};
    SDL_Texture* texture = nullptr;  
};
