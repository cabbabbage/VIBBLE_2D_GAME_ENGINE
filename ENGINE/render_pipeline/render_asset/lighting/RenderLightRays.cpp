#include "render_pipeline/render_asset/lighting/RenderLightRays.hpp"

#include "asset/Asset.hpp"

namespace render_pipeline::lighting {

bool RenderLightRays::supports(const Asset& asset) const {
    return asset.generate_rays;
}

SDL_Texture* RenderLightRays::run(SDL_Renderer*, const Asset&, StageContext&) {
    return nullptr;
}

} // namespace render_pipeline::lighting

