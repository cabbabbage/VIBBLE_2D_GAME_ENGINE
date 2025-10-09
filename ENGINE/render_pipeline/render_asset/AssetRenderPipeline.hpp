#pragma once

#include <memory>
#include <vector>

#include <SDL.h>

#include "render_pipeline/render_asset/render_asset.hpp"

class Asset;
class camera;
class Global_Light_Source;
class IRenderStage;

struct SceneLighting {
    camera&               camera;
    Global_Light_Source&  main_light;
    Asset*                player = nullptr;
};

struct StageContext {
    SDL_Texture* base_texture    = nullptr;
    SDL_Texture* working_texture = nullptr;
};

class AssetRenderPipeline {
public:
    AssetRenderPipeline(SDL_Renderer* renderer, const SceneLighting& lighting);

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
    std::vector<std::unique_ptr<IRenderStage>> stages_;
};

