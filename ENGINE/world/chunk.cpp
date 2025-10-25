// Unified Chunk + LightMap implementation
#include "world/chunk.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include "utils/log.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/font_cache.hpp"
#include "render/camera.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"
#include "render/global_light_source.hpp"
#include "world/grid.hpp"

namespace world {

Chunk::~Chunk() {
    releaseLightingArtifacts();
}

void Chunk::ChunkShadowHistory::reset() {
    samples.fill(ChunkShadowParameters{});
    count  = 0;
    cursor = 0;
    blended = ChunkShadowParameters{};
}

void Chunk::ChunkShadowHistory::push(const ChunkShadowParameters& sample) {
    samples[static_cast<std::size_t>(cursor)] = sample;
    cursor = (cursor + 1) % kHistoryLength;
    if (count < kHistoryLength) {
        ++count;
    }

    if (count <= 0) {
        blended = ChunkShadowParameters{};
        return;
    }

    if (count == 1) {
        blended = sample;
        return;
    }

    ChunkShadowParameters accum{};
    float                 total_weight = 0.0f;
    const int             last_index   = (cursor - 1 + kHistoryLength) % kHistoryLength;

    for (int i = 0; i < count; ++i) {
        const int idx = (last_index - i + kHistoryLength) % kHistoryLength;
        const auto& entry = samples[static_cast<std::size_t>(idx)];

        const int   age           = i;
        const float weight_numer  = static_cast<float>(kHistoryLength - age - 1);
        const float weight_denom  = static_cast<float>(kHistoryLength - 1);
        const float weight        = (weight_denom <= 0.0f) ? 1.0f : weight_numer / weight_denom;
        if (weight <= 0.0f) {
            continue;
        }

        total_weight += weight;
        accum.opacity += entry.opacity * weight;
        accum.scale += entry.scale * weight;
        accum.offset_x_percent += entry.offset_x_percent * weight;
        accum.offset_y_percent += entry.offset_y_percent * weight;
        accum.parallax_intensity_percent += entry.parallax_intensity_percent * weight;
    }

    if (total_weight <= 1e-4f) {
        blended = sample;
        return;
    }

    const float inv_total = 1.0f / total_weight;
    blended.opacity                    = accum.opacity * inv_total;
    blended.scale                      = accum.scale * inv_total;
    blended.offset_x_percent           = accum.offset_x_percent * inv_total;
    blended.offset_y_percent           = accum.offset_y_percent * inv_total;
    blended.parallax_intensity_percent = accum.parallax_intensity_percent * inv_total;
}

void Chunk::releaseLightingArtifacts() {
    lighting_dirty                     = true;
    has_dynamic_overlay                = false;
    lighting                           = ChunkLightingState{};
    lighting.current_strength          = 1.0f;
    lighting.runtime_average_strength  = 1.0f;
    lighting.has_runtime_average       = false;
    lighting.is_active                 = false;
    lighting.needs_update              = true;
    shadow_history.reset();
    shadow = ChunkShadowParameters{};
}

} // namespace world

