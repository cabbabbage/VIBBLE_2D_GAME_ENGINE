#include "render/runtime_lighting_sampler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "utils/area.hpp"
#include "utils/light_source.hpp"
#include "world/chunk.hpp"
#include "world/grid.hpp"

namespace runtime_lighting {
namespace {

struct RuntimeEmitter {
    SDL_FPoint position{0.0f, 0.0f};
    SDL_FPoint default_direction{0.0f, -1.0f};
    bool       has_default_direction = false;
    float      radius          = 0.0f;
    float      radius_squared  = 0.0f;
    float      intensity       = 0.0f;
    SDL_Color  color{255, 255, 255, 255};
    SDL_FRect  influence_bounds_f{0.0f, 0.0f, 0.0f, 0.0f};
    SDL_Rect   influence_bounds{0, 0, 0, 0};
    bool       is_player_light = false;
    struct Attenuation {
        float constant  = 1.0f;
        float linear    = 0.0f;
        float quadratic = 0.0f;
        bool  enabled   = false;

        float evaluate(float distance, float radius) const {
            if (!enabled) {
                if (radius <= 1e-4f) {
                    return 1.0f;
                }
                const float falloff = 1.0f - (distance / std::max(radius, 1.0f));
                return std::clamp(falloff, 0.0f, 1.0f);
            }

            const float denom = constant + linear * distance + quadratic * distance * distance;
            if (denom <= 1e-5f) {
                return 1.0f;
            }
            const float attenuation = 1.0f / denom;
            if (!std::isfinite(attenuation)) {
                return 0.0f;
            }
            return std::clamp(attenuation, 0.0f, 1.0f);
        }
    } attenuation{};
};

void finalize_emitter(RuntimeEmitter& emitter) {
    if (emitter.radius > 0.0f && emitter.intensity > 0.0f) {
        const float min_x = emitter.position.x - emitter.radius;
        const float min_y = emitter.position.y - emitter.radius;
        const float max_x = emitter.position.x + emitter.radius;
        const float max_y = emitter.position.y + emitter.radius;

        emitter.radius_squared       = emitter.radius * emitter.radius;
        emitter.influence_bounds_f.x = min_x;
        emitter.influence_bounds_f.y = min_y;
        emitter.influence_bounds_f.w = max_x - min_x;
        emitter.influence_bounds_f.h = max_y - min_y;

        emitter.influence_bounds.x = static_cast<int>(std::floor(min_x));
        emitter.influence_bounds.y = static_cast<int>(std::floor(min_y));
        const int ceil_max_x       = static_cast<int>(std::ceil(max_x));
        const int ceil_max_y       = static_cast<int>(std::ceil(max_y));
        emitter.influence_bounds.w = std::max(0, ceil_max_x - emitter.influence_bounds.x);
        emitter.influence_bounds.h = std::max(0, ceil_max_y - emitter.influence_bounds.y);
    } else {
        emitter.radius          = std::max(0.0f, emitter.radius);
        emitter.radius_squared  = 0.0f;
        emitter.intensity       = std::max(0.0f, emitter.intensity);
        emitter.influence_bounds_f = SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f};
        emitter.influence_bounds   = SDL_Rect{0, 0, 0, 0};
    }
}

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
    emitter.color     = sample.color;
    if (sample.has_direction) {
        emitter.default_direction      = sample.direction;
        const float mag                = std::sqrt(sample.direction.x * sample.direction.x + sample.direction.y * sample.direction.y);
        if (mag > 1e-4f) {
            emitter.default_direction.x = sample.direction.x / mag;
            emitter.default_direction.y = sample.direction.y / mag;
            emitter.has_default_direction = true;
        }
    }
    emitter.attenuation.constant  = sample.attenuation.constant;
    emitter.attenuation.linear    = sample.attenuation.linear;
    emitter.attenuation.quadratic = sample.attenuation.quadratic;
    emitter.attenuation.enabled   = sample.attenuation.enabled;
    finalize_emitter(emitter);
    return emitter;
}

