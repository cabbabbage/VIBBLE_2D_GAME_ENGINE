#pragma once

#include "render_pipeline/render_asset/IRenderStage.hpp"

namespace render_pipeline::lighting {

class RenderLightFront : public IRenderStage {
public:
    ~RenderLightFront() override;
    bool         supports(const Asset& asset) const override;
    SDL_Texture* run(SDL_Renderer* renderer, const Asset& asset, StageContext& context) override;
private:
    SDL_Texture* cached_texture_ = nullptr;
    int          cached_width_   = 0;
    int          cached_height_  = 0;
};

}

