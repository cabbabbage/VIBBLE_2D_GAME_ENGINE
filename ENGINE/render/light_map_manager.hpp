#pragma once

#include <SDL.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <cstdint>

#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"

class Assets;
class LightMap;
class LightMapQuadrant;

class LightMapManager {
public:
    struct QuadrantParams {
        float opacity_q = 1.0f;
        float offset_x_q = 0.0f;
        float offset_y_q = 0.0f;
        float scale_q    = 1.0f;
    };

    struct QuadrantSnapshot {
        int      index               = -1;
        SDL_Rect world_rect{0, 0, 0, 0};
        bool     active              = false;
        bool     dirty               = false;
        float    base_brightness     = 0.0f;
        float    combined_brightness = 0.0f;
        float    static_min          = 0.0f;
        float    static_max          = 0.0f;
        float    static_average      = 0.0f;
        bool     static_empty        = true;
        float    shadow_opacity_min  = 0.0f;
        float    shadow_opacity_max  = 0.0f;
    };

    explicit LightMapManager(Assets* assets);

    void begin_frame();

    const LightMap* light_map() const;
    std::vector<QuadrantSnapshot> all_snapshots() const;
    std::vector<std::string>      assets_sampling_quadrant(int index) const;
    std::optional<QuadrantSnapshot> snapshot_for_quadrant(int index) const;
    std::optional<QuadrantParams> get_quadrant_params(SDL_FPoint world_or_screen_pos) const;
    std::optional<QuadrantParams> get_quadrant_params_for_index(int index) const;

private:
    struct QuadrantSignature {
        SDL_Rect world_rect{0, 0, 0, 0};
        int      grid_w        = 0;
        int      grid_h        = 0;
        float    base_brightness = 0.0f;
        float    static_average  = 0.0f;
        bool     static_empty     = true;
    };

    struct QuadrantCache {
        QuadrantParams   params{};
        QuadrantSignature signature{};
        std::uint32_t    last_compute_ticks = 0;
        std::uint64_t    last_frame         = 0;
        bool             valid              = false;
        bool             dirty              = true;
        bool             attempted_this_frame = false;
    };

    void ensure_cache_size(const LightMap* map) const;
    void mark_all_dirty() const;
    void refresh_global_state() const;
    QuadrantSignature signature_for(const LightMapQuadrant& quadrant) const;
    bool signature_changed(const QuadrantSignature& a, const QuadrantSignature& b) const;
    LightMapManager::QuadrantParams compute_params(const LightMapQuadrant& quadrant,
                                                   const QuadrantSignature& signature) const;
    SDL_FPoint current_light_direction() const;
    float      direction_factor_for_rect(const SDL_Rect& rect, const SDL_FPoint& light_dir) const;
    std::optional<int> find_quadrant_index(SDL_FPoint world_or_screen_pos) const;

    Assets* assets_ = nullptr;
    mutable std::vector<QuadrantCache> quadrant_caches_{};
    mutable render_pipeline::shading::ReactiveShadowSettings cached_settings_ =
        render_pipeline::shading::sanitize_reactive_shadow_settings({});
    mutable bool settings_initialized_ = false;
    mutable SDL_Point last_light_reference_{0, 0};
    mutable SDL_Point last_light_target_{0, 0};
    mutable int       last_light_brightness_ = -1;
    mutable SDL_Color last_light_color_{0, 0, 0, 0};
    mutable bool      light_state_initialized_ = false;
    mutable std::uint64_t frame_counter_ = 0;
};

