#include "render_pipeline/render_asset/shading/RenderShadowMask.hpp"

namespace render_pipeline::shading {

bool RenderShadowMask::supports(const Asset&) const {
    return false;
}

SDL_Texture* RenderShadowMask::run(SDL_Renderer*, const Asset&, StageContext&) {
    return nullptr;
}

} // namespace render_pipeline::shading

