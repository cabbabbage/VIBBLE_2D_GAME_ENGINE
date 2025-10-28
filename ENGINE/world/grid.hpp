#pragma once

#include <algorithm>
#include <unordered_map>
#include <vector>

#include <SDL.h>

#include "world/chunk_manager.hpp"

class Asset;

namespace world {

class Grid {
public:
    Grid(SDL_Point origin = SDL_Point{0,0}, int r_chunk = 0);

    void set_chunk_resolution(int r);
    int  chunk_resolution() const { return r_chunk_; }
    SDL_Point origin() const { return origin_; }
    int  lighting_chunk_resolution() const { return std::max(0, r_chunk_ - 2); }
    int  lighting_subdivisions_per_chunk() const;
    int  requested_lighting_subdivisions_per_chunk() const { return requested_lighting_subdivisions_; }
    bool set_lighting_subdivisions_per_chunk(int subdivisions);

    void register_asset(Asset* a);
    void move_asset(Asset* a, SDL_Point old_pos, SDL_Point new_pos);
    void unregister_asset(Asset* a);

    void update_active_chunks(const SDL_Rect& camera_world, int margin_px);
    const std::vector<Chunk*>& active_chunks() const { return chunks_.active(); }

    Chunk* find_chunk_ij(int i, int j) const { return chunks_.find(i, j); }
    Chunk& get_or_create_chunk_ij(int i, int j) { return chunks_.ensure(i, j, r_chunk_, origin_, lighting_subdivisions_per_chunk()); }
    Chunk* ensure_chunk_from_world(SDL_Point world_px);
    Chunk* chunk_from_world(SDL_Point world_px) const { return chunks_.from_world(world_px, r_chunk_, origin_); }
    std::vector<Chunk*> all_chunks() const;

private:
    void remove_from_chunk(Asset* a, Chunk* c);
    void rebuild_chunks();
    void invalidate_active_cache();
    int  clamp_lighting_subdivisions(int subdivisions) const;
    bool refresh_lighting_subdivision_cache(bool apply_to_chunks);

private:
    SDL_Point origin_{0,0};
    int r_chunk_ = 0;
    ChunkManager chunks_{};
    std::unordered_map<Asset*, Chunk*> residency_{};
    SDL_Rect last_expanded_camera_{0, 0, 0, 0};
    int last_margin_px_ = -1;
    int last_chunk_resolution_ = -1;
    bool has_cached_camera_rect_ = false;
    int requested_lighting_subdivisions_ = 1;
    int cached_lighting_subdivisions_    = 1;
};

}

