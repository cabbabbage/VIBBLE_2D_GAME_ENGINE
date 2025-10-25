#pragma once

#include <SDL.h>

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

class Assets;
class Asset;
class camera;
namespace world { class Grid; }
namespace world {

struct Chunk {
    int i = 0;
    int j = 0;
    int r_chunk = 0;
    SDL_Rect world_bounds{0, 0, 0, 0};

    std::vector<Asset*> assets;

    struct ChunkShadowParameters {
        // Scalar applied to the shadow mask relative to its authored size.
        float scale = 1.0f;
        // Opacity multiplier applied to the darkness mask [0,1].
        float opacity = 1.0f;
        // Horizontal offset of the shadow mask expressed as a percent of chunk width [-100,100].
        float offset_x_percent = 0.0f;
        // Vertical offset of the shadow mask expressed as a percent of chunk height [-100,100].
        float offset_y_percent = 0.0f;
        // Parallax offset intensity expressed as a percent [0,100].
        float parallax_intensity_percent = 0.0f;
    };

    struct ChunkShadowHistory {
        static constexpr int kHistoryLength = 5;

        std::array<ChunkShadowParameters, kHistoryLength> samples{};
        int                                              count  = 0;
        int                                              cursor = 0;
        ChunkShadowParameters                            blended{};

        void reset();
        void push(const ChunkShadowParameters& sample);
        const ChunkShadowParameters& value() const { return blended; }
    } shadow_history{};

    struct ChunkLightingState {
        // Whether this chunk participates in lighting updates during the current frame.
        bool is_active = false;
        // Flags that the chunk's cached lighting values should be recomputed.
        bool needs_update = true;
        // Net light contribution applied to the chunk after static and dynamic blending [0,1].
        float current_strength = 1.0f;
        // Runtime measurement captured from the on-screen render output during this frame.
        float runtime_average_strength = 1.0f;
        // Marks whether runtime_average_strength contains a valid measurement for the current frame.
        bool has_runtime_average = false;
    } lighting;

    ChunkShadowParameters shadow{};

    bool lighting_dirty = true;
    bool has_dynamic_overlay = false;

    Chunk() = default;
    Chunk(int in_i, int in_j, int r, SDL_Rect bounds) : i(in_i), j(in_j), r_chunk(r), world_bounds(bounds) {}
    ~Chunk();

    void releaseLightingArtifacts();

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
        // Controls how strongly global scene brightness influences opacity (0..100).
        float opacity_sensitivity_percent = 50.0f;
        // New: min/max shadow scale as integer percents (50..200)
        int   min_scale_percent       = 80;   // 80% default
        int   max_scale_percent       = 120;  // 120% default
        // Strength (0..1) of map-light directional offset contribution
        float map_light_dir_offset_strength = 0.5f;
        float parallax_percent        = 0.0f;
    };

    static constexpr float kDefaultStaticWeight  = 0.8f;
    static constexpr float kDefaultDynamicWeight = 1.0f;

    LightMap(Assets* assets,
             int screen_width,
             int screen_height)
        : assets_(assets)
        , screen_width_(screen_width)
        , screen_height_(screen_height) {}
    ~LightMap() = default;

    void rebuild(SDL_Renderer* renderer);
    void update(SDL_Renderer* renderer, std::uint32_t delta_ms);
    void capture_runtime_brightness(SDL_Renderer* renderer);

    float sample_brightness(int world_x,
                            int world_y,
                            float static_weight = kDefaultStaticWeight,
                            float dynamic_weight = kDefaultDynamicWeight) const;
    float sample_brightness_bilinear(float world_x,
                                     float world_y,
                                     float static_weight = kDefaultStaticWeight,
                                     float dynamic_weight = kDefaultDynamicWeight) const;

    void render_visible_chunks(SDL_Renderer* renderer, const SDL_Rect& view_rect) const;
    void render_visible_chunks(SDL_Renderer* renderer,
                               const SDL_Rect& view_rect,
                               float alpha_multiplier,
                               const SDL_Color& color_mod) const;
    void render_chunk_preview(SDL_Renderer* renderer, const SDL_Rect& view_rect) const;
    void present_static_previews(SDL_Renderer* renderer) const;

    void mark_region_dirty(const SDL_Rect& screen_rect);
    void mark_asset_lights_dirty(const Asset* asset);
    void mark_static_cache_dirty();

    int screen_width() const { return screen_width_; }
    int screen_height() const { return screen_height_; }

    const std::vector<world::Chunk*>& active_chunks() const;
    world::Chunk* ensure_chunk_from_world(SDL_Point world_px) const;
    world::Chunk* chunk_from_world(SDL_Point world_px) const;

    std::optional<world::Chunk::ChunkShadowParameters> get_shadow_data(SDL_FPoint world_or_screen_pos) const;
    ShadowSettings shadow_settings() const;

    int chunk_count() const;
    int chunk_columns() const;
    int chunk_rows() const;
    const world::Chunk* chunk_at(int index) const;
    SDL_Rect chunk_bounds(int index) const;

private:
    Assets* assets_ = nullptr;
    int     screen_width_  = 0;
    int     screen_height_ = 0;

    mutable float        last_map_light_opacity_ = -1.0f;
    mutable std::recursive_mutex mutex_;
};


