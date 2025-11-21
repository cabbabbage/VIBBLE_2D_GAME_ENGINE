#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL.h>

#include <nlohmann/json.hpp>

#include "core/manifest/manifest_loader.hpp"
#include "render/asset_light.hpp"
#include "utils/ranged_color.hpp"

class Assets;
class Asset;
class AnimationFrame;
class LightMap;
class AssetLibrary;
class camera;
class LightSource;
namespace world { class Chunk; }
namespace world { class Grid; }
namespace runtime_lighting { struct AssetLight; }




class Global_Light_Source {

public:
    Global_Light_Source(SDL_Renderer* renderer, SDL_Point screen_center, int screen_width, SDL_Color fallback_base_color);
    ~Global_Light_Source() = default;
    bool initialize_from_map_manifest(const nlohmann::json& map_info, std::string_view map_id);
    void apply_config(const nlohmann::json& data);
    SDL_Color get_current_color() const;
    int       get_brightness() const;
    // Temporarily override the alpha channel of the current map light color
    // without modifying underlying map configuration. Pass std::nullopt to clear.
    void      set_alpha_override(std::optional<Uint8> alpha);

private:
    void set_defaults(SDL_Color fallback_base_color);
    bool load_from_map_manifest(const nlohmann::json& map_info, std::string_view map_id);
    SDL_Color resolve_color_from_config(const nlohmann::json& data) const;
    Uint8 clamp_alpha(Uint8 value) const;
    SDL_Color clamp_color_alpha(SDL_Color color) const;

private:
    SDL_Renderer* renderer_;
    SDL_Point     screen_center_;
    utils::color::RangedColor base_color_range_{{255,255},{255,255},{255,255},{255,255}};
    SDL_Color base_color_;
    SDL_Color current_color_;
    int       light_brightness = 255;

    // Optional override for alpha channel applied in get_current_color()
    std::optional<Uint8> alpha_override_{};
};





// Renders prebuilt per-grid tiles with parallax applied. Intended to be
// invoked at the start of SceneRenderer::render().
class GridTileRenderer {
public:
    explicit GridTileRenderer(Assets* assets) : assets_(assets) {}

    // Render using Assets-owned camera and grid.
    void render(SDL_Renderer* renderer);

    // Render using explicit camera and grid references.
    void render(SDL_Renderer* renderer, const camera& cam, const world::Grid& grid);

private:
    Assets* assets_ = nullptr;
};







class AssetLightRenderer {
public:
    struct DarkMaskResult {
        std::size_t max_vertices = 0;
        std::size_t max_indices  = 0;
    };

    AssetLightRenderer(SDL_Renderer* renderer,
                       const runtime_lighting::AssetLight& source,
                       std::vector<SDL_Vertex>& scratch_vertices,
                       std::vector<int>& scratch_indices,
                       float light_visibility,
                       float flicker_time_seconds);
    ~AssetLightRenderer();

    bool valid() const { return valid_; }

    DarkMaskResult accumulate_dark_mask();
    void draw_behind();
    void draw_in_front();

private:
    enum class Pass { kBehind, kFront };

    struct ComputedLight {
        const LightSource* source = nullptr;
        int                intensity = 0;
        float              center_x  = 0.0f;
        float              center_y  = 0.0f;
        float              radius_x  = 0.0f;
        float              radius_y  = 0.0f;
        SDL_Rect           bounds{0, 0, 0, 0};
        float              fade_exponent = 1.0f;
        bool               textured      = false;
        SDL_Rect           texture_dst{0, 0, 0, 0};
        float              center_ratio_x = 0.0f;
        float              center_ratio_y = 0.0f;
        float              radius_ratio_x = 0.0f;
        float              radius_ratio_y = 0.0f;
        float              texture_ratio_x = 0.0f;
        float              texture_ratio_y = 0.0f;
        float              texture_ratio_w = 0.0f;
        float              texture_ratio_h = 0.0f;
    };

    bool prepare_light(const LightSource& light, ComputedLight& out) const;
    void draw_pass(Pass pass);
    void render_textured_light(const ComputedLight& info, const SDL_Rect& dst);
    void render_radial_light(const ComputedLight& info,
                             const SDL_Color&     base_color,
                             float                alpha_scale,
                             float                center_x,
                             float                center_y,
                             float                radius_x,
                             float                radius_y,
                             const SDL_Rect&      fallback_rect);
    bool render_light_with_asset_mask(const LightSource& light,
                                      const ComputedLight& computed,
                                      const SDL_Color& base_color);
    float compute_flicker_multiplier(const LightSource& light) const;
    SDL_Texture* ensure_mask_composite_texture(int width, int height, Uint32 format_hint);
    SDL_Rect     scale_mask_rect_to_asset(const SDL_Rect& rect, int mask_width, int mask_height) const;

