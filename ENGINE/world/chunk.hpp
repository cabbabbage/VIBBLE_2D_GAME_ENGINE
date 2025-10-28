#pragma once

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

class Assets;
class Asset;
class camera;
namespace world { class Grid; }
namespace world { class Grid; }
namespace runtime_lighting { struct RuntimeLightingFrame; }
namespace runtime_lighting { struct RuntimeLightingFrame; }
namespace world {

struct Chunk {
    int i = 0;
    int j = 0;
    int r_chunk = 0;
    SDL_Rect world_bounds{0, 0, 0, 0};

    std::vector<Asset*> assets;
    std::uint64_t       occlusion_revision = 0;

    struct ChunkShadowParameters {

        float opacity = 1.0f;

        float offset_x_percent = 0.0f;

        float offset_y_percent = 0.0f;

        float offset_x_px = 0.0f;
        float offset_y_px = 0.0f;

        float scale = 1.0f;
};

    struct ChunkShadowHistory {
        static constexpr int kMaxHistoryLength = 256;

        std::array<ChunkShadowParameters, kMaxHistoryLength> samples{};
        int                                                  count  = 0;
        int                                                  cursor = 0;
        ChunkShadowParameters                                blended{};

        void reset();
        void push(const ChunkShadowParameters& sample, int fade_frames);
        const ChunkShadowParameters& value() const { return blended; }
};

    struct ChunkLightingState {

        bool is_active = false;

        bool needs_update = true;

        float static_strength = 1.0f;

        float dynamic_strength = 1.0f;

        float current_strength = 1.0f;

        float pre_shadow_strength = 1.0f;

        float runtime_average_strength = 1.0f;

        bool has_runtime_average = false;

        SDL_Color runtime_average_color{255, 255, 255, 255};

        float runtime_average_raw_intensity = 0.0f;

        SDL_FPoint runtime_average_direction{0.0f, 0.0f};

        bool has_runtime_direction = false;

        SDL_FPoint runtime_light_offset{0.0f, 0.0f};

        float runtime_light_offset_weight = 0.0f;

        bool has_runtime_light_offset = false;
};

    struct LightingChunk {
        Chunk* parent = nullptr;

        int local_i = 0;
        int local_j = 0;
        int global_i = 0;
        int global_j = 0;
        int resolution = 0;
        int step = 1;
        SDL_Rect world_bounds{0, 0, 0, 0};

        ChunkShadowHistory shadow_history{};

        ChunkShadowParameters shadow{};
        ChunkLightingState    lighting{};

        bool lighting_dirty       = true;
        bool has_dynamic_overlay  = false;

        LightingChunk() = default;
        LightingChunk(Chunk* parent_chunk,
                      int    in_local_i,
                      int    in_local_j,
                      int    in_global_i,
                      int    in_global_j,
                      int    in_resolution,
                      int    in_step,
                      SDL_Rect bounds)
            : parent(parent_chunk)
            , local_i(in_local_i)
            , local_j(in_local_j)
            , global_i(in_global_i)
            , global_j(in_global_j)
            , resolution(in_resolution)
            , step(in_step)
            , world_bounds(bounds) {}

        void releaseLightingArtifacts();
};

    ChunkShadowHistory shadow_history{};

    ChunkLightingState lighting{};
    ChunkShadowParameters shadow{};

    bool lighting_dirty      = true;
    bool has_dynamic_overlay = false;

    int lighting_resolution() const { return lighting_resolution_; }
    int lighting_step() const { return lighting_step_; }
    int lighting_columns() const { return lighting_columns_; }
    int lighting_rows() const { return lighting_rows_; }
    const std::vector<LightingChunk>& lighting_chunks() const { return lighting_chunks_; }
    std::vector<LightingChunk>& lighting_chunks() { return lighting_chunks_; }
    LightingChunk* lighting_chunk_at(int local_i, int local_j);
    const LightingChunk* lighting_chunk_at(int local_i, int local_j) const;
    LightingChunk* lighting_chunk_from_world(SDL_Point world_px);
    const LightingChunk* lighting_chunk_from_world(SDL_Point world_px) const;
    void rebuild_lighting_chunks();
    void update_aggregate_from_lighting_chunks();

    Chunk() = default;
    Chunk(int in_i, int in_j, int r, SDL_Rect bounds)
        : i(in_i)
        , j(in_j)
        , r_chunk(r)
        , world_bounds(bounds) {
        rebuild_lighting_chunks();
    }
    ~Chunk();

