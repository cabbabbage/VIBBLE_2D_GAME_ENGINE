#pragma once

#include <memory>
#include <vector>

#include <SDL.h>

#include "render_pipeline/render_asset/IRenderStage.hpp"
#include "render_pipeline/render_asset/render_asset.hpp"

class Asset;
class camera;
class Global_Light_Source;
struct SceneLighting {
    camera&               camera_view;
    Global_Light_Source&  main_light;
    Asset*                player = nullptr;
};

struct StageContext {
    SDL_Texture* base_texture = nullptr;
    SceneLighting* lighting   = nullptr;
    int           width       = 0;
    int           height      = 0;

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
};

class AssetRenderPipeline {
public:
    AssetRenderPipeline(SDL_Renderer* renderer, const SceneLighting& lighting);

    SDL_Texture* run(Asset& asset);
    SDL_Texture* regenerateFinalTexture(Asset* asset);
    SDL_Texture* texture_for_scale(Asset* asset,
                                   SDL_Texture* base_tex,
                                   int base_w,
                                   int base_h,
                                   int target_w,
                                   int target_h);

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
    };
    std::vector<StageEntry> stages_;
};