    SDL_Renderer*                          renderer_ = nullptr;
    const runtime_lighting::AssetLight&    source_;
    Asset*                                 asset_ = nullptr;
    const std::vector<LightSource>*        lights_ = nullptr;
    float                                  scale_x_ = 1.0f;
    float                                  scale_y_ = 1.0f;
    float                                  safe_zoom_scale_x_ = 1.0f;
    float                                  safe_zoom_scale_y_ = 1.0f;
    float                                  center_base_x_ = 0.0f;
    float                                  center_base_y_ = 0.0f;
    bool                                   valid_ = false;
    std::vector<SDL_Vertex>&               scratch_vertices_;
    std::vector<int>&                      scratch_indices_;
    SDL_Texture*                           mask_composite_texture_ = nullptr;
    int                                    mask_composite_w_ = 0;
    int                                    mask_composite_h_ = 0;
    Uint32                                 mask_composite_format_ = 0;
    float                                  overlay_visibility_ = 1.0f;
    float                                  flicker_time_seconds_ = 0.0f;
};





class RenderAsset {

        public:
    explicit RenderAsset(SDL_Renderer* renderer);
    SDL_Texture* texture_for_scale(Asset* asset,
                                   SDL_Texture* base_tex,
                                   int base_w,
                                   int base_h,
                                   int target_w,
                                   int target_h,
                                   float hysteresis_margin);

	private:
    SDL_Renderer* renderer_;
};




struct StageContext;

class IRenderStage {
public:
    virtual ~IRenderStage() = default;

    virtual bool         supports(const Asset& asset) const = 0;
    virtual SDL_Texture* run(SDL_Renderer* renderer, const Asset& asset, StageContext& context) = 0;
};








struct SceneLighting {
    camera&                camera_view;
    Global_Light_Source&   main_light;
    Asset*                 player = nullptr;
    const LightMap*        light_map_sampler = nullptr;
    world::Grid*           world_grid = nullptr;
    std::nullptr_t         reactive_shadow_settings_stub = nullptr; // TODO(#reactive-shadows): placeholder until settings return.
};

struct StageContext {
    SDL_Texture* base_texture = nullptr;
    SceneLighting* lighting   = nullptr;
    int           width       = 0;
    int           height      = 0;
    SDL_Texture*  reusable_final = nullptr;
    SDL_Texture*  final_texture = nullptr;
    SDL_Texture*  stage_destination = nullptr;
    SDL_BlendMode stage_blend       = SDL_BLENDMODE_BLEND;
    bool          stage_drew_to_destination = false;
    SDL_Rect      screen_rect{ 0, 0, 0, 0 };
    SDL_FPoint    screen_center{ 0.0f, 0.0f }; // Reserved for future reactive shadow alignment.
    float         reference_screen_height = 1.0f;
    float         base_shadow_opacity     = 204.0f / 255.0f;
    int           screen_width_px         = 0;
    int           screen_height_px        = 0;
    float         static_light_strength   = 1.0f;
    float         dynamic_light_strength  = 1.0f;
    float         blended_light_strength  = 1.0f;
    SDL_Color     runtime_light_color{255, 255, 255, 255};
    bool          has_runtime_light_color = false;

    SDL_Rect asset_bounds() const { return SDL_Rect{ 0, 0, width, height }; }
    SDL_Point anchor_bottom_center() const { return SDL_Point{ width / 2, height }; }
    SDL_Rect dest_from_world_offset(int dx_world, int dy_world, int lw, int lh) const {
        SDL_Point anchor = anchor_bottom_center();
        return SDL_Rect{ anchor.x + dx_world - (lw / 2), anchor.y + dy_world - (lh / 2), lw, lh };
    }

    Uint8                    main_light_alpha() const;
    Uint8                    main_light_brightness() const;
    Global_Light_Source&     main_light();
    const Global_Light_Source& main_light() const;
    camera&                  camera_view();
    const camera&            camera_view() const;
    Asset*                   player() const;
    const LightMap*   light_map() const { return lighting ? lighting->light_map_sampler : nullptr; }
    void update_projection(Asset& asset);
    std::nullptr_t reactive_shadow_settings() const { return nullptr; } // TODO(#reactive-shadows): Remove stub when new settings land.
};