namespace {
constexpr const char* kEnableChunkLightingEnv  = "VIBBLE_ENABLE_CHUNK_LIGHTING";
constexpr const char* kDisableChunkLightingEnv = "VIBBLE_DISABLE_CHUNK_LIGHTING";

bool env_truthy(const char* value) {
    if (!value || !value[0]) {
        return false;
    }
    const char c = value[0];
    return c == '1' || c == 'y' || c == 'Y' || c == 't' || c == 'T';
}

bool env_falsey(const char* value) {
    if (!value || !value[0]) {
        return false;
    }
    const char c = value[0];
    return c == '0' || c == 'n' || c == 'N' || c == 'f' || c == 'F';
}

bool chunk_lighting_suspended_flag() {
    if (env_truthy(std::getenv(kDisableChunkLightingEnv))) {
        return true;
    }
    if (const char* value = std::getenv(kEnableChunkLightingEnv)) {
        if (env_falsey(value)) {
            return true;
        }
        if (env_truthy(value)) {
            return false;
        }
    }
    return false;
}

float smoothstep(float edge0, float edge1, float x) {
    if (edge0 == edge1) {
        return (x < edge0) ? 0.0f : 1.0f;
    }
    const float denom = edge1 - edge0;
    const float t     = std::clamp((x - edge0) / denom, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Uint8 clamp_alpha(float value) {
    return static_cast<Uint8>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

template <typename Callback>
void for_each_static_light_draw(SDL_Renderer* renderer,
                                const Assets* assets,
                                const world::Chunk& chunk,
                                Callback&& callback) {
    if (!renderer || !assets) {
        return;
    }

    const auto& static_lights = assets->getActiveStaticLightAssets();
    for (const Asset* asset : static_lights) {
        if (!asset || !asset->info) {
            continue;
        }
        if (asset->info->light_sources.empty()) {
            continue;
        }

        for (const auto& light : asset->info->light_sources) {
            SDL_Texture* tex = light.texture;
            if (!tex) {
                continue;
            }

            int src_w = light.cached_w > 0 ? light.cached_w : 0;
            int src_h = light.cached_h > 0 ? light.cached_h : 0;
            if (src_w <= 0 || src_h <= 0) {
                SDL_QueryTexture(tex, nullptr, nullptr, &src_w, &src_h);
            }
            if (src_w <= 0 || src_h <= 0) {
                continue;
            }

            const int draw_w = std::max(1, src_w);
            const int draw_h = std::max(1, src_h);

            SDL_Point world_center{asset->pos.x + light.offset_x, asset->pos.y + light.offset_y};
            SDL_Rect  world_dst{world_center.x - draw_w / 2,
                                world_center.y - draw_h / 2,
                                draw_w,
                                draw_h};

            SDL_Rect intersection{};
            if (!SDL_IntersectRect(&world_dst, &chunk.world_bounds, &intersection)) {
                continue;
            }

            SDL_Rect src_rect{};
            src_rect.x = std::clamp(intersection.x - world_dst.x, 0, std::max(0, draw_w));
            src_rect.y = std::clamp(intersection.y - world_dst.y, 0, std::max(0, draw_h));
            src_rect.w = intersection.w;
            src_rect.h = intersection.h;

            if (src_rect.x >= draw_w || src_rect.y >= draw_h) {
                continue;
            }

            src_rect.w = std::min(src_rect.w, draw_w - src_rect.x);
            src_rect.h = std::min(src_rect.h, draw_h - src_rect.y);

            SDL_Rect local_dst{};
            local_dst.x = std::max(0, intersection.x - chunk.world_bounds.x);
            local_dst.y = std::max(0, intersection.y - chunk.world_bounds.y);
            local_dst.w = src_rect.w;
            local_dst.h = src_rect.h;

            if (src_rect.w <= 0 || src_rect.h <= 0 || local_dst.w <= 0 || local_dst.h <= 0) {
                continue;
            }

            callback(tex, src_rect, local_dst);
        }
    }
}

SDL_Rect world_rect_from_screen(const camera& cam, const SDL_Rect& screen_rect) {
    SDL_Point top_left     = cam.screen_to_map({screen_rect.x, screen_rect.y});
    SDL_Point bottom_right = cam.screen_to_map({screen_rect.x + screen_rect.w, screen_rect.y + screen_rect.h});
    SDL_Rect result{};
    result.x = std::min(top_left.x, bottom_right.x);
    result.y = std::min(top_left.y, bottom_right.y);
    result.w = std::abs(bottom_right.x - top_left.x);
    result.h = std::abs(bottom_right.y - top_left.y);
    return result;
}

bool intersects(const SDL_Rect& a, const SDL_Rect& b) {
    return SDL_HasIntersection(&a, &b) == SDL_TRUE;
}

template <typename T>
T lerp(T a, T b, float t) {
    return static_cast<T>(a + (b - a) * t);
}

float average_luminance_for_region(const std::vector<std::uint8_t>& pixels,
                                   int buffer_width,
                                   int buffer_height,
                                   const SDL_Rect& region) {
    if (buffer_width <= 0 || buffer_height <= 0 || pixels.empty()) {
        return 0.0f;
    }

    const int start_x = std::clamp(region.x, 0, buffer_width);
    const int start_y = std::clamp(region.y, 0, buffer_height);
    const int end_x   = std::clamp(region.x + region.w, 0, buffer_width);
    const int end_y   = std::clamp(region.y + region.h, 0, buffer_height);

    if (end_x <= start_x || end_y <= start_y) {
        return 0.0f;
    }

    double       accum = 0.0;
    std::size_t  count = 0;
    const double rw    = 0.2126;
    const double gw    = 0.7152;
    const double bw    = 0.0722;

    for (int y = start_y; y < end_y; ++y) {
        for (int x = start_x; x < end_x; ++x) {
            const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(buffer_width) +
                                       static_cast<std::size_t>(x)) *
                                      4u;
            if (index + 3 >= pixels.size()) {
                continue;
            }

            const double r = static_cast<double>(pixels[index + 0]) / 255.0;
            const double g = static_cast<double>(pixels[index + 1]) / 255.0;
            const double b = static_cast<double>(pixels[index + 2]) / 255.0;
            const double a = static_cast<double>(pixels[index + 3]) / 255.0;

            const double luminance = (r * rw) + (g * gw) + (b * bw);
            accum += luminance * a;
            ++count;
        }
    }

    if (count == 0) {
        return 0.0f;
    }

    return static_cast<float>(accum / static_cast<double>(count));
}

std::pair<float, float> compute_brightness_gradient(const world::Chunk& center,
                                                    const world::Grid& grid,
                                                    int radius,
                                                    float falloff_x,
                                                    float falloff_y) {
    if (radius <= 0) return {0.0f, 0.0f};
    const float cb = std::clamp(center.lighting.current_strength, 0.0f, 1.0f);
    float gx = 0.0f, gy = 0.0f;
    for (int dj = -radius; dj <= radius; ++dj) {
        for (int di = -radius; di <= radius; ++di) {
            if (di == 0 && dj == 0) continue;
            const int ni = center.i + di;
            const int nj = center.j + dj;
            const world::Chunk* n = grid.find_chunk_ij(ni, nj);
            const float nb = n ? std::clamp(n->lighting.current_strength, 0.0f, 1.0f) : cb;
            const float db = nb - cb;
            const float dx = static_cast<float>(di);
            const float dy = static_cast<float>(dj);
            const float dist = std::max(1.0f, std::sqrt(dx*dx + dy*dy));
            const float wx = falloff_x / dist;
            const float wy = falloff_y / dist;
            gx += db * (dx / dist) * wx;
            gy += db * (dy / dist) * wy;
        }
    }
    return {gx, gy};
}

// Compute average brightness in front of the chunk (negative j direction),
// adjusted by anisotropic horizontal/vertical falloff. Returns [0,1].
// Compute weighted averages of light strength in-front (negative j) and behind (positive j).
static std::pair<float, float> compute_directional_average_strengths(const LightMap::ShadowSettings& settings,
                                                                     const world::Grid& grid,
                                                                     const world::Chunk& center) {
    const int   R  = std::max(0, settings.search_radius_cells);
    const float fh = std::max(0.0f, settings.falloff_horizontal);
    const float fv = std::max(0.0f, settings.falloff_vertical);

    auto sample_dir = [&](int j_begin, int j_end) -> float {
        double accum_w = 0.0;
        double accum_v = 0.0;
        const int step = (j_begin <= j_end) ? 1 : -1;
        for (int dj = j_begin; dj != j_end + step; dj += step) {
            for (int di = -R; di <= R; ++di) {
                if (dj == 0) continue; // skip same row
                const int ni = center.i + di;
                const int nj = center.j + dj;
                const world::Chunk* n = grid.find_chunk_ij(ni, nj);
                if (!n) continue;
                const float sx = std::abs(static_cast<float>(di));
                const float sy = std::abs(static_cast<float>(dj));
                const float w  = 1.0f / (1.0f + sx * fh + sy * fv);
                const float s  = std::clamp(n->lighting.current_strength, 0.0f, 1.0f);
                accum_w += static_cast<double>(w);
                accum_v += static_cast<double>(w) * static_cast<double>(std::clamp(s, 0.0f, 1.0f));
            }
        }
        if (accum_w <= 1e-8) {
            return std::clamp(center.lighting.current_strength, 0.0f, 1.0f);
        }
        return static_cast<float>(std::clamp(accum_v / accum_w, 0.0, 1.0));
    };

    const float front_avg  = (R > 0) ? sample_dir(-R, -1) : std::clamp(center.lighting.current_strength, 0.0f, 1.0f);
    const float behind_avg = (R > 0) ? sample_dir(1,  R)  : std::clamp(center.lighting.current_strength, 0.0f, 1.0f);
    return {front_avg, behind_avg};
}

static void compute_use_shadow_data_for_chunk(const LightMap::ShadowSettings& settings,
                                              const world::Grid& grid,
                                              const std::pair<float, float>& grad,
                                              const std::optional<SDL_FPoint>& map_light_direction,
                                              float map_light_opacity,
                                              world::Chunk& chunk) {
    world::Chunk::ChunkShadowParameters sample{};

    // Opacity: inverse of front average strength.
    const auto [front_avg, behind_avg] =
        compute_directional_average_strengths(settings, grid, chunk);
    sample.opacity = std::clamp(1.0f - front_avg, 0.0f, 1.0f);

    // Scale: grow with front dominance, shrink with behind dominance (nonlinear towards min).
    const float d = std::clamp(front_avg - behind_avg, -1.0f, 1.0f); // [-1,1]
    const int min_p = std::clamp(settings.min_scale_percent, 50, 200);
    const int max_p = std::clamp(settings.max_scale_percent, 50, 200);
    const float base_p = 100.0f;
    float scale_percent = base_p;
    if (d >= 0.0f) {
        const float t = d; // more front light -> larger scale
        scale_percent = base_p + t * (static_cast<float>(max_p) - base_p);
    } else {
        const float b = -d; // more behind light -> smaller scale
        // Ease-out towards min: fast at first, slower as approaching min.
        const float ease_out = 1.0f - std::pow(1.0f - b, 2.0f); // gamma=2.0
        scale_percent = base_p - ease_out * (base_p - static_cast<float>(min_p));
    }
    scale_percent = std::clamp(scale_percent, static_cast<float>(min_p), static_cast<float>(max_p));
    const float base_scale = std::max(0.0f, settings.base_shadow_scale);
    sample.scale = std::max(0.0f, base_scale * (scale_percent / 100.0f));

    // Base offset away from brightest direction (opposite brightness gradient)
    float gx = grad.first, gy = grad.second;
    float mag = std::sqrt(gx*gx + gy*gy);
    float nx = (mag > 1e-4f) ? (gx / mag) : 0.0f;
    float ny = (mag > 1e-4f) ? (gy / mag) : 0.0f;

    // Move opposite the gradient (away from brighter areas)
    float px = -nx * 100.0f;
    float py = -ny * 100.0f;

    const float chunk_w = static_cast<float>(std::max(1, chunk.world_bounds.w));
    const float chunk_h = static_cast<float>(std::max(1, chunk.world_bounds.h));
    const float raw_max_x_percent = (chunk_w > 1e-3f)
                                        ? (settings.max_offset_x_px / chunk_w) * 100.0f
                                        : 0.0f;
    const float raw_max_y_percent = (chunk_h > 1e-3f)
                                        ? (settings.max_offset_y_px / chunk_h) * 100.0f
                                        : 0.0f;
    const float safe_max_x_percent = std::clamp(raw_max_x_percent, 0.0f, 100.0f);
    const float safe_max_y_percent = std::clamp(raw_max_y_percent, 0.0f, 100.0f);

    // Map-light directional adjustment: push away from map-light direction with
    // strength that peaks when the light is furthest left/right and fades to 0
    // when directly above or below. The influence also fades smoothly as the
    // map-light opacity drops, hitting zero once the opacity is 100/255 or
    // lower.
    if (map_light_direction) {
        const SDL_FPoint dir = *map_light_direction;
        const float horizontal_influence = std::clamp(std::abs(dir.x), 0.0f, 1.0f);
        const float direction_factor     = std::clamp(settings.map_light_dir_offset_strength, 0.0f, 1.0f);
        constexpr float kMinOpacityForDirection = 100.0f / 255.0f;
        const float opacity_visibility         = smoothstep(kMinOpacityForDirection, 1.0f, map_light_opacity);
        const float opacity_scale              = opacity_visibility * opacity_visibility;
        const float dir_push = horizontal_influence * direction_factor * opacity_scale * 100.0f;
        if (dir_push > 1e-4f) {
            px += -dir.x * dir_push;
            py += -dir.y * dir_push;
        }
    }

    const float clamped_px = std::clamp(px, -safe_max_x_percent, safe_max_x_percent);
    const float clamped_py = std::clamp(py, -safe_max_y_percent, safe_max_y_percent);
    sample.offset_x_percent = std::clamp(clamped_px, -100.0f, 100.0f);
    sample.offset_y_percent = std::clamp(clamped_py, -100.0f, 100.0f);

    sample.parallax_intensity_percent = std::clamp(settings.parallax_percent, 0.0f, 100.0f);

    chunk.shadow_history.push(sample);
    chunk.shadow = chunk.shadow_history.value();
}

} // namespace

// LightMap implementation
// ctor/dtor inlined in header

LightMap::ShadowSettings LightMap::shadow_settings() const {
    ShadowSettings settings{};
    if (!assets_) {
        return settings;
    }

    const auto* reactive = assets_->reactive_shadow_settings();
    if (!reactive) {
        return settings;
    }

    using render_pipeline::shading::sanitize_reactive_shadow_settings;
    const auto sanitized = sanitize_reactive_shadow_settings(*reactive);

    settings.search_radius_cells = std::max(0, sanitized.virtual_light_map.search_radius);
    settings.falloff_horizontal  = std::max(0.0f, sanitized.virtual_light_map.horizontal_falloff);
    settings.falloff_vertical    = std::max(0.0f, sanitized.virtual_light_map.vertical_falloff);
    settings.max_offset_x_px     = std::max(0.0f, sanitized.virtual_light_map.max_offset_x);
    settings.max_offset_y_px     = std::max(0.0f, sanitized.virtual_light_map.max_offset_y);
    settings.base_shadow_scale   = std::max(0.0f, sanitized.virtual_light_map.shadow_scale);
    settings.min_scale_percent   = sanitized.virtual_light_map.min_scale_percent;
    settings.max_scale_percent   = sanitized.virtual_light_map.max_scale_percent;
    settings.map_light_dir_offset_strength =
        std::clamp(sanitized.virtual_light_map.map_light_dir_offset_strength, 0.0f, 1.0f);
    settings.parallax_percent = std::clamp(sanitized.virtual_light_map.parallax_percent, 0.0f, 100.0f);

    return settings;
}

void LightMap::rebuild(SDL_Renderer* /*renderer*/) {
    std::scoped_lock lock(mutex_);
    if (!assets_) {
        return;
    }
    world::Grid& grid = assets_->world_grid();
    for (world::Chunk* chunk : grid.active_chunks()) {
        if (!chunk) {
            continue;
        }
        chunk->releaseLightingArtifacts();
    }
}

void LightMap::update(SDL_Renderer* /*renderer*/, std::uint32_t /*delta_ms*/) {
    std::scoped_lock lock(mutex_);
    if (chunk_lighting_suspended_flag() || !assets_) {
        return;
    }

    const auto& chunks = active_chunks();
    if (chunks.empty()) {
        return;
    }

    const ShadowSettings settings = shadow_settings();

    float map_light_opacity = 0.0f;
    if (const Global_Light_Source* gl = assets_->map_light_source()) {
        map_light_opacity = std::clamp(static_cast<float>(gl->get_current_color().a) / 255.0f, 0.0f, 1.0f);
    }
    const bool map_opacity_changed = (std::abs(map_light_opacity - last_map_light_opacity_) > 1e-4f);
    last_map_light_opacity_        = map_light_opacity;

    const world::Grid& grid = assets_->world_grid();

    int min_i = INT32_MAX;
    int max_i = INT32_MIN;
    int min_j = INT32_MAX;
    int max_j = INT32_MIN;
    for (const world::Chunk* chunk : chunks) {
        if (!chunk) {
            continue;
        }
        min_i = std::min(min_i, chunk->i);
        max_i = std::max(max_i, chunk->i);
        min_j = std::min(min_j, chunk->j);
        max_j = std::max(max_j, chunk->j);
    }

    std::unordered_set<world::Chunk*> dedupe_set;
    dedupe_set.reserve(chunks.size() * 2);
    std::vector<world::Chunk*> border_candidates;
    border_candidates.reserve(chunks.size());

    auto enqueue_candidate = [&](world::Chunk* candidate) {
        if (!candidate) {
            return;
        }
        dedupe_set.insert(candidate);
    };

    for (world::Chunk* chunk : chunks) {
        enqueue_candidate(chunk);
    }

    for (world::Chunk* chunk : chunks) {
        if (!chunk) {
            continue;
        }
        const bool is_edge = (chunk->i == min_i) || (chunk->i == max_i) || (chunk->j == min_j) || (chunk->j == max_j);
        if (!is_edge) {
            continue;
        }
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                if (di == 0 && dj == 0) {
                    continue;
                }
                if (world::Chunk* neighbor = grid.find_chunk_ij(chunk->i + di, chunk->j + dj)) {
                    border_candidates.push_back(neighbor);
                    enqueue_candidate(neighbor);
                }
            }
        }
    }

