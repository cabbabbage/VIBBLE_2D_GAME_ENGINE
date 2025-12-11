#pragma once

#include <SDL.h>
#include <vector>

struct GridTile {
    SDL_Rect    world_rect{0, 0, 0, 0};
    SDL_Texture* texture = nullptr;
};

