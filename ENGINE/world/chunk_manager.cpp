#include "world/chunk_manager.hpp"

#include <algorithm>

namespace world {

int floor_div(int value, int step);

std::uint64_t ChunkManager::key(int i, int j) {
    const auto hi = static_cast<std::uint32_t>(i);
    const auto lo = static_cast<std::uint32_t>(j);
    return (static_cast<std::uint64_t>(hi) << 32) | static_cast<std::uint64_t>(lo);
}

SDL_Rect ChunkManager::bounds_for(int i, int j, int r_chunk, SDL_Point origin) {
    const int step = 1 << r_chunk;
    const int x = origin.x + i * step;
    const int y = origin.y + j * step;
    return SDL_Rect{x, y, step, step};
}

Chunk& ChunkManager::ensure(int i, int j, int r_chunk, SDL_Point origin) {
    const auto k = key(i, j);
    if (auto it = lookup_.find(k); it != lookup_.end()) {
        Chunk* existing = it->second;
        return *existing;
    }
    SDL_Rect rect = bounds_for(i, j, r_chunk, origin);
    auto chunk = std::make_unique<Chunk>(i, j, r_chunk, rect);
    Chunk* raw = chunk.get();
    storage_.push_back(std::move(chunk));
    lookup_.emplace(k, raw);
    return *raw;
}

Chunk* ChunkManager::find(int i, int j) const {
    const auto k = key(i, j);
    const auto it = lookup_.find(k);
    return it == lookup_.end() ? nullptr : it->second;
}

Chunk* ChunkManager::from_world(SDL_Point world_px, int r_chunk, SDL_Point origin) const {
    const int step = 1 << r_chunk;
    const int i = floor_div(world_px.x - origin.x, step);
    const int j = floor_div(world_px.y - origin.y, step);
    return find(i, j);
}

int floor_div(int value, int step) {
    if (step == 0) {
        return 0;
    }
    int quotient = value / step;
    int remainder = value % step;
    if (remainder != 0 && ((remainder > 0) != (step > 0))) {
        --quotient;
    }
    return quotient;
}

}

