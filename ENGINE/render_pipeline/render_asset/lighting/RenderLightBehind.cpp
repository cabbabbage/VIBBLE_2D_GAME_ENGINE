#include "render_pipeline/render_asset/lighting/RenderLightBehind.hpp"

#include "asset/Asset.hpp"
#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"
#include "render_pipeline/render_asset/lighting/LightingStageCommon.hpp"

namespace render_pipeline::lighting {

bool RenderLightBehind::supports(const Asset& asset) const {
    return asset.is_shaded;
}

SDL_Texture* RenderLightBehind::run(SDL_Renderer* renderer, const Asset& asset, StageContext& context) {
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

    auto& cache = asset.light_behind_cache();
    if (cache.texture) {
        int tex_w = 0;
        int tex_h = 0;
        if (SDL_QueryTexture(cache.texture, nullptr, nullptr, &tex_w, &tex_h) != 0 || tex_w != width || tex_h != height) {
            SDL_DestroyTexture(cache.texture);
            cache.texture = nullptr;
            cache.width   = 0;
            cache.height  = 0;
        }
    }

    SDL_Texture* existing_texture = cache.texture;
    SDL_Texture* texture          = detail::build_light_texture(renderer, asset, context, true, existing_texture);
    if (!texture) {
        if (!existing_texture) {
            cache.width  = 0;
            cache.height = 0;
        }
        return nullptr;
    }

    cache.texture = texture;
    cache.width   = width;
    cache.height  = height;
    return cache.texture;
}

}

