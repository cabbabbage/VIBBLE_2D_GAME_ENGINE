#include "render_pipeline/render_asset/shading/RenderShadingStages.hpp"

#include "asset/Asset.hpp"
#include "render/global_light_source.hpp"
#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"
#include "render/camera.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

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

struct ShadowResponseSample {
    float opacity = 1.0f;
    float offset  = 0.0f;
    float scale   = 1.0f;
};

ShadowResponseSample evaluate_shadow_response(const ReactiveShadowSettings& settings, float brightness) {
    ShadowResponseSample result{};
    const auto& entries = settings.response_lut.entries;
    if (entries.empty()) {
        return result;
    }

    const float clamped_brightness = clampf(brightness, 0.0f, 1.0f);
    if (entries.size() == 1 || clamped_brightness <= entries.front().brightness) {
        const auto& entry = entries.front();
        result.opacity    = entry.opacity;
        result.offset     = entry.offset;
        result.scale      = entry.scale;
        return result;
    }

    for (std::size_t i = 1; i < entries.size(); ++i) {
        const auto& prev = entries[i - 1];
        const auto& next = entries[i];
        if (clamped_brightness <= next.brightness) {
            const float span = std::max(1e-6f, next.brightness - prev.brightness);
            const float t    = (clamped_brightness - prev.brightness) / span;
            result.opacity    = prev.opacity + (next.opacity - prev.opacity) * t;
            result.offset     = prev.offset + (next.offset - prev.offset) * t;
            result.scale      = prev.scale + (next.scale - prev.scale) * t;
            return result;
        }
    }

    const auto& tail = entries.back();
    result.opacity    = tail.opacity;
    result.offset     = tail.offset;
    result.scale      = tail.scale;
    return result;
}

float compute_screen_light_opacity_factor(const StageContext& context) {
    if (!context.lighting) {
        return 1.0f;
    }
    const Global_Light_Source& light = context.main_light();
    const int min_opacity            = light.min_opacity();
    const int max_opacity            = light.max_opacity();
    const int current_alpha          = std::clamp(static_cast<int>(light.get_current_color().a), min_opacity, max_opacity);
    const int range                  = std::max(1, max_opacity - min_opacity);
    const float normalized           = static_cast<float>(current_alpha - min_opacity) / static_cast<float>(range);
    return clampf(normalized, 0.0f, 1.0f);
}

SDL_FPoint normalized_map_light_direction(const StageContext& context) {
    if (!context.lighting) {
        return SDL_FPoint{ 0.0f, 0.0f };
    }
    const Global_Light_Source& light = context.main_light();
    const SDL_Point            ref   = light.get_direction_reference();
    const SDL_Point            target = light.get_direction_target();
    const float                dx     = static_cast<float>(target.x - ref.x);
    const float                dy     = static_cast<float>(target.y - ref.y);
    const float                len    = std::sqrt((dx * dx) + (dy * dy));
    if (!(len > 1e-3f)) {
        return SDL_FPoint{ 0.0f, 0.0f };
    }
    return SDL_FPoint{ dx / len, dy / len };
}

