#pragma once

#include <unordered_map>
#include <vector>

#include <SDL.h>

#include "world/chunk_manager.hpp"

class Asset;

namespace world {

class Grid {
public:
    Grid(SDL_Point origin = SDL_Point{0,0}, int r_chunk = 9) : origin_(origin), r_chunk_(r_chunk) {}

    void set_chunk_resolution(int r);
    int  chunk_resolution() const { return r_chunk_; }
    SDL_Point origin() const { return origin_; }

    // Residency API
    void register_asset(Asset* a);
    void move_asset(Asset* a, SDL_Point old_pos, SDL_Point new_pos);
    void unregister_asset(Asset* a);

    // Activation
    void update_active_chunks(const SDL_Rect& camera_world, int margin_px);
    const std::vector<Chunk*>& active_chunks() const { return chunks_.active(); }

    // Queries
    Chunk* find_chunk_ij(int i, int j) const { return chunks_.find(i, j); }
    Chunk& get_or_create_chunk_ij(int i, int j) { return chunks_.ensure(i, j, r_chunk_, origin_); }
    Chunk* ensure_chunk_from_world(SDL_Point world_px);
    Chunk* chunk_from_world(SDL_Point world_px) const { return chunks_.from_world(world_px, r_chunk_, origin_); }

private:
    void remove_from_chunk(Asset* a, Chunk* c);
    void rebuild_chunks();

private:
    SDL_Point origin_{0,0};
    int r_chunk_ = 9;
    ChunkManager chunks_{};
    std::unordered_map<Asset*, Chunk*> residency_{};
};

} // namespace world

