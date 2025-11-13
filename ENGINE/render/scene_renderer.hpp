#pragma once

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <cstddef>
#include <cstdint>
#include <SDL.h>
#include <nlohmann/json.hpp>
#include "global_light_source.hpp"
#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"
#include "render/camera.hpp"
#include "render/asset_light.hpp"
#include "render/grid_tile_renderer.hpp"

class Assets;
class Asset;
class AnimationFrame;
class LightMap;

class SceneRenderer {

public:
    SceneRenderer(SDL_Renderer* renderer, Assets* assets, int screen_width, int screen_height, const nlohmann::json& map_manifest, const std::string& map_id);
    ~SceneRenderer();
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
        float        depth_blur_px       = 0.0f; // perspective blur radius in pixels
        float        depth_brightness    = 0.0f; // depth cue brightness offset (percent, -50..50)
        float        depth_saturation    = 0.0f; // saturation delta (percent, -50..50)
        float        depth_primary_boost = 0.0f; // primary color boost (percent, -50..50)
    };

    bool ensure_darkness_overlay();
    void destroy_darkness_overlay();
    void render_dynamic_darkness_overlay(float map_light_opacity, float flicker_time_seconds);

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

    std::unordered_map<Asset*, const AnimationFrame*> last_rendered_frames_;
    std::uint64_t frame_counter_ = 0;
    std::vector<AssetRenderCommand> texture_commands_;
    std::vector<AssetRenderCommand> remaining_commands_;
    std::vector<LightOverlaySource> light_overlay_sources_;
    std::vector<SDL_Vertex> darkness_overlay_vertices_;
    std::vector<int>        darkness_overlay_indices_;
    std::size_t             darkness_overlay_vertex_capacity_hint_ = 0;
    std::size_t             darkness_overlay_index_capacity_hint_  = 0;
    SDL_Texture* darkness_overlay_texture_ = nullptr;
    int          darkness_overlay_width_   = 0;
    int          darkness_overlay_height_  = 0;
    SDL_Color    map_clear_color_{0, 0, 0, 255};

    // Depth-cue warmup: skip expensive per-asset effects for first N frames
    // after initialization to avoid stalls when transitioning from the loading screen.
    // Configurable via constructor constants in .cpp; defaults to a small number of frames.
    std::uint32_t depthcue_warmup_frames_ = 8; // frames to skip depth-cue effects after init

    // Tinted texture cache: keyed by (source SDL_Texture*, sat%, primary%).
    // Prevents repeated CPU-side pixel reads for the same adjustments.
    struct TintedTextureEntry {
        SDL_Texture* texture = nullptr;
        std::uint64_t last_used_frame = 0; // for simple LRU pruning
    };
    // Key is source texture pointer; inner key packs (sat, primary) into uint32.
    std::unordered_map<SDL_Texture*, std::unordered_map<std::uint32_t, TintedTextureEntry>> tinted_texture_cache_;
    // Prune tinted cache entries not used for a while to prevent unbounded growth.
    void prune_tinted_texture_cache(std::uint64_t current_frame);
    // Look up or build a tinted texture for given source and parameters.
    SDL_Texture* get_or_build_tinted_texture(SDL_Texture* source,
                                             float saturation_percent,
                                             float primary_percent);

    // Full-scene post-processing targets
    SDL_Texture* scene_composite_tex_ = nullptr;   // Draws full scene here first
    SDL_Texture* postprocess_tex_     = nullptr;   // Reused staging for color pass
    SDL_Texture* blur_tex_            = nullptr;   // Reused staging for blur pass
};

