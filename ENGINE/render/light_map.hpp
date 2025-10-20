#pragma once

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "precomputed_light_map.hpp"

class Assets;
class Asset;
class camera;

namespace world {
struct Chunk;
class Grid;
} // namespace world

class LightMap {
public:
    static constexpr float kDefaultStaticWeight  = 0.8f;
    static constexpr float kDefaultDynamicWeight = 1.0f;

    // Legacy UI constants kept for backward compatibility with
    // developer panels and persisted settings. These values are
    // only used for clamping and defaults in the dev UI and do
    // not affect the chunk-based light map implementation.
    static constexpr int kMinQuadrantCount       = 1;
    static constexpr int kMaxQuadrantCount       = 64;
    static constexpr int kDefaultQuadrantCount   = 16;
    static constexpr int kMinQuadrantSizePx      = 16;
    static constexpr int kMaxQuadrantSizePx      = 4096;
    static constexpr int kDefaultQuadrantSizePx  = 256;

    LightMap(Assets* assets,
             int screen_width,
             int screen_height,
             std::unique_ptr<PrecomputedLightMap> precomputed_map = nullptr);
    ~LightMap();

    void rebuild(SDL_Renderer* renderer);
    void update(SDL_Renderer* renderer, std::uint32_t delta_ms);

    float sample_brightness(int world_x,
                            int world_y,
                            float static_weight = kDefaultStaticWeight,
                            float dynamic_weight = kDefaultDynamicWeight) const;
    float sample_brightness_bilinear(float world_x,
                                     float world_y,
                                     float static_weight = kDefaultStaticWeight,
                                     float dynamic_weight = kDefaultDynamicWeight) const;

    void render_visible_quadrants(SDL_Renderer* renderer, const SDL_Rect& view_rect) const;
    void render_visible_quadrants(SDL_Renderer* renderer, const SDL_Rect& view_rect, float alpha_multiplier) const;
    void render_visible_quadrants_debug(SDL_Renderer* renderer, const SDL_Rect& view_rect, float alpha_multiplier) const;

    void mark_region_dirty(const SDL_Rect& screen_rect);
    void mark_asset_lights_dirty(const Asset* asset);
    void mark_static_cache_dirty();

    int screen_width() const { return screen_width_; }
    int screen_height() const { return screen_height_; }

    const std::vector<world::Chunk*>& active_chunks() const;
    world::Chunk* chunk_from_world(SDL_Point world_px) const;

    int quadrant_count() const;
    int quadrant_columns() const;
    int quadrant_rows() const;
    const world::Chunk* quadrant(int index) const;
    SDL_Rect quadrant_bounds(int index) const;

    void set_virtual_light_map_quadrants(int quadrants);
    void set_virtual_light_map_quadrant_size(int size_px);
    void set_cells_per_quadrant(int cells);
    int  virtual_light_map_quadrant_size() const;
    int  virtual_light_map_quadrants() const;
    int  static_grid_resolution() const;
    int  padding_cells() const;

private:
    void ensure_chunk_rebaked(SDL_Renderer* renderer, world::Chunk& chunk) const;
    void destroy_chunk_texture(world::Chunk& chunk) const;
    void apply_precomputed_light_map(SDL_Renderer* renderer);

    // Batch path: build a single world-space mask and crop into chunks.
    bool begin_full_world_mask(SDL_Renderer* renderer) const;
    void end_full_world_mask(SDL_Renderer* renderer) const;
    bool rebuild_chunk_from_batch(SDL_Renderer* renderer, world::Chunk& chunk) const;

private:
    Assets* assets_ = nullptr;
    int     screen_width_  = 0;
    int     screen_height_ = 0;

    std::unique_ptr<PrecomputedLightMap> pending_precomputed_map_;
    mutable bool precomputed_applied_ = false;

    // Scratch state for a single-update full-world light mask build.
    mutable SDL_Texture* batch_full_mask_ = nullptr;
    mutable SDL_Rect     batch_full_bounds_{0, 0, 0, 0};
    mutable bool         batch_active_ = false;
};

