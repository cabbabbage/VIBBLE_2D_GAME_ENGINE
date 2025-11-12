#pragma once

#include <algorithm>
#include <unordered_map>
#include <vector>

#include <SDL.h>

#include "world/chunk_manager.hpp"
#include "utils/transform_smoothing.hpp"

class Asset;
class camera;

namespace world {

class Grid {
public:
    Grid(SDL_Point origin = SDL_Point{0,0}, int r_chunk = 0);

    void set_chunk_resolution(int r);
    void set_parallax_resolution(int r);
    int  chunk_resolution() const { return r_chunk_; }
    int  parallax_resolution() const { return parallax_resolution_; }
    SDL_Point origin() const { return origin_; }

    void register_asset(Asset* a);
    void move_asset(Asset* a, SDL_Point old_pos, SDL_Point new_pos);
    void unregister_asset(Asset* a);

    void update_active_chunks(const SDL_Rect& camera_world, int margin_px);
    const std::vector<Chunk*>& active_chunks() const { return chunks_.active(); }

    Chunk* find_chunk_ij(int i, int j) const { return chunks_.find(i, j); }
    Chunk& get_or_create_chunk_ij(int i, int j) { return chunks_.ensure(i, j, r_chunk_, origin_); }
    Chunk* ensure_chunk_from_world(SDL_Point world_px);
    Chunk* chunk_from_world(SDL_Point world_px) const { return chunks_.from_world(world_px, r_chunk_, origin_); }
    std::vector<Chunk*> all_chunks() const;

    void  update_parallax(const camera& cam, float dt);
    float parallax_offset(SDL_Point world) const;
    float parallax_adjusted_screen_x(SDL_Point world, float base_screen_x) const;
    SDL_FPoint parallax_adjusted_screen_position(SDL_Point world, SDL_FPoint base_screen) const;

private:
    void remove_from_chunk(Asset* a, Chunk* c);
    void rebuild_chunks();
    void invalidate_active_cache();
    std::uint64_t parallax_key(int i, int j) const;
    int parallax_step_size() const;

private:
    SDL_Point origin_{0,0};
    int r_chunk_ = 0;
    int parallax_resolution_ = 0;
    ChunkManager chunks_{};
    std::unordered_map<Asset*, Chunk*> residency_{};
    SDL_Rect last_expanded_camera_{0, 0, 0, 0};
    int last_margin_px_ = -1;
    int last_chunk_resolution_ = -1;
    bool has_cached_camera_rect_ = false;
    struct ParallaxEntry {
        TransformSmoothingState smoothing{};
        float                   last_value = 0.0f;
        bool                    initialized = false;
        std::uint64_t           last_used_frame = 0;
    };

    std::unordered_map<std::uint64_t, ParallaxEntry> parallax_entries_{};
    bool                                             parallax_active_ = false;
    std::uint64_t                                    parallax_frame_counter_ = 0;
};

}