    std::vector<world::Chunk*> update_set;
    update_set.reserve(dedupe_set.size());
    for (world::Chunk* chunk : chunks) {
        if (dedupe_set.erase(chunk) > 0) {
            update_set.push_back(chunk);
        }
    }
    for (world::Chunk* chunk : border_candidates) {
        if (dedupe_set.erase(chunk) > 0) {
            update_set.push_back(chunk);
        }
    }

    std::optional<SDL_FPoint> map_light_direction;
    if (const Global_Light_Source* gl = assets_->map_light_source()) {
        const SDL_Point ref = gl->get_direction_reference();
        const SDL_Point tgt = gl->get_direction_target();
        const float dx      = static_cast<float>(tgt.x - ref.x);
        const float dy      = static_cast<float>(tgt.y - ref.y);
        const float len     = std::sqrt(dx * dx + dy * dy);
        if (len > 1e-4f) {
            map_light_direction = SDL_FPoint{dx / len, dy / len};
        }
    }

    for (world::Chunk* chunk : update_set) {
        if (!chunk) {
            continue;
        }

        chunk->lighting.is_active = true;

        if (chunk->lighting.has_runtime_average) {
            chunk->lighting.current_strength =
                std::clamp(chunk->lighting.runtime_average_strength, 0.0f, 1.0f);
            chunk->lighting.runtime_average_strength = chunk->lighting.current_strength;
            chunk->lighting.has_runtime_average      = false;
            chunk->lighting.needs_update             = true;
        }

        if (map_opacity_changed) {
            chunk->lighting.needs_update = true;
        }

        if (!chunk->lighting.needs_update) {
            continue;
        }

        chunk->lighting.current_strength = std::clamp(chunk->lighting.current_strength, 0.0f, 1.0f);

        const int   radius = std::max(0, settings.search_radius_cells);
        const float fx     = std::max(0.0f, settings.falloff_horizontal);
        const float fy     = std::max(0.0f, settings.falloff_vertical);
        const auto  grad   = compute_brightness_gradient(*chunk, grid, radius, fx, fy);

        compute_use_shadow_data_for_chunk(settings,
                                          grid,
                                          grad,
                                          map_light_direction,
                                          map_light_opacity,
                                          *chunk);

        chunk->lighting.needs_update = false;
    }
}

