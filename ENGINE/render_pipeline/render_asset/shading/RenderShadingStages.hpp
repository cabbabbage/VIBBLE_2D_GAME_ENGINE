#pragma once

#include "render_pipeline/render_asset/IRenderStage.hpp"

class Asset;

namespace render_pipeline::shading {

void ClearShadowStateFor(const Asset* asset);

class RenderAsset : public IRenderStage {
public:
    bool         supports(const Asset& asset) const override;
    SDL_Texture* run(SDL_Renderer* renderer, const Asset& asset, StageContext& context) override;
};

class RenderShadowMask : public IRenderStage {
public:
    ~RenderShadowMask() override = default;
    bool         supports(const Asset& asset) const override;
    SDL_Texture* run(SDL_Renderer* renderer, const Asset& asset, StageContext& context) override;
};

}

