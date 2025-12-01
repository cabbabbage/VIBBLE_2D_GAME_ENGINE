#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <SDL.h>

#include "world/chunk.hpp"

namespace world {

class ChunkManager {
public:
    ChunkManager() = default;

    Chunk& ensure(int i, int j, int r_chunk, SDL_Point origin);
    Chunk* find(int i, int j) const;
    Chunk* from_world(SDL_Point world_px, int r_chunk, SDL_Point origin) const;

    const std::vector<std::unique_ptr<Chunk>>& storage() const { return storage_; }
    std::vector<Chunk*>& active() { return active_; }
    const std::vector<Chunk*>& active() const { return active_; }

    void clear_active() { active_.clear(); }
    void reset() {
        lookup_.clear();
        storage_.clear();
        active_.clear();
    }

private:
    static std::uint64_t key(int i, int j);
    static SDL_Rect bounds_for(int i, int j, int r_chunk, SDL_Point origin);

    std::unordered_map<std::uint64_t, Chunk*> lookup_;
    std::vector<std::unique_ptr<Chunk>> storage_;
    std::vector<Chunk*> active_;
};

}

