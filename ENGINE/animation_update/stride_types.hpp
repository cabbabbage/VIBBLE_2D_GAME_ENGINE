#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <SDL.h>

namespace axis {

struct WorldPos {
    int x = 0;
    int y = 0;
    int z = 0;
};

inline WorldPos make_world_pos(int x, int y, int z) {
    return WorldPos{ x, y, z };
}

inline SDL_Point project_xz(WorldPos pos) {
    return SDL_Point{ pos.x, pos.z };
}

inline WorldPos from_xz(SDL_Point point, int world_y = 0) {
    return WorldPos{ point.x, world_y, point.y };
}

} // namespace axis

struct Stride {
    std::string animation_id;
    int         frames = 0;
    std::size_t path_index = 0;
};

struct Plan {
    std::vector<axis::WorldPos> sanitized_checkpoints;
    std::vector<Stride>    strides;
    axis::WorldPos         final_dest{};
    axis::WorldPos         world_start{};
    bool                   override_non_locked = true;
};
