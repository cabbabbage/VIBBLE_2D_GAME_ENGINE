#include "render_pipeline/render_asset/shading/RenderShadingStages.hpp"

#include "asset/Asset.hpp"
#include "render/global_light_source.hpp"
#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"
#include "render/camera.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <unordered_map>

#include "render/light_map.hpp"

namespace render_pipeline::shading {

namespace {

const ReactiveShadowSettings& reactive_settings_or_default(const StageContext& context) {
    if (const ReactiveShadowSettings* live = context.reactive_shadow_settings()) {
        return *live;
    }
    static const ReactiveShadowSettings defaults =
        sanitize_reactive_shadow_settings(ReactiveShadowSettings{});
    return defaults;
}

struct ShadowTemporalState {
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float opacity  = 0.0f;
};

struct ShadowPersistentState {
    ShadowTemporalState output{};
    float               scale = 1.0f;
    bool                has_output     = false;
    int                 last_quadrant  = -1;
    float               last_scale_factor   = 1.0f;
    float               last_map_line_shift = 0.0f;
    float               last_parallax_shift = 0.0f;
};

std::unordered_map<const Asset*, ShadowPersistentState>& shadow_state_cache() {
    static std::unordered_map<const Asset*, ShadowPersistentState> cache;
    return cache;
}

constexpr float kPi     = 3.14159265358979323846f;
constexpr float kTwoPi  = kPi * 2.0f;

float blend_value(float previous, float target, float smoothing) {
    return previous * smoothing + target * (1.0f - smoothing);
}

float compute_map_line_shift(const StageContext& context, int width, float weight) {
    if (!context.lighting || width <= 0 || weight <= 0.0f || context.screen_width_px <= 0) {
        return 0.0f;
    }
    const float screen_width = static_cast<float>(context.screen_width_px);
    const SDL_Point light_pos = context.main_light().get_position();
    const float half_width = std::max(screen_width * 0.5f, 1.0f);
    const float centered = clampf((static_cast<float>(light_pos.x) - half_width) / half_width, -2.0f, 2.0f);
    const float light_opacity = static_cast<float>(context.main_light_alpha()) / 255.0f;
    const float width_scale = static_cast<float>(width) * 0.5f;
    return centered * light_opacity * weight * width_scale;
}

float compute_parallax_shift(const StageContext& context, const Asset& asset, int height, float weight) {
    if (!context.lighting || weight <= 0.0f) {
        return 0.0f;
    }
    const camera& cam = context.camera_view();
    SDL_Point world_pos{ asset.pos.x, asset.pos.y };
    SDL_Point baseline = cam.map_to_screen(world_pos);

    float asset_scale = 1.0f;
    if (asset.info && std::isfinite(asset.info->scale_factor) && asset.info->scale_factor >= 0.0f) {
        asset_scale = asset.info->scale_factor;
    }
    const float cam_scale = cam.get_scale();
    float       inv_scale = 1.0f;
    if (std::isfinite(cam_scale) && cam_scale > 1e-6f) {
        inv_scale = 1.0f / cam_scale;
    }

    int effective_height = height > 0 ? height : context.height;
    if (effective_height <= 0) {
        effective_height = 1;
    }
    const float asset_screen_height = static_cast<float>(effective_height) * asset_scale * inv_scale;
    const float reference_height = context.reference_screen_height > 0.0f
                                       ? context.reference_screen_height
                                       : 1.0f;

    camera::RenderEffects effects = cam.compute_render_effects(world_pos, asset_screen_height, reference_height);
    const float parallax_px = static_cast<float>(effects.screen_position.x - baseline.x);
    return parallax_px * weight;
}

}  // namespace

void ClearShadowStateFor(const Asset* asset) {
    if (!asset) {
        return;
    }
    shadow_state_cache().erase(asset);
}

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

    const ReactiveShadowSettings& cfg = reactive_settings_or_default(context);

    auto&                     state_cache          = shadow_state_cache();
    ShadowPersistentState&    persistent           = state_cache[&asset];
    const ShadowTemporalState previous_output      = persistent.output;
    const float               previous_scale       = persistent.scale;
    const bool                has_previous_output  = persistent.has_output;

    const VirtualLightMap* map = context.virtual_light_map();
    ShadowTemporalState    target{};
    target.offset_x = 0.0f;
    target.offset_y = 0.0f;
    target.opacity  = context.base_shadow_opacity;

    const float base_scale       = context.base_shadow_scale;
    const float min_scale_limit  = base_scale * 0.9f;
    const float max_scale_limit  = base_scale * 2.0f;
    const float requested_scale_factor = cfg.output.scale_factor;
    const float safe_scale_factor      = std::max(requested_scale_factor, 1e-4f);
    const float scaled_min_limit       = min_scale_limit * safe_scale_factor;
    const float scaled_max_limit       = max_scale_limit * safe_scale_factor;
    const float limit_min              = std::min(scaled_min_limit, scaled_max_limit);
    const float limit_max              = std::max(scaled_min_limit, scaled_max_limit);
    float       target_scale           = base_scale;

    int current_quadrant = -1;
    if (map) {
        current_quadrant = map->quadrant_for_rect(context.screen_rect);
    }

