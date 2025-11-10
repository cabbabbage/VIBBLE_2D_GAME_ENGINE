#pragma once

#include <SDL.h>
#include <vector>

// Lightweight representation of a single grid-aligned tile and its texture.
// The texture is an RGBA target containing the composition of all tileable
// assets that intersect this tile's world_rect.
struct GridTile {
    SDL_Rect    world_rect{0, 0, 0, 0};
    SDL_Texture* texture = nullptr; // owned by the chunk that holds it
};

