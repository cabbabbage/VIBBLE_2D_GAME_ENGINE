#include "render_pipeline/render_asset/shading/RenderAsset.hpp"

namespace render_pipeline::shading {

bool RenderAsset::supports(const Asset&) const {
    return false;
}

SDL_Texture* RenderAsset::run(SDL_Renderer*, const Asset&, StageContext&) {
    return nullptr;
}

} // namespace render_pipeline::shading

