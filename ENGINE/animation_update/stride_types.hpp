#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <SDL.h>

struct Stride {
    std::string animation_id;
    int         frames = 0;
    std::size_t path_index = 0;
};

struct Plan {
    std::vector<SDL_Point> sanitized_checkpoints;
    std::vector<Stride>    strides;
    SDL_Point              final_dest{0, 0};
    SDL_Point              world_start{0, 0};
    bool                   override_non_locked = true;
};
