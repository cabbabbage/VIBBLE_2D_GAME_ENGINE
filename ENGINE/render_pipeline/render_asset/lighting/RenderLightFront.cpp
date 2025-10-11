#include "render_pipeline/render_asset/lighting/RenderLightFront.hpp"

#include "asset/Asset.hpp"
#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"
#include "render_pipeline/render_asset/lighting/LightingStageCommon.hpp"

namespace render_pipeline::lighting {

bool RenderLightFront::supports(const Asset& asset) const {
    return asset.is_shaded;
}

SDL_Texture* RenderLightFront::run(SDL_Renderer* renderer, const Asset& asset, StageContext& context) {
    return detail::build_light_texture(renderer, asset, context, false);
}

}

