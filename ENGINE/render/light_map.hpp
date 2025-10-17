#pragma once

#include <SDL.h>
#include <vector>
#include <cstddef>
#include <optional>
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"

struct VirtualLightMap {
    static constexpr int kDefaultGridSize = 50;
    static constexpr int kMinGridSize     = 1;
    static constexpr int kMaxGridSize     = 100;

    struct ShadowCell {
        float brightness = 0.0f;
        float opacity    = 0.0f;
        float offset_x   = 0.0f;
        float offset_y   = 0.0f;
        float scale      = 1.0f;
    };

    struct GridMetrics {
        float cell_width     = 0.0f;
        float cell_height    = 0.0f;
        float inv_cell_width = 0.0f;
        float inv_cell_height= 0.0f;
        int   grid_width     = 0;
        int   grid_height    = 0;

        SDL_Rect  cell_bounds(int gx, int gy) const;
        SDL_FPoint cell_center(int gx, int gy) const;
        SDL_FPoint screen_to_grid(float x, float y) const;
    };

    struct GridCoord {
        int        x      = 0;
        int        y      = 0;
        int        index  = -1;
        SDL_Rect   bounds { 0, 0, 0, 0 };
        SDL_FPoint center { 0.0f, 0.0f };
    };

    VirtualLightMap();

    void set_square_grid(int quadrants);
    int  grid_width() const { return grid_width_; }
    int  grid_height() const { return grid_height_; }
    int  quadrant_cols() const { return grid_width_; }
    int  quadrant_rows() const { return grid_height_; }
    int  quadrant_count() const { return grid_width_ * grid_height_; }

    void clear(float brightness = 0.0f);

    std::size_t index_of(int x, int y) const;

    ShadowCell& cell(int x, int y);
    const ShadowCell& cell(int x, int y) const;

    ShadowCell& cell_by_index(std::size_t index);
    const ShadowCell& cell_by_index(std::size_t index) const;

    const ShadowCell& cell_for_index(int index) const;

    std::optional<GridMetrics> grid_metrics() const;
    std::optional<GridCoord>   locate_index(int index) const;
    std::optional<GridCoord>   locate_screen_point(float x, float y) const;
    std::optional<GridCoord>   locate_world_point(SDL_Point world, const camera& view) const;

    SDL_Rect quadrant_bounds(int index) const;
    int      quadrant_for_point(float x, float y) const;
    int      quadrant_for_rect(const SDL_Rect& rect) const;

    int grid_size() const { return grid_width_; }

    void reserve_cells(std::size_t count);

    int screen_width  = 0;
    int screen_height = 0;

private:
    void ensure_cell_storage();

    int grid_width_  = kDefaultGridSize;
    int grid_height_ = kDefaultGridSize;
    std::vector<ShadowCell> grid_{};
};

class LightMap {

public:
    struct LightEntry {
        SDL_Rect     dst;
        Uint8        alpha;
        SDL_Color    color_mod;
        SDL_Texture* texture = nullptr;
    };

    LightMap(Assets* assets, int screen_width, int screen_height);
    ~LightMap();

    void prepare_fullscreen_light_map(SDL_Renderer* renderer);
    void render_fullscreen_light_map(SDL_Renderer* renderer) const;
    void update_virtual_light_map(SDL_Renderer* renderer);
    const VirtualLightMap& virtual_light_map() const { return virtual_light_map_; }
    void set_virtual_light_map_quadrants(int quadrants);
    int  virtual_light_map_quadrants() const { return virtual_light_map_.grid_size(); }

private:
    void collect_layers(std::vector<LightEntry>& out);
    SDL_Rect get_scaled_position_rect(SDL_Point pos, int fw, int fh, float inv_scale, int min_w, int min_h);
    void compute_fullscreen_texture(SDL_Renderer* renderer, const std::vector<LightEntry>& layers);
    void compute_virtual_light_map(SDL_Renderer* renderer);

private:
    Assets* assets_;
    int screen_width_;
    int screen_height_;
    std::vector<LightEntry> scratch_layers_;
    VirtualLightMap virtual_light_map_{};
    SDL_Texture* fullscreen_texture_ = nullptr;
    std::vector<Uint32> pixel_buffer_;
    std::vector<float>  cell_brightness_accum_;
    std::vector<int>    cell_sample_counts_;
    SDL_PixelFormat* capture_format_ = nullptr;
};
