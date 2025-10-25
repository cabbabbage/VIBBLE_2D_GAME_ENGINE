#include "render/runtime_lighting_sampler.hpp"

#include <algorithm>
#include <cmath>

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "utils/light_source.hpp"
#include "world/chunk.hpp"
#include "world/grid.hpp"

namespace runtime_lighting {
namespace {

struct RuntimeEmitter {
    SDL_FPoint position{0.0f, 0.0f};
    float      radius    = 0.0f;
    float      intensity = 0.0f; // normalized [0,1]
};

float distance(SDL_FPoint a, SDL_FPoint b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

RuntimeEmitter make_emitter_from_external(const ExternalLightSample& sample) {
    RuntimeEmitter emitter;
    emitter.position = sample.position;
    emitter.radius   = std::max(0.0f, sample.radius);
    emitter.intensity = clamp01(sample.intensity);
    return emitter;
}

RuntimeEmitter make_emitter_from_light(const AssetLight&              source,
                                       const LightSource&             light,
                                       const SDL_Rect&                dst,
                                       const camera&                  cam) {
    RuntimeEmitter emitter;
    if (!source.asset) {
        return emitter;
    }

    const float center_x = static_cast<float>(dst.x) + static_cast<float>(dst.w) * 0.5f;
    const float center_y = static_cast<float>(dst.y) + static_cast<float>(dst.h) * 0.5f;

    const SDL_Point screen_center{
        static_cast<int>(std::lround(center_x)),
        static_cast<int>(std::lround(center_y))};
    const SDL_Point world_center = cam.screen_to_map(screen_center);
    emitter.position.x           = static_cast<float>(world_center.x);
    emitter.position.y           = static_cast<float>(world_center.y);

    const SDL_Point screen_right{screen_center.x + std::max(dst.w / 2, 1), screen_center.y};
    const SDL_Point screen_up{screen_center.x, screen_center.y - std::max(dst.h / 2, 1)};

    const SDL_Point world_right = cam.screen_to_map(screen_right);
    const SDL_Point world_up    = cam.screen_to_map(screen_up);

    const SDL_FPoint world_right_f{static_cast<float>(world_right.x),
                                   static_cast<float>(world_right.y)};
    const SDL_FPoint world_up_f{static_cast<float>(world_up.x), static_cast<float>(world_up.y)};

    const float radius_x = distance(emitter.position, world_right_f);
    const float radius_y = distance(emitter.position, world_up_f);

    float radius = std::max(radius_x, radius_y);
    radius       = std::max(radius, 0.0f);
    if (radius <= 0.0f) {
        int fallback = std::max(light.radius, dst.w / 2);
        fallback     = std::max(fallback, dst.h / 2);
        fallback     = std::max(fallback, 1);
        radius       = static_cast<float>(fallback);
    }

    emitter.radius    = radius;
    emitter.intensity = clamp01(static_cast<float>(light.intensity) / 255.0f);
    return emitter;
}

} // namespace

RuntimeLightingSampler::RuntimeLightingSampler(Assets* assets)
    : assets_(assets) {}

void RuntimeLightingSampler::begin_frame() {
    external_samples_.clear();
}

void RuntimeLightingSampler::add_external_sample(const ExternalLightSample& sample) {
    if (sample.radius <= 0.0f || sample.intensity <= 0.0f) {
        return;
    }
    external_samples_.push_back(sample);
}

RuntimeLightingFrame RuntimeLightingSampler::gather(const std::vector<AssetLight>& asset_lights,
                                                    const camera&                  cam) {
    RuntimeLightingFrame frame;
    if (!assets_) {
        return frame;
    }

    world::Grid& grid = assets_->world_grid();
    const std::vector<world::Chunk*>& active_chunks = grid.active_chunks();
    if (active_chunks.empty()) {
        external_samples_.clear();
        return frame;
    }

    std::vector<RuntimeEmitter> emitters;
    emitters.reserve(asset_lights.size() * 2 + external_samples_.size());

    for (const AssetLight& source : asset_lights) {
        Asset* asset = source.asset;
        if (!asset || !asset->info) {
            continue;
        }

        const auto& lights = asset->info->light_sources;
        if (lights.empty()) {
            continue;
        }

        const float base_width  = static_cast<float>(std::max(1, source.base_width));
        const float base_height = static_cast<float>(std::max(1, source.base_height));
        const float scale_x     = std::isfinite(static_cast<float>(source.asset_rect.w) / base_width)
                                      ? static_cast<float>(source.asset_rect.w) / base_width
                                      : 1.0f;
        const float scale_y_base = std::isfinite(static_cast<float>(source.asset_rect.h) / base_height)
                                        ? static_cast<float>(source.asset_rect.h) / base_height
                                        : scale_x;
        const float scale_y = (source.base_height > 0) ? scale_y_base : scale_x;
        if (!std::isfinite(scale_x) || !std::isfinite(scale_y)) {
            continue;
        }

        const float safe_base_scale = (std::isfinite(source.asset_base_scale) && source.asset_base_scale > 0.0f)
                                          ? source.asset_base_scale
                                          : 1.0f;
        const float zoom_scale_x    = scale_x / safe_base_scale;
        const float zoom_scale_y    = scale_y / safe_base_scale;
        const float safe_zoom_scale_x = (std::isfinite(zoom_scale_x) && zoom_scale_x > 0.0f)
                                            ? zoom_scale_x
                                            : 1.0f;
        const float safe_zoom_scale_y = (std::isfinite(zoom_scale_y) && zoom_scale_y > 0.0f)
                                            ? zoom_scale_y
                                            : 1.0f;

        const float center_base_x = static_cast<float>(source.asset_rect.x) +
                                    static_cast<float>(source.asset_rect.w) * 0.5f;
        const float center_base_y = static_cast<float>(source.asset_rect.y + source.asset_rect.h);

        for (const LightSource& light : lights) {
            int base_w = light.cached_w;
            int base_h = light.cached_h;

            if (base_w <= 0 || base_h <= 0) {
                SDL_Texture* reference = light.texture;
                if (reference) {
                    SDL_QueryTexture(reference, nullptr, nullptr, &base_w, &base_h);
                }
            }

            if (base_w <= 0 || base_h <= 0) {
                if (light.radius > 0) {
                    base_w = light.radius * 2;
                    base_h = light.radius * 2;
                }
            }

            if (base_w <= 0 || base_h <= 0) {
                continue;
            }

            const float scaled_w = std::max(1.0f, static_cast<float>(base_w) * safe_zoom_scale_x);
            const float scaled_h = std::max(1.0f, static_cast<float>(base_h) * safe_zoom_scale_y);

            const float offset_x = static_cast<float>(source.flipped ? -light.offset_x : light.offset_x);
            const float offset_y = static_cast<float>(light.offset_y);

            const float center_x = center_base_x + offset_x * scale_x;
            const float center_y = center_base_y + offset_y * scale_y;

            SDL_Rect dst{};
            dst.w = std::max(1, static_cast<int>(std::lround(scaled_w)));
            dst.h = std::max(1, static_cast<int>(std::lround(scaled_h)));
            dst.x = static_cast<int>(std::lround(center_x - static_cast<float>(dst.w) * 0.5f));
            dst.y = static_cast<int>(std::lround(center_y - static_cast<float>(dst.h) * 0.5f));

            if (light.intensity <= 0) {
                continue;
            }

            RuntimeEmitter emitter = make_emitter_from_light(source, light, dst, cam);
            if (emitter.radius <= 0.0f || emitter.intensity <= 0.0f) {
                continue;
            }

            emitters.push_back(emitter);
        }
    }

    for (const ExternalLightSample& sample : external_samples_) {
        RuntimeEmitter emitter = make_emitter_from_external(sample);
        if (emitter.radius <= 0.0f || emitter.intensity <= 0.0f) {
            continue;
        }
        emitters.push_back(emitter);
    }

    external_samples_.clear();

    if (emitters.empty()) {
        return frame;
    }

    frame.samples.reserve(emitters.size() * 4);

    for (world::Chunk* chunk : active_chunks) {
        if (!chunk) {
            continue;
        }
        for (auto& cell : chunk->lighting_chunks()) {
            const SDL_Rect& bounds = cell.world_bounds;
            const SDL_FPoint center{
                static_cast<float>(bounds.x) + static_cast<float>(bounds.w) * 0.5f,
                static_cast<float>(bounds.y) + static_cast<float>(bounds.h) * 0.5f};

            float brightness = 0.0f;
            for (const RuntimeEmitter& emitter : emitters) {
                if (emitter.radius <= 0.0f || emitter.intensity <= 0.0f) {
                    continue;
                }
                const float dist = distance(center, emitter.position);
                if (dist > emitter.radius) {
                    continue;
                }
                const float falloff = 1.0f - (dist / std::max(emitter.radius, 1.0f));
                brightness = clamp01(brightness + emitter.intensity * clamp01(falloff));
                if (brightness >= 1.0f) {
                    break;
                }
            }

            if (brightness <= 0.0f) {
                continue;
            }

            RuntimeLightingFrame::Sample sample;
            sample.chunk_i    = chunk->i;
            sample.chunk_j    = chunk->j;
            sample.global_i   = cell.global_i;
            sample.global_j   = cell.global_j;
            sample.brightness = brightness;
            frame.samples.push_back(sample);
        }
    }

    return frame;
}

} // namespace runtime_lighting

