#include "render_pipeline/render_asset/lighting/RenderLightRays.hpp"

namespace render_pipeline::lighting {

bool RenderLightRays::supports(const Asset&) const {
    return false;
}

SDL_Texture* RenderLightRays::run(SDL_Renderer*, const Asset&, StageContext&) {
    return nullptr;
}

} // namespace render_pipeline::lighting

