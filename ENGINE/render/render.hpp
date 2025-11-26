#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "render/composite_asset_renderer.hpp"
#include "render/scaling_logic.hpp"
#include <SDL.h>

#include <nlohmann/json.hpp>

class Assets;
class camera_grid;
namespace world { class Grid; }

// Renders prebuilt per-grid tiles with parallax applied. Intended to be
// invoked at the start of SceneRenderer::render().
class GridTileRenderer {
public:
    explicit GridTileRenderer(Assets* assets) : assets_(assets) {}

    // Render using Assets-owned camera and grid.
    void render(SDL_Renderer* renderer);

    // Render using explicit camera and grid references.
    void render(SDL_Renderer* renderer, const camera_grid& cam, const world::Grid& grid);

private:
    Assets* assets_ = nullptr;
};
////////////////////////////////////////////////////////////////////////////////

class SceneRenderer {
public:
    SceneRenderer(SDL_Renderer* renderer,
                  Assets* assets,
                  int screen_width,
                  int screen_height,
                  const nlohmann::json& map_manifest,
                  const std::string& map_id);
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
    SDL_Renderer* get_renderer() const;

    void set_dark_mask_enabled(bool enabled);
    void set_map_clear_color(SDL_Color color) { map_clear_color_ = color; }
    SDL_Color map_clear_color() const { return map_clear_color_; }

    bool dark_mask_enabled() const { return dark_mask_enabled_; }

private:
    struct PrevalidatedTag {};

    SceneRenderer(PrevalidatedTag,
                  SDL_Renderer* renderer,
                  Assets* assets,
                  int screen_width,
                  int screen_height,
                  const nlohmann::json& map_manifest,
                  const std::string& map_id);
    static PrevalidatedTag require_prerequisites(SDL_Renderer* renderer, Assets* assets);

    bool ensure_darkness_overlay();
    void destroy_darkness_overlay();
    void render_dynamic_darkness_overlay(float map_light_opacity, float flicker_time_seconds);

    bool ensure_sky_texture();
    void destroy_sky_texture();
    void render_sky_layer(const camera_grid& cam, bool depth_effects_enabled);
    bool ensure_fog_texture();
    void destroy_fog_texture();
    void render_fog_layer(const camera_grid& cam, const world::Grid& grid, bool depth_effects_enabled);

    SDL_Renderer*  renderer_;
    Assets*        assets_;
    int            screen_width_;
    int            screen_height_;
    
    std::unique_ptr<GridTileRenderer> tile_renderer_;
    
    bool           debugging = false;
    bool           low_quality_rendering_ = false;
    bool           dark_mask_enabled_ = true;

    std::uint64_t frame_counter_ = 0;




    SDL_Texture* darkness_overlay_texture_ = nullptr;
    int          darkness_overlay_width_   = 0;
    int          darkness_overlay_height_  = 0;
    SDL_Color    map_clear_color_{0, 128, 0, 255};  // Green for debugging

    CompositeAssetRenderer composite_renderer_;

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
    std::filesystem::path fog_texture_path_;
    SDL_Texture*          fog_texture_        = nullptr;
    int                   fog_texture_width_  = 0;
    int                   fog_texture_height_ = 0;
    bool                  fog_texture_failed_ = false;
};
////////////////////////////////////////////////////////////////////////////////`
