#include "render_pipeline/render_asset/shading/RenderShadingStages.hpp"

#include "asset/Asset.hpp"
#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace render_pipeline::shading {

namespace {

void ensure_texture_defaults(SDL_Texture* texture, Asset::MaskRenderMetadata::TextureDefaults& defaults) {
    if (!texture) {
        defaults.reset();
        return;
    }
    if (defaults.texture != texture) {
        defaults.reset();
        defaults.texture = texture;
    }
    if (!defaults.initialized) {
        Uint8 r = defaults.r;
        Uint8 g = defaults.g;
        Uint8 b = defaults.b;
        Uint8 a = defaults.a;
        SDL_BlendMode blend = defaults.blend;
        if (SDL_GetTextureColorMod(texture, &r, &g, &b) != 0) {
            r = g = b = 255;
        }
        if (SDL_GetTextureAlphaMod(texture, &a) != 0) {
            a = 255;
        }
        if (SDL_GetTextureBlendMode(texture, &blend) != 0) {
            blend = SDL_BLENDMODE_BLEND;
        }
        defaults.r = r;
        defaults.g = g;
        defaults.b = b;
        defaults.a = a;
        defaults.blend = blend;
        defaults.initialized = true;
    }
}

void ensure_mask_dimensions(Asset::MaskRenderMetadata& metadata,
                            SDL_Texture*               texture,
                            int                        fallback_w,
                            int                        fallback_h) {
    if (!texture) {
        metadata.mask_w         = std::max(1, fallback_w);
        metadata.mask_h         = std::max(1, fallback_h);
        metadata.last_mask_texture = nullptr;
        metadata.has_dimensions = true;
        return;
    }

    if (metadata.last_mask_texture != texture) {
        metadata.last_mask_texture = texture;
        metadata.has_dimensions    = false;
    }

    if (!metadata.has_dimensions) {
        int queried_w = fallback_w;
        int queried_h = fallback_h;
        if (SDL_QueryTexture(texture, nullptr, nullptr, &queried_w, &queried_h) != 0) {
            queried_w = fallback_w;
            queried_h = fallback_h;
        }
        metadata.mask_w         = std::max(1, queried_w);
        metadata.mask_h         = std::max(1, queried_h);
        metadata.has_dimensions = true;
    }
}

#if SDL_VERSION_ATLEAST(2, 0, 6)
SDL_BlendMode cached_crop_blend_mode() {
    static SDL_BlendMode blend     = SDL_BLENDMODE_INVALID;
    static bool          computed  = false;
    if (!computed) {
        blend    = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO,
                                              SDL_BLENDFACTOR_SRC_ALPHA,
                                              SDL_BLENDOPERATION_ADD,
                                              SDL_BLENDFACTOR_ZERO,
                                              SDL_BLENDFACTOR_SRC_ALPHA,
                                              SDL_BLENDOPERATION_ADD);
        computed = true;
    }
    return blend;
}
#endif

} // namespace

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

    auto& cache = asset.cast_shadow_cache();
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

    SDL_Texture* texture = cache.texture;

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, prev_target);
    cache.width  = width;
    cache.height = height;
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
    const auto ensure_cache_target = [&]() -> SDL_Texture* {
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
        }
        SDL_SetTextureBlendMode(cache.texture, SDL_BLENDMODE_BLEND);
        cache.width  = width;
        cache.height = height;
        return cache.texture;
    };

    SDL_Texture* destination = nullptr;
    if (context.stage_destination) {
        int dst_w = 0;
        int dst_h = 0;
        if (SDL_QueryTexture(context.stage_destination, nullptr, nullptr, &dst_w, &dst_h) == 0 && dst_w == width && dst_h == height) {
            destination = context.stage_destination;
        }
    }

    const float opacity = context.base_shadow_opacity;

    // TODO(#reactive-shadows): Mask currently renders unadjusted until new settings land.

    SDL_Texture* mask_texture = nullptr;
    const auto&  scale_usage  = asset.last_scale_usage();
    std::size_t  mask_variant = (scale_usage.variant_index < 0) ? 0u : static_cast<std::size_t>(scale_usage.variant_index);
    if (SDL_Texture* mask = asset.get_current_mask_texture(mask_variant)) {
        mask_texture = mask;
    } else {
        mask_texture = context.base_texture;
    }

    if (!mask_texture) {
        if (destination) {
            return destination;
        }
        return ensure_cache_target();
    }

    Asset::MaskRenderMetadata& metadata = asset.mask_render_metadata();
    ensure_mask_dimensions(metadata, mask_texture, width, height);
    ensure_texture_defaults(mask_texture, metadata.mask_defaults);

    const auto render_mask = [&](SDL_Texture* target, SDL_BlendMode blend_mode, bool clear_target) {
        SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
        SDL_BlendMode prev_blend = SDL_BLENDMODE_INVALID;
        if (SDL_GetRenderDrawBlendMode(renderer, &prev_blend) != 0) {
            prev_blend = SDL_BLENDMODE_INVALID;
        }

        SDL_SetRenderTarget(renderer, target);
        SDL_SetRenderDrawBlendMode(renderer, blend_mode);
        if (clear_target) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            SDL_RenderClear(renderer);
        }

        SDL_SetTextureBlendMode(mask_texture, SDL_BLENDMODE_BLEND);
        const Uint8 shade_alpha = static_cast<Uint8>(std::lround(opacity * 255.0f));
        SDL_SetTextureColorMod(mask_texture, 0, 0, 0);
        SDL_SetTextureAlphaMod(mask_texture, shade_alpha);

        const int   scaled_w      = std::max(1, metadata.mask_w);
        const int   scaled_h      = std::max(1, metadata.mask_h);
        const float base_center_x = static_cast<float>(width) * 0.5f;
        const float base_center_y = static_cast<float>(height) * 0.5f;
        const float dest_x_f      = base_center_x - static_cast<float>(scaled_w) * 0.5f;
        const float dest_y_f      = base_center_y - static_cast<float>(scaled_h) * 0.5f;
        const int   dest_px_x     = static_cast<int>(std::lround(dest_x_f));
        const int   dest_px_y     = static_cast<int>(std::lround(dest_y_f));
        SDL_Rect    dest{dest_px_x, dest_px_y, scaled_w, scaled_h};
        SDL_RenderCopy(renderer, mask_texture, nullptr, &dest);

        SDL_SetTextureBlendMode(mask_texture, metadata.mask_defaults.blend);
        SDL_SetTextureColorMod(mask_texture, metadata.mask_defaults.r, metadata.mask_defaults.g, metadata.mask_defaults.b);
        SDL_SetTextureAlphaMod(mask_texture, metadata.mask_defaults.a);

