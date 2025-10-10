#pragma once

#include "render_pipeline/render_asset/IRenderStage.hpp"

namespace render_pipeline::lighting {

class RenderLightFront : public IRenderStage {
public:
    bool         supports(const Asset& asset) const override;
    SDL_Texture* run(SDL_Renderer* renderer, const Asset& asset, StageContext& context) override;
};

} // namespace render_pipeline::lighting