class AssetRenderPipeline {
public:
    AssetRenderPipeline(SDL_Renderer* renderer, const SceneLighting& lighting);

    SDL_Texture* run(Asset& asset);
    SDL_Texture* regenerateFinalTexture(Asset* asset);
    SDL_Texture* texture_for_scale(Asset* asset,
                                   SDL_Texture* base_tex,
                                   int          base_w,
                                   int          base_h,
                                   int          target_w,
                                   int          target_h,
                                   float        hysteresis_margin);

    void set_low_quality_mode(bool enable);
    bool low_quality_mode() const { return low_quality_mode_; }

    SceneLighting&       lighting();
    const SceneLighting& lighting() const;
    void                 set_player_asset(Asset* player);

private:
    SDL_Renderer*                          renderer_ = nullptr;
    SceneLighting                          lighting_;
    RenderAsset                            render_asset_;
    struct StageEntry {
        std::unique_ptr<IRenderStage> stage;
        SDL_BlendMode                 blend = SDL_BLENDMODE_BLEND;
        bool                          stage_manages_texture = false;
        bool                          skip_in_low_quality   = false;
};
    std::vector<StageEntry> stages_;
    bool                    low_quality_mode_ = false;
};






class SceneRenderer {

public:
    SceneRenderer(SDL_Renderer* renderer, Assets* assets, int screen_width, int screen_height, const nlohmann::json& map_manifest, const std::string& map_id);
    ~SceneRenderer();
    static inline bool prerequisites_ready(SDL_Renderer* renderer, Assets* assets, std::string* reason = nullptr) {
        if (!renderer) {
            if (reason) { *reason = "SDL_Renderer pointer is null."; }
            return false;
        }
        if (!assets) {
            if (reason) { *reason = "Assets pointer is null."; }
            return false;
        }
        if (reason) { reason->clear(); }
        return true;
    }
    void render();
    void apply_map_light_config(const nlohmann::json& data);
    SDL_Renderer* get_renderer() const;
    void set_low_quality_rendering(bool enabled);
    bool low_quality_rendering() const { return low_quality_rendering_; }
    void toggle_light_map_only_mode() { light_map_only_mode_ = !light_map_only_mode_; }
    bool light_map_only_mode() const { return light_map_only_mode_; }
    void toggle_chunk_preview() { chunk_preview_enabled_ = !chunk_preview_enabled_; }
    bool chunk_preview_enabled() const { return chunk_preview_enabled_; }
    bool update_map_light_enabled() const { return update_map_light_enabled_; }
    void set_update_map_light_enabled(bool enabled);
    void set_dark_mask_enabled(bool enabled);
    bool dark_mask_enabled() const { return dark_mask_enabled_; }
    Global_Light_Source& map_light_source() { return main_light_source_; }
    const Global_Light_Source& map_light_source() const { return main_light_source_; }
    LightMap* light_map();
    const LightMap* light_map() const;

private:
    bool shouldRegen(Asset* a);
    SDL_FRect get_scaled_position_rect(Asset* a, int fw, int fh, float inv_scale, int min_w, int min_h, float reference_screen_height);
    SDL_FRect get_child_position_rect(const Asset* parent,
                                      SDL_Point world_point,
                                      int fw,
                                      int fh,
                                      float inv_scale,
                                      int min_w,
                                      int min_h,
                                      float reference_screen_height);
    bool initialize_static_light_chunks();

    private:
        using LightOverlaySource = runtime_lighting::AssetLight;
        struct PrevalidatedTag {};

        SceneRenderer(PrevalidatedTag,
                      SDL_Renderer* renderer,
                      Assets* assets,
                      int screen_width,
                      int screen_height,
                      const nlohmann::json& map_manifest,
                      const std::string& map_id);
        static PrevalidatedTag require_prerequisites(SDL_Renderer* renderer, Assets* assets);

    struct AssetRenderCommand {
        Asset*      asset               = nullptr;
        SDL_Texture* source_texture      = nullptr;
        SDL_Texture* final_texture       = nullptr;
        SDL_FRect    dst                 { 0.0f, 0.0f, 0.0f, 0.0f };
        bool         uses_scaled_texture = false;
        bool         highlighted         = false;
        bool         selected            = false;
        bool         flipped             = false;
        float        alpha               = 1.0f;
        float        rotation_degrees    = 0.0f;
        bool         has_custom_pivot    = false;
        SDL_FPoint   rotation_pivot      { 0.0f, 0.0f };
        SDL_Texture* depthcue_foreground_texture = nullptr;
        SDL_Texture* depthcue_background_texture = nullptr;
        Uint8        depthcue_foreground_alpha   = 0;
        Uint8        depthcue_background_alpha   = 0;
    };