    if (map && current_quadrant >= 0) {
        const auto& quadrant = map->quadrant_settings(current_quadrant);
        if (cfg.response.enable_opacity) {
            float clamped = clampf(quadrant.opacity, cfg.response.min_opacity, cfg.response.max_opacity);
            target.opacity = clampf(clamped, cfg.response.min_opacity, cfg.response.max_opacity);
        } else {
            target.opacity = context.base_shadow_opacity;
        }

        float offset_strength = cfg.directionality.enable_offsets
                                     ? std::max(cfg.directionality.offset_strength, 0.0f)
                                     : 0.0f;
        const float max_offset_x = static_cast<float>(width) * cfg.directionality.max_offset_ratio;
        const float max_offset_y = static_cast<float>(height) * cfg.directionality.max_offset_ratio;
        target.offset_x = clampf(quadrant.offset.x * offset_strength, -max_offset_x, max_offset_x);
        target.offset_y = clampf(quadrant.offset.y * offset_strength, -max_offset_y, max_offset_y);

        target_scale = clampf(base_scale * quadrant.scale, min_scale_limit, max_scale_limit);
    } else {
        if (cfg.response.enable_opacity) {
            target.opacity = clampf(target.opacity, cfg.response.min_opacity, cfg.response.max_opacity);
        }
        target.offset_x = 0.0f;
        target.offset_y = 0.0f;
        target_scale    = base_scale;
    }

    float base_offset_x = target.offset_x;
    float base_offset_y = target.offset_y;

    const float map_line_shift = compute_map_line_shift(context, width, cfg.output.map_line_weight);
    const float parallax_shift = compute_parallax_shift(context, asset, height, cfg.output.parallax_strength);

    target.offset_x = base_offset_x + map_line_shift + parallax_shift;
    target.offset_y = base_offset_y;
    float base_scale_only = clampf(target_scale, min_scale_limit, max_scale_limit);
    target_scale          = clampf(base_scale_only * safe_scale_factor, limit_min, limit_max);

    const float max_offset_x = static_cast<float>(width) * cfg.directionality.max_offset_ratio;
    const float max_offset_y = static_cast<float>(height) * cfg.directionality.max_offset_ratio;
    target.offset_x = clampf(target.offset_x, -max_offset_x, max_offset_x);
    target.offset_y = clampf(target.offset_y, -max_offset_y, max_offset_y);

    bool quadrant_changed = persistent.last_quadrant != current_quadrant;

    persistent.last_scale_factor   = safe_scale_factor;
    persistent.last_map_line_shift = map_line_shift;
    persistent.last_parallax_shift = parallax_shift;
    persistent.last_quadrant       = current_quadrant;

    ShadowTemporalState output      = target;
    float               output_scale = target_scale;
    const float         smoothing = cfg.stability.enable_temporal_smoothing
                                        ? clampf(cfg.stability.temporal_smoothing, 0.0f, 0.999f)
                                        : 0.0f;
    if (smoothing > 0.0f && has_previous_output && !quadrant_changed) {
        output.offset_x = blend_value(previous_output.offset_x, target.offset_x, smoothing);
        output.offset_y = blend_value(previous_output.offset_y, target.offset_y, smoothing);
        output.opacity  = blend_value(previous_output.opacity, target.opacity, smoothing);
        output_scale    = blend_value(previous_scale, target_scale, smoothing);
    }

    output.offset_x = clampf(output.offset_x, -max_offset_x, max_offset_x);
    output.offset_y = clampf(output.offset_y, -max_offset_y, max_offset_y);
    if (cfg.response.enable_opacity) {
        output.opacity = clampf(output.opacity, cfg.response.min_opacity, cfg.response.max_opacity);
    } else {
        output.opacity = context.base_shadow_opacity;
    }
    output_scale = clampf(output_scale, limit_min, limit_max);

    persistent.output    = output;
    persistent.scale     = output_scale;
    persistent.has_output = true;

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
    const Uint8 shade_alpha = static_cast<Uint8>(std::lround(output.opacity * 255.0f));
    SDL_SetTextureColorMod(mask_texture, 0, 0, 0);
    SDL_SetTextureAlphaMod(mask_texture, shade_alpha);

    const int scaled_w = std::max(1, static_cast<int>(std::lround(static_cast<float>(width) * output_scale)));
    const int scaled_h = std::max(1, static_cast<int>(std::lround(static_cast<float>(height) * output_scale)));
    const int offset_px_x = static_cast<int>(std::lround(output.offset_x));
    const int offset_px_y = static_cast<int>(std::lround(output.offset_y));
    const SDL_Point anchor{ width / 2, height / 2 };
    SDL_Rect dest{ anchor.x - scaled_w / 2 + offset_px_x,
                   anchor.y - scaled_h / 2 + offset_px_y,
                   scaled_w,
                   scaled_h };
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

        const SDL_BlendMode crop_blend = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO,
                                                                    SDL_BLENDFACTOR_SRC_ALPHA,
                                                                    SDL_BLENDOPERATION_ADD,
                                                                    SDL_BLENDFACTOR_ZERO,
                                                                    SDL_BLENDFACTOR_SRC_ALPHA,
                                                                    SDL_BLENDOPERATION_ADD);
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

    SDL_SetRenderTarget(renderer, prev_target);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    cache.width  = width;
    cache.height = height;

    return cache.texture;
}

}  // namespace render_pipeline::shading

