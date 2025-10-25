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
#include "lighting/PreloadInputs.hpp"
#include "render/global_light_source.hpp"
#include "world/grid.hpp"

namespace world {
int floor_div(int value, int step);
}

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

void Chunk::ChunkShadowHistory::push(const ChunkShadowParameters& sample, int fade_frames) {
    samples[static_cast<std::size_t>(cursor)] = sample;
    cursor = (cursor + 1) % kMaxHistoryLength;
    if (count < kMaxHistoryLength) {
        ++count;
    }

    fade_frames = std::clamp(fade_frames, 0, kMaxHistoryLength - 1);

    if (count <= 0) {
        blended = ChunkShadowParameters{};
        return;
    }

    if (fade_frames <= 0 || count == 1) {
        blended = sample;
        return;
    }

    ChunkShadowParameters accum{};
    float                 total_weight = 0.0f;
    const int             last_index   = (cursor - 1 + kMaxHistoryLength) % kMaxHistoryLength;
    const int             max_samples = std::min(count, fade_frames + 1);

    for (int i = 0; i < max_samples; ++i) {
        const int idx = (last_index - i + kMaxHistoryLength) % kMaxHistoryLength;
        const auto& entry = samples[static_cast<std::size_t>(idx)];

        const int   age = i;
        float       weight = 0.0f;
        if (fade_frames > 0) {
            weight = static_cast<float>(fade_frames - age) / static_cast<float>(fade_frames);
        }

        if (weight <= 0.0f) {
            continue;
        }

        total_weight += weight;
        accum.opacity += entry.opacity * weight;
        accum.scale += entry.scale * weight;
        accum.offset_x_percent += entry.offset_x_percent * weight;
        accum.offset_y_percent += entry.offset_y_percent * weight;
        accum.offset_x_px += entry.offset_x_px * weight;
        accum.offset_y_px += entry.offset_y_px * weight;
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
    blended.offset_x_px                = accum.offset_x_px * inv_total;
    blended.offset_y_px                = accum.offset_y_px * inv_total;
    blended.parallax_intensity_percent = accum.parallax_intensity_percent * inv_total;
}

void Chunk::LightingChunk::releaseLightingArtifacts() {
    lighting_dirty                    = true;
    has_dynamic_overlay               = false;
    lighting                          = ChunkLightingState{};
    lighting.current_strength         = 1.0f;
    lighting.runtime_average_strength = 1.0f;
    lighting.has_runtime_average      = false;
    lighting.is_active                = false;
    lighting.needs_update             = true;
    shadow_history.reset();
    shadow = ChunkShadowParameters{};
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
    for (auto& cell : lighting_chunks_) {
        cell.releaseLightingArtifacts();
    }
}

Chunk::LightingChunk* Chunk::lighting_chunk_at(int local_i, int local_j) {
    if (local_i < 0 || local_j < 0) {
        return nullptr;
    }
    if (local_i >= lighting_columns_ || local_j >= lighting_rows_) {
        return nullptr;
    }
    const std::size_t index = static_cast<std::size_t>(local_j) * static_cast<std::size_t>(lighting_columns_) +
                              static_cast<std::size_t>(local_i);
    if (index >= lighting_chunks_.size()) {
        return nullptr;
    }
    return &lighting_chunks_[index];
}

const Chunk::LightingChunk* Chunk::lighting_chunk_at(int local_i, int local_j) const {
    return const_cast<Chunk*>(this)->lighting_chunk_at(local_i, local_j);
}

Chunk::LightingChunk* Chunk::lighting_chunk_from_world(SDL_Point world_px) {
    if (!SDL_PointInRect(&world_px, &world_bounds)) {
        return nullptr;
    }
    if (lighting_step_ <= 0) {
        return nullptr;
    }
    const int local_x = world_px.x - world_bounds.x;
    const int local_y = world_px.y - world_bounds.y;
    const int local_i = std::clamp(local_x / lighting_step_, 0, std::max(0, lighting_columns_ - 1));
    const int local_j = std::clamp(local_y / lighting_step_, 0, std::max(0, lighting_rows_ - 1));
    return lighting_chunk_at(local_i, local_j);
}

const Chunk::LightingChunk* Chunk::lighting_chunk_from_world(SDL_Point world_px) const {
    return const_cast<Chunk*>(this)->lighting_chunk_from_world(world_px);
}

void Chunk::rebuild_lighting_chunks() {
    lighting_chunks_.clear();

    const int subdivisions = 1 << std::min(2, std::max(0, r_chunk));
    lighting_columns_       = subdivisions;
    lighting_rows_          = subdivisions;
    lighting_resolution_    = std::max(0, r_chunk - 2);
    lighting_step_          = 1 << lighting_resolution_;

    if (lighting_step_ <= 0) {
        lighting_step_ = 1;
    }

    lighting_chunks_.reserve(static_cast<std::size_t>(lighting_columns_ * lighting_rows_));

    for (int row = 0; row < lighting_rows_; ++row) {
        for (int col = 0; col < lighting_columns_; ++col) {
            SDL_Rect cell_bounds{};
            cell_bounds.x = world_bounds.x + col * lighting_step_;
            cell_bounds.y = world_bounds.y + row * lighting_step_;
            cell_bounds.w = lighting_step_;
            cell_bounds.h = lighting_step_;

            const int global_i = i * lighting_columns_ + col;
            const int global_j = j * lighting_rows_ + row;

            lighting_chunks_.emplace_back(this,
                                          col,
                                          row,
                                          global_i,
                                          global_j,
                                          lighting_resolution_,
                                          lighting_step_,
                                          cell_bounds);
            lighting_chunks_.back().releaseLightingArtifacts();
        }
    }

    update_aggregate_from_lighting_chunks();
}

void Chunk::update_aggregate_from_lighting_chunks() {
    if (lighting_chunks_.empty()) {
        lighting = ChunkLightingState{};
        shadow   = ChunkShadowParameters{};
        shadow_history.reset();
        return;
    }

    double accum_strength = 0.0;
    double accum_runtime  = 0.0;
    bool   any_active     = false;
    bool   any_needs      = false;
    bool   any_runtime    = false;

    for (auto& cell : lighting_chunks_) {
        accum_strength += static_cast<double>(cell.lighting.current_strength);
        accum_runtime  += static_cast<double>(cell.lighting.runtime_average_strength);
        any_active     = any_active || cell.lighting.is_active;
        any_needs      = any_needs || cell.lighting.needs_update;
        any_runtime    = any_runtime || cell.lighting.has_runtime_average;
    }

    const double inv = 1.0 / static_cast<double>(lighting_chunks_.size());
    lighting.current_strength         = static_cast<float>(accum_strength * inv);
    lighting.runtime_average_strength = static_cast<float>(accum_runtime * inv);
    lighting.is_active                = any_active;
    lighting.needs_update             = any_needs;
    lighting.has_runtime_average      = any_runtime;

    // Use center cell's shadow parameters as representative aggregate.
    const int center_index = static_cast<int>(lighting_chunks_.size() / 2);
    shadow                  = lighting_chunks_[center_index].shadow;
    shadow_history.reset();
    shadow_history.push(shadow, 0);
}

} // namespace world