    bool ensure_darkness_overlay();
    void destroy_darkness_overlay();
    void render_dynamic_darkness_overlay(float map_light_opacity, float flicker_time_seconds);
    bool has_dark_mask_overlay_sources();
    bool ensure_sky_texture();
    void destroy_sky_texture();
    void render_sky_layer(const camera& cam);


    SDL_Renderer*  renderer_;
    Assets*        assets_;
    int            screen_width_;
    int            screen_height_;
    Global_Light_Source main_light_source_;
    AssetRenderPipeline render_pipeline_;
    std::unique_ptr<GridTileRenderer> tile_renderer_;
    std::unique_ptr<LightMap> light_map_;
    bool           debugging = false;
    bool           low_quality_rendering_ = false;
    bool           light_map_only_mode_ = false;
    bool           chunk_preview_enabled_ = false;
    bool           update_map_light_enabled_ = true;
    bool           chunk_lighting_suspended_ = false;
    bool           dark_mask_enabled_ = true;

    std::uint64_t frame_counter_ = 0;
    std::vector<AssetRenderCommand> texture_commands_;
    std::vector<AssetRenderCommand> remaining_commands_;
    std::vector<LightOverlaySource> light_overlay_sources_;
    bool                            light_overlay_sources_dark_mask_cache_dirty_ = true;
    bool                            light_overlay_sources_have_dark_mask_cached_ = false;
    std::vector<SDL_Vertex> darkness_overlay_vertices_;
    std::vector<int>        darkness_overlay_indices_;
    std::size_t             darkness_overlay_vertex_capacity_hint_ = 0;
    std::size_t             darkness_overlay_index_capacity_hint_  = 0;
    std::vector<SDL_Vertex> grid_slice_vertices_;
    std::vector<int>        grid_slice_indices_;
    std::size_t             grid_slice_vertex_capacity_hint_ = 0;
    std::size_t             grid_slice_index_capacity_hint_  = 0;
    std::uint64_t           grid_slice_draw_calls_saved_accum_ = 0;
    std::uint64_t           grid_slice_batches_accum_         = 0;
    SDL_Texture* darkness_overlay_texture_ = nullptr;
    int          darkness_overlay_width_   = 0;
    int          darkness_overlay_height_  = 0;
    SDL_Color    map_clear_color_{0, 0, 0, 255};

    // Depth-cue warmup: skip expensive per-asset effects for first N frames
    // after initialization to avoid stalls when transitioning from the loading screen.
    // Configurable via constructor constants in .cpp; defaults to a small number of frames.
    std::uint32_t depthcue_warmup_frames_ = 8; // frames to skip depth-cue effects after init

    // Full-scene post-processing targets
    SDL_Texture* scene_composite_tex_ = nullptr;   // Draws full scene here first
    SDL_Texture* postprocess_tex_     = nullptr;   // Reused staging for color pass
    SDL_Texture* blur_tex_            = nullptr;   // Reused staging for blur pass

    std::uint64_t darkness_overlay_skipped_frames_  = 0;
    std::uint64_t darkness_overlay_rendered_frames_ = 0;
    bool          darkness_overlay_skip_logged_     = false;
    std::filesystem::path sky_texture_path_;
    SDL_Texture*          sky_texture_       = nullptr;
    int                   sky_texture_width_ = 0;
    int                   sky_texture_height_ = 0;
    bool                  sky_texture_failed_ = false;
};







namespace render_pipeline {

namespace detail {
    inline float& quality_cap_storage() {
        static float cap = 1.0f;
        return cap;
    }
    inline std::once_flag& scale_hint_once() {
        static std::once_flag flag;
        return flag;
    }
}

inline void EnsureBestScaleHint() {
#if SDL_VERSION_ATLEAST(2,0,12)
    std::call_once(detail::scale_hint_once(), []() {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    });
#endif
}

struct ScaleSelection {
    int   index           = 0;
    float requested_scale = 1.0f;
    float stored_scale    = 1.0f;
    float remainder_scale = 1.0f;
    float hysteresis_min  = 0.0f;
    float hysteresis_max  = std::numeric_limits<float>::max();
    int   preload_index   = -1;
};

struct ScalingLogic {
    using ScaleSteps = std::vector<float>;

