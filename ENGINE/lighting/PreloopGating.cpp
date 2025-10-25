#include "lighting/PreloopGating.hpp"

#include "world/chunk.hpp"

namespace lighting {

bool PreloopGating::verifyReady(const world::Chunk& chunk) const {
    (void)chunk;
    return true;
}

bool PreloopGating::tryRetry(world::Chunk& chunk) const {
    (void)chunk;
    return false;
}

bool PreloopGating::allChunksReady(const std::vector<world::Chunk*>& chunks) const {
    (void)chunks;
    return true;
}

} // namespace lighting

