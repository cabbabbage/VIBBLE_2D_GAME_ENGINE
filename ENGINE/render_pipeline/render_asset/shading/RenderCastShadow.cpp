#include "render_pipeline/render_asset/shading/RenderCastShadow.hpp"

namespace render_pipeline::shading {

bool RenderCastShadow::supports(const Asset&) const {
    return false;
}

SDL_Texture* RenderCastShadow::run(SDL_Renderer*, const Asset&, StageContext&) {
    return nullptr;
}

} // namespace render_pipeline::shading