    struct HysteresisState {
        int   last_index = 0;
        float min_scale  = 0.0f;
        float max_scale  = std::numeric_limits<float>::max();
    };

    struct HysteresisOptions {
        float margin         = 0.05f;
        float preload_margin = 0.02f;
    };

    static constexpr float kDefaultHysteresisMargin = 0.05f;
    static constexpr float kDefaultPreloadMargin    = 0.02f;

    static void SetQualityCap(float cap) {
        if (!std::isfinite(cap) || cap <= 0.0f) {
            cap = 0.1f;
        }
        cap = std::clamp(cap, 0.1f, 1.0f);
        detail::quality_cap_storage() = cap;
    }

    static float QualityCap() {
        return detail::quality_cap_storage();
    }

    struct ScaleProfile {
        ScaleSteps    steps;
        std::uint64_t revision  = 0;
        bool          had_entry = false;
        bool          created_entry = false;
        bool          revision_changed = false;
        float         min_scale = 1.0f;
        float         max_scale = 1.0f;
        bool has_custom_steps() const { return !steps.empty(); }
    };

    static constexpr std::size_t kMaxVariantCount     = 4;
    static constexpr std::size_t kDefaultVariantCount = kMaxVariantCount;
    static inline const ScaleSteps& DefaultScaleSteps() {
        static const ScaleSteps kDefaultSteps = {1.00f, 0.75f, 0.50f, 0.25f};
        return kDefaultSteps;
    }

    static inline void NormalizeVariantSteps(ScaleSteps& steps) {
        // Enforce a single global variant set: 100%, 75%, 50%, 25%.
        const auto& defaults = DefaultScaleSteps();
        steps.assign(defaults.begin(), defaults.end());
    }

    static inline float ComputeScale(int base_w, int base_h, int target_w, int target_h) {
        if (base_w <= 0 || base_h <= 0 || target_w <= 0 || target_h <= 0) {
            return 1.0f;
        }
        const float scale_w = static_cast<float>(target_w) / static_cast<float>(base_w);
        const float scale_h = static_cast<float>(target_h) / static_cast<float>(base_h);
        return (scale_w < scale_h) ? scale_w : scale_h;
    }

    static inline ScaleSelection Choose(float desired_scale) {
        return Choose(desired_scale,
                      DefaultScaleSteps(),
                      HysteresisState{},
                      desired_scale,
                      HysteresisOptions{});
    }

    static inline ScaleSelection Choose(float desired_scale, const ScaleSteps& steps) {
        return Choose(desired_scale,
                      steps,
                      HysteresisState{},
                      desired_scale,
                      HysteresisOptions{});
    }

