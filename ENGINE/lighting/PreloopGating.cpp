#include "lighting/PreloopGating.hpp"

#include "world/chunk.hpp"

namespace lighting {

bool PreloopGating::verifyReady(const world::Chunk& chunk) const {
    return chunk.static_light_mask != nullptr &&
           chunk.lighting_preloaded &&
           chunk.static_clean &&
           !chunk.needs_retry;
}

bool PreloopGating::tryRetry(world::Chunk& chunk) const {
    if (!chunk.needs_retry) {
        return false;
    }
    chunk.needs_retry        = false;
    chunk.lighting_preloaded = false;
    chunk.static_clean       = false;
    return true;
}

bool PreloopGating::allChunksReady(const std::vector<world::Chunk*>& chunks) const {
    for (const world::Chunk* chunk : chunks) {
        if (!chunk) {
            continue;
        }
        if (!verifyReady(*chunk)) {
            return false;
        }
    }
    return true;
}

} // namespace lighting

