#include "render_pipeline/render_asset/lighting/RenderLightBehind.hpp"

namespace render_pipeline::lighting {

bool RenderLightBehind::supports(const Asset&) const {
    return false;
}

SDL_Texture* RenderLightBehind::run(SDL_Renderer*, const Asset&, StageContext&) {
    return nullptr;
}

} // namespace render_pipeline::lighting

