#include "world/chunk.hpp"

#include <SDL.h>

namespace world {

Chunk::~Chunk() {
    if (static_light_map) {
        SDL_DestroyTexture(static_light_map);
        static_light_map = nullptr;
    }
}

} // namespace world

