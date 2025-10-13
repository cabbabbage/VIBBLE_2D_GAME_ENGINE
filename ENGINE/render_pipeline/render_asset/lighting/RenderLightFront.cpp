#include "render_pipeline/render_asset/lighting/RenderLightFront.hpp"

#include "asset/Asset.hpp"
#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"
#include "render_pipeline/render_asset/lighting/LightingStageCommon.hpp"

namespace render_pipeline::lighting {

RenderLightFront::~RenderLightFront() {
    if (cached_texture_) {
        SDL_DestroyTexture(cached_texture_);
        cached_texture_ = nullptr;
    }
}

bool RenderLightFront::supports(const Asset& asset) const {
    return asset.is_shaded;
}

SDL_Texture* RenderLightFront::run(SDL_Renderer* renderer, const Asset& asset, StageContext& context) {
    if (!renderer) {
        return nullptr;
    }

    int width = context.width;
    int height = context.height;
    if (width <= 0 || height <= 0) {
        if (SDL_Texture* base = context.base_texture) {
            SDL_QueryTexture(base, nullptr, nullptr, &width, &height);
            context.width  = width;
            context.height = height;
        }
    }

    if (width <= 0 || height <= 0) {
        return nullptr;
    }

    if (width != cached_width_ || height != cached_height_) {
        if (cached_texture_) {
            SDL_DestroyTexture(cached_texture_);
            cached_texture_ = nullptr;
        }
        cached_width_  = width;
        cached_height_ = height;
    }

    SDL_Texture* existing_texture = cached_texture_;
    SDL_Texture* texture          = detail::build_light_texture(renderer, asset, context, false, existing_texture);
    if (!texture) {
        if (!existing_texture) {
            cached_width_  = 0;
            cached_height_ = 0;
        }
        return nullptr;
    }

    cached_texture_ = texture;
    return cached_texture_;
}

}