float compute_map_direction_factor(const StageContext& context, const SDL_FPoint& light_dir) {
    if (!context.lighting) {
        return 1.0f;
    }
    const float dir_len_sq = (light_dir.x * light_dir.x) + (light_dir.y * light_dir.y);
    if (!(dir_len_sq > 1e-6f)) {
        return 1.0f;
    }

    const Global_Light_Source& light = context.main_light();
    const SDL_Point            ref   = light.get_direction_reference();
    SDL_FPoint asset_vec{ context.screen_center.x - static_cast<float>(ref.x),
                          context.screen_center.y - static_cast<float>(ref.y) };
    const float asset_len = std::sqrt((asset_vec.x * asset_vec.x) + (asset_vec.y * asset_vec.y));
    if (!(asset_len > 1e-3f)) {
        return 1.0f;
    }
    asset_vec.x /= asset_len;
    asset_vec.y /= asset_len;
    const float dot = std::clamp(asset_vec.x * light_dir.x + asset_vec.y * light_dir.y, -1.0f, 1.0f);
    return (dot + 1.0f) * 0.5f;
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

    const ReactiveShadowSettings& cfg = reactive_settings_or_default(context);

    const LightMap* map = context.light_map();

    float base_opacity = clampf(context.base_shadow_opacity, 0.0f, 1.0f);
    float scale        = std::max(context.base_shadow_scale, 0.0f);

    float brightness = 0.0f;
    if (map) {
        const SDL_Rect& rect = context.screen_rect;
        const float     center_x = static_cast<float>(rect.x) + static_cast<float>(rect.w) * 0.5f;
        const float     center_y = static_cast<float>(rect.y) + static_cast<float>(rect.h) * 0.5f;

        bool single_quadrant = true;
        if (rect.w > 1 && rect.h > 1) {
            int reference_quadrant = -1;
            const SDL_Point corners[4] = { SDL_Point{ rect.x, rect.y },
                                           SDL_Point{ rect.x + rect.w - 1, rect.y },
                                           SDL_Point{ rect.x, rect.y + rect.h - 1 },
                                           SDL_Point{ rect.x + rect.w - 1, rect.y + rect.h - 1 } };
            for (const SDL_Point& pt : corners) {
                const int q = map->quadrant_for_point(static_cast<float>(pt.x), static_cast<float>(pt.y));
                if (q < 0) {
                    continue;
                }
                if (reference_quadrant < 0) {
                    reference_quadrant = q;
                } else if (q != reference_quadrant) {
                    single_quadrant = false;
                    break;
                }
            }
        }

        const auto& weights = cfg.sampling_weights;
        if (single_quadrant) {
            brightness = map->sample_brightness(static_cast<int>(std::lround(center_x)),
                                                static_cast<int>(std::lround(center_y)),
                                                weights.static_weight,
                                                weights.dynamic_weight);
        } else {
            brightness = map->sample_brightness_bilinear(center_x,
                                                         center_y,
                                                         weights.static_weight,
                                                         weights.dynamic_weight);
        }
        brightness = clampf(brightness, 0.0f, 1.0f);
    }

    const ShadowResponseSample response = evaluate_shadow_response(cfg, brightness);
    SDL_FPoint light_dir                 = normalized_map_light_direction(context);
    const float direction_factor         = compute_map_direction_factor(context, light_dir);
    const float direction_weight         = clampf(cfg.virtual_light_map.map_light_factor, 0.0f, 1.0f);
    const float direction_mix            = 1.0f + (direction_factor - 1.0f) * direction_weight;
    const float screen_light_factor      = compute_screen_light_opacity_factor(context);

    float opacity = clampf(base_opacity * response.opacity * screen_light_factor * direction_mix, 0.0f, 1.0f);
    float offset_x = light_dir.x * response.offset * direction_mix;
    float offset_y = light_dir.y * response.offset * direction_mix;
    scale *= cfg.virtual_light_map.shadow_scale;
    scale *= response.scale;
    scale = std::max(scale, 0.0f);

    float asset_scale = 1.0f;
    if (asset.info && std::isfinite(asset.info->scale_factor) && asset.info->scale_factor >= 0.0f) {
        asset_scale = asset.info->scale_factor;
    }
    const float cam_scale = context.camera_view().get_scale();
    float       inv_cam   = 1.0f;
    if (std::isfinite(cam_scale) && cam_scale > 1e-6f) {
        inv_cam = 1.0f / cam_scale;
    }

    const float screen_width  = static_cast<float>(width) * asset_scale * inv_cam;
    const float screen_height = static_cast<float>(height) * asset_scale * inv_cam;
    const float reference     = std::max(context.reference_screen_height, 1.0f);
    float       screen_size   = std::max(screen_width, screen_height);
    if (!std::isfinite(screen_size) || screen_size <= 0.0f) {
        screen_size = 1.0f;
    }

    const float size_multiplier = std::max(cfg.virtual_light_map.size_scale_factor, 0.0f) * (screen_size / reference);
    const float offset_length   = std::sqrt((offset_x * offset_x) + (offset_y * offset_y));
    if (offset_length > 0.0f && size_multiplier > 0.0f) {
        const float dir_x = offset_x / offset_length;
        const float dir_y = offset_y / offset_length;
        const float scaled_length = offset_length * size_multiplier;
        offset_x = dir_x * scaled_length;
        offset_y = dir_y * scaled_length;
    } else if (size_multiplier <= 0.0f) {
        offset_x = 0.0f;
        offset_y = 0.0f;
    }

    const float parallax_shift = compute_parallax_shift(context, asset, height, 1.0f);
    offset_x += parallax_shift;

    const float max_offset_x = std::max(cfg.virtual_light_map.max_offset_x, 0.0f);
    const float max_offset_y = std::max(cfg.virtual_light_map.max_offset_y, 0.0f);
    offset_x                 = clampf(offset_x, -max_offset_x, max_offset_x);
    offset_y                 = clampf(offset_y, -max_offset_y, max_offset_y);

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

    const int scaled_w   = std::max(1, static_cast<int>(std::lround(static_cast<float>(width) * scale)));
    const int scaled_h   = std::max(1, static_cast<int>(std::lround(static_cast<float>(height) * scale)));
    const int offset_px_x = static_cast<int>(std::lround(offset_x));
    const int offset_px_y = static_cast<int>(std::lround(offset_y));
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

