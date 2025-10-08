#pragma once
#include <SDL.h>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <nlohmann/json.hpp>

#include "global_light_source.hpp"
#include "light_map.hpp"
#include "light_rays.hpp"

class Assets;
class Asset;
class RenderAsset;
class SceneRenderer {
public:
    SceneRenderer(SDL_Renderer* renderer,
                  Assets* assets,
                  int screen_width,
                  int screen_height,
                  const std::string& map_path);
    ~SceneRenderer();

    SDL_Renderer* get_renderer() const;

    void set_low_quality_rendering(bool low_quality);
    void apply_map_light_config(const nlohmann::json& data);

    // Configure final blur and full-screen light rays from JSON
    void apply_light_rays_config(const nlohmann::json& data);

    void render();

private:
    void recreate_fullscreen_light_texture();
    void resize_render_targets_if_needed();
    void update_shading_groups();
    bool shouldRegen(Asset* a);

    SDL_Rect get_scaled_position_rect(Asset* a,
                                      int fw,
                                      int fh,
                                      float inv_scale,
                                      int min_w,
                                      int min_h,
                                      float reference_screen_height);

private:
    // Core
    std::string map_path_;
    SDL_Renderer* renderer_ = nullptr;
    Assets* assets_ = nullptr;

    // Screen
    int screen_width_ = 0;
    int screen_height_ = 0;

    // Scene targets
    SDL_Texture* fullscreen_light_tex_ = nullptr; // cleared with main_light color
    SDL_Texture* scene_target_tex_ = nullptr;

    // Lighting
    Global_Light_Source main_light_source_;
    std::unique_ptr<LightMap> z_light_pass_;

    // Asset renderer
    std::unique_ptr<RenderAsset> render_asset_;

    // Full-screen light rays
    std::unique_ptr<LightRaysPass> light_rays_pass_;
    LightRaysParams light_rays_params_{};
    int configured_downsample_log2_ = 2;

    bool  light_rays_enabled_ = true;

    // Misc
    bool low_quality_mode_ = false;
    int  current_shading_group_ = 0;
    int  num_groups_ = 3;
    bool debugging = false;
};
