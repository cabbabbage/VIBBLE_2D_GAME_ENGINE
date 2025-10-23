#pragma once

#include <string>
#include <memory>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <SDL.h>
#include <nlohmann/json.hpp>
#include "world/chunk.hpp"
#include "global_light_source.hpp"
#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"
#include "render/camera.hpp"

class Assets;
class Asset;
class AnimationFrame;

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
    void toggle_chunk_preview() { chunk_preview_enabled_ = !chunk_preview_enabled_; }
    bool chunk_preview_enabled() const { return chunk_preview_enabled_; }
    Global_Light_Source& map_light_source() { return main_light_source_; }
    const Global_Light_Source& map_light_source() const { return main_light_source_; }
    render_pipeline::shading::ReactiveShadowSettings& reactive_shadow_settings() { return reactive_shadow_settings_; }
    const render_pipeline::shading::ReactiveShadowSettings& reactive_shadow_settings() const { return reactive_shadow_settings_; }
    LightMap* light_map();
    const LightMap* light_map() const;

private:
    bool shouldRegen(Asset* a);
    SDL_Rect get_scaled_position_rect(Asset* a, int fw, int fh, float inv_scale, int min_w, int min_h, float reference_screen_height);
    bool initialize_static_light_chunks();

private:
    struct AssetRenderCommand {
        SDL_Texture* source_texture      = nullptr;
        SDL_Texture* final_texture       = nullptr;
        SDL_Rect     dst                 { 0, 0, 0, 0 };
        bool         uses_scaled_texture = false;
        bool         highlighted         = false;
        bool         selected            = false;
        bool         flipped             = false;
    };

    SDL_Renderer*  renderer_;
    Assets*        assets_;
    int            screen_width_;
    int            screen_height_;
    Global_Light_Source main_light_source_;
    render_pipeline::shading::ReactiveShadowSettings reactive_shadow_settings_{};
    AssetRenderPipeline render_pipeline_;
    std::unique_ptr<LightMap> light_map_;
    bool           debugging = false;
    bool           low_quality_rendering_ = false;
    bool           light_map_only_mode_ = false;
    bool           chunk_preview_enabled_ = false;
    bool           chunk_lighting_suspended_ = false;

    std::unordered_set<Asset*> last_active_assets_;
    std::unordered_map<Asset*, const AnimationFrame*> last_rendered_frames_;
    std::unordered_set<Asset*> current_active_assets_;
    std::vector<AssetRenderCommand> texture_commands_;
    std::vector<AssetRenderCommand> remaining_commands_;
};


