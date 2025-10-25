#pragma once

#include <algorithm>
#include <vector>

#include "world/chunk.hpp"

namespace lighting {

inline bool chunk_ready_for_static_preload(const world::Chunk& chunk) {
    (void)chunk;
    return true;
}

inline bool reset_chunk_retry_flags(world::Chunk& chunk) {
    (void)chunk;
    return false;
}

inline bool all_chunks_ready_for_static_preload(const std::vector<world::Chunk*>& chunks) {
    (void)chunks;
    return true;
}

} // namespace lighting

