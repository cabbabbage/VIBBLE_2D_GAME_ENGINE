#pragma once

#include <string>
#include <vector>

#include <SDL.h>

struct Stride {
    std::string animation_id;
    int         frames = 0;
};

struct Plan {
    std::vector<SDL_Point> sanitized_checkpoints;
    std::vector<Stride>    strides;
    SDL_Point              final_dest{0, 0};
};