    void releaseLightingArtifacts();

    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;
    Chunk(Chunk&&) noexcept = default;
    Chunk& operator=(Chunk&&) noexcept = default;

private:
    int lighting_resolution_ = 0;
    int lighting_step_       = 1;
    int lighting_columns_    = 1;
    int lighting_rows_       = 1;
    std::vector<LightingChunk> lighting_chunks_{};
};

}

class LightMap {
public:
    struct ShadowSettings {
        int   search_radius_cells     = 1;
        float falloff_horizontal      = 1.0f;
        float falloff_vertical        = 1.0f;
        float max_offset_x_px         = 64.0f;
        float max_offset_y_px         = 48.0f;

        float opacity_sensitivity_percent = 50.0f;
        int   frame_blend_falloff_frames = 100;
        float sampling_static_weight  = 0.0f;
        float sampling_dynamic_weight = 1.0f;
};

    static constexpr float kDefaultStaticWeight  = 0.0f;
    static constexpr float kDefaultDynamicWeight = 1.0f;

    struct SampledBrightness {
        float static_component  = 1.0f;
        float dynamic_component = 1.0f;
        float blended           = 1.0f;
        SDL_Color color{255, 255, 255, 255};
        bool      has_color = false;
};

    LightMap(Assets* assets,
             int screen_width,
             int screen_height)
        : assets_(assets)
        , screen_width_(screen_width)
        , screen_height_(screen_height) {}
    ~LightMap();

    void rebuild(SDL_Renderer* renderer);
    void update(SDL_Renderer* renderer, std::uint32_t delta_ms);
    void ingest_runtime_samples(const runtime_lighting::RuntimeLightingFrame& frame);

    SampledBrightness sample_lighting(int world_x, int world_y, float static_weight = kDefaultStaticWeight, float dynamic_weight = kDefaultDynamicWeight) const;
    SampledBrightness sample_lighting_bilinear(float world_x, float world_y, float static_weight = kDefaultStaticWeight, float dynamic_weight = kDefaultDynamicWeight) const;
    float sample_brightness(int world_x, int world_y, float static_weight = kDefaultStaticWeight, float dynamic_weight = kDefaultDynamicWeight) const;
    float sample_brightness_bilinear(float world_x, float world_y, float static_weight = kDefaultStaticWeight, float dynamic_weight = kDefaultDynamicWeight) const;

    void render_visible_chunks(SDL_Renderer* renderer, const SDL_Rect& view_rect) const;
    void render_visible_chunks(SDL_Renderer* renderer, const SDL_Rect& view_rect, float alpha_multiplier, const SDL_Color& color_mod) const;
    void subtract_runtime_shadow_from_texture(SDL_Renderer* renderer, SDL_Texture* target_texture, const SDL_Rect& target_rect, const SDL_Rect& screen_rect, float alpha_multiplier) const;
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
    struct RuntimeShadowMaskRender {
        SDL_Rect  dest_rect{0, 0, 0, 0};
        SDL_Color color{0, 0, 0, 0};
        float     alpha = 0.0f;
};

    void collect_runtime_shadow_masks(const SDL_Rect& view_rect, float alpha_multiplier, const SDL_Color& color_mod, std::vector<RuntimeShadowMaskRender>& out) const;

    SDL_Texture* ensure_runtime_shadow_mask(SDL_Renderer* renderer) const;
    void         destroy_runtime_shadow_mask() const;

    void invalidate_scene_light_cache();
    void rebuild_scene_light_cache(const std::vector<world::Chunk*>& chunks);

    std::pair<float, float> resolve_sampling_weights(float static_weight, float dynamic_weight) const;

    Assets* assets_ = nullptr;
    int     screen_width_  = 0;
    int     screen_height_ = 0;

    mutable float        last_map_light_opacity_ = -1.0f;
    mutable SDL_FPoint   last_map_light_direction_{0.0f, 0.0f};
    mutable bool         last_map_light_direction_valid_ = false;
    mutable std::recursive_mutex mutex_;

    mutable SDL_Texture*   runtime_shadow_mask_          = nullptr;
    mutable SDL_Renderer*  runtime_shadow_mask_renderer_ = nullptr;
    mutable int            runtime_shadow_mask_w_        = 0;
    mutable int            runtime_shadow_mask_h_        = 0;
    mutable SDL_BlendMode  runtime_shadow_mask_blend_    = SDL_BLENDMODE_BLEND;
    mutable Uint32         last_render_tick_             = 0;
    mutable bool           rendered_in_current_tick_     = false;

    double scene_light_sum_        = 0.0;
    int    scene_light_count_      = 0;
    int    cached_chunk_count_     = 0;
    bool   scene_light_cache_valid_ = false;
};

