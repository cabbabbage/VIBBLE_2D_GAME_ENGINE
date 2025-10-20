#pragma once

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class Assets;
class Asset;
class camera;
namespace world { class Grid; }
class PrecomputedLightMap;
#include "render/precomputed_light_map.hpp"

namespace world {

struct Chunk {
    int i = 0;
    int j = 0;
    int r_chunk = 9;
    SDL_Rect world_bounds{0, 0, 0, 0};

    std::vector<Asset*> assets;

    SDL_Texture* static_light_map = nullptr;
    // Average transparency of the static darkness mask for this chunk (0..1).
    // Higher means brighter from static lights alone.
    float base_brightness = 1.0f;

    // Runtime overlay control used by the existing light-map pass.
    float brightness_strength = 1.0f;
    float opacity_strength = 1.0f;
    float scale_strength = 1.0f;
    int offset_x = 0;
    int offset_y = 0;

    struct LightData {
        bool  is_active = false;
        bool  needs_update = true;
        bool  is_occupied_by_moving_source = false;
        float current_strength = 0.0f;
        float min_static_avg_strength = 0.0f;
        float max_static_avg_strength = 1.0f;
    } light;

    struct UseShadowData {
        float scale = 1.0f;
        float opacity = 1.0f;
        float offset_x_percent = 0.0f;
        float offset_y_percent = 0.0f;
        float parallax_intensity_percent = 0.0f;
    } shadow;

    bool lighting_dirty = true;
    bool has_dynamic_overlay = false;

    Chunk() = default;
    Chunk(int in_i, int in_j, int r, SDL_Rect bounds) : i(in_i), j(in_j), r_chunk(r), world_bounds(bounds) {}
    ~Chunk();

    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;
    Chunk(Chunk&&) noexcept = default;
    Chunk& operator=(Chunk&&) noexcept = default;
};

} // namespace world

// Unified LightMap implementation co-located with Chunk.
class LightMap {
public:
    struct ShadowSettings {
        int   search_radius_cells     = 1;
        float falloff_horizontal      = 1.0f;
        float falloff_vertical        = 1.0f;
        float max_offset_x_px         = 64.0f;
        float max_offset_y_px         = 48.0f;
        float base_shadow_scale       = 1.0f;
        // New: min/max shadow scale as integer percents (50..200)
        int   min_scale_percent       = 80;   // 80% default
        int   max_scale_percent       = 120;  // 120% default
        // Strength (0..1) of map-light directional X offset contribution
        float map_light_dir_offset_strength = 0.5f;
        float parallax_percent        = 0.0f;
    };

    static constexpr float kDefaultStaticWeight  = 0.8f;
    static constexpr float kDefaultDynamicWeight = 1.0f;

    // Legacy UI constants kept for dev panels and persisted settings.
    static constexpr int kMinQuadrantCount       = 1;
    static constexpr int kMaxQuadrantCount       = 64;
    static constexpr int kDefaultQuadrantCount   = 16;
    static constexpr int kMinQuadrantSizePx      = 16;
    static constexpr int kMaxQuadrantSizePx      = 4096;
    static constexpr int kDefaultQuadrantSizePx  = 256;

    LightMap(Assets* assets,
             int screen_width,
             int screen_height,
             std::unique_ptr<PrecomputedLightMap> precomputed_map = nullptr)
        : assets_(assets)
        , screen_width_(screen_width)
        , screen_height_(screen_height)
        , pending_precomputed_map_(std::move(precomputed_map)) {}
    ~LightMap() = default;

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

    std::optional<world::Chunk::UseShadowData> get_shadow_data(SDL_FPoint world_or_screen_pos) const;
    ShadowSettings shadow_settings() const { return ShadowSettings{}; }

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

    bool begin_full_world_mask(SDL_Renderer* renderer) const;
    void end_full_world_mask(SDL_Renderer* renderer) const;
    bool rebuild_chunk_from_batch(SDL_Renderer* renderer, world::Chunk& chunk) const;

private:
    Assets* assets_ = nullptr;
    int     screen_width_  = 0;
    int     screen_height_ = 0;

    std::unique_ptr<PrecomputedLightMap> pending_precomputed_map_;
    mutable bool precomputed_applied_ = false;

    mutable SDL_Texture* batch_full_mask_ = nullptr;
    mutable SDL_Rect     batch_full_bounds_{0, 0, 0, 0};
    mutable bool         batch_active_ = false;
    mutable float        last_screen_light_opacity_ = -1.0f;
};
