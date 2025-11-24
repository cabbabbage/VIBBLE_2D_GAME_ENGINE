#pragma once

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "utils/grid.hpp"
#include "utils/transform_smoothing_settings.hpp"
#include "world/chunk_manager.hpp"
#include "world/grid_point.hpp"

// Forward declarations
typedef std::uint64_t ParallaxKey;
class Asset;
class camera;

namespace world {

/**
 * Grid of chunks for asset residency and x parallax.
 */
class Grid {
public:
    Grid() : Grid(SDL_Point{0, 0}, 0) {}
    Grid(SDL_Point origin, int r_chunk);

    void set_chunk_resolution(int r);
    void set_parallax_resolution(int r);
    int  parallax_step_size() const;
    int  parallax_resolution() const;
    int  chunk_resolution() const { return r_chunk_; }
    SDL_Point origin() const { return origin_; }
    void set_origin(SDL_Point origin);

    // Creation helpers that store ownership inside the grid points.
    Asset* create_asset_at_point(std::unique_ptr<Asset> a);
    Asset* create_asset_at_point(Asset* a); // convenience overload; transfers ownership
    Asset* register_asset(std::unique_ptr<Asset> a);
    Asset* register_asset(Asset* a); // legacy helper; transfers ownership
    Chunk* ensure_chunk_from_world(SDL_Point world_px);
    Chunk* chunk_from_world(SDL_Point world_px) const;
    Chunk* get_or_create_chunk_ij(int i, int j);
    std::vector<Chunk*> all_chunks() const;

    Asset* move_asset_to_point(Asset* a, SDL_Point old_pos, SDL_Point new_pos);
    void move_asset(Asset* a, SDL_Point old_pos, SDL_Point new_pos);
    // Removes an asset from the grid, returning ownership to the caller.
    std::unique_ptr<Asset> remove_asset(Asset* a);
    void unregister_asset(Asset* a);
    void rebuild_chunks();

    void update_active_chunks(const SDL_Rect& camera_world, int margin_px);

    const std::vector<Chunk*>& active_chunks() const;

    void update_parallax(const camera& cam, float dt);
    float parallax_offset(SDL_Point world) const;

    float parallax_adjusted_screen_x(SDL_Point world, float base_screen_x) const;
    SDL_FPoint parallax_adjusted_screen_position(SDL_Point world, SDL_FPoint base_screen) const;

    // Helper that applies camera floor warping on Y then grid parallax on X.
    SDL_FPoint floor_warped_screen_position(const camera& cam, SDL_Point world) const;

    bool parallax_active() const;

    ChunkManager& chunks();
    const ChunkManager& chunks() const;
    std::vector<Asset*> all_assets() const;

    // Grid point helpers for linking to ScreenGrid / render
    SDL_Point grid_index_from_world(SDL_Point world) const;
    GridId point_id_from_world(SDL_Point world) const;
    const std::unordered_map<GridId, GridPoint>& points() const { return points_; }
    GridPoint* point_for_id(GridId id);
    const GridPoint* point_for_id(GridId id) const;
    GridPoint* point_for_asset(const Asset* asset);
    const GridPoint* point_for_asset(const Asset* asset) const;

private:
    struct ParallaxSmoothingState {
        TransformSmoothingParams params;
        float current  = 0.0f;
        float target   = 0.0f;
        float velocity = 0.0f;
        bool initialized = false;

        ParallaxSmoothingState();
        void set_params(const TransformSmoothingParams& p);
        void reset(float value);
        void advance(float dt);
        float value_for_render() const { return current; }
    };

    struct ParallaxEntry {
        ParallaxSmoothingState smoothing{};
        std::uint64_t last_used_frame = 0;
        float last_value            = 0.0f;
        bool initialized = false;
    };

    struct ParallaxCache {
        int origin_i = 0;
        int origin_j = 0;
        int width    = 0;
        int height   = 0;
        int step     = 0;
        std::vector<float> values;
        bool ready   = false;

        ParallaxCache();
        void clear();
        void configure(int origin_i, int origin_j, int width, int height, int step);
        bool try_index(int i, int j, int step, std::size_t& out_index) const;
        void mark_ready() { ready = true; }
    };

    void remove_from_chunk(Asset* a, Chunk* c);
    void invalidate_active_cache();
    void clear_parallax_state();
    ParallaxKey parallax_key(int i, int j) const;
    void remove_asset_from_point(Asset* a, GridPoint& point);
    GridPoint& ensure_point(SDL_Point grid_index);
    void bind_asset_to_point(Asset* a,
                             GridPoint& point,
                             SDL_Point world_pos,
                             Chunk* owning_chunk,
                             SDL_Point chunk_index);
    void prune_empty_points();
    std::unique_ptr<Asset> extract_from_point(Asset* a, GridPoint& point);

    SDL_Point origin_{0, 0};
    int       r_chunk_ = 0;
    int       parallax_resolution_ = 0;

    ChunkManager chunks_;
    std::unordered_map<Asset*, Chunk*> residency_;

    bool     has_cached_camera_rect_ = false;
    SDL_Rect last_expanded_camera_{0, 0, 0, 0};
    int      last_margin_px_         = -1;
    int      last_chunk_resolution_  = -1;

    std::uint64_t parallax_frame_counter_ = 0;
    bool          parallax_active_        = false;

    std::unordered_map<std::uint64_t, ParallaxEntry> parallax_entries_;
    ParallaxCache parallax_cache_;

    std::unordered_map<GridId, GridPoint> points_;
    std::unordered_map<Asset*, GridId> asset_to_point_;
};

} // namespace world
