#include <SDL.h>

#include "world/chunk.hpp"

namespace world {

Chunk::Chunk(int in_i, int in_j, int r, SDL_Rect bounds)
    : i(in_i), j(in_j), r_chunk(r), world_bounds(bounds) {}

Chunk::~Chunk() = default;

} // namespace world
