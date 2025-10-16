#include "render_pipeline/render_asset/shading/RenderShadingStages.hpp"

#include "asset/Asset.hpp"
#include "render/global_light_source.hpp"
#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"
#include "render/camera.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
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
    VirtualLightMap     last_light_map{};
    bool                has_last_map   = false;
    bool                has_output     = false;
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

bool light_maps_similar(const VirtualLightMap& a, const VirtualLightMap& b, float tolerance) {
    float diff_sum     = 0.0f;
    float magnitude_a  = 0.0f;
    float magnitude_b  = 0.0f;
    for (std::size_t i = 0; i < a.cells.size(); ++i) {
        const float va = a.cells[i];
        const float vb = b.cells[i];
        diff_sum += std::fabs(va - vb);
        magnitude_a += std::fabs(va);
        magnitude_b += std::fabs(vb);
    }
    const float reference = std::max({ magnitude_a, magnitude_b, 1e-5f });
    const float ratio     = diff_sum / reference;
    return ratio <= tolerance;
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

struct LightProbe {
    float      local_average     = 0.0f;
    float      scene_average     = 0.0f;
    float      forward_average   = 0.0f;
    SDL_FPoint gradient_dir{ 0.0f, 0.0f };
    float      gradient_magnitude = 0.0f;
    bool       valid             = false;
};

LightProbe analyze_light_map(const VirtualLightMap& map, const StageContext& context) {
    LightProbe result{};
    const ReactiveShadowSettings& cfg = reactive_settings_or_default(context);

    const auto& cells = map.cells;
    const float scene_sum = std::accumulate(cells.begin(), cells.end(), 0.0f);
    result.scene_average = cells.empty() ? 0.0f : scene_sum / static_cast<float>(cells.size());

    if (context.screen_width_px <= 0 || context.screen_height_px <= 0) {
        result.local_average   = result.scene_average;
        result.forward_average = result.scene_average;
        result.valid           = true;
        return result;
    }

    if (context.screen_rect.w <= 0 || context.screen_rect.h <= 0) {
        result.local_average   = result.scene_average;
        result.forward_average = result.scene_average;
        result.valid           = true;
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

    const int radius = std::max(1, cfg.sampling.kernel_radius);
    float local_sum   = 0.0f;
    float local_weight = 0.0f;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const float value = sample(cx + dx, cy + dy);
            const float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy));
            float weight = 1.0f / (1.0f + distance);
            if (cfg.sampling.outer_ring_weight != 1.0f && radius > 0 &&
                (std::abs(dx) == radius || std::abs(dy) == radius)) {
                weight *= cfg.sampling.outer_ring_weight;
            }
            local_sum += value * weight;
            local_weight += weight;
        }
    }

    result.local_average = (local_weight > 0.0f) ? (local_sum / local_weight) : sample(cx, cy);

    const auto& map_cfg = cfg.virtual_light_map;
    const int   quadrant_count = std::max(1, map_cfg.quadrant_count);
    const float falloff        = std::max(0.05f, map_cfg.distance_strength_falloff);
    const float dir_strength   = std::max(0.0f, map_cfg.directional_strength);
    const float angle_step     = kTwoPi / static_cast<float>(quadrant_count);
    const float half_steps     = std::max(1.0f, static_cast<float>(quadrant_count) * 0.5f);

    const float frac_x = grid_fx - std::floor(grid_fx);
    const float frac_y = grid_fy - std::floor(grid_fy);
    const float center_fx = static_cast<float>(cx) + frac_x + 0.5f;
    const float center_fy = static_cast<float>(cy) + frac_y + 0.5f;

    int front_quadrant_to_skip = -1;
    if (normalized_y <= 0.5f) {
        front_quadrant_to_skip = (normalized_x < 0.5f) ? 0 : 1;
    }

    float forward_sum    = 0.0f;
    float forward_weight = 0.0f;
    for (int y = 0; y < grid_h; ++y) {
        for (int x = 0; x < grid_w; ++x) {
            const float sample_value = map.at(x, y);
            const float cell_x = static_cast<float>(x) + 0.5f;
            const float cell_y = static_cast<float>(y) + 0.5f;
            const float dx = cell_x - center_fx;
            const float dy = cell_y - center_fy;

            const bool cell_in_front = cell_y <= center_fy;
            if (!cell_in_front) {
                continue;
            }

            if (front_quadrant_to_skip >= 0) {
                const bool cell_is_left = cell_x < center_fx;
                const int  cell_front_quadrant = cell_is_left ? 0 : 1;
                if (cell_front_quadrant == front_quadrant_to_skip) {
                    continue;
                }
            }

            const float distance = std::sqrt(dx * dx + dy * dy);
            const float distance_weight = 1.0f / (1.0f + distance * falloff);

            const float angle = std::atan2(-dy, dx);
            float diff = std::fabs(std::remainder(angle, kTwoPi));
            if (diff > kPi) {
                diff = kTwoPi - diff;
            }
            const float steps_f = (angle_step > 0.0f) ? diff / angle_step : 0.0f;
            const float normalized_steps =
                std::clamp(std::round(steps_f) / half_steps, 0.0f, 1.0f);
            const float directional_weight = std::pow(std::max(0.0f, 1.0f - normalized_steps), 1.0f + dir_strength);

            const float weight = distance_weight * directional_weight;
            if (weight <= 0.0f) {
                continue;
            }

            forward_sum += sample_value * weight;
            forward_weight += weight;
        }
    }

    result.forward_average = (forward_weight > 0.0f) ? (forward_sum / forward_weight) : sample(cx, cy);

    float grad_x = 0.0f;
    float grad_y = 0.0f;
    float grad_weight_x = 0.0f;
    float grad_weight_y = 0.0f;
    for (int step = 1; step <= radius; ++step) {
        float base_weight = 1.0f / static_cast<float>(step);
        if (cfg.sampling.outer_ring_weight != 1.0f && step == radius) {
            base_weight *= cfg.sampling.outer_ring_weight;
        }

        const float right = sample(cx + step, cy);
        const float left  = sample(cx - step, cy);
        grad_x += (right - left) * base_weight;
        grad_weight_x += base_weight;

        const float down = sample(cx, cy + step);
        const float up   = sample(cx, cy - step);
        grad_y += (down - up) * base_weight;
        grad_weight_y += base_weight;

        if (cfg.sampling.diagonal_weight > 0.0f) {
            const float diag_weight = base_weight * cfg.sampling.diagonal_weight * 0.5f;
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
    bool        reuse_previous   = false;

    if (map && persistent.has_last_map && has_previous_output) {
        const float similarity_threshold = clampf(cfg.stability.reuse_similarity_threshold, 0.0f, 1.0f);
        if (light_maps_similar(persistent.last_light_map, *map, similarity_threshold)) {
            reuse_previous = true;
            target         = previous_output;
            target_scale   = previous_scale;
        }
    }

    LightProbe probe{};
    float      gradient_strength = 0.0f;
    float      directional_weight = 0.0f;
    if (map && !reuse_previous) {
        probe = analyze_light_map(*map, context);
        if (probe.valid) {
            const float sensitivity = std::max(0.0f, cfg.directionality.gradient_sensitivity);
            if (probe.gradient_magnitude > sensitivity) {
                const float gradient_excess = probe.gradient_magnitude - sensitivity;
                gradient_strength = gradient_excess / (gradient_excess + 1.0f);

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
                if (directional_norm > 1e-6f) {
                    const float weighted_front = front_component * std::max(cfg.directionality.front_weight, 0.0f);
                    const float weighted_side  = side_component * std::max(cfg.directionality.side_weight, 0.0f);
                    const float weighted_back  = back_component * std::max(cfg.directionality.back_weight, 0.0f);
                    const float weighted_sum   = weighted_front + weighted_side + weighted_back;
                    if (weighted_sum > 1e-6f) {
                        directional_weight = weighted_sum / directional_norm;
                    }
                }

                const float reactivity = gradient_strength * directional_weight * cfg.directionality.offset_strength;

                const float base_width  = static_cast<float>(width);
                const float base_height = static_cast<float>(height);

                float offset_x = target.offset_x;
                float offset_y = target.offset_y;
                if (cfg.directionality.enable_offsets) {
                    const float max_offset_x = base_width * cfg.directionality.max_offset_ratio;
                    const float max_offset_y = base_height * cfg.directionality.max_offset_ratio;
                    offset_x = -dir.x * reactivity * max_offset_x;
                    offset_y = -dir.y * reactivity * max_offset_y;
                    offset_x = clampf(offset_x, -max_offset_x, max_offset_x);
                    offset_y = clampf(offset_y, -max_offset_y, max_offset_y);
                }

                float opacity = context.base_shadow_opacity;
                if (cfg.response.enable_opacity) {
                    const float scene_avg   = std::max(probe.scene_average, 1e-4f);
                    const float forward_avg = std::max(probe.forward_average, 1e-4f);
                    float       opacity_factor = (forward_avg > 0.0f) ? (scene_avg / forward_avg) : 1.0f;
                    opacity_factor = 1.0f + (opacity_factor - 1.0f) * cfg.response.opacity_strength;
                    opacity        = context.base_shadow_opacity * opacity_factor;
                    const float front_influence = gradient_strength * front_component;
                    const float front_boost     = std::max(cfg.response.front_opacity_boost, 0.0f);
                    if (front_influence > 0.0f && front_boost > 0.0f) {
                        opacity *= 1.0f + front_influence * front_boost;
                    }
                    opacity        = clampf(opacity, cfg.response.min_opacity, cfg.response.max_opacity);
                }

                target.offset_x = offset_x;
                target.offset_y = offset_y;
                target.opacity  = opacity;
            }

            float brightness_ratio = 0.0f;
            if (probe.scene_average > 1e-4f) {
                brightness_ratio = (probe.scene_average - probe.local_average) / probe.scene_average;
            }
            brightness_ratio = clampf(brightness_ratio, -1.0f, 1.0f);

            const float directional_influence = std::max(directional_weight, 0.0f);
            float       scale_multiplier      = 1.0f;
            if (gradient_strength > 0.0f) {
                scale_multiplier += gradient_strength * brightness_ratio * std::max(directional_influence, 0.25f);
            }
            target_scale = clampf(base_scale * scale_multiplier, min_scale_limit, max_scale_limit);
        }
    }

    float base_offset_x   = target.offset_x;
    float base_scale_only = target_scale;
    if (reuse_previous) {
        base_offset_x -= persistent.last_map_line_shift + persistent.last_parallax_shift;
        const float previous_factor = std::max(persistent.last_scale_factor, 1e-4f);
        base_scale_only = target_scale / previous_factor;
    }
    base_scale_only = clampf(base_scale_only, min_scale_limit, max_scale_limit);

    const float map_line_shift = compute_map_line_shift(context, width, cfg.output.map_line_weight);
    const float parallax_shift = compute_parallax_shift(context, asset, height, cfg.output.parallax_strength);

    target.offset_x = base_offset_x + map_line_shift + parallax_shift;
    target_scale    = clampf(base_scale_only * safe_scale_factor, limit_min, limit_max);

    persistent.last_scale_factor   = safe_scale_factor;
    persistent.last_map_line_shift = map_line_shift;
    persistent.last_parallax_shift = parallax_shift;

    ShadowTemporalState output      = target;
    float               output_scale = target_scale;
    const float         smoothing = cfg.stability.enable_temporal_smoothing
                                        ? clampf(cfg.stability.temporal_smoothing, 0.0f, 0.999f)
                                        : 0.0f;
    if (smoothing > 0.0f && has_previous_output) {
        output.offset_x = blend_value(previous_output.offset_x, target.offset_x, smoothing);
        output.offset_y = blend_value(previous_output.offset_y, target.offset_y, smoothing);
        output.opacity  = blend_value(previous_output.opacity, target.opacity, smoothing);
        output_scale    = blend_value(previous_scale, target_scale, smoothing);
    }
    output.offset_x = clampf(output.offset_x,
                             -static_cast<float>(width) * cfg.directionality.max_offset_ratio,
                             static_cast<float>(width) * cfg.directionality.max_offset_ratio);
    output.offset_y = clampf(output.offset_y,
                             -static_cast<float>(height) * cfg.directionality.max_offset_ratio,
                             static_cast<float>(height) * cfg.directionality.max_offset_ratio);
    output.opacity = clampf(output.opacity, cfg.response.min_opacity, cfg.response.max_opacity);
    output_scale   = clampf(output_scale, limit_min, limit_max);

    persistent.output    = output;
    persistent.scale     = output_scale;
    persistent.has_output = true;
    if (map && !reuse_previous) {
        persistent.last_light_map = *map;
        persistent.has_last_map   = true;
    }

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