    static inline ScaleSelection Choose(float desired_scale,
                                        const ScaleSteps& steps,
                                        const HysteresisState& state,
                                        float smoothed_scale,
                                        HysteresisOptions options = HysteresisOptions{}) {
        // Base selection prefers downscaling: choose smallest stored scale >= requested.
        // If none is available (e.g., zoomed-in beyond largest allowed), choose largest allowed.
        ScaleSelection base = choose_closest(desired_scale, steps);
        if (steps.empty()) {
            return base;
        }

        if (!std::isfinite(options.margin) || options.margin < 0.0f) {
            options.margin = kDefaultHysteresisMargin;
        }
        if (!std::isfinite(options.preload_margin) || options.preload_margin < 0.0f) {
            options.preload_margin = kDefaultPreloadMargin;
        }

        const float safe_smoothed = (std::isfinite(smoothed_scale) && smoothed_scale > 0.0f)
                                        ? smoothed_scale
                                        : base.requested_scale;

        HysteresisState current = state;
        const int max_index = static_cast<int>(steps.size() - 1);
        current.last_index = std::clamp(current.last_index, 0, max_index);
        if (!std::isfinite(current.min_scale) || current.min_scale < 0.0f) {
            current.min_scale = 0.0f;
        }
        if (!std::isfinite(current.max_scale) || current.max_scale < current.min_scale) {
            current.max_scale = std::numeric_limits<float>::max();
        }

        int candidate = current.last_index;
        auto bounds = variant_bounds(steps, candidate, options.margin);
        float min_bound = bounds.first;
        float max_bound = bounds.second;

        if (safe_smoothed >= current.min_scale && safe_smoothed <= current.max_scale) {
            // Stay with previous variant while within hysteresis window.
            candidate = current.last_index;
            bounds = variant_bounds(steps, candidate, options.margin);
            min_bound = bounds.first;
            max_bound = bounds.second;
        } else if (safe_smoothed < current.min_scale && candidate < max_index) {
            do {
                candidate = std::min(candidate + 1, max_index);
                bounds = variant_bounds(steps, candidate, options.margin);
                min_bound = bounds.first;
                max_bound = bounds.second;
            } while (safe_smoothed < min_bound && candidate < max_index);
        } else if (safe_smoothed > current.max_scale && candidate > 0) {
            do {
                candidate = std::max(candidate - 1, 0);
                bounds = variant_bounds(steps, candidate, options.margin);
                min_bound = bounds.first;
                max_bound = bounds.second;
            } while (safe_smoothed > max_bound && candidate > 0);
        } else {
            candidate = base.index;
            bounds = variant_bounds(steps, candidate, options.margin);
            min_bound = bounds.first;
            max_bound = bounds.second;
        }

        // Do not clamp candidate to the "closest" index; allow selecting a larger
        // variant (smaller index) when it avoids upscaling a smaller variant.

        ScaleSelection result = base;
        result.index = candidate;
        result.stored_scale = steps[candidate];
        if (result.stored_scale <= 0.0f) {
            result.stored_scale = 1.0f;
        }
        result.remainder_scale = (result.stored_scale > 0.0f)
                                     ? (result.requested_scale / result.stored_scale)
                                     : 1.0f;
        bounds = variant_bounds(steps, candidate, options.margin);
        result.hysteresis_min = bounds.first;
        result.hysteresis_max = bounds.second;

        result.preload_index = -1;
        float best_distance = std::numeric_limits<float>::max();

        if (candidate < max_index) {
            const float boundary = 0.5f * (steps[candidate] + steps[candidate + 1]);
            const float diff = std::fabs(safe_smoothed - boundary);
            if (diff <= options.preload_margin) {
                result.preload_index = candidate + 1;
                best_distance       = diff;
            }
        }
        if (candidate > 0) {
            const float boundary = 0.5f * (steps[candidate] + steps[candidate - 1]);
            const float diff = std::fabs(safe_smoothed - boundary);
            if (diff <= options.preload_margin && diff < best_distance && (candidate - 1) >= base.index) {
                result.preload_index = candidate - 1;
                best_distance       = diff;
            }
        }

        if (result.preload_index < 0 || result.preload_index > max_index) {
            result.preload_index = -1;
        }

        return result;
    }

    static inline int ScalePercent(std::size_t index) {
        return ScalePercent(DefaultScaleSteps(), index);
    }

    static inline int ScalePercent(const ScaleSteps& steps, std::size_t index) {
        if (index >= steps.size()) {
            return 0;
        }
        return static_cast<int>(std::lround(steps[index] * 100.0f));
    }

    static inline std::string VariantFolder(const std::string& base, std::size_t index) {
        return VariantFolder(base, DefaultScaleSteps(), index);
    }

    static inline std::string VariantFolder(const std::string& base, const ScaleSteps& steps, std::size_t index) {
        return std::filesystem::path(base).append("scale_" + std::to_string(ScalePercent(steps, index))).string();
    }

    static inline std::array<int, kDefaultVariantCount> PercentSteps() {
        std::array<int, kDefaultVariantCount> percents{};
        const auto& defaults = DefaultScaleSteps();
        const std::size_t limit = std::min<std::size_t>(percents.size(), defaults.size());
        for (std::size_t i = 0; i < limit; ++i) {
            percents[i] = ScalePercent(defaults, i);
        }
        return percents;
    }

    static inline std::vector<int> PercentSteps(const ScaleSteps& steps) {
        std::vector<int> percents;
        percents.reserve(steps.size());
        for (std::size_t i = 0; i < steps.size(); ++i) {
            percents.push_back(ScalePercent(steps, i));
        }
        return percents;
    }

    static inline void LoadPrecomputedProfiles(bool force_reload = false) {
        ProfilesState& state = profiles_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        if (force_reload) {
            state.loaded = false;
        }
        ensure_loaded(state);
    }

    // Reset profile history so subsequent loads treat current manifest revisions as the baseline.
    // This avoids triggering cache invalidation after tools rewrite scaling profiles at runtime.
    static inline void ResetProfileHistory() {
        ProfilesState& state = profiles_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        state.history.clear();
        state.loaded = false;
    }

