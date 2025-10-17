#pragma once

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class Assets;
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
                   int padding_cells);

    const SDL_Rect& world_rect() const { return world_rect_; }
    int              grid_width() const { return grid_width_; }
    int              grid_height() const { return grid_height_; }
    int              padding() const { return padding_cells_; }
    float            base_brightness() const { return base_brightness_; }
    bool             dirty() const { return dirty_; }
    bool             active() const { return active_; }

    void set_dirty(bool value) { dirty_ = value; }
    void set_active(bool value) { active_ = value; }

    void build_static(const std::vector<std::uint8_t>& grid, int width, int height);
    void stamp_moving_lights(const std::vector<std::uint8_t>& grid, int width, int height, std::uint8_t clamp = 255);
    void fade_dynamic(std::uint8_t fade);

    void update_tile_mask(SDL_Renderer* renderer, float static_weight, float dynamic_weight);
    void render_tile_mask(SDL_Renderer* renderer) const;

    float sample_brightness(float local_x,
                            float local_y,
                            float static_weight,
                            float dynamic_weight,
                            bool bilinear) const;

private:
    void destroy_texture();
    void ensure_texture(SDL_Renderer* renderer);
    std::size_t index_from_cell(int cx, int cy) const;
    float       cell_sample(int cx, int cy, float static_weight, float dynamic_weight) const;

    SDL_Rect            world_rect_{0, 0, 0, 0};
    int                 grid_width_      = 0;
    int                 grid_height_     = 0;
    int                 padding_cells_   = 0;
    int                 stride_          = 0;
    std::vector<std::uint8_t> static_grid_{};
    std::vector<std::uint8_t> dynamic_grid_{};
    SDL_Texture*        tile_mask_       = nullptr;
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

    LightMap(Assets* assets, int screen_width, int screen_height);
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
    int  virtual_light_map_quadrants() const { return requested_quadrants_; }

private:
    int   find_quadrant_index(int world_x, int world_y) const;
    float sample_internal(int quadrant_index,
                          float local_x,
                          float local_y,
                          bool  bilinear,
                          float static_weight,
                          float dynamic_weight) const;

    Assets* assets_ = nullptr;
    int     screen_width_  = 0;
    int     screen_height_ = 0;

    int quadrant_cols_ = 0;
    int quadrant_rows_ = 0;
    int quadrant_size_px_       = 256;
    int static_grid_resolution_ = 32;
    int padding_cells_          = 2;
    int requested_quadrants_    = 32;

    std::vector<LightMapQuadrant> quadrants_{};
};