RuntimeEmitter make_emitter_from_light(const AssetLight&              source,
                                       const LightSource&             light,
                                       const SDL_Rect&                dst,
                                       const camera&                  cam,
                                       const SDL_FPoint&              world_offset) {
    RuntimeEmitter emitter;
    if (!source.asset) {
        return emitter;
    }

    if (source.asset->info) {
        const std::string canonical_type = asset_types::canonicalize(source.asset->info->type);
        emitter.is_player_light          = (canonical_type == asset_types::player);
    }

    const float center_x = static_cast<float>(dst.x) + static_cast<float>(dst.w) * 0.5f;
    const float center_y = static_cast<float>(dst.y) + static_cast<float>(dst.h) * 0.5f;

    const SDL_Point screen_center{
        static_cast<int>(std::lround(center_x)), static_cast<int>(std::lround(center_y))};
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
    emitter.color     = light.color;
    const float offset_mag = std::sqrt(world_offset.x * world_offset.x + world_offset.y * world_offset.y);
    if (offset_mag > 1e-4f) {
        emitter.default_direction.x = world_offset.x / offset_mag;
        emitter.default_direction.y = world_offset.y / offset_mag;
        emitter.has_default_direction = true;
    }
    const float radius_safe = std::max(radius, 1.0f);
    const float falloff_pct = std::clamp(static_cast<float>(light.fall_off) / 100.0f, 0.0f, 1.0f);
    emitter.attenuation.enabled   = true;
    emitter.attenuation.constant  = 1.0f;
    emitter.attenuation.linear    = falloff_pct / radius_safe;
    emitter.attenuation.quadratic = (1.0f - falloff_pct) / (radius_safe * radius_safe);
    finalize_emitter(emitter);
    return emitter;
}

}

class OcclusionSampler {
public:
    OcclusionSampler(world::Grid& grid, RuntimeLightingSampler::OcclusionCache& cache)
        : grid_(grid)
        , cache_(cache) {}

    float visibility(const SDL_FPoint& from, const SDL_FPoint& to) {
        const float dx     = to.x - from.x;
        const float dy     = to.y - from.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 1e-4f) {
            return 1.0f;
        }

        const float step_length = 4.0f;
        const int   steps       = std::max(1, static_cast<int>(std::ceil(length / step_length)));
        int         occluded    = 0;
        int         total       = 0;

        for (int step = 1; step <= steps; ++step) {
            const float t = static_cast<float>(step) / static_cast<float>(steps);
            if (t >= 1.0f) {
                break;
            }
            const float sample_x = from.x + dx * t;
            const float sample_y = from.y + dy * t;
            const SDL_Point probe{static_cast<int>(std::lround(sample_x)),
                                  static_cast<int>(std::lround(sample_y))};
            if (is_blocked(probe)) {
                ++occluded;
            }
            ++total;
        }

        if (total <= 0) {
            return 1.0f;
        }

        const float visibility = 1.0f - static_cast<float>(occluded) / static_cast<float>(total);
        return std::clamp(visibility, 0.0f, 1.0f);
    }