void LightMap::capture_runtime_brightness(SDL_Renderer* renderer) {
    std::scoped_lock lock(mutex_);
    if (chunk_lighting_suspended_flag()) {
        return;
    }
    if (!renderer || !assets_) {
        return;
    }
    if (screen_width_ <= 0 || screen_height_ <= 0) {
        return;
    }

    for (world::Chunk* chunk : active_chunks()) {
        if (!chunk) {
            continue;
        }
        chunk->lighting.has_runtime_average = false;
        chunk->lighting.runtime_average_strength = chunk->lighting.current_strength;
    }

    const SDL_Rect screen_rect{0, 0, screen_width_, screen_height_};
    const int      pitch       = screen_rect.w * 4;

    std::vector<std::uint8_t> pixels;
    try {
        pixels.resize(static_cast<std::size_t>(pitch) * static_cast<std::size_t>(screen_rect.h));
    } catch (const std::bad_alloc&) {
        vibble::log::warn("[LightMap] Unable to allocate runtime brightness buffer");
        return;
    }

    if (SDL_RenderReadPixels(renderer,
                              &screen_rect,
                              SDL_PIXELFORMAT_RGBA32,
                              pixels.data(),
                              pitch) != 0) {
        vibble::log::warn(std::string{"[LightMap] Failed to capture runtime brightness: "} + SDL_GetError());
        return;
    }

    const camera& cam = assets_->getView();
    SDL_Rect      world_view = world_rect_from_screen(cam, screen_rect);

    for (world::Chunk* chunk : active_chunks()) {
        if (!chunk) {
            continue;
        }
        if (!intersects(chunk->world_bounds, world_view)) {
            continue;
        }

        SDL_Point top_left =
            cam.map_to_screen({chunk->world_bounds.x, chunk->world_bounds.y});
        SDL_Point bottom_right = cam.map_to_screen({chunk->world_bounds.x + chunk->world_bounds.w,
                                                    chunk->world_bounds.y + chunk->world_bounds.h});

        SDL_Rect screen_chunk{};
        screen_chunk.x = std::min(top_left.x, bottom_right.x);
        screen_chunk.y = std::min(top_left.y, bottom_right.y);
        screen_chunk.w = std::abs(bottom_right.x - top_left.x);
        screen_chunk.h = std::abs(bottom_right.y - top_left.y);

        SDL_Rect overlap{};
        if (SDL_IntersectRect(&screen_chunk, &screen_rect, &overlap) == SDL_FALSE) {
            continue;
        }
        if (overlap.w <= 0 || overlap.h <= 0) {
            continue;
        }

        const float luminance = average_luminance_for_region(pixels, screen_rect.w, screen_rect.h, overlap);
        chunk->lighting.runtime_average_strength = std::clamp(luminance, 0.0f, 1.0f);
        chunk->lighting.has_runtime_average      = true;
        chunk->lighting.needs_update             = true;
        chunk->lighting.is_active                = true;
    }
}

