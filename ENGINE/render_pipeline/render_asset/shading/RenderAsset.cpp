#include "render_pipeline/render_asset/shading/RenderAsset.hpp"

#include <algorithm>
#include <cmath>

#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"

namespace render_pipeline::shading {

bool RenderAsset::supports(const Asset& asset) const {
    return asset.get_current_frame() != nullptr;
}

SDL_Texture* RenderAsset::run(SDL_Renderer* renderer, const Asset& asset, StageContext& context) {
    if (!renderer) {
        return nullptr;
    }

    SDL_Texture* base_texture = context.base_texture ? context.base_texture : asset.get_current_frame();
    if (!base_texture) {
        return nullptr;
    }

    int width = context.width;
    int height = context.height;
    if (width <= 0 || height <= 0) {
        SDL_QueryTexture(base_texture, nullptr, nullptr, &width, &height);
        context.width  = width;
        context.height = height;
    }

    if (width <= 0 || height <= 0) {
        return nullptr;
    }

    SDL_Texture* target =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    if (!target) {
        return nullptr;
    }
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(
        target,
        (asset.info && !asset.info->smooth_scaling) ? SDL_ScaleModeNearest : SDL_ScaleModeBest);
#endif

    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, target);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    const float alpha_percentage = static_cast<float>(asset.alpha_percentage);
    const Uint8 main_alpha       = context.main_light_alpha();
    int         alpha_mod        = (alpha_percentage >= 1.0f)
                                  ? 255
                                  : static_cast<int>(std::round(main_alpha * alpha_percentage));
    if (asset.info && asset.info->type == asset_types::player) {
        alpha_mod = std::min(255, alpha_mod * 3);
    }
    alpha_mod = std::clamp(alpha_mod, 0, 255);

    SDL_SetTextureAlphaMod(base_texture, static_cast<Uint8>(alpha_mod));
    SDL_SetTextureColorMod(base_texture, 255, 255, 255);
    SDL_RenderCopy(renderer, base_texture, nullptr, nullptr);
    SDL_SetTextureAlphaMod(base_texture, 255);
    SDL_SetTextureColorMod(base_texture, 255, 255, 255);

    SDL_SetRenderTarget(renderer, prev_target);

    return target;
}

} // namespace render_pipeline::shading