private:
    bool is_blocked(const SDL_Point& point) {
        world::Chunk* chunk = grid_.chunk_from_world(point);
        if (!chunk) {
            return false;
        }

        RuntimeLightingSampler::CachedOcclusion& data = ensure_chunk(chunk);
        if (data.width <= 0 || data.height <= 0) {
            return false;
        }
        if (!SDL_PointInRect(&point, &data.bounds)) {
            return false;
        }

        const int local_x = point.x - data.bounds.x;
        const int local_y = point.y - data.bounds.y;
        if (local_x < 0 || local_y < 0 || local_x >= data.width || local_y >= data.height) {
            return false;
        }
        const std::size_t index = static_cast<std::size_t>(local_y) * static_cast<std::size_t>(data.width) + static_cast<std::size_t>(local_x);
        if (index >= data.mask.size()) {
            return false;
        }
        return data.mask[index] != 0;
    }

    RuntimeLightingSampler::CachedOcclusion& ensure_chunk(world::Chunk* chunk) {
        auto [it, inserted] = cache_.try_emplace(chunk);
        RuntimeLightingSampler::CachedOcclusion& entry = it->second;
        if (!inserted && entry.revision == chunk->occlusion_revision) {
            return entry;
        }

        entry.revision = chunk->occlusion_revision;
        entry.bounds    = chunk->world_bounds;
        entry.width     = std::max(0, entry.bounds.w);
        entry.height    = std::max(0, entry.bounds.h);

        if (entry.width <= 0 || entry.height <= 0) {
            entry.mask.clear();
            return entry;
        }

        const std::size_t total = static_cast<std::size_t>(entry.width) * static_cast<std::size_t>(entry.height);
        entry.mask.assign(total, 0);

        for (Asset* asset : chunk->assets) {
            if (!asset) {
                continue;
            }
            Area area = asset->get_area("impassable");
            if (area.get_points().empty()) {
                area = asset->get_area("collision_area");
            }
            for (const SDL_Point& pt : area.get_points()) {
                if (!SDL_PointInRect(&pt, &entry.bounds)) {
                    continue;
                }
                const int local_x = pt.x - entry.bounds.x;
                const int local_y = pt.y - entry.bounds.y;
                if (local_x < 0 || local_y < 0 || local_x >= entry.width || local_y >= entry.height) {
                    continue;
                }
                const std::size_t index = static_cast<std::size_t>(local_y) * static_cast<std::size_t>(entry.width) + static_cast<std::size_t>(local_x);
                if (index < entry.mask.size()) {
                    entry.mask[index] = 1;
                }
            }
        }

        return entry;
    }

private:
    world::Grid&                            grid_;
    RuntimeLightingSampler::OcclusionCache& cache_;
};

RuntimeLightingSampler::RuntimeLightingSampler(Assets* assets)
    : assets_(assets) {}

