#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "render/composite_asset_renderer.hpp"
#include "render/scaling_logic.hpp"
#include <SDL.h>

#include <nlohmann/json.hpp>

class Assets;
class WarpedScreenGrid;
namespace world { class WorldGrid; }
namespace world { class WorldGrid; }

class GridTileRenderer {
public:
    explicit GridTileRenderer(Assets* assets) : assets_(assets) {}

    void render(SDL_Renderer* renderer);

    void render(SDL_Renderer* renderer, const WarpedScreenGrid& cam, const world::WorldGrid& grid);

private:
    Assets* assets_ = nullptr;
};

class SceneRenderer {
public:
    SceneRenderer(SDL_Renderer* renderer, Assets* assets, int screen_width, int screen_height, const nlohmann::json& map_manifest, const std::string& map_id);
    ~SceneRenderer();

    static constexpr float kDefaultMapLightOpacity = 0.75f;

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
    struct DarkMaskSprite {
        SDL_Texture*    texture     = nullptr;
        SDL_Rect        screen_rect{};
        SDL_Color       color_mod{255, 255, 255, 255};
        SDL_RendererFlip flip       = SDL_FLIP_NONE;
};
    struct PrevalidatedTag {};

    SceneRenderer(PrevalidatedTag, SDL_Renderer* renderer, Assets* assets, int screen_width, int screen_height, const nlohmann::json& map_manifest, const std::string& map_id);
    static PrevalidatedTag require_prerequisites(SDL_Renderer* renderer, Assets* assets);

    bool ensure_darkness_overlay();
    void destroy_darkness_overlay();
    void render_dynamic_darkness_overlay(float map_light_opacity, const std::vector<DarkMaskSprite>& sprites);

    bool ensure_sky_texture();
    void destroy_sky_texture();
    void render_sky_layer(const WarpedScreenGrid& cam, bool depth_effects_enabled);
    bool ensure_fog_texture();
    void destroy_fog_texture();
    void render_fog_layer(const WarpedScreenGrid& cam, const world::WorldGrid& grid, bool depth_effects_enabled);

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
    float        map_light_opacity_        = kDefaultMapLightOpacity;
    SDL_Color    map_clear_color_{0, 128, 0, 255};
    bool         debug_auto_paths_ = true;

    CompositeAssetRenderer composite_renderer_;

    std::uint32_t depthcue_warmup_frames_ = 8;

    SDL_Texture* scene_composite_tex_ = nullptr;
    SDL_Texture* postprocess_tex_     = nullptr;
    SDL_Texture* blur_tex_            = nullptr;

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