    static inline ScaleProfile ProfileForAsset(const std::string& asset_key) {
        ProfilesState& state = profiles_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        ensure_loaded(state);

        ScaleProfile profile;
        profile.had_entry = false;
        profile.created_entry = false;
        profile.min_scale = 1.0f;
        profile.max_scale = 1.0f;

        if (!asset_key.empty()) {
            auto it = state.entries.find(asset_key);
            if (it != state.entries.end()) {
                profile.had_entry = true;
                profile.steps     = it->second.steps;
                profile.revision  = it->second.revision;
                profile.min_scale = it->second.min_scale;
                profile.max_scale = it->second.max_scale;
                record_profile_history(state, asset_key, profile);
                return profile;
            }
        }

        const auto& defaults = DefaultScaleSteps();
        profile.steps.assign(defaults.begin(), defaults.end());
        profile.revision = 0;
        record_profile_history(state, asset_key, profile);
        return profile;
    }

private:
    static inline ScaleSelection choose_closest(float desired_scale, const ScaleSteps& steps) {
        ScaleSelection result{};
        if (steps.empty()) {
            result.requested_scale = std::isfinite(desired_scale) && desired_scale > 0.0f ? desired_scale : 1.0f;
            result.stored_scale    = 1.0f;
            result.index           = 0;
            result.remainder_scale = result.requested_scale;
            return result;
        }
        float sanitized = desired_scale;
        if (!std::isfinite(sanitized)) {
            sanitized = 1.0f;
        }
        if (sanitized <= 0.0f) {
            sanitized = steps.back();
        }

        result.requested_scale = sanitized;

        const float quality_cap = QualityCap();
        const bool  enforce_cap = std::isfinite(quality_cap) && quality_cap > 0.0f && quality_cap < 0.999f;
        bool has_allowed = false;
        if (enforce_cap) {
            for (float candidate : steps) {
                if (candidate <= quality_cap + 1e-4f) {
                    has_allowed = true;
                    break;
                }
            }
        }

        // Steps are sorted descending. Pick the first allowed step >= requested.
        int   chosen_index = -1;
        float chosen_scale = steps.front();
        for (std::size_t i = 0; i < steps.size(); ++i) {
            const float candidate = steps[i];
            if (enforce_cap && has_allowed && candidate > quality_cap + 1e-4f) {
                continue;
            }
            if (candidate + 1e-4f >= sanitized) {
                chosen_index = static_cast<int>(i);
                chosen_scale = candidate;
                break;
            }
        }

        // If none is >= requested (zoomed-in), choose the largest allowed.
        if (chosen_index < 0) {
            for (std::size_t i = 0; i < steps.size(); ++i) {
                const float candidate = steps[i];
                if (enforce_cap && has_allowed && candidate > quality_cap + 1e-4f) {
                    continue;
                }
                chosen_index = static_cast<int>(i);
                chosen_scale = candidate;
                break;
            }
            if (chosen_index < 0) {
                // Fallback to the largest step if filtering excluded all.
                chosen_index = 0;
                chosen_scale = steps.front();
            }
        }

        result.index           = chosen_index;
        result.stored_scale    = chosen_scale;
        result.remainder_scale = (chosen_scale > 0.0f) ? (sanitized / chosen_scale) : 1.0f;
        return result;
    }

    static inline std::pair<float, float> variant_bounds(const ScaleSteps& steps,
                                                         int index,
                                                         float margin) {
        if (steps.empty()) {
            return {0.0f, std::numeric_limits<float>::max()};
        }
        const float safe_margin = (std::isfinite(margin) && margin > 0.0f) ? margin : 0.0f;
        const float current     = steps[std::clamp(index, 0, static_cast<int>(steps.size() - 1))];
        float min_bound = 0.0f;
        float max_bound = std::numeric_limits<float>::max();

        if (index + 1 < static_cast<int>(steps.size())) {
            const float boundary = 0.5f * (current + steps[index + 1]);
            min_bound = std::max(0.0f, boundary - safe_margin);
        }
        if (index > 0) {
            const float boundary = 0.5f * (current + steps[index - 1]);
            max_bound = boundary + safe_margin;
        }

        if (min_bound > max_bound) {
            const float midpoint = 0.5f * (min_bound + max_bound);
            min_bound            = std::min(min_bound, midpoint);
            max_bound            = std::max(max_bound, midpoint);
        }

        return {min_bound, max_bound};
    }

    struct ProfileEntry {
        ScaleSteps    steps;
        std::uint64_t revision = 0;
        float         min_scale = 1.0f;
        float         max_scale = 1.0f;
    };

    struct ProfileObservation {
        bool          had_entry = false;
        std::uint64_t revision  = 0;
    };

    struct ProfilesState {
        bool                   loaded = false;
        std::mutex             mutex;
        std::unordered_map<std::string, ProfileEntry> entries;
        std::unordered_map<std::string, ProfileObservation> history;
    };