void RuntimeLightingSampler::begin_frame() {
    external_samples_.clear();

    if (!assets_) {
        occlusion_cache_.clear();
        active_chunk_lookup_.clear();
        return;
    }

    world::Grid& grid = assets_->world_grid();
    const auto&  active_chunks = grid.active_chunks();

    active_chunk_lookup_.clear();
    active_chunk_lookup_.reserve(active_chunks.size());
    for (world::Chunk* chunk : active_chunks) {
        active_chunk_lookup_.insert(chunk);
    }

    for (auto it = occlusion_cache_.begin(); it != occlusion_cache_.end();) {
        world::Chunk* chunk = it->first;
        if (active_chunk_lookup_.find(chunk) == active_chunk_lookup_.end()) {
            it = occlusion_cache_.erase(it);
        } else {
            ++it;
        }
    }
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
        const float scale_x     = std::isfinite(static_cast<float>(source.asset_rect.w) / base_width) ? static_cast<float>(source.asset_rect.w) / base_width : 1.0f;
        const float scale_y_base = std::isfinite(static_cast<float>(source.asset_rect.h) / base_height) ? static_cast<float>(source.asset_rect.h) / base_height : scale_x;
        const float scale_y = (source.base_height > 0) ? scale_y_base : scale_x;
        if (!std::isfinite(scale_x) || !std::isfinite(scale_y)) {
            continue;
        }

        const float safe_base_scale = (std::isfinite(source.asset_base_scale) && source.asset_base_scale > 0.0f) ? source.asset_base_scale : 1.0f;
        const float zoom_scale_x    = scale_x / safe_base_scale;
        const float zoom_scale_y    = scale_y / safe_base_scale;
        const float safe_zoom_scale_x = (std::isfinite(zoom_scale_x) && zoom_scale_x > 0.0f) ? zoom_scale_x : 1.0f;
        const float safe_zoom_scale_y = (std::isfinite(zoom_scale_y) && zoom_scale_y > 0.0f) ? zoom_scale_y : 1.0f;

        const float center_base_x = static_cast<float>(source.asset_rect.x) + static_cast<float>(source.asset_rect.w) * 0.5f;
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

            const SDL_FPoint world_offset{
                offset_x * scale_x,
                offset_y * scale_y,
};

            const float center_x = center_base_x + world_offset.x;
            const float center_y = center_base_y + world_offset.y;

            SDL_Rect dst{};
            dst.w = std::max(1, static_cast<int>(std::lround(scaled_w)));
            dst.h = std::max(1, static_cast<int>(std::lround(scaled_h)));
            dst.x = static_cast<int>(std::lround(center_x - static_cast<float>(dst.w) * 0.5f));
            dst.y = static_cast<int>(std::lround(center_y - static_cast<float>(dst.h) * 0.5f));

            if (light.intensity <= 0) {
                continue;
            }

            RuntimeEmitter emitter = make_emitter_from_light(source, light, dst, cam, world_offset);
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

    OcclusionSampler occlusion_sampler(grid, occlusion_cache_);

    for (world::Chunk* chunk : active_chunks) {
        if (!chunk) {
            continue;
        }
        const auto& lighting_cells = chunk->lighting_chunks();
        if (lighting_cells.empty()) {
            continue;
        }

        std::vector<std::vector<std::size_t>> cell_emitters(lighting_cells.size());
        const int                           step    = std::max(1, chunk->lighting_step());
        const int                           columns = chunk->lighting_columns();
        const int                           rows    = chunk->lighting_rows();
        if (columns <= 0 || rows <= 0) {
            continue;
        }

        for (std::size_t emitter_index = 0; emitter_index < emitters.size(); ++emitter_index) {
            const RuntimeEmitter& emitter = emitters[emitter_index];
            if (emitter.radius_squared <= 0.0f || emitter.intensity <= 0.0f) {
                continue;
            }

            SDL_Rect overlap{};
            if (!SDL_IntersectRect(&emitter.influence_bounds, &chunk->world_bounds, &overlap)) {
                continue;
            }

            const int chunk_x = chunk->world_bounds.x;
            const int chunk_y = chunk->world_bounds.y;

            const int min_col = std::clamp((overlap.x - chunk_x) / step, 0, columns - 1);
            const int max_col = std::clamp((overlap.x + overlap.w - 1 - chunk_x) / step, 0, columns - 1);
            const int min_row = std::clamp((overlap.y - chunk_y) / step, 0, rows - 1);
            const int max_row = std::clamp((overlap.y + overlap.h - 1 - chunk_y) / step, 0, rows - 1);

            for (int row = min_row; row <= max_row; ++row) {
                for (int col = min_col; col <= max_col; ++col) {
                    const std::size_t cell_index = static_cast<std::size_t>(row) * static_cast<std::size_t>(columns) + static_cast<std::size_t>(col);
                    if (cell_index >= lighting_cells.size()) {
                        continue;
                    }

                    const SDL_Rect& cell_bounds = lighting_cells[cell_index].world_bounds;
                    if (SDL_HasIntersection(&emitter.influence_bounds, &cell_bounds)) {
                        cell_emitters[cell_index].push_back(emitter_index);
                    }
                }
            }
        }

        for (std::size_t cell_index = 0; cell_index < lighting_cells.size(); ++cell_index) {
            const auto&       cell   = lighting_cells[cell_index];
            const SDL_Rect& bounds = cell.world_bounds;
            const SDL_FPoint center{
                static_cast<float>(bounds.x) + static_cast<float>(bounds.w) * 0.5f, static_cast<float>(bounds.y) + static_cast<float>(bounds.h) * 0.5f};

            float brightness_sum = 0.0f;
            float accum_r        = 0.0f;
            float accum_g        = 0.0f;
            float accum_b        = 0.0f;
            float accum_dir_x    = 0.0f;
            float accum_dir_y    = 0.0f;
            const auto& emitter_indices = cell_emitters[cell_index];
            for (std::size_t emitter_index : emitter_indices) {
                const RuntimeEmitter& emitter = emitters[emitter_index];
                const float dx               = center.x - emitter.position.x;
                const float dy               = center.y - emitter.position.y;
                const float dist_squared     = dx * dx + dy * dy;
                if (dist_squared > emitter.radius_squared) {
                    continue;
                }
                const float dist    = std::sqrt(dist_squared);
                float contribution = emitter.intensity * emitter.attenuation.evaluate(dist, emitter.radius);
                if (contribution <= 0.0f) {
                    continue;
                }
                if (emitter.is_player_light) {
                    constexpr float kPlayerBaseBoost      = 1.35f;
                    constexpr float kPlayerProximityBoost = 0.9f;
                    float           normalized_distance   = 0.0f;
                    if (emitter.radius > 1e-4f) {
                        normalized_distance = std::clamp(dist / emitter.radius, 0.0f, 1.0f);
                    }
                    const float proximity_factor = kPlayerBaseBoost +
                                                   kPlayerProximityBoost * (1.0f - normalized_distance);
                    contribution *= std::max(proximity_factor, 0.0f);
                }
                const float visibility = occlusion_sampler.visibility(emitter.position, center);
                if (visibility <= 0.0f) {
                    continue;
                }
                contribution *= visibility;
                if (contribution <= 0.0f) {
                    continue;
                }
                SDL_FPoint contribution_dir{0.0f, 0.0f};
                if (dist > 1e-4f) {
                    const float inv = 1.0f / dist;
                    contribution_dir.x = dx * inv;
                    contribution_dir.y = dy * inv;
                } else if (emitter.has_default_direction) {
                    contribution_dir = emitter.default_direction;
                }
                const float dir_mag = std::sqrt(contribution_dir.x * contribution_dir.x + contribution_dir.y * contribution_dir.y);
                if (dir_mag > 1e-4f) {
                    contribution_dir.x /= dir_mag;
                    contribution_dir.y /= dir_mag;
                    accum_dir_x += contribution_dir.x * contribution;
                    accum_dir_y += contribution_dir.y * contribution;
                }
                brightness_sum += contribution;
                accum_r += static_cast<float>(emitter.color.r) * contribution;
                accum_g += static_cast<float>(emitter.color.g) * contribution;
                accum_b += static_cast<float>(emitter.color.b) * contribution;
            }

            const float brightness = clamp01(brightness_sum);
            if (brightness <= 0.0f) {
                continue;
            }

            RuntimeLightingFrame::Sample sample;
            sample.chunk_i        = chunk->i;
            sample.chunk_j        = chunk->j;
            sample.global_i       = cell.global_i;
            sample.global_j       = cell.global_j;
            sample.brightness     = brightness;
            sample.raw_intensity  = brightness_sum;
            sample.world_position = center;
            if (brightness_sum > 1e-5f) {
                SDL_FPoint dir{accum_dir_x / brightness_sum, accum_dir_y / brightness_sum};
                const float dir_mag = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                if (dir_mag > 1e-4f) {
                    dir.x /= dir_mag;
                    dir.y /= dir_mag;
                    sample.direction   = dir;
                    sample.has_direction = true;
                }
            }
            if (brightness_sum > 1e-5f) {
                const float inv = 1.0f / brightness_sum;
                const float r   = std::clamp(accum_r * inv, 0.0f, 255.0f);
                const float g   = std::clamp(accum_g * inv, 0.0f, 255.0f);
                const float b   = std::clamp(accum_b * inv, 0.0f, 255.0f);
                sample.color.r  = static_cast<Uint8>(std::lround(r));
                sample.color.g  = static_cast<Uint8>(std::lround(g));
                sample.color.b  = static_cast<Uint8>(std::lround(b));
            } else {
                sample.color = SDL_Color{255, 255, 255, 255};
            }
            frame.samples.push_back(sample);
        }
    }

    if (!frame.samples.empty()) {
        constexpr std::size_t kMaxBrightestSamples = 32;
        std::vector<std::size_t> indices(frame.samples.size());
        std::iota(indices.begin(), indices.end(), std::size_t{0});
        const std::size_t take_count = std::min(indices.size(), kMaxBrightestSamples);
        if (take_count > 0) {
            std::partial_sort(indices.begin(), indices.begin() + take_count, indices.end(),
                              [&](std::size_t a, std::size_t b) {
                                  return frame.samples[a].brightness > frame.samples[b].brightness;
                              });

            float total_weight = 0.0f;
            SDL_FPoint centroid{0.0f, 0.0f};
            const RuntimeLightingFrame::Sample* brightest_sample = nullptr;
            std::size_t contributing_samples                     = 0;
            for (std::size_t idx = 0; idx < take_count; ++idx) {
                const RuntimeLightingFrame::Sample& sample = frame.samples[indices[idx]];
                float weight = std::max(sample.raw_intensity, sample.brightness);
                if (weight <= 0.0f) {
                    continue;
                }
                centroid.x += sample.world_position.x * weight;
                centroid.y += sample.world_position.y * weight;
                total_weight += weight;
                ++contributing_samples;
                if (!brightest_sample || sample.brightness > brightest_sample->brightness) {
                    brightest_sample = &sample;
                }
            }

            if (brightest_sample) {
                frame.has_brightest_sample        = true;
                frame.brightest_sample_position   = brightest_sample->world_position;
                frame.brightest_sample_brightness = brightest_sample->brightness;
            }

            if (total_weight > 1e-5f) {
                const float inv_weight = 1.0f / total_weight;
                centroid.x *= inv_weight;
                centroid.y *= inv_weight;
                frame.brightest_centroid       = centroid;
                frame.has_brightest_centroid   = true;
                frame.brightest_sample_count   = contributing_samples;

                SDL_FPoint accum_dir{0.0f, 0.0f};
                float direction_weight = 0.0f;
                for (std::size_t idx = 0; idx < take_count; ++idx) {
                    const RuntimeLightingFrame::Sample& sample = frame.samples[indices[idx]];
                    float weight = std::max(sample.raw_intensity, sample.brightness);
                    if (weight <= 0.0f) {
                        continue;
                    }
                    if (sample.has_direction) {
                        accum_dir.x += sample.direction.x * weight;
                        accum_dir.y += sample.direction.y * weight;
                        direction_weight += weight;
                    }
                }

                if (direction_weight <= 1e-5f) {
                    for (std::size_t idx = 0; idx < take_count; ++idx) {
                        const RuntimeLightingFrame::Sample& sample = frame.samples[indices[idx]];
                        float weight = std::max(sample.raw_intensity, sample.brightness);
                        if (weight <= 0.0f) {
                            continue;
                        }
                        const float dx = sample.world_position.x - centroid.x;
                        const float dy = sample.world_position.y - centroid.y;
                        const float len = std::sqrt(dx * dx + dy * dy);
                        if (len <= 1e-4f) {
                            continue;
                        }
                        const float nx = dx / len;
                        const float ny = dy / len;
                        accum_dir.x += nx * weight;
                        accum_dir.y += ny * weight;
                        direction_weight += weight;
                    }
                }

                if (direction_weight > 1e-5f) {
                    const float len = std::sqrt(accum_dir.x * accum_dir.x + accum_dir.y * accum_dir.y);
                    if (len > 1e-4f) {
                        frame.brightest_direction.x = accum_dir.x / len;
                        frame.brightest_direction.y = accum_dir.y / len;
                        frame.has_brightest_direction = true;
                    }
                }
            }
        }
    }

    return frame;
}

}