namespace {
using LightingChunk = world::Chunk::LightingChunk;

LightingChunk* find_lighting_chunk_from_global(const world::Grid& grid, int global_i, int global_j) {
    const int subdivisions = grid.lighting_subdivisions_per_chunk();
    if (subdivisions <= 0) {
        return nullptr;
    }

    const int parent_i = world::floor_div(global_i, subdivisions);
    const int parent_j = world::floor_div(global_j, subdivisions);
    world::Chunk* parent = grid.find_chunk_ij(parent_i, parent_j);
    if (!parent) {
        return nullptr;
    }

    int local_i = global_i - parent_i * subdivisions;
    int local_j = global_j - parent_j * subdivisions;

    if (local_i < 0) {
        local_i += subdivisions;
    }
    if (local_j < 0) {
        local_j += subdivisions;
    }

    return parent->lighting_chunk_at(local_i, local_j);
}

const LightingChunk* find_lighting_chunk_from_global_const(const world::Grid& grid,
                                                           int               global_i,
                                                           int               global_j) {
    return find_lighting_chunk_from_global(grid, global_i, global_j);
}

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

std::pair<float, float> compute_brightness_gradient(const LightingChunk& center,
                                                    const world::Grid& grid,
                                                    int radius,
                                                    float falloff_x,
                                                    float falloff_y) {
    if (radius <= 0) return {0.0f, 0.0f};
    const int subdivisions = grid.lighting_subdivisions_per_chunk();
    if (subdivisions <= 0) {
        return {0.0f, 0.0f};
    }

    const int effective_radius = radius * subdivisions;
    if (effective_radius <= 0) {
        return {0.0f, 0.0f};
    }

    const float cb = std::clamp(center.lighting.current_strength, 0.0f, 1.0f);
    float gx = 0.0f, gy = 0.0f;
    for (int dj = -effective_radius; dj <= effective_radius; ++dj) {
        for (int di = -effective_radius; di <= effective_radius; ++di) {
            if (di == 0 && dj == 0) continue;
            const int target_i = center.global_i + di;
            const int target_j = center.global_j + dj;
            const LightingChunk* n = find_lighting_chunk_from_global_const(grid, target_i, target_j);
            const float nb = n ? std::clamp(n->lighting.current_strength, 0.0f, 1.0f) : cb;
            const float db = nb - cb;
            const float dx_chunks = static_cast<float>(di) / static_cast<float>(subdivisions);
            const float dy_chunks = static_cast<float>(dj) / static_cast<float>(subdivisions);
            const float dist = std::max(1e-3f, std::sqrt(dx_chunks * dx_chunks + dy_chunks * dy_chunks));
            const float wx = falloff_x / dist;
            const float wy = falloff_y / dist;
            gx += db * (dx_chunks / dist) * wx;
            gy += db * (dy_chunks / dist) * wy;
        }
    }
    return {gx, gy};
}

// Compute average brightness in front of the chunk (negative j direction),
// adjusted by anisotropic horizontal/vertical falloff. Returns [0,1].
// Compute weighted averages of light strength in-front (negative j) and behind (positive j).
static std::pair<float, float> compute_directional_average_strengths(const LightMap::ShadowSettings& settings,
                                                                     const world::Grid& grid,
                                                                     const LightingChunk& center) {
    const int   R  = std::max(0, settings.search_radius_cells);
    const float fh = std::max(0.0f, settings.falloff_horizontal);
    const float fv = std::max(0.0f, settings.falloff_vertical);
    const int   subdivisions = grid.lighting_subdivisions_per_chunk();
    if (subdivisions <= 0) {
        const float base = std::clamp(center.lighting.current_strength, 0.0f, 1.0f);
        return {base, base};
    }

    const int effective_radius = R * subdivisions;

    auto sample_dir = [&](int j_begin, int j_end) -> float {
        double accum_w = 0.0;
        double accum_v = 0.0;
        const int step = (j_begin <= j_end) ? 1 : -1;
        for (int dj = j_begin; dj != j_end + step; dj += step) {
            for (int di = -effective_radius; di <= effective_radius; ++di) {
                if (dj == 0) continue;
                const int target_i = center.global_i + di;
                const int target_j = center.global_j + dj;
                const LightingChunk* n = find_lighting_chunk_from_global_const(grid, target_i, target_j);
                if (!n) continue;
                const float sx = std::abs(static_cast<float>(di)) / static_cast<float>(subdivisions);
                const float sy = std::abs(static_cast<float>(dj)) / static_cast<float>(subdivisions);
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

    const float base_strength = std::clamp(center.lighting.current_strength, 0.0f, 1.0f);
    const float front_avg  = (R > 0) ? sample_dir(-effective_radius, -1) : base_strength;
    const float behind_avg = (R > 0) ? sample_dir(1,  effective_radius)  : base_strength;
    return {front_avg, behind_avg};
}

static void compute_use_shadow_data_for_chunk(const LightMap::ShadowSettings& settings,
                                              const world::Grid& grid,
                                              float scene_average_strength,
                                              const std::pair<float, float>& grad,
                                              const std::optional<SDL_FPoint>& map_light_direction,
                                              float map_light_opacity,
                                              bool prefer_fast_blend,
                                              LightingChunk& chunk) {
    world::Chunk::ChunkShadowParameters sample{};

    const auto [front_avg, behind_avg] =
        compute_directional_average_strengths(settings, grid, chunk);

    const float sensitivity = std::clamp(settings.opacity_sensitivity_percent, 0.0f, 100.0f) / 100.0f;
    const float blended_avg =
        std::clamp(front_avg * (1.0f - sensitivity) + scene_average_strength * sensitivity, 0.0f, 1.0f);
    sample.opacity = std::clamp(1.0f - blended_avg, 0.0f, 1.0f);

    const float total_light = front_avg + behind_avg;
    float       front_balance = 0.5f;
    if (total_light > 1e-5f) {
        front_balance = std::clamp(front_avg / total_light, 0.0f, 1.0f);
    }
    const int min_p = std::clamp(settings.min_scale_percent, 10, 500);
    const int max_p = std::clamp(settings.max_scale_percent, 10, 500);
    const float scale_percent = std::clamp(static_cast<float>(min_p) +
                                               (static_cast<float>(max_p - min_p) * front_balance),
                                           static_cast<float>(min_p),
                                           static_cast<float>(max_p));
    sample.scale = std::max(0.0f, scale_percent / 100.0f);

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
    const float percent_to_px_x = (chunk_w > 1e-3f) ? (chunk_w / 100.0f) : 0.0f;
    const float percent_to_px_y = (chunk_h > 1e-3f) ? (chunk_h / 100.0f) : 0.0f;
    const float raw_max_x_percent = (chunk_w > 1e-3f)
                                        ? (settings.max_offset_x_px / chunk_w) * 100.0f
                                        : 0.0f;
    const float raw_max_y_percent = (chunk_h > 1e-3f)
                                        ? (settings.max_offset_y_px / chunk_h) * 100.0f
                                        : 0.0f;
    const float safe_max_x_percent = std::clamp(raw_max_x_percent, 0.0f, 100.0f);
    const float safe_max_y_percent = std::clamp(raw_max_y_percent, 0.0f, 100.0f);

    // Map-light directional adjustment: push away from map-light direction with
    // strength that peaks when the light is furthest above/below and fades to 0
    // when directly left or right. The influence also fades smoothly as the
    // map-light opacity drops, hitting zero once the opacity is 100/255 or
    // lower.
    if (map_light_direction) {
        const SDL_FPoint dir = *map_light_direction;
        const float vertical_influence   = std::clamp(std::abs(dir.y), 0.0f, 1.0f);
        const float direction_factor     = std::clamp(settings.map_light_dir_offset_strength, 0.0f, 1.0f);
        constexpr float kMinOpacityForDirection = 100.0f / 255.0f;
        const float opacity_visibility         = smoothstep(kMinOpacityForDirection, 1.0f, map_light_opacity);
        const float opacity_scale              = opacity_visibility * opacity_visibility;
        const float dir_push = vertical_influence * direction_factor * opacity_scale * 100.0f;
        if (dir_push > 1e-4f) {
            px += -dir.x * dir_push;
            py += -dir.y * dir_push;
        }
    }

    if (safe_max_x_percent <= 0.0f) {
        px = 0.0f;
    }
    if (safe_max_y_percent <= 0.0f) {
        py = 0.0f;
    }

    const float clamped_px = std::clamp(px, -safe_max_x_percent, safe_max_x_percent);
    const float clamped_py = std::clamp(py, -safe_max_y_percent, safe_max_y_percent);
    sample.offset_x_percent = std::clamp(clamped_px, -100.0f, 100.0f);
    sample.offset_y_percent = std::clamp(clamped_py, -100.0f, 100.0f);
    sample.offset_x_px      = sample.offset_x_percent * percent_to_px_x;
    sample.offset_y_px      = sample.offset_y_percent * percent_to_px_y;

    sample.parallax_intensity_percent = std::clamp(settings.parallax_percent, 0.0f, 100.0f);

    const world::Chunk::ChunkShadowParameters previous = chunk.shadow_history.value();
    const float delta_opacity = std::abs(sample.opacity - previous.opacity);
    const float delta_scale   = std::abs(sample.scale - previous.scale);
    const float delta_offset  = std::max(std::abs(sample.offset_x_percent - previous.offset_x_percent),
                                         std::abs(sample.offset_y_percent - previous.offset_y_percent));

    int blend_frames = std::clamp(settings.frame_blend_falloff_frames, 0, world::Chunk::ChunkShadowHistory::kMaxHistoryLength - 1);
    const bool significant_change = (delta_opacity > 0.1f) || (delta_scale > 0.1f) || (delta_offset > 5.0f);
    if (prefer_fast_blend || significant_change) {
        blend_frames = std::min(blend_frames, 12);
    }

    chunk.shadow_history.push(sample, blend_frames);

    world::Chunk::ChunkShadowParameters blended = chunk.shadow_history.value();
    const auto clamp_percent = [](float value, float limit) {
        const float clamped_limit = std::max(0.0f, limit);
        if (clamped_limit <= 0.0f) {
            return 0.0f;
        }
        return std::clamp(value, -clamped_limit, clamped_limit);
    };
    blended.offset_x_percent = std::clamp(clamp_percent(blended.offset_x_percent, safe_max_x_percent), -100.0f, 100.0f);
    blended.offset_y_percent = std::clamp(clamp_percent(blended.offset_y_percent, safe_max_y_percent), -100.0f, 100.0f);
    blended.offset_x_px      = blended.offset_x_percent * percent_to_px_x;
    blended.offset_y_px      = blended.offset_y_percent * percent_to_px_y;

    chunk.shadow = blended;
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
    settings.opacity_sensitivity_percent =
        std::clamp(sanitized.opacity_sensitivity_percent, 0.0f, 100.0f);
    settings.min_scale_percent   = sanitized.virtual_light_map.min_scale_percent;
    settings.max_scale_percent   = sanitized.virtual_light_map.max_scale_percent;
    settings.map_light_dir_offset_strength =
        std::clamp(sanitized.virtual_light_map.map_light_dir_offset_strength, 0.0f, 1.0f);
    settings.parallax_percent = std::clamp(sanitized.virtual_light_map.parallax_percent, 0.0f, 100.0f);
    settings.frame_blend_falloff_frames = sanitized.frame_blend_falloff_frames;

    return settings;
}

void LightMap::invalidate_scene_light_cache() {
    scene_light_cache_valid_ = false;
    scene_light_sum_         = 0.0;
    scene_light_count_       = 0;
    cached_chunk_count_      = 0;
}

void LightMap::rebuild_scene_light_cache(const std::vector<world::Chunk*>& chunks) {
    scene_light_sum_        = 0.0;
    scene_light_count_      = 0;
    cached_chunk_count_     = static_cast<int>(chunks.size());
    for (world::Chunk* chunk : chunks) {
        if (!chunk) {
            continue;
        }
        const int cell_count = static_cast<int>(chunk->lighting_chunks().size());
        scene_light_count_ += cell_count;
        scene_light_sum_ += static_cast<double>(std::clamp(chunk->lighting.current_strength, 0.0f, 1.0f)) *
                            static_cast<double>(cell_count);
    }
    scene_light_cache_valid_ = true;
}

void LightMap::rebuild(SDL_Renderer* /*renderer*/) {
    std::scoped_lock lock(mutex_);
    if (!assets_) {
        return;
    }
    invalidate_scene_light_cache();
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
    int total_cells = 0;
    for (const world::Chunk* chunk : chunks) {
        if (!chunk) {
            continue;
        }
        min_i = std::min(min_i, chunk->i);
        max_i = std::max(max_i, chunk->i);
        min_j = std::min(min_j, chunk->j);
        max_j = std::max(max_j, chunk->j);
        total_cells += static_cast<int>(chunk->lighting_chunks().size());
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

    bool map_direction_changed = false;
    if (map_light_direction) {
        const SDL_FPoint dir = *map_light_direction;
        if (!last_map_light_direction_valid_ ||
            std::abs(dir.x - last_map_light_direction_.x) > 1e-4f ||
            std::abs(dir.y - last_map_light_direction_.y) > 1e-4f) {
            map_direction_changed = true;
        }
        last_map_light_direction_        = dir;
        last_map_light_direction_valid_ = true;
    } else if (last_map_light_direction_valid_) {
        map_direction_changed            = true;
        last_map_light_direction_valid_ = false;
        last_map_light_direction_        = SDL_FPoint{0.0f, 0.0f};
    }

    if (!scene_light_cache_valid_ || cached_chunk_count_ != static_cast<int>(chunks.size()) ||
        scene_light_count_ != total_cells) {
        rebuild_scene_light_cache(chunks);
    }

    const int   radius = std::max(0, settings.search_radius_cells);
    const float fx     = std::max(0.0f, settings.falloff_horizontal);
    const float fy     = std::max(0.0f, settings.falloff_vertical);

    struct DirtyCellInfo {
        world::Chunk::LightingChunk* cell            = nullptr;
        bool                         runtime_changed = false;
    };

    std::vector<DirtyCellInfo> dirty_cells;

    for (world::Chunk* chunk : update_set) {
        if (!chunk) {
            continue;
        }

        dirty_cells.clear();
        dirty_cells.reserve(chunk->lighting_chunks().size());

        double chunk_strength_delta = 0.0;

        const bool chunk_dirty = chunk->lighting_dirty || chunk->lighting.needs_update || map_opacity_changed ||
                                 map_direction_changed;

        for (auto& cell : chunk->lighting_chunks()) {
            const float prev_strength = std::clamp(cell.lighting.current_strength, 0.0f, 1.0f);

            bool runtime_changed = false;
            if (cell.lighting.has_runtime_average) {
                const float updated_strength =
                    std::clamp(cell.lighting.runtime_average_strength, 0.0f, 1.0f);
                runtime_changed                          = (std::abs(updated_strength - prev_strength) > 1e-4f);
                cell.lighting.current_strength           = updated_strength;
                cell.lighting.runtime_average_strength   = updated_strength;
                cell.lighting.has_runtime_average        = false;
                chunk_strength_delta += static_cast<double>(updated_strength - prev_strength);
            }

            cell.lighting.current_strength = std::clamp(cell.lighting.current_strength, 0.0f, 1.0f);

            const bool cell_dirty = runtime_changed || chunk_dirty || cell.lighting.needs_update;
            cell.lighting.is_active = cell_dirty;

            if (cell_dirty) {
                dirty_cells.push_back(DirtyCellInfo{&cell, runtime_changed});
            }
        }

        if (dirty_cells.empty() && std::abs(chunk_strength_delta) <= 1e-6) {
            chunk->lighting_dirty        = false;
            chunk->lighting.needs_update = false;
            chunk->lighting.is_active    = false;
            continue;
        }

        const double scene_sum_with_chunk = scene_light_sum_ + chunk_strength_delta;
        float        scene_average_strength = 1.0f;
        if (scene_light_count_ > 0) {
            scene_average_strength = static_cast<float>(scene_sum_with_chunk /
                                                        static_cast<double>(scene_light_count_));
        }

        const bool chunk_prefers_fast_blend = chunk_dirty;

        for (const DirtyCellInfo& info : dirty_cells) {
            auto& cell = *info.cell;
            const auto grad = compute_brightness_gradient(cell, grid, radius, fx, fy);
            const bool prefer_fast_blend = chunk_prefers_fast_blend || info.runtime_changed;

            compute_use_shadow_data_for_chunk(settings,
                                              grid,
                                              scene_average_strength,
                                              grad,
                                              map_light_direction,
                                              map_light_opacity,
                                              prefer_fast_blend,
                                              cell);

            cell.lighting.needs_update = false;
        }

        if (!dirty_cells.empty() || std::abs(chunk_strength_delta) > 1e-6) {
            chunk->update_aggregate_from_lighting_chunks();
        }

        chunk->lighting_dirty      = false;
        chunk->lighting.needs_update = false;

        scene_light_sum_ = scene_sum_with_chunk;
        scene_light_cache_valid_ = true;
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
        for (auto& cell : chunk->lighting_chunks()) {
            cell.lighting.has_runtime_average      = false;
            cell.lighting.runtime_average_strength = 0.0f;
        }
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
        bool any_cell_updated = false;
        for (auto& cell : chunk->lighting_chunks()) {
            if (!intersects(cell.world_bounds, world_view)) {
                continue;
            }

            SDL_Point top_left =
                cam.map_to_screen({cell.world_bounds.x, cell.world_bounds.y});
            SDL_Point bottom_right = cam.map_to_screen({cell.world_bounds.x + cell.world_bounds.w,
                                                        cell.world_bounds.y + cell.world_bounds.h});

            SDL_Rect screen_cell{};
            screen_cell.x = std::min(top_left.x, bottom_right.x);
            screen_cell.y = std::min(top_left.y, bottom_right.y);
            screen_cell.w = std::abs(bottom_right.x - top_left.x);
            screen_cell.h = std::abs(bottom_right.y - top_left.y);

            SDL_Rect overlap{};
            if (SDL_IntersectRect(&screen_cell, &screen_rect, &overlap) == SDL_FALSE) {
                continue;
            }
            if (overlap.w <= 0 || overlap.h <= 0) {
                continue;
            }

            const float luminance = average_luminance_for_region(pixels, screen_rect.w, screen_rect.h, overlap);
            cell.lighting.runtime_average_strength = std::clamp(luminance, 0.0f, 1.0f);
            cell.lighting.has_runtime_average      = true;
            cell.lighting.needs_update             = true;
            cell.lighting.is_active                = true;
            any_cell_updated                       = true;
        }

        if (any_cell_updated) {
            chunk->update_aggregate_from_lighting_chunks();
        }
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
    const float weight = std::clamp(static_weight, 0.0f, 1.0f);
    const world::Chunk::LightingChunk* cell =
        chunk->lighting_chunk_from_world(SDL_Point{world_x, world_y});
    const float brightness = cell ? std::clamp(cell->lighting.current_strength, 0.0f, 1.0f)
                                  : std::clamp(chunk->lighting.current_strength, 0.0f, 1.0f);
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

LightMap::~LightMap() {
    destroy_runtime_shadow_mask();
}

SDL_Texture* LightMap::ensure_runtime_shadow_mask(SDL_Renderer* renderer) const {
    if (!renderer) {
        return nullptr;
    }

    if (runtime_shadow_mask_renderer_ && runtime_shadow_mask_renderer_ != renderer) {
        destroy_runtime_shadow_mask();
    }

    if (!runtime_shadow_mask_) {
        runtime_shadow_mask_renderer_ = renderer;
        constexpr int kMaskSize       = 256;
        runtime_shadow_mask_blend_    = lighting::PreloadInputs::computeRuntimeLightBlendMode();

        SDL_Texture* mask = SDL_CreateTexture(renderer,
                                              SDL_PIXELFORMAT_RGBA32,
                                              SDL_TEXTUREACCESS_STREAMING,
                                              kMaskSize,
                                              kMaskSize);
        if (!mask) {
            vibble::log::warn(std::string{"[LightMap] Failed to allocate runtime shadow mask: "} + SDL_GetError());
            return nullptr;
        }

        void* pixels = nullptr;
        int   pitch  = 0;
        if (SDL_LockTexture(mask, nullptr, &pixels, &pitch) != 0) {
            vibble::log::warn(std::string{"[LightMap] Failed to lock runtime shadow mask: "} + SDL_GetError());
            SDL_DestroyTexture(mask);
            return nullptr;
        }

        runtime_shadow_mask_w_ = kMaskSize;
        runtime_shadow_mask_h_ = kMaskSize;

        std::uint8_t* row      = static_cast<std::uint8_t*>(pixels);
        const float    center_x = static_cast<float>(kMaskSize - 1) * 0.5f;
        const float    center_y = static_cast<float>(kMaskSize - 1) * 0.5f;
        const float    inv_rx   = (center_x <= 0.0f) ? 0.0f : 1.0f / center_x;
        const float    inv_ry   = (center_y <= 0.0f) ? 0.0f : 1.0f / center_y;

        for (int y = 0; y < kMaskSize; ++y) {
            std::uint8_t* pixel = row + y * pitch;
            for (int x = 0; x < kMaskSize; ++x) {
                const float norm_x = (static_cast<float>(x) - center_x) * inv_rx;
                const float norm_y = (static_cast<float>(y) - center_y) * inv_ry;
                const float dist   = std::sqrt(norm_x * norm_x + norm_y * norm_y);
                const float falloff = smoothstep(0.0f, 1.0f, std::clamp(1.0f - dist, 0.0f, 1.0f));
                const Uint8 alpha   = clamp_alpha(falloff);

                pixel[x * 4 + 0] = 255;
                pixel[x * 4 + 1] = 255;
                pixel[x * 4 + 2] = 255;
                pixel[x * 4 + 3] = alpha;
            }
        }

        SDL_UnlockTexture(mask);

        runtime_shadow_mask_ = mask;
        SDL_SetTextureBlendMode(runtime_shadow_mask_, runtime_shadow_mask_blend_);
    } else {
        runtime_shadow_mask_blend_ = lighting::PreloadInputs::computeRuntimeLightBlendMode();
        SDL_SetTextureBlendMode(runtime_shadow_mask_, runtime_shadow_mask_blend_);
    }

    return runtime_shadow_mask_;
}

void LightMap::destroy_runtime_shadow_mask() const {
    if (runtime_shadow_mask_) {
        SDL_DestroyTexture(runtime_shadow_mask_);
        runtime_shadow_mask_ = nullptr;
    }
    runtime_shadow_mask_renderer_ = nullptr;
    runtime_shadow_mask_w_        = 0;
    runtime_shadow_mask_h_        = 0;
    runtime_shadow_mask_blend_    = SDL_BLENDMODE_BLEND;
    rendered_in_current_tick_     = false;
    last_render_tick_             = 0;
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
    static constexpr SDL_Color kDefaultColor{0, 0, 0, 255};
    render_visible_chunks(renderer, view_rect, 1.0f, kDefaultColor);
}

void LightMap::render_visible_chunks(SDL_Renderer* renderer,
                                     const SDL_Rect& view_rect,
                                     float alpha_multiplier,
                                     const SDL_Color& color_mod) const {
    std::scoped_lock lock(mutex_);
    if (!renderer || !assets_) {
        return;
    }
    if (view_rect.w <= 0 || view_rect.h <= 0) {
        return;
    }

    alpha_multiplier = std::clamp(alpha_multiplier, 0.0f, 1.0f);
    if (alpha_multiplier <= 1e-4f) {
        return;
    }

    const Uint32 now_ticks = SDL_GetTicks();
    if (now_ticks != last_render_tick_) {
        last_render_tick_         = now_ticks;
        rendered_in_current_tick_ = false;
    } else if (rendered_in_current_tick_) {
        return;
    }

    SDL_Texture* mask_texture = ensure_runtime_shadow_mask(renderer);
    if (!mask_texture) {
        return;
    }

    const camera& cam        = assets_->getView();
    SDL_Rect      world_view = world_rect_from_screen(cam, view_rect);

    Uint8 saved_r = 255;
    Uint8 saved_g = 255;
    Uint8 saved_b = 255;
    Uint8 saved_a = 255;
    SDL_GetTextureColorMod(mask_texture, &saved_r, &saved_g, &saved_b);
    SDL_GetTextureAlphaMod(mask_texture, &saved_a);
    SDL_BlendMode saved_blend = SDL_BLENDMODE_BLEND;
    SDL_GetTextureBlendMode(mask_texture, &saved_blend);

    SDL_SetTextureColorMod(mask_texture, color_mod.r, color_mod.g, color_mod.b);
    SDL_SetTextureBlendMode(mask_texture, runtime_shadow_mask_blend_);

    for (world::Chunk* chunk : active_chunks()) {
        if (!chunk) {
            continue;
        }

        SDL_Point top_left = cam.map_to_screen({chunk->world_bounds.x, chunk->world_bounds.y});
        SDL_Point bottom_right =
            cam.map_to_screen({chunk->world_bounds.x + chunk->world_bounds.w,
                               chunk->world_bounds.y + chunk->world_bounds.h});

        SDL_Rect screen_rect{};
        screen_rect.x = std::min(top_left.x, bottom_right.x);
        screen_rect.y = std::min(top_left.y, bottom_right.y);
        screen_rect.w = std::abs(bottom_right.x - top_left.x);
        screen_rect.h = std::abs(bottom_right.y - top_left.y);

        if (screen_rect.w <= 0 || screen_rect.h <= 0) {
            continue;
        }
        if (SDL_HasIntersection(&screen_rect, &view_rect) != SDL_TRUE) {
            continue;
        }

        float brightness = std::clamp(chunk->lighting.current_strength, 0.0f, 1.0f);
        double visible_sum = 0.0;
        int    visible_count = 0;
        for (const auto& cell : chunk->lighting_chunks()) {
            if (!intersects(cell.world_bounds, world_view)) {
                continue;
            }
            visible_sum += static_cast<double>(std::clamp(cell.lighting.current_strength, 0.0f, 1.0f));
            ++visible_count;
        }
        if (visible_count > 0) {
            brightness = static_cast<float>(visible_sum / static_cast<double>(visible_count));
        }

        float alpha = (1.0f - brightness) * alpha_multiplier;
        alpha *= std::clamp(chunk->shadow.opacity, 0.0f, 1.0f);
        alpha = std::clamp(alpha, 0.0f, 1.0f);
        if (alpha <= 1e-4f) {
            continue;
        }

        SDL_SetTextureAlphaMod(mask_texture, clamp_alpha(alpha));

        const auto& shadow = chunk->shadow;
        const float scale = (shadow.scale <= 1e-4f) ? 1.0f : shadow.scale;
        const float base_center_x = static_cast<float>(screen_rect.x) +
                                    static_cast<float>(screen_rect.w) * 0.5f;
        const float base_center_y = static_cast<float>(screen_rect.y) +
                                    static_cast<float>(screen_rect.h) * 0.5f;

        const float dest_w_f = std::max(static_cast<float>(screen_rect.w) * scale, 1.0f);
        const float dest_h_f = std::max(static_cast<float>(screen_rect.h) * scale, 1.0f);

        float offset_x = shadow.offset_x_px;
        float offset_y = shadow.offset_y_px;
        if (std::abs(offset_x) <= 1e-4f && std::abs(shadow.offset_x_percent) > 1e-4f) {
            offset_x = static_cast<float>(screen_rect.w) * (shadow.offset_x_percent / 100.0f);
        }
        if (std::abs(offset_y) <= 1e-4f && std::abs(shadow.offset_y_percent) > 1e-4f) {
            offset_y = static_cast<float>(screen_rect.h) * (shadow.offset_y_percent / 100.0f);
        }

        const float dest_x_f = base_center_x - dest_w_f * 0.5f + offset_x;
        const float dest_y_f = base_center_y - dest_h_f * 0.5f + offset_y;

        SDL_Rect dest{};
        dest.x = static_cast<int>(std::lround(dest_x_f));
        dest.y = static_cast<int>(std::lround(dest_y_f));
        dest.w = std::max(1, static_cast<int>(std::lround(dest_w_f)));
        dest.h = std::max(1, static_cast<int>(std::lround(dest_h_f)));

        if (SDL_RenderCopy(renderer, mask_texture, nullptr, &dest) != 0) {
            vibble::log::warn(std::string{"[LightMap] Failed to render shadow mask: "} + SDL_GetError());
        }
    }

    SDL_SetTextureColorMod(mask_texture, saved_r, saved_g, saved_b);
    SDL_SetTextureAlphaMod(mask_texture, saved_a);
    SDL_SetTextureBlendMode(mask_texture, saved_blend);

    rendered_in_current_tick_ = true;
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
    SDL_Point query_point{static_cast<int>(std::lround(world_or_screen_pos.x)),
                          static_cast<int>(std::lround(world_or_screen_pos.y))};
    if (const auto* cell = chunk->lighting_chunk_from_world(query_point)) {
        return cell->shadow;
    }
    return chunk->shadow;
}


