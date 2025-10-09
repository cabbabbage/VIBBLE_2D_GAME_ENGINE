#include "render_pipeline/render_asset/lighting/RenderLightFront.hpp"

namespace render_pipeline::lighting {

bool RenderLightFront::supports(const Asset&) const {
    return false;
}

SDL_Texture* RenderLightFront::run(SDL_Renderer*, const Asset&, StageContext&) {
    return nullptr;
}

} // namespace render_pipeline::lighting

