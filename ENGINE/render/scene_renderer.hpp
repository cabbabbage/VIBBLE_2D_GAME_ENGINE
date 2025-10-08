#pragma once

#include <string>
#include <memory>
#include <vector>
#include <SDL.h>
#include <nlohmann/json.hpp>
#include "light_map.hpp"
#include "global_light_source.hpp"
#include "render_asset.hpp"
#include "render/camera.hpp"
#include "render/light_rays.hpp"

class GaussianBlurHelper;
class AssetLightRaysRenderer;

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
    void set_low_quality_rendering(bool low_quality);

    private:
    void update_shading_groups();
    bool shouldRegen(Asset* a);
    SDL_Rect get_scaled_position_rect(Asset* a, int fw, int fh, float inv_scale, int min_w, int min_h, float reference_screen_height);
    void resize_render_targets_if_needed();
    void recreate_fullscreen_light_texture();
    void apply_final_blur_pass();
    void refresh_blur_helpers();

    std::string    map_path_;
    SDL_Renderer*  renderer_;
    Assets*        assets_;
    int            screen_width_;
    int            screen_height_;
    Global_Light_Source main_light_source_;
    SDL_Texture*   fullscreen_light_tex_;
    RenderAsset    render_asset_;
    std::unique_ptr<AssetLightRaysRenderer> asset_light_rays_;
    std::unique_ptr<LightMap> z_light_pass_;
    std::unique_ptr<GaussianBlurHelper> final_blur_helper_;
    int            current_shading_group_ = 0;
    int            num_groups_ = 20;
    bool           debugging = false;
    bool           low_quality_mode_ = false;

    LightRaysConfig light_rays_config_{};

    float          final_blur_radius_ = 0.f;
    float          final_blur_mix_ = 0.f;
    bool           final_blur_requested_ = false;
    bool           final_blur_enabled_ = false;

    SDL_Texture*   scene_target_tex_    = nullptr;
};
