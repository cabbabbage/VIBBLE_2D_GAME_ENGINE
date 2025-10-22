#pragma once

#include <vector>

namespace world {
class Chunk;
} // namespace world

namespace lighting {

class PreloopGating {
public:
    bool verifyReady(const world::Chunk& chunk) const;
    bool tryRetry(world::Chunk& chunk) const;
    bool allChunksReady(const std::vector<world::Chunk*>& chunks) const;
};

} // namespace lighting

