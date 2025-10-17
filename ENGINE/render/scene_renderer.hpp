#pragma once

#include <string>
#include <memory>
#include <vector>
#include <unordered_set>
#include <SDL.h>
#include <nlohmann/json.hpp>
#include "light_map.hpp"
#include "global_light_source.hpp"
#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"
#include "render/camera.hpp"

class Assets;
class Asset;

class SceneRenderer {

public:
    SceneRenderer(SDL_Renderer* renderer,
                 Assets* assets,
                 int screen_width,
                 int screen_height,
                 const nlohmann::json& map_manifest,
                 const std::string& map_id);
    ~SceneRenderer() = default;
    void render();
    void apply_map_light_config(const nlohmann::json& data);
    SDL_Renderer* get_renderer() const;
    void set_low_quality_rendering(bool enabled);
    bool low_quality_rendering() const { return low_quality_rendering_; }
    void toggle_light_map_only_mode() { light_map_only_mode_ = !light_map_only_mode_; }
    bool light_map_only_mode() const { return light_map_only_mode_; }
    Global_Light_Source& map_light_source() { return main_light_source_; }
    const Global_Light_Source& map_light_source() const { return main_light_source_; }
    render_pipeline::shading::ReactiveShadowSettings& reactive_shadow_settings() { return reactive_shadow_settings_; }
    const render_pipeline::shading::ReactiveShadowSettings& reactive_shadow_settings() const { return reactive_shadow_settings_; }
    const LightMap* light_map() const;
    void set_virtual_light_map_quadrants(int quadrants);
    void set_virtual_light_map_quadrant_size(int size_px);
    void force_virtual_light_map_refresh();

private:
    bool shouldRegen(Asset* a);
    SDL_Rect get_scaled_position_rect(Asset* a, int fw, int fh, float inv_scale, int min_w, int min_h, float reference_screen_height);

private:
    SDL_Renderer*  renderer_;
    Assets*        assets_;
    int            screen_width_;
    int            screen_height_;
    Global_Light_Source main_light_source_;
    render_pipeline::shading::ReactiveShadowSettings reactive_shadow_settings_{};
    AssetRenderPipeline render_pipeline_;
    std::unique_ptr<LightMap> z_light_pass_;
    bool           debugging = false;
    bool           low_quality_rendering_ = false;
    bool           light_map_only_mode_ = false;
    // Temporary switch to disable dynamic light-ray stamping
    bool           enable_dynamic_light_rays_ = false;

    std::unordered_set<Asset*> last_active_assets_;
};
