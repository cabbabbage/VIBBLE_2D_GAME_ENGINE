#pragma once

#include <memory>
#include <vector>

#include <SDL.h>

#include "render_pipeline/render_asset/IRenderStage.hpp"
#include "render_pipeline/render_asset/render_asset.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"

class Asset;
class camera;
class Global_Light_Source;
class LightMap;
class LightMapManager;

namespace render_pipeline::shading {
struct ReactiveShadowSettings;
}
struct SceneLighting {
    camera&                camera_view;
    Global_Light_Source&   main_light;
    Asset*                 player = nullptr;
    const LightMap*        light_map_sampler = nullptr;
    render_pipeline::shading::ReactiveShadowSettings* reactive_shadow_settings = nullptr;
    LightMapManager*       light_map_manager = nullptr;
};

struct StageContext {
    SDL_Texture* base_texture = nullptr;
    SceneLighting* lighting   = nullptr;
    int           width       = 0;
    int           height      = 0;
    SDL_Texture*  reusable_final = nullptr;
    SDL_Texture*  final_texture = nullptr;
    SDL_Rect      screen_rect{ 0, 0, 0, 0 };
    SDL_FPoint    screen_center{ 0.0f, 0.0f };
    float         reference_screen_height = 1.0f;
    float         base_shadow_scale       = 1.0f;
    float         base_shadow_opacity     = 204.0f / 255.0f;
    int           screen_width_px         = 0;
    int           screen_height_px        = 0;

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
    LightMapManager*   light_map_manager() const { return lighting ? lighting->light_map_manager : nullptr; }
    const render_pipeline::shading::ReactiveShadowSettings* reactive_shadow_settings() const;

    void update_projection(Asset& asset);

    const render_pipeline::shading::ReactiveShadowSettings* reactive_shadow_settings_override = nullptr;
};

class AssetRenderPipeline {
public:
    AssetRenderPipeline(SDL_Renderer* renderer, const SceneLighting& lighting);

    SDL_Texture* run(Asset& asset);
    SDL_Texture* regenerateFinalTexture(Asset* asset);
    SDL_Texture* texture_for_scale(Asset* asset, SDL_Texture* base_tex, int base_w, int base_h, int target_w, int target_h);

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
    };
    std::vector<StageEntry> stages_;
};

