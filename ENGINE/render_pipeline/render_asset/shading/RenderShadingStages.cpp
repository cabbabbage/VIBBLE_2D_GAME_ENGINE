#include "render_pipeline/render_asset/shading/RenderShadingStages.hpp"

#include "asset/Asset.hpp"
#include "render/global_light_source.hpp"
#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>

#include "render/light_map.hpp"

namespace render_pipeline::shading {

namespace {

struct ShadowSettings {
    int   kernel_radius         = 2;
    float outer_ring_weight     = 1.2f;
    float diagonal_weight       = 0.5f;
    float gradient_deadzone     = 0.06f;
    float gradient_max          = 0.6f;
    float temporal_smoothing    = 0.6f;
    float offset_ratio_x        = 0.25f;
    float offset_ratio_y        = 0.18f;
    float offset_x_bias         = 1.25f;
    float offset_y_bias         = 0.9f;
    float offset_max_ratio_x    = 0.35f;
    float offset_max_ratio_y    = 0.25f;
    float scale_strength        = 0.3f;
    float scale_front_limit     = 0.35f;
    float scale_back_limit      = 0.22f;
    float scale_min             = 0.5f;
    float scale_max             = 1.6f;
    float opacity_gamma         = 1.4f;
    float opacity_min_factor    = 0.45f;
    float opacity_max_factor    = 1.35f;
    float absolute_opacity_min  = 0.1f;
    float absolute_opacity_max  = 1.0f;
    float brightness_floor      = 0.05f;
};

const ShadowSettings& shadow_settings() {
    static const ShadowSettings settings{};
    return settings;
}

struct ShadowTemporalState {
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float opacity  = 0.0f;
    float scale    = 1.0f;
};

std::unordered_map<const Asset*, ShadowTemporalState>& shadow_state_cache() {
    static std::unordered_map<const Asset*, ShadowTemporalState> cache;
    return cache;
}

float clampf(float value, float min_value, float max_value) {
    return std::max(min_value, std::min(value, max_value));
}

float blend_value(float previous, float target, float smoothing) {
    return previous * smoothing + target * (1.0f - smoothing);
}

struct LightProbe {
    float      local_average     = 0.0f;
    float      scene_average     = 0.0f;
    SDL_FPoint gradient_dir{ 0.0f, 0.0f };
    float      gradient_magnitude = 0.0f;
    bool       valid             = false;
};

LightProbe analyze_light_map(const VirtualLightMap& map, const StageContext& context) {
    LightProbe result{};
    const ShadowSettings& cfg = shadow_settings();

    const auto cells = map.cells;
    const float scene_sum = std::accumulate(cells.begin(), cells.end(), 0.0f);
    result.scene_average = cells.empty() ? 0.0f : scene_sum / static_cast<float>(cells.size());

    if (context.screen_width_px <= 0 || context.screen_height_px <= 0) {
        result.local_average = result.scene_average;
        result.valid         = true;
        return result;
    }

    if (context.screen_rect.w <= 0 || context.screen_rect.h <= 0) {
        result.local_average = result.scene_average;
        result.valid         = true;
        return result;
    }

    const int grid_w = VirtualLightMap::kGridWidth;
    const int grid_h = VirtualLightMap::kGridHeight;
    if (grid_w <= 0 || grid_h <= 0) {
        return result;
    }

    const float center_x = static_cast<float>(context.screen_rect.x) +
                           static_cast<float>(context.screen_rect.w) * 0.5f;
    const float center_y = static_cast<float>(context.screen_rect.y) +
                           static_cast<float>(context.screen_rect.h) * 0.5f;

    const float normalized_x = clampf(center_x / static_cast<float>(context.screen_width_px), 0.0f, 1.0f);
    const float normalized_y = clampf(center_y / static_cast<float>(context.screen_height_px), 0.0f, 1.0f);

    const float grid_fx = normalized_x * static_cast<float>(grid_w);
    const float grid_fy = normalized_y * static_cast<float>(grid_h);

    int cx = std::clamp(static_cast<int>(std::floor(grid_fx)), 0, grid_w - 1);
    int cy = std::clamp(static_cast<int>(std::floor(grid_fy)), 0, grid_h - 1);

    auto sample = [&](int x, int y) {
        x = std::clamp(x, 0, grid_w - 1);
        y = std::clamp(y, 0, grid_h - 1);
        return map.at(x, y);
    };

    const int radius = std::max(1, cfg.kernel_radius);
    float local_sum   = 0.0f;
    float local_weight = 0.0f;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const float value = sample(cx + dx, cy + dy);
            const float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy));
            float weight = 1.0f / (1.0f + distance);
            if (cfg.outer_ring_weight != 1.0f && radius > 0 &&
                (std::abs(dx) == radius || std::abs(dy) == radius)) {
                weight *= cfg.outer_ring_weight;
            }
            local_sum += value * weight;
            local_weight += weight;
        }
    }

    result.local_average = (local_weight > 0.0f) ? (local_sum / local_weight) : sample(cx, cy);

    float grad_x = 0.0f;
    float grad_y = 0.0f;
    float grad_weight_x = 0.0f;
    float grad_weight_y = 0.0f;
    for (int step = 1; step <= radius; ++step) {
        float base_weight = 1.0f / static_cast<float>(step);
        if (cfg.outer_ring_weight != 1.0f && step == radius) {
            base_weight *= cfg.outer_ring_weight;
        }

        const float right = sample(cx + step, cy);
        const float left  = sample(cx - step, cy);
        grad_x += (right - left) * base_weight;
        grad_weight_x += base_weight;

        const float down = sample(cx, cy + step);
        const float up   = sample(cx, cy - step);
        grad_y += (down - up) * base_weight;
        grad_weight_y += base_weight;

        if (cfg.diagonal_weight > 0.0f) {
            const float diag_weight = base_weight * cfg.diagonal_weight * 0.5f;
            const float ur = sample(cx + step, cy - step);
            const float dr = sample(cx + step, cy + step);
            const float ul = sample(cx - step, cy - step);
            const float dl = sample(cx - step, cy + step);
            grad_x += (dr + ur - dl - ul) * diag_weight;
            grad_y += (dr + dl - ur - ul) * diag_weight;
            grad_weight_x += diag_weight;
            grad_weight_y += diag_weight;
        }
    }

    if (grad_weight_x > 0.0f) {
        grad_x /= grad_weight_x;
    }
    if (grad_weight_y > 0.0f) {
        grad_y /= grad_weight_y;
    }

    const float magnitude = std::sqrt(grad_x * grad_x + grad_y * grad_y);
    if (std::isfinite(magnitude) && magnitude > 1e-5f) {
        result.gradient_magnitude = magnitude;
        result.gradient_dir       = SDL_FPoint{ grad_x / magnitude, grad_y / magnitude };
    }

    result.valid = true;
    return result;
}

}  // namespace

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
        int tex_w = 0;
        int tex_h = 0;
        if (SDL_QueryTexture(cache.texture, nullptr, nullptr, &tex_w, &tex_h) != 0 || tex_w != width || tex_h != height) {
            SDL_DestroyTexture(cache.texture);
            cache.texture = nullptr;
            cache.width   = 0;
            cache.height  = 0;
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

    const ShadowSettings& cfg = shadow_settings();

    const VirtualLightMap* map = context.virtual_light_map();
    LightProbe probe{};
    if (map) {
        probe = analyze_light_map(*map, context);
    }

    ShadowTemporalState target{};
    target.offset_x = 0.0f;
    target.offset_y = 0.0f;
    target.scale    = context.base_shadow_scale;
    target.opacity  = context.base_shadow_opacity;

    const float gradient_deadzone = std::max(0.0f, cfg.gradient_deadzone);
    const float gradient_max      = std::max(gradient_deadzone + 1e-4f, cfg.gradient_max);
    if (map && probe.valid && probe.gradient_magnitude > gradient_deadzone) {
        const float clamped_mag = clampf(probe.gradient_magnitude, gradient_deadzone, gradient_max);
        const float gradient_strength = (clamped_mag - gradient_deadzone) / (gradient_max - gradient_deadzone);

        SDL_FPoint dir = probe.gradient_dir;
        const float dir_len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (dir_len > 1e-5f) {
            dir.x /= dir_len;
            dir.y /= dir_len;
        } else {
            dir = SDL_FPoint{ 0.0f, 0.0f };
        }

        const float front_component = std::max(0.0f, -dir.y);
        const float back_component  = std::max(0.0f, dir.y);
        const float side_component  = std::abs(dir.x);
        const float directional_norm = front_component + back_component + side_component;
        float directional_weight = 0.0f;
        if (directional_norm > 1e-6f) {
            directional_weight = (front_component * 2.0f + side_component * 1.5f + back_component * 1.0f) /
                                 directional_norm;
        }

        const float reactivity = gradient_strength * directional_weight;

        const float base_width  = static_cast<float>(width);
        const float base_height = static_cast<float>(height);

        float offset_x = -dir.x * reactivity * base_width * cfg.offset_ratio_x * cfg.offset_x_bias;
        float offset_y = -dir.y * reactivity * base_height * cfg.offset_ratio_y * cfg.offset_y_bias;
        const float max_offset_x = base_width * cfg.offset_max_ratio_x;
        const float max_offset_y = base_height * cfg.offset_max_ratio_y;
        offset_x = clampf(offset_x, -max_offset_x, max_offset_x);
        offset_y = clampf(offset_y, -max_offset_y, max_offset_y);

        float scale_delta = (front_component - back_component) * reactivity * cfg.scale_strength;
        scale_delta = clampf(scale_delta, -cfg.scale_back_limit, cfg.scale_front_limit);
        float scale = context.base_shadow_scale * (1.0f + scale_delta);
        scale = clampf(scale, cfg.scale_min, cfg.scale_max);

        float scene_avg = std::max(probe.scene_average, cfg.brightness_floor);
        float local_avg = std::max(probe.local_average, cfg.brightness_floor);
        float opacity_factor = (local_avg > 0.0f) ? (scene_avg / local_avg) : 1.0f;
        opacity_factor = clampf(opacity_factor, cfg.opacity_min_factor, cfg.opacity_max_factor);
        float opacity = context.base_shadow_opacity * std::pow(opacity_factor, cfg.opacity_gamma);
        opacity = clampf(opacity, cfg.absolute_opacity_min, cfg.absolute_opacity_max);

        target.offset_x = offset_x;
        target.offset_y = offset_y;
        target.scale    = scale;
        target.opacity  = opacity;
    }

    ShadowTemporalState output = target;
    const float smoothing = clampf(cfg.temporal_smoothing, 0.0f, 0.999f);
    auto& state_cache = shadow_state_cache();
    if (smoothing > 0.0f) {
        const auto it = state_cache.find(&asset);
        if (it != state_cache.end()) {
            output.offset_x = blend_value(it->second.offset_x, target.offset_x, smoothing);
            output.offset_y = blend_value(it->second.offset_y, target.offset_y, smoothing);
            output.scale    = blend_value(it->second.scale, target.scale, smoothing);
            output.opacity  = blend_value(it->second.opacity, target.opacity, smoothing);
        }
    }
    output.offset_x = clampf(output.offset_x, -static_cast<float>(width) * cfg.offset_max_ratio_x,
                             static_cast<float>(width) * cfg.offset_max_ratio_x);
    output.offset_y = clampf(output.offset_y, -static_cast<float>(height) * cfg.offset_max_ratio_y,
                             static_cast<float>(height) * cfg.offset_max_ratio_y);
    output.scale = clampf(output.scale, cfg.scale_min, cfg.scale_max);
    output.opacity = clampf(output.opacity, cfg.absolute_opacity_min, cfg.absolute_opacity_max);
    state_cache[&asset] = output;

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
    const Uint8 shade = static_cast<Uint8>(std::lround(output.opacity * 255.0f));
    SDL_SetTextureColorMod(mask_texture, shade, shade, shade);
    SDL_SetTextureAlphaMod(mask_texture, shade);

    const int scaled_w = std::max(1, static_cast<int>(std::lround(static_cast<float>(width) * output.scale)));
    const int scaled_h = std::max(1, static_cast<int>(std::lround(static_cast<float>(height) * output.scale)));
    const int offset_px_x = static_cast<int>(std::lround(output.offset_x));
    const int offset_px_y = static_cast<int>(std::lround(output.offset_y));
    const SDL_Point anchor = context.anchor_bottom_center();
    SDL_Rect dest{ anchor.x - scaled_w / 2 + offset_px_x, anchor.y - scaled_h + offset_px_y, scaled_w, scaled_h };
    SDL_RenderCopy(renderer, mask_texture, nullptr, &dest);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_MOD);
    SDL_SetTextureBlendMode(mask_texture, SDL_BLENDMODE_MOD);
    SDL_SetTextureColorMod(mask_texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(mask_texture, 255);
    SDL_RenderCopy(renderer, mask_texture, nullptr, nullptr);

    SDL_SetRenderTarget(renderer, prev_target);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(mask_texture, saved_blend);
    SDL_SetTextureColorMod(mask_texture, saved_r, saved_g, saved_b);
    SDL_SetTextureAlphaMod(mask_texture, saved_a);

    cache.width  = width;
    cache.height = height;

    return cache.texture;
}

}  // namespace render_pipeline::shading

