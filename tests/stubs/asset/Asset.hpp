#pragma once

#include <SDL.h>

class Asset {
public:
    SDL_Point pos{0, 0};
    int       grid_resolution = 0;
};
