#include "render_pipeline/render_asset/shading/RenderShadingStages.hpp"

#include "asset/Asset.hpp"
#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace render_pipeline::shading {

void ClearShadowStateFor(const Asset*) {}

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

    int width  = context.width;
    int height = context.height;
    if (width <= 0 || height <= 0) {
        SDL_QueryTexture(base_texture, nullptr, nullptr, &width, &height);
        context.width  = width;
        context.height = height;
    }

    if (width <= 0 || height <= 0) {
        return nullptr;
    }

    SDL_Texture* target = nullptr;
    if (context.reusable_final) {
        int tex_w = 0;
        int tex_h = 0;
        if (SDL_QueryTexture(context.reusable_final, nullptr, nullptr, &tex_w, &tex_h) == 0 && tex_w == width && tex_h == height) {
            target = context.reusable_final;
        }
    }

    if (!target) {
        target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
        if (!target) {
            return nullptr;
        }
    }

    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(target, (asset.info && !asset.info->smooth_scaling) ? SDL_ScaleModeNearest : SDL_ScaleModeBest);
#endif

    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, target);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    SDL_SetTextureAlphaMod(base_texture, 255);
    SDL_SetTextureColorMod(base_texture, 255, 255, 255);
    SDL_RenderCopy(renderer, base_texture, nullptr, nullptr);
    SDL_SetTextureAlphaMod(base_texture, 255);
    SDL_SetTextureColorMod(base_texture, 255, 255, 255);

    SDL_SetRenderTarget(renderer, prev_target);

    return target;
}

bool RenderCastShadow::supports(const Asset& asset) const {
    return asset.is_shaded;
}

SDL_Texture* RenderCastShadow::run(SDL_Renderer* renderer, const Asset& asset, StageContext& context) {
    if (!renderer || !asset.is_shaded) {
        return nullptr;
    }

    int width  = context.width;
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

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    if (!texture) {
        return nullptr;
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, prev_target);
    return texture;
}

bool RenderShadowMask::supports(const Asset& asset) const {
    return asset.is_shaded;
}

SDL_Texture* RenderShadowMask::run(SDL_Renderer* renderer, const Asset& asset, StageContext& context) {
    if (!renderer || !asset.is_shaded) {
        return nullptr;
    }

    int width  = context.width;
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

    auto& cache = asset.shadow_mask_cache();
    if (cache.texture) {
        const bool metadata_matches = cache.width == width && cache.height == height && cache.width > 0 && cache.height > 0;
        if (!metadata_matches) {
            int tex_w = 0;
            int tex_h = 0;
            if (SDL_QueryTexture(cache.texture, nullptr, nullptr, &tex_w, &tex_h) != 0 || tex_w != width || tex_h != height) {
                SDL_DestroyTexture(cache.texture);
                cache.texture = nullptr;
                cache.width   = 0;
                cache.height  = 0;
            } else {
                cache.width  = tex_w;
                cache.height = tex_h;
            }
        }
    }

    if (!cache.texture) {
        cache.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
        if (!cache.texture) {
            cache.width  = 0;
            cache.height = 0;
            return nullptr;
        }
        cache.width  = width;
        cache.height = height;
    }
    SDL_SetTextureBlendMode(cache.texture, SDL_BLENDMODE_BLEND);

    const float opacity = context.base_shadow_opacity;

    // TODO(#reactive-shadows): Mask currently renders unadjusted until new settings land.

    SDL_Texture* mask_texture = nullptr;
    const auto& scale_usage   = asset.last_scale_usage();
    std::size_t mask_variant  = (scale_usage.variant_index < 0) ? 0u : static_cast<std::size_t>(scale_usage.variant_index);
    if (SDL_Texture* mask = asset.get_current_mask_texture(mask_variant)) {
        mask_texture = mask;
    } else {
        mask_texture = context.base_texture;
    }

    if (!mask_texture) {
        cache.width  = width;
        cache.height = height;
        return cache.texture;
    }

    Uint8 saved_r = 255;
    Uint8 saved_g = 255;
    Uint8 saved_b = 255;
    Uint8 saved_a = 255;
    SDL_BlendMode saved_blend = SDL_BLENDMODE_BLEND;
    SDL_GetTextureColorMod(mask_texture, &saved_r, &saved_g, &saved_b);
    SDL_GetTextureAlphaMod(mask_texture, &saved_a);
    SDL_GetTextureBlendMode(mask_texture, &saved_blend);

    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, cache.texture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(mask_texture, SDL_BLENDMODE_BLEND);
    const Uint8 shade_alpha = static_cast<Uint8>(std::lround(opacity * 255.0f));
    SDL_SetTextureColorMod(mask_texture, 0, 0, 0);
    SDL_SetTextureAlphaMod(mask_texture, shade_alpha);

    int mask_w = width;
    int mask_h = height;
    if (mask_texture) {
        int queried_w = mask_w;
        int queried_h = mask_h;
        if (SDL_QueryTexture(mask_texture, nullptr, nullptr, &queried_w, &queried_h) == 0) {
            mask_w = std::max(1, queried_w);
            mask_h = std::max(1, queried_h);
        }
    }

    const int scaled_w = std::max(1, mask_w);
    const int scaled_h = std::max(1, mask_h);
    const float base_center_x = static_cast<float>(width) * 0.5f;
    const float base_center_y = static_cast<float>(height) * 0.5f;
    const float dest_x_f    = base_center_x - static_cast<float>(scaled_w) * 0.5f;
    const float dest_y_f    = base_center_y - static_cast<float>(scaled_h) * 0.5f;
    const int   dest_px_x   = static_cast<int>(std::lround(dest_x_f));
    const int   dest_px_y   = static_cast<int>(std::lround(dest_y_f));
    SDL_Rect    dest{dest_px_x, dest_px_y, scaled_w, scaled_h};
    SDL_RenderCopy(renderer, mask_texture, nullptr, &dest);

    SDL_SetTextureBlendMode(mask_texture, saved_blend);
    SDL_SetTextureColorMod(mask_texture, saved_r, saved_g, saved_b);
    SDL_SetTextureAlphaMod(mask_texture, saved_a);

#if SDL_VERSION_ATLEAST(2, 0, 6)
    if (SDL_Texture* base_mask = context.base_texture ? context.base_texture : asset.get_current_frame()) {
        Uint8 base_r = 255;
        Uint8 base_g = 255;
        Uint8 base_b = 255;
        Uint8 base_a = 255;
        SDL_BlendMode base_blend = SDL_BLENDMODE_BLEND;
        SDL_GetTextureColorMod(base_mask, &base_r, &base_g, &base_b);
        SDL_GetTextureAlphaMod(base_mask, &base_a);
        SDL_GetTextureBlendMode(base_mask, &base_blend);

        const SDL_BlendMode crop_blend = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
        if (crop_blend != SDL_BLENDMODE_INVALID) {
            SDL_SetTextureBlendMode(base_mask, crop_blend);
            SDL_SetTextureColorMod(base_mask, 255, 255, 255);
            SDL_SetTextureAlphaMod(base_mask, 255);
            SDL_RenderCopy(renderer, base_mask, nullptr, nullptr);
            SDL_SetTextureBlendMode(base_mask, base_blend);
            SDL_SetTextureColorMod(base_mask, base_r, base_g, base_b);
            SDL_SetTextureAlphaMod(base_mask, base_a);
        }
    }
#endif
    // TODO(#reactive-shadows): Runtime subtraction is disabled until reactive tuning returns.

    SDL_SetRenderTarget(renderer, prev_target);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    cache.width  = width;
    cache.height = height;

    return cache.texture;
}

}