float LightMap::sample_brightness(int world_x,
                                  int world_y,
                                  float static_weight,
                                  float dynamic_weight) const {
    std::scoped_lock lock(mutex_);
    (void)dynamic_weight;
    world::Chunk* chunk = ensure_chunk_from_world(SDL_Point{world_x, world_y});
    if (!chunk) {
        vibble::log::warn("[LightMap] sample_brightness missing chunk for world point (" +
                          std::to_string(world_x) + ", " + std::to_string(world_y) + ")");
        return 1.0f;
    }
    const float weight           = std::clamp(static_weight, 0.0f, 1.0f);
    const float brightness       = std::clamp(chunk->lighting.current_strength, 0.0f, 1.0f);
    return std::clamp(brightness * weight, 0.0f, 1.0f);
}

float LightMap::sample_brightness_bilinear(float world_x,
                                           float world_y,
                                           float static_weight,
                                           float dynamic_weight) const {
    std::scoped_lock lock(mutex_);
    return sample_brightness(static_cast<int>(std::lround(world_x)),
                             static_cast<int>(std::lround(world_y)),
                             static_weight,
                             dynamic_weight);
}

void LightMap::present_static_previews(SDL_Renderer* renderer) const {
    std::scoped_lock lock(mutex_);
    if (!renderer || !assets_) {
        return;
    }

    world::Grid& grid = assets_->world_grid();
    std::vector<world::Chunk*> chunks = grid.all_chunks();
    if (chunks.empty()) {
        return;
    }

    SDL_SetRenderTarget(renderer, nullptr);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    constexpr int kPadding = 8;
    int           x        = kPadding;
    int           y        = kPadding;
    int           column_w = 0;

    for (world::Chunk* chunk : chunks) {
        if (!chunk) {
            continue;
        }

        const int chunk_w = std::max(1, chunk->world_bounds.w);
        const int chunk_h = std::max(1, chunk->world_bounds.h);

        if (y + chunk_h > screen_height_) {
            y         = kPadding;
            x        += column_w + kPadding;
            column_w  = 0;
        }
        if (x + chunk_w > screen_width_) {
            break;
        }

        column_w = std::max(column_w, chunk_w);

        const float brightness = std::clamp(chunk->lighting.current_strength, 0.0f, 1.0f);
        const Uint8 alpha      = clamp_alpha(1.0f - brightness);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, alpha);
        SDL_Rect rect{x, y, chunk_w, chunk_h};
        SDL_RenderFillRect(renderer, &rect);

        y += chunk_h + kPadding;
    }

    SDL_RenderPresent(renderer);
}

