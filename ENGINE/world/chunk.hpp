#pragma once

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "tiling/grid_tile.hpp"

class Assets;
class Asset;
class WarpedScreenGrid;
namespace world {
class Grid;
}
namespace world {

struct Chunk {
    int      i = 0;
    int      j = 0;
    int      r_chunk = 0;
    SDL_Rect world_bounds{0, 0, 0, 0};

    std::vector<Asset*> assets;
    std::uint64_t       occlusion_revision = 0;

    std::vector<GridTile> tiles;

    struct LightingState {
        bool      needs_update            = true;
        float     static_strength         = 1.0f;
        float     dynamic_strength        = 1.0f;
        float     current_strength        = 1.0f;
        bool      has_runtime_average     = false;
        float     runtime_average_strength = 1.0f;
        SDL_Color runtime_average_color{255, 255, 255, 255};
    } lighting{};

    Chunk() = default;
    Chunk(int in_i, int in_j, int r, SDL_Rect bounds);
    ~Chunk();

    void releaseLightingArtifacts();
    void releaseTileTextures();

    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;
    Chunk(Chunk&&) noexcept = default;
    Chunk& operator=(Chunk&&) noexcept = default;
};

}

class LightMap {
public:
    struct SampledBrightness {
        float     static_component  = 1.0f;
        float     dynamic_component = 1.0f;
        float     blended           = 1.0f;
        SDL_Color color{255, 255, 255, 255};
        bool      has_color = false;
};

    LightMap(Assets* assets, int screen_width, int screen_height);
    ~LightMap();

    void rebuild(SDL_Renderer* renderer);
    void update(SDL_Renderer* renderer, std::uint32_t delta_ms);
    SampledBrightness sample_lighting(int world_x, int world_y, float static_weight  = 0.0f, float dynamic_weight = 1.0f) const;
    SampledBrightness sample_lighting_bilinear(float world_x, float world_y, float static_weight  = 0.0f, float dynamic_weight = 1.0f) const;
    float sample_brightness(int world_x, int world_y, float static_weight  = 0.0f, float dynamic_weight = 1.0f) const;
    float sample_brightness_bilinear(float world_x, float world_y, float static_weight  = 0.0f, float dynamic_weight = 1.0f) const;

    void render_visible_chunks(SDL_Renderer* renderer, const SDL_Rect& view_rect) const;
    void render_visible_chunks(SDL_Renderer* renderer, const SDL_Rect& view_rect, float           alpha_multiplier, const SDL_Color& color_mod) const;
    void subtract_runtime_shadow_from_texture(SDL_Renderer* renderer, SDL_Texture*  target_texture, const SDL_Rect& target_rect, const SDL_Rect& screen_rect, float           alpha_multiplier) const;
    void render_chunk_preview(SDL_Renderer* renderer, const SDL_Rect& view_rect) const;
    void present_static_previews(SDL_Renderer* renderer) const;

    void mark_region_dirty(const SDL_Rect& screen_rect);
    void mark_asset_lights_dirty(const Asset* asset);
    void mark_static_cache_dirty();

    int screen_width() const { return screen_width_; }
    int screen_height() const { return screen_height_; }

    const std::vector<world::Chunk*>& active_chunks() const;
    world::Chunk*                     ensure_chunk_from_world(SDL_Point world_px) const;
    world::Chunk*                     chunk_from_world(SDL_Point world_px) const;

    int              chunk_count() const;
    int              chunk_columns() const;
    int              chunk_rows() const;
    const world::Chunk* chunk_at(int index) const;
    SDL_Rect         chunk_bounds(int index) const;

private:
    std::pair<float, float> resolve_sampling_weights(float static_weight, float dynamic_weight) const;

    Assets* assets_ = nullptr;
    int     screen_width_  = 0;
    int     screen_height_ = 0;

    mutable std::recursive_mutex mutex_;
    mutable Uint32               last_render_tick_         = 0;
    mutable bool                 rendered_in_current_tick_ = false;
};

