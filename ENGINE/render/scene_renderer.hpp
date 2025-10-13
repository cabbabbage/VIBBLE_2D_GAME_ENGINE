#pragma once

#include <string>
#include <memory>
#include <vector>
#include <SDL.h>
#include <nlohmann/json.hpp>
#include "light_map.hpp"
#include "global_light_source.hpp"
#include "render/light_rays.hpp"
#include "render/light_rays_config.hpp"
#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"
#include "render/camera.hpp"

class Assets;
class Asset;

class SceneRenderer {

	public:
    SceneRenderer(SDL_Renderer* renderer, Assets* assets, int screen_width, int screen_height, const std::string& map_path);
    ~SceneRenderer();
    void render();
    void apply_map_light_config(const nlohmann::json& data);
    void apply_light_rays_config(const nlohmann::json& data);
    SDL_Renderer* get_renderer() const;
    void set_low_quality_rendering(bool enabled);
    bool low_quality_rendering() const { return low_quality_rendering_; }
    Global_Light_Source& map_light_source() { return main_light_source_; }
    const Global_Light_Source& map_light_source() const { return main_light_source_; }
    SDL_Color screen_light_color() const { return screen_light_color_; }
    int screen_light_min_opacity() const { return screen_light_min_opacity_; }
    int screen_light_max_opacity() const { return screen_light_max_opacity_; }

private:
    void update_shading_groups();
    bool shouldRegen(Asset* a);
    SDL_Rect get_scaled_position_rect(Asset* a, int fw, int fh, float inv_scale, int min_w, int min_h, float reference_screen_height);
    void apply_screen_light_settings(const nlohmann::json& data);
    void update_fullscreen_light_texture();

    std::string    map_path_;
    SDL_Renderer*  renderer_;
    Assets*        assets_;
    int            screen_width_;
    int            screen_height_;
    Global_Light_Source main_light_source_;
    SDL_Texture*   fullscreen_light_tex_;
    SDL_Color      screen_light_color_{255, 255, 255, 255};
    int            screen_light_min_opacity_ = 0;
    int            screen_light_max_opacity_ = 255;
    AssetRenderPipeline render_pipeline_;
    std::unique_ptr<LightMap> z_light_pass_;
    std::unique_ptr<LightRaysPass> light_rays_pass_;
    LightRaysConfig light_rays_config_{};
    LightRaysParams light_rays_params_{};
    bool light_rays_enabled_ = false;
    bool fullscreen_light_rays_disabled_ = true;
    int            current_shading_group_ = 0;
    int            num_groups_ = 40;
    bool           debugging = false;
    bool           low_quality_rendering_ = false;

    SDL_Texture*   scene_target_tex_    = nullptr;
    SDL_Texture*   post_small_tex_a_    = nullptr;
    SDL_Texture*   post_small_tex_b_    = nullptr;

};