void LightMap::render_visible_chunks(SDL_Renderer* renderer, const SDL_Rect& view_rect) const {
    (void)renderer;
    (void)view_rect;
}

void LightMap::render_visible_chunks(SDL_Renderer* renderer,
                                     const SDL_Rect& view_rect,
                                     float alpha_multiplier,
                                     const SDL_Color& color_mod) const {
    std::scoped_lock lock(mutex_);
    (void)renderer;
    (void)view_rect;
    (void)alpha_multiplier;
    (void)color_mod;
}

void LightMap::render_chunk_preview(SDL_Renderer* renderer, const SDL_Rect& view_rect) const {
    std::scoped_lock lock(mutex_);
    if (!renderer || !assets_) {
        return;
    }

    const camera& cam = assets_->getView();
    SDL_Rect      world_view = world_rect_from_screen(cam, view_rect);

    struct ChunkPreviewRender {
        world::Chunk* chunk = nullptr;
        SDL_Rect      world_rect_on_screen{0, 0, 0, 0};
    };

    std::vector<ChunkPreviewRender> preview_chunks;
    preview_chunks.reserve(active_chunks().size());

    for (world::Chunk* chunk : active_chunks()) {
        if (!chunk) {
            continue;
        }
        if (!intersects(chunk->world_bounds, world_view)) {
            continue;
        }

        SDL_Point top_left = cam.map_to_screen({chunk->world_bounds.x, chunk->world_bounds.y});
        SDL_Point bottom_right =
            cam.map_to_screen({chunk->world_bounds.x + chunk->world_bounds.w,
                               chunk->world_bounds.y + chunk->world_bounds.h});

        SDL_Rect world_rect_screen{};
        world_rect_screen.x = std::min(top_left.x, bottom_right.x);
        world_rect_screen.y = std::min(top_left.y, bottom_right.y);
        world_rect_screen.w = std::abs(bottom_right.x - top_left.x);
        world_rect_screen.h = std::abs(bottom_right.y - top_left.y);
        if (world_rect_screen.w <= 0 || world_rect_screen.h <= 0) {
            continue;
        }

        preview_chunks.push_back(ChunkPreviewRender{chunk, world_rect_screen});
    }

    if (preview_chunks.empty()) {
        return;
    }

    SDL_BlendMode previous_mode = SDL_BLENDMODE_BLEND;
    if (SDL_GetRenderDrawBlendMode(renderer, &previous_mode) != 0) {
        previous_mode = SDL_BLENDMODE_BLEND;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    const DMLabelStyle& label_style = DMStyles::Label();

    SDL_SetRenderDrawColor(renderer, 0, 200, 255, 200);

    for (const ChunkPreviewRender& entry : preview_chunks) {
        const SDL_Rect& rect = entry.world_rect_on_screen;
        SDL_RenderDrawRect(renderer, &rect);

        const int mid_world_x = entry.chunk->world_bounds.x + entry.chunk->world_bounds.w / 2;
        const int mid_world_y = entry.chunk->world_bounds.y + entry.chunk->world_bounds.h / 2;

        SDL_Point mid_top = cam.map_to_screen({mid_world_x, entry.chunk->world_bounds.y});
        SDL_Point mid_bottom =
            cam.map_to_screen({mid_world_x, entry.chunk->world_bounds.y + entry.chunk->world_bounds.h});
        SDL_Point mid_left = cam.map_to_screen({entry.chunk->world_bounds.x, mid_world_y});
        SDL_Point mid_right =
            cam.map_to_screen({entry.chunk->world_bounds.x + entry.chunk->world_bounds.w, mid_world_y});

        SDL_RenderDrawLine(renderer, mid_left.x, mid_left.y, mid_right.x, mid_right.y);
        SDL_RenderDrawLine(renderer, mid_top.x, mid_top.y, mid_bottom.x, mid_bottom.y);

        std::ostringstream label_stream;
        label_stream << "Chunk (" << entry.chunk->i << ", " << entry.chunk->j << ") "
                     << "World: [" << entry.chunk->world_bounds.x << ", " << entry.chunk->world_bounds.y << "] "
                     << "Size: " << entry.chunk->world_bounds.w << "x" << entry.chunk->world_bounds.h;
        const std::string label_text = label_stream.str();

        SDL_Point text_size = DMFontCache::instance().measure_text(label_style, label_text);
        const int padding_x = 6;
        const int padding_y = 4;
        SDL_Rect label_bg{rect.x + 4,
                          rect.y + 4,
                          text_size.x + padding_x * 2,
                          text_size.y + padding_y * 2};

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
        SDL_RenderFillRect(renderer, &label_bg);

        const int text_x = label_bg.x + padding_x;
        const int text_y = label_bg.y + padding_y;
        DMFontCache::instance().draw_text(renderer, label_style, label_text, text_x, text_y, nullptr);

        SDL_SetRenderDrawColor(renderer, 0, 200, 255, 200);
    }

    SDL_SetRenderDrawBlendMode(renderer, previous_mode);
}

void LightMap::mark_region_dirty(const SDL_Rect& screen_rect) {
    std::scoped_lock lock(mutex_);
    if (!assets_) {
        return;
    }
    const camera& cam     = assets_->getView();
    SDL_Rect      world_r = world_rect_from_screen(cam, screen_rect);
    for (world::Chunk* chunk : active_chunks()) {
        if (chunk && intersects(chunk->world_bounds, world_r)) {
            chunk->lighting.needs_update = true;
        }
    }
}

void LightMap::mark_asset_lights_dirty(const Asset* asset) {
    std::scoped_lock lock(mutex_);
    if (!asset) {
        return;
    }
    if (world::Chunk* chunk = ensure_chunk_from_world(asset->pos)) {
        chunk->lighting.needs_update = true;
    } else {
        vibble::log::warn("[LightMap] mark_asset_lights_dirty missing chunk for asset at (" +
                          std::to_string(asset->pos.x) + ", " + std::to_string(asset->pos.y) + ")");
    }
}

void LightMap::mark_static_cache_dirty() {
    std::scoped_lock lock(mutex_);
    if (!assets_) {
        return;
    }
    for (world::Chunk* chunk : active_chunks()) {
        if (chunk) {
            chunk->lighting_dirty = true;
            chunk->lighting.needs_update = true;
        }
    }
}

const std::vector<world::Chunk*>& LightMap::active_chunks() const {
    static const std::vector<world::Chunk*> kEmpty{};
    if (!assets_) {
        return kEmpty;
    }
    return assets_->world_grid().active_chunks();
}

world::Chunk* LightMap::chunk_from_world(SDL_Point world_px) const {
    std::scoped_lock lock(mutex_);
    if (!assets_) {
        return nullptr;
    }
    return assets_->world_grid().chunk_from_world(world_px);
}

world::Chunk* LightMap::ensure_chunk_from_world(SDL_Point world_px) const {
    std::scoped_lock lock(mutex_);
    if (!assets_) {
        return nullptr;
    }
    return assets_->world_grid().ensure_chunk_from_world(world_px);
}

int LightMap::chunk_count() const {
    std::scoped_lock lock(mutex_);
    return static_cast<int>(active_chunks().size());
}

int LightMap::chunk_columns() const {
    std::scoped_lock lock(mutex_);
    const int count = chunk_count();
    if (count <= 0) {
        return 0;
    }
    const int columns = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
    return std::max(1, columns);
}

int LightMap::chunk_rows() const {
    std::scoped_lock lock(mutex_);
    const int count = chunk_count();
    if (count <= 0) {
        return 0;
    }
    const int columns = chunk_columns();
    return std::max(1, (count + columns - 1) / columns);
}

const world::Chunk* LightMap::chunk_at(int index) const {
    std::scoped_lock lock(mutex_);
    const auto& chunks = active_chunks();
    if (index < 0 || static_cast<std::size_t>(index) >= chunks.size()) {
        return nullptr;
    }
    return chunks[static_cast<std::size_t>(index)];
}

SDL_Rect LightMap::chunk_bounds(int index) const {
    std::scoped_lock lock(mutex_);
    if (const world::Chunk* chunk = chunk_at(index)) {
        return chunk->world_bounds;
    }
    return SDL_Rect{0, 0, 0, 0};
}

std::optional<world::Chunk::ChunkShadowParameters> LightMap::get_shadow_data(SDL_FPoint world_or_screen_pos) const {
    std::scoped_lock lock(mutex_);
    world::Chunk* chunk = nullptr;
    if (assets_) {
        chunk = chunk_from_world(SDL_Point{static_cast<int>(std::lround(world_or_screen_pos.x)),
                                           static_cast<int>(std::lround(world_or_screen_pos.y))});
        if (!chunk) {
            const camera& cam = assets_->getView();
            SDL_Point from_screen = cam.screen_to_map({static_cast<int>(std::lround(world_or_screen_pos.x)),
                                                       static_cast<int>(std::lround(world_or_screen_pos.y))});
            chunk = chunk_from_world(from_screen);
        }
    }
    if (!chunk) return std::nullopt;
    return chunk->shadow;
}