#if SDL_VERSION_ATLEAST(2, 0, 6)
        if (SDL_Texture* base_mask = context.base_texture ? context.base_texture : asset.get_current_frame()) {
            ensure_texture_defaults(base_mask, metadata.base_defaults);
            const SDL_BlendMode crop_blend = cached_crop_blend_mode();
            if (crop_blend != SDL_BLENDMODE_INVALID) {
                SDL_SetTextureBlendMode(base_mask, crop_blend);
                SDL_SetTextureColorMod(base_mask, 255, 255, 255);
                SDL_SetTextureAlphaMod(base_mask, 255);
                SDL_RenderCopy(renderer, base_mask, nullptr, nullptr);
                SDL_SetTextureBlendMode(base_mask, metadata.base_defaults.blend);
                SDL_SetTextureColorMod(base_mask, metadata.base_defaults.r, metadata.base_defaults.g, metadata.base_defaults.b);
                SDL_SetTextureAlphaMod(base_mask, metadata.base_defaults.a);
            }
        }
#endif
        // TODO(#reactive-shadows): Runtime subtraction is disabled until reactive tuning returns.

        SDL_SetRenderTarget(renderer, prev_target);
        if (prev_blend != SDL_BLENDMODE_INVALID) {
            SDL_SetRenderDrawBlendMode(renderer, prev_blend);
        } else {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        }
    };

    if (destination) {
        render_mask(destination, context.stage_blend, false);
        context.stage_drew_to_destination = true;
        return destination;
    }

    SDL_Texture* cache_target = ensure_cache_target();
    if (!cache_target) {
        return nullptr;
    }
    render_mask(cache_target, SDL_BLENDMODE_BLEND, true);
    return cache_target;
}

}