    static inline ProfilesState& profiles_state() {
        static ProfilesState state;
        return state;
    }

    static inline void ensure_loaded(ProfilesState& state) {
        // Dynamic scaling profiles are disabled; always fall back to the fixed defaults.
        // Mark as loaded to avoid repeated work.
        if (state.loaded) {
            return;
        }
        state.loaded = true;
        state.entries.clear();
    }

    static inline void record_profile_history(ProfilesState& state,
                                              const std::string& asset_key,
                                              ScaleProfile& profile) {
        if (asset_key.empty()) {
            return;
        }

        auto [it, inserted] = state.history.emplace(asset_key, ProfileObservation{
            profile.had_entry,
            profile.revision
        });

        if (inserted) {
            // First observation becomes the baseline; do not flag any changes.
            return;
        }

        ProfileObservation& previous = it->second;
        if (!previous.had_entry && profile.had_entry) {
            profile.created_entry = true;
        }
        if (previous.had_entry && profile.had_entry && previous.revision != profile.revision) {
            profile.revision_changed = true;
        }

        previous.had_entry = profile.had_entry;
        previous.revision  = profile.revision;
    }
};

inline SDL_Texture* CreateScaledTexture(SDL_Renderer* renderer,
                                        SDL_Texture* source,
                                        int src_w,
                                        int src_h,
                                        float scale) {
    if (!renderer || !source || scale <= 0.0f) {
        return nullptr;
    }

    const int dst_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(src_w) * scale)));
    const int dst_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(src_h) * scale)));

    if (dst_w == src_w && dst_h == src_h) {
        return nullptr;
    }

    Uint32 format = SDL_PIXELFORMAT_RGBA8888;
    if (SDL_QueryTexture(source, &format, nullptr, nullptr, nullptr) != 0) {
        format = SDL_PIXELFORMAT_RGBA8888;
    }

    SDL_Texture* scaled = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_TARGET, dst_w, dst_h);
    if (!scaled) {
        return nullptr;
    }

    SDL_SetTextureBlendMode(scaled, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(scaled, SDL_ScaleModeBest);
#endif

    EnsureBestScaleHint();

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, scaled);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    SDL_Rect dst{0, 0, dst_w, dst_h};
    SDL_RenderCopy(renderer, source, nullptr, &dst);

    SDL_SetRenderTarget(renderer, previous_target);
    return scaled;
}

inline SDL_Surface* CreateScaledSurface(SDL_Surface* src, float scale) {
    if (!src || scale <= 0.0f) {
        return nullptr;
    }

    if (std::fabs(scale - 1.0f) <= 1e-4f) {
        SDL_Surface* copy = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_RGBA8888);
        if (!copy) {
            return nullptr;
        }
        SDL_Rect rect{0, 0, src->w, src->h};
        if (SDL_BlitSurface(src, &rect, copy, &rect) != 0) {
            SDL_FreeSurface(copy);
            return nullptr;
        }
        return copy;
    }

    const int dst_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(src->w) * scale)));
    const int dst_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(src->h) * scale)));

    SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, dst_w, dst_h, 32, SDL_PIXELFORMAT_RGBA8888);
    if (!dst) {
        return nullptr;
    }

    SDL_Rect src_rect{0, 0, src->w, src->h};
    SDL_Rect dst_rect{0, 0, dst_w, dst_h};
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    if (SDL_BlitScaled(src, &src_rect, dst, &dst_rect) != 0) {
        SDL_FreeSurface(dst);
        return nullptr;
    }

    return dst;
}

} // namespace render_pipeline




namespace render_pipeline {

struct ScalingProfileBuildOptions {
    double                screen_aspect = 16.0 / 9.0;
    const AssetLibrary*   asset_library = nullptr;  // Optional: use existing asset library instead of creating new one
};

bool BuildScalingProfiles(const ScalingProfileBuildOptions& options);

}





namespace render_pipeline::shading {

void ClearShadowStateFor(const Asset* asset);

class RenderAsset : public IRenderStage {
public:
    bool         supports(const Asset& asset) const override;
    SDL_Texture* run(SDL_Renderer* renderer, const Asset& asset, StageContext& context) override;
};

class RenderShadowMask : public IRenderStage {
public:
    ~RenderShadowMask() override = default;
    bool         supports(const Asset& asset) const override;
    SDL_Texture* run(SDL_Renderer* renderer, const Asset& asset, StageContext& context) override;
};

}


