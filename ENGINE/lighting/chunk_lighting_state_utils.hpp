#pragma once

#include <algorithm>
#include <vector>

#include "world/chunk.hpp"

namespace lighting {

inline bool chunk_ready_for_static_preload(const world::Chunk& chunk) {
    return chunk.static_light_mask != nullptr &&
           chunk.lighting_preloaded &&
           chunk.static_clean &&
           !chunk.needs_retry;
}

inline bool reset_chunk_retry_flags(world::Chunk& chunk) {
    if (!chunk.needs_retry) {
        return false;
    }

    chunk.needs_retry        = false;
    chunk.lighting_preloaded = false;
    chunk.static_clean       = false;
    return true;
}

inline bool all_chunks_ready_for_static_preload(const std::vector<world::Chunk*>& chunks) {
    return std::all_of(chunks.begin(), chunks.end(), [](const world::Chunk* chunk) {
        return chunk == nullptr || chunk_ready_for_static_preload(*chunk);
    });
}

} // namespace lighting

