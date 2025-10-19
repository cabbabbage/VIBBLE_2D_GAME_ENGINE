#pragma once

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "precomputed_light_map.hpp"

class Assets;
class Asset;
class camera;

class LightMapQuadrant {
public:
    LightMapQuadrant() = default;
    LightMapQuadrant(const LightMapQuadrant&) = delete;
    LightMapQuadrant& operator=(const LightMapQuadrant&) = delete;
    LightMapQuadrant(LightMapQuadrant&& other) noexcept;
    LightMapQuadrant& operator=(LightMapQuadrant&& other) noexcept;
    ~LightMapQuadrant();

    void configure(SDL_Renderer* renderer,
                   const SDL_Rect& world_rect,
                   int grid_resolution,
                   int padding_cells,
                   float texture_scale);

    const SDL_Rect& world_rect() const { return world_rect_; }
    int              grid_width() const { return grid_width_; }
    int              grid_height() const { return grid_height_; }
    int              padding() const { return padding_cells_; }
    float            base_brightness() const { return base_brightness_; }
    bool             dirty() const { return dirty_; }
    bool             active() const { return active_; }

    void set_dirty(bool value) { dirty_ = value; }
    void set_active(bool value) { active_ = value; }

    struct GridStatistics {
        float min      = 0.0f;
        float max      = 0.0f;
        float average  = 0.0f;
        bool  empty    = true;
    };

    GridStatistics static_grid_stats() const;
    float          combined_average(float static_weight, float /*dynamic_weight*/) const;

    void build_static(const std::vector<std::uint8_t>& grid, int width, int height);
    // Dynamic light rays removed.

    void update_tile_mask(SDL_Renderer* renderer,
                          const class Assets* assets,
                          float static_weight,
                          float dynamic_weight,
                          bool include_static_lights);
    void render_tile_mask(SDL_Renderer* renderer) const;
    void render_tile_mask(SDL_Renderer* renderer, Uint8 alpha_mod) const;
    void render_tile_mask_with_mode(SDL_Renderer* renderer, Uint8 alpha_mod, SDL_BlendMode mode) const;
    void render_tile_mask_at(SDL_Renderer* renderer, const SDL_Rect& dst) const;
    void render_tile_mask_at(SDL_Renderer* renderer, const SDL_Rect& dst, Uint8 alpha_mod) const;
    void render_tile_mask_with_mode_at(SDL_Renderer* renderer, const SDL_Rect& dst, Uint8 alpha_mod, SDL_BlendMode mode) const;
    void populate_static_base(SDL_Renderer* renderer,
                              SDL_Texture* static_full_map,
                              const Assets* assets,
                              bool use_world_space,
                              float full_map_scale,
                              const SDL_Rect& full_map_bounds);
    void adopt_static_mask(SDL_Texture* texture);
    void set_base_brightness(float value);

    float sample_brightness(float local_x,
                            float local_y,
                            float static_weight,
                            float /*dynamic_weight*/,
                            bool bilinear) const;

private:
    void destroy_texture();
    void ensure_texture(SDL_Renderer* renderer);
    void ensure_static_mask(SDL_Renderer* renderer);
    std::size_t index_from_cell(int cx, int cy) const;
    float       cell_sample(int cx, int cy, float static_weight, float dynamic_weight) const;
    void        clear_static_samples();
    bool        sample_static_mask(SDL_Renderer* renderer);

    SDL_Rect            world_rect_{0, 0, 0, 0};
    int                 grid_width_      = 0;
    int                 grid_height_     = 0;
    int                 padding_cells_   = 0;
    int                 stride_          = 0;
    float               texture_scale_   = 1.0f;
    int                 texture_width_px_  = 0;
    int                 texture_height_px_ = 0;
    std::vector<std::uint8_t> static_grid_{};
    // Removed dynamic grid and all related behavior.
    SDL_Texture*        tile_mask_       = nullptr;
    SDL_Texture*        static_mask_     = nullptr;
    float               base_brightness_ = 0.0f;
    bool                dirty_           = true;
    bool                active_          = false;
};

class LightMap {
public:
    static constexpr float kDefaultStaticWeight  = 0.8f;
    static constexpr float kDefaultDynamicWeight = 1.0f;
    static constexpr int   kMinQuadrantCount     = 1;
    static constexpr int   kMaxQuadrantCount     = 100;
    static constexpr int   kDefaultQuadrantCount = 32;
    static constexpr int   kMinQuadrantSizePx    = 32;
    static constexpr int   kMaxQuadrantSizePx    = 1024;
    static constexpr int   kDefaultQuadrantSizePx = 256;
    static constexpr float kTextureDownscale     = 0.25f;

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

    int quadrant_count() const { return static_cast<int>(quadrants_.size()); }
    int quadrant_columns() const { return quadrant_cols_; }
    int quadrant_rows() const { return quadrant_rows_; }

    const LightMapQuadrant* quadrant(int index) const;
    int quadrant_for_point(float x, float y) const;
    SDL_Rect quadrant_bounds(int index) const;

    int quadrant_size_px() const { return quadrant_size_px_; }
    int static_grid_resolution() const { return static_grid_resolution_; }
    int padding_cells() const { return padding_cells_; }

    void set_virtual_light_map_quadrants(int quadrants);
    void set_virtual_light_map_quadrant_size(int size_px); // deprecated: pixel-sized quadrants no longer used
    void set_cells_per_quadrant(int cells);
    int  virtual_light_map_quadrant_size() const { return requested_quadrant_size_px_; }
    int  virtual_light_map_quadrants() const { return requested_quadrants_; }

    // Dynamic moving light rays removed.

private:
    int   find_quadrant_index(int world_x, int world_y) const;
    float sample_internal(int quadrant_index,
                          float local_x,
                          float local_y,
                          bool  bilinear,
                          float static_weight,
                          float dynamic_weight) const;
    std::pair<int, int> padding_pixels() const;
    bool use_world_space_coordinates() const;
    void build_static_full_map(SDL_Renderer* renderer);
    void destroy_static_full_map(bool mark_dirty = true);
    bool adopt_precomputed_map(SDL_Renderer* renderer);

    Assets* assets_ = nullptr;
    int     screen_width_  = 0;
    int     screen_height_ = 0;

    int quadrant_cols_ = 0;
    int quadrant_rows_ = 0;
    int quadrant_size_px_       = kDefaultQuadrantSizePx;
    int static_grid_resolution_ = 32;
    int padding_cells_          = 2;
    int requested_quadrants_    = kDefaultQuadrantCount;
    int requested_quadrant_size_px_ = kDefaultQuadrantSizePx;

    std::vector<LightMapQuadrant> quadrants_{};
    SDL_Texture*                  static_full_map_ = nullptr;
    bool                          static_cache_dirty_ = true;
    std::unique_ptr<PrecomputedLightMap> pending_precomputed_map_;
    bool                          static_full_map_supported_ = true;
    int                           last_static_full_map_fail_w_ = 0;
    int                           last_static_full_map_fail_h_ = 0;
    float                         static_full_map_scale_factor_ = 1.0f;
    SDL_Rect                      static_full_map_bounds_{0, 0, 0, 0};
    float                         quadrant_texture_scale_ = kTextureDownscale;

    struct LayoutInfo {
        int map_width          = 0;
        int map_height         = 0;
        int grid_spacing       = 0;
        int cells_per_quadrant = 0;
        int grid_resolution    = 32;
        int padding_cells      = 0;
        int origin_x           = 0;
        int origin_y           = 0;
    } layout_{};

    int desired_cells_per_quadrant_ = 2; // default: 2 grid-spaces per quadrant
};

