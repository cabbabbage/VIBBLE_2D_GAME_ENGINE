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
#include "render/runtime_lighting_sampler.hpp"
#include "world/grid.hpp"

namespace {

SDL_BlendMode compute_runtime_light_blend_mode() {
    // Darken the destination by modulating it against the runtime shadow mask's alpha.
    // The mask is tinted black before rendering, so the default blend mode already
    // produces the desired "multiply" style effect: dst = dst * (1 - srcAlpha).
    // Cache the result so the SDL runtime only computes the blend mode once.
    static const SDL_BlendMode kCachedBlend = SDL_BLENDMODE_BLEND;
    return kCachedBlend;
}

float blend_light_components(float static_strength, float dynamic_strength, float static_weight, float dynamic_weight) {
    const float clamped_static  = std::clamp(static_strength, 0.0f, 1.0f);
    const float clamped_dynamic = std::clamp(dynamic_strength, 0.0f, 1.0f);
    const float sw = std::max(0.0f, static_weight);
    const float dw = std::max(0.0f, dynamic_weight);
    const float weight_sum = sw + dw;
    if (weight_sum <= 1e-5f) {
        return clamped_static;
    }
    const float blended = (clamped_static * sw + clamped_dynamic * dw) / weight_sum;
    return std::clamp(blended, 0.0f, 1.0f);
}

} // namespace

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
    lighting.static_strength          = 1.0f;
    lighting.dynamic_strength         = 1.0f;
    lighting.current_strength         = 1.0f;
    lighting.runtime_average_strength = 1.0f;
    lighting.has_runtime_average      = false;
    lighting.runtime_average_color    = SDL_Color{255, 255, 255, 255};
    lighting.runtime_average_raw_intensity = 0.0f;
    lighting.runtime_average_direction     = SDL_FPoint{0.0f, 0.0f};
    lighting.has_runtime_direction         = false;
    lighting.is_active                = false;
    lighting.needs_update             = true;
    shadow_history.reset();
    shadow = ChunkShadowParameters{};
}

void Chunk::releaseLightingArtifacts() {
    lighting_dirty                     = true;
    has_dynamic_overlay                = false;
    lighting                           = ChunkLightingState{};
    lighting.static_strength           = 1.0f;
    lighting.dynamic_strength          = 1.0f;
    lighting.current_strength          = 1.0f;
    lighting.runtime_average_strength  = 1.0f;
    lighting.has_runtime_average       = false;
    lighting.runtime_average_color     = SDL_Color{255, 255, 255, 255};
    lighting.runtime_average_raw_intensity = 0.0f;
    lighting.runtime_average_direction     = SDL_FPoint{0.0f, 0.0f};
    lighting.has_runtime_direction         = false;
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

    double accum_strength   = 0.0;
    double accum_runtime    = 0.0;
    double accum_static     = 0.0;
    double accum_dynamic    = 0.0;
    double accum_color_r    = 0.0;
    double accum_color_g    = 0.0;
    double accum_color_b    = 0.0;
    double color_weight     = 0.0;
    double accum_raw_int    = 0.0;
    double accum_dir_x      = 0.0;
    double accum_dir_y      = 0.0;
    double accum_dir_weight = 0.0;
    bool   any_active       = false;
    bool   any_needs        = false;
    bool   any_runtime      = false;

    for (auto& cell : lighting_chunks_) {
        accum_strength += static_cast<double>(cell.lighting.current_strength);
        accum_runtime  += static_cast<double>(cell.lighting.runtime_average_strength);
        accum_static   += static_cast<double>(cell.lighting.static_strength);
        accum_dynamic  += static_cast<double>(cell.lighting.dynamic_strength);
        accum_raw_int  += static_cast<double>(cell.lighting.runtime_average_raw_intensity);
        any_active       = any_active || cell.lighting.is_active;
        any_needs        = any_needs || cell.lighting.needs_update;
        any_runtime      = any_runtime || cell.lighting.has_runtime_average;
        if (cell.lighting.has_runtime_average) {
            const float weight = std::max(cell.lighting.runtime_average_strength, 0.0f);
            if (weight > 1e-5f) {
                accum_color_r += static_cast<double>(cell.lighting.runtime_average_color.r) * weight;
                accum_color_g += static_cast<double>(cell.lighting.runtime_average_color.g) * weight;
                accum_color_b += static_cast<double>(cell.lighting.runtime_average_color.b) * weight;
                color_weight += static_cast<double>(weight);
            }
        }
        if (cell.lighting.has_runtime_direction) {
            const float dir_weight = std::max(cell.lighting.runtime_average_raw_intensity,
                                              cell.lighting.runtime_average_strength);
            if (dir_weight > 1e-5f) {
                accum_dir_x += static_cast<double>(cell.lighting.runtime_average_direction.x) * dir_weight;
                accum_dir_y += static_cast<double>(cell.lighting.runtime_average_direction.y) * dir_weight;
                accum_dir_weight += static_cast<double>(dir_weight);
            }
        }
    }

    const double inv = 1.0 / static_cast<double>(lighting_chunks_.size());
    lighting.current_strength         = static_cast<float>(accum_strength * inv);
    lighting.runtime_average_strength = static_cast<float>(accum_runtime * inv);
    lighting.static_strength          = static_cast<float>(accum_static * inv);
    lighting.dynamic_strength         = static_cast<float>(accum_dynamic * inv);
    lighting.pre_shadow_strength      = lighting.current_strength;
    lighting.is_active                = any_active;
    lighting.needs_update             = any_needs;
    lighting.has_runtime_average      = any_runtime;
    lighting.runtime_average_raw_intensity = static_cast<float>(accum_raw_int * inv);
    if (color_weight > 1e-5) {
        const double inv_weight = 1.0 / color_weight;
        const double avg_r      = std::clamp(accum_color_r * inv_weight, 0.0, 255.0);
        const double avg_g      = std::clamp(accum_color_g * inv_weight, 0.0, 255.0);
        const double avg_b      = std::clamp(accum_color_b * inv_weight, 0.0, 255.0);
        lighting.runtime_average_color.r = static_cast<Uint8>(std::lround(avg_r));
        lighting.runtime_average_color.g = static_cast<Uint8>(std::lround(avg_g));
        lighting.runtime_average_color.b = static_cast<Uint8>(std::lround(avg_b));
    } else {
        lighting.runtime_average_color = SDL_Color{255, 255, 255, 255};
    }
    if (accum_dir_weight > 1e-5) {
        const double inv_dir = 1.0 / accum_dir_weight;
        float dir_x = static_cast<float>(accum_dir_x * inv_dir);
        float dir_y = static_cast<float>(accum_dir_y * inv_dir);
        const float mag = std::sqrt(dir_x * dir_x + dir_y * dir_y);
        if (mag > 1e-4f) {
            dir_x /= mag;
            dir_y /= mag;
            lighting.runtime_average_direction = SDL_FPoint{dir_x, dir_y};
            lighting.has_runtime_direction     = true;
        } else {
            lighting.runtime_average_direction = SDL_FPoint{0.0f, 0.0f};
            lighting.has_runtime_direction     = false;
        }
    } else {
        lighting.runtime_average_direction = SDL_FPoint{0.0f, 0.0f};
        lighting.has_runtime_direction     = false;
    }

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

    const float cb = std::clamp(center.lighting.pre_shadow_strength, 0.0f, 1.0f);
    float gx = 0.0f, gy = 0.0f;
    for (int dj = -effective_radius; dj <= effective_radius; ++dj) {
        for (int di = -effective_radius; di <= effective_radius; ++di) {
            if (di == 0 && dj == 0) continue;
            const int target_i = center.global_i + di;
            const int target_j = center.global_j + dj;
            const LightingChunk* n = find_lighting_chunk_from_global_const(grid, target_i, target_j);
            const float nb = n ? std::clamp(n->lighting.pre_shadow_strength, 0.0f, 1.0f) : cb;
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
        const float base = std::clamp(center.lighting.pre_shadow_strength, 0.0f, 1.0f);
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
                const float s  = std::clamp(n->lighting.pre_shadow_strength, 0.0f, 1.0f);
                accum_w += static_cast<double>(w);
                accum_v += static_cast<double>(w) * static_cast<double>(std::clamp(s, 0.0f, 1.0f));
            }
        }
        if (accum_w <= 1e-8) {
            return std::clamp(center.lighting.pre_shadow_strength, 0.0f, 1.0f);
        }
        return static_cast<float>(std::clamp(accum_v / accum_w, 0.0, 1.0));
    };

    const float base_strength = std::clamp(center.lighting.pre_shadow_strength, 0.0f, 1.0f);
    const float front_avg  = (R > 0) ? sample_dir(-effective_radius, -1) : base_strength;
    const float behind_avg = (R > 0) ? sample_dir(1,  effective_radius)  : base_strength;
    return {front_avg, behind_avg};
}

static void compute_use_shadow_data_for_chunk(const LightMap::ShadowSettings& settings,
                                              const world::Grid& grid,
                                              float scene_average_strength,
                                              const std::pair<float, float>& grad,
                                              bool prefer_fast_blend,
                                              float static_strength,
                                              float dynamic_strength,
                                              float blended_strength,
                                              LightingChunk& chunk) {
    world::Chunk::ChunkShadowParameters sample{};

    chunk.has_dynamic_overlay = (std::abs(dynamic_strength - static_strength) > 1e-3f);
    chunk.lighting.current_strength = std::clamp(blended_strength, 0.0f, 1.0f);

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

    // Move away from brighter areas. The lighting grid reports the horizontal gradient inverted,
    // so keep the X push aligned with the reported direction while negating Y to counter bright
    // spots vertically.
    float px = nx * 100.0f;
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

    if (chunk.lighting.has_runtime_average && chunk.lighting.has_runtime_direction) {
        SDL_FPoint runtime_dir = chunk.lighting.runtime_average_direction;
        const float dir_mag = std::sqrt(runtime_dir.x * runtime_dir.x + runtime_dir.y * runtime_dir.y);
        if (dir_mag > 1e-4f) {
            runtime_dir.x /= dir_mag;
            runtime_dir.y /= dir_mag;
            const float raw_intensity = std::max(chunk.lighting.runtime_average_raw_intensity, 0.0f);
            const float strength      = std::clamp(chunk.lighting.runtime_average_strength, 0.0f, 1.0f);
            const float runtime_push  = std::clamp(raw_intensity, 0.0f, 4.0f) * strength * 50.0f;
            if (runtime_push > 1e-3f) {
                px += -runtime_dir.x * runtime_push;
                py += -runtime_dir.y * runtime_push;
            }
            if (raw_intensity > 0.0f) {
                const float runtime_opacity_scale = 1.0f / (1.0f + raw_intensity);
                sample.opacity *= runtime_opacity_scale;
            }
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
    settings.sampling_static_weight  = std::max(0.0f, sanitized.sampling_weights.static_weight);
    settings.sampling_dynamic_weight = std::max(0.0f, sanitized.sampling_weights.dynamic_weight);

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
        for (const auto& cell : chunk->lighting_chunks()) {
            scene_light_sum_ +=
                static_cast<double>(std::clamp(cell.lighting.pre_shadow_strength, 0.0f, 1.0f));
            ++scene_light_count_;
        }
    }
    scene_light_cache_valid_ = true;
}

std::pair<float, float> LightMap::resolve_sampling_weights(float static_weight, float dynamic_weight) const {
    float base_static  = kDefaultStaticWeight;
    float base_dynamic = kDefaultDynamicWeight;
    if (assets_) {
        if (const auto* reactive = assets_->reactive_shadow_settings()) {
            const auto sanitized =
                render_pipeline::shading::sanitize_reactive_shadow_settings(*reactive);
            base_static  = std::max(0.0f, sanitized.sampling_weights.static_weight);
            base_dynamic = std::max(0.0f, sanitized.sampling_weights.dynamic_weight);
        }
    }

    float effective_static  = static_weight;
    float effective_dynamic = dynamic_weight;
    if (std::abs(static_weight - kDefaultStaticWeight) <= 1e-4f) {
        effective_static = base_static;
    }
    if (std::abs(dynamic_weight - kDefaultDynamicWeight) <= 1e-4f) {
        effective_dynamic = base_dynamic;
    }

    return {std::max(0.0f, effective_static), std::max(0.0f, effective_dynamic)};
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

    double frame_pre_shadow_sum   = 0.0;
    int    frame_pre_shadow_count = 0;
    for (world::Chunk* chunk : chunks) {
        if (!chunk) {
            continue;
        }
        chunk->lighting.pre_shadow_strength =
            std::clamp(chunk->lighting.current_strength, 0.0f, 1.0f);
        for (auto& cell : chunk->lighting_chunks()) {
            const float snapshot = std::clamp(cell.lighting.current_strength, 0.0f, 1.0f);
            cell.lighting.pre_shadow_strength = snapshot;
            frame_pre_shadow_sum += static_cast<double>(snapshot);
            ++frame_pre_shadow_count;
        }
    }

    scene_light_sum_        = frame_pre_shadow_sum;
    scene_light_count_      = frame_pre_shadow_count;
    cached_chunk_count_     = static_cast<int>(chunks.size());
    scene_light_cache_valid_ = true;

    const ShadowSettings settings = shadow_settings();
    const float          static_weight  = std::max(0.0f, settings.sampling_static_weight);
    const float          dynamic_weight = std::max(0.0f, settings.sampling_dynamic_weight);

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

    const double frame_scene_sum   = scene_light_sum_;
    const int    frame_scene_count = scene_light_count_;
    const float  frame_scene_average =
        (frame_scene_count > 0)
            ? static_cast<float>(frame_scene_sum / static_cast<double>(frame_scene_count))
            : 1.0f;

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
            const float prev_blended = std::clamp(cell.lighting.current_strength, 0.0f, 1.0f);
            const float prev_dynamic = std::clamp(cell.lighting.dynamic_strength, 0.0f, 1.0f);

            bool runtime_changed = false;
            if (cell.lighting.has_runtime_average) {
                const float updated_dynamic =
                    std::clamp(cell.lighting.runtime_average_strength, 0.0f, 1.0f);
                runtime_changed = (std::abs(updated_dynamic - prev_dynamic) > 1e-4f);
                cell.lighting.dynamic_strength          = updated_dynamic;
                cell.lighting.runtime_average_strength  = updated_dynamic;
                cell.lighting.has_runtime_average       = false;
            }

            cell.lighting.static_strength  = std::clamp(cell.lighting.static_strength, 0.0f, 1.0f);
            cell.lighting.dynamic_strength = std::clamp(cell.lighting.dynamic_strength, 0.0f, 1.0f);

            const float blended =
                blend_light_components(cell.lighting.static_strength,
                                       cell.lighting.dynamic_strength,
                                       static_weight,
                                       dynamic_weight);
            const bool brightness_changed = std::abs(blended - prev_blended) > 1e-4f;
            if (brightness_changed) {
                chunk_strength_delta += static_cast<double>(blended - prev_blended);
            }

            cell.lighting.current_strength = blended;
            runtime_changed                = runtime_changed || brightness_changed;

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

        const float scene_average_strength = frame_scene_average;

        const bool chunk_prefers_fast_blend = chunk_dirty;

        for (const DirtyCellInfo& info : dirty_cells) {
            auto& cell = *info.cell;
            const auto grad = compute_brightness_gradient(cell, grid, radius, fx, fy);
            const bool prefer_fast_blend = chunk_prefers_fast_blend || info.runtime_changed;

            compute_use_shadow_data_for_chunk(settings,
                                              grid,
                                              scene_average_strength,
                                              grad,
                                              prefer_fast_blend,
                                              cell.lighting.static_strength,
                                              cell.lighting.dynamic_strength,
                                              cell.lighting.current_strength,
                                              cell);

            cell.lighting.needs_update = false;
        }

        if (!dirty_cells.empty() || std::abs(chunk_strength_delta) > 1e-6) {
            chunk->update_aggregate_from_lighting_chunks();
        }

        chunk->lighting_dirty      = false;
        chunk->lighting.needs_update = false;

        scene_light_cache_valid_ = true;
    }
}

void LightMap::ingest_runtime_samples(const runtime_lighting::RuntimeLightingFrame& frame) {
    std::scoped_lock lock(mutex_);
    if (chunk_lighting_suspended_flag()) {
        return;
    }
    if (!assets_) {
        return;
    }

    for (world::Chunk* chunk : active_chunks()) {
        if (!chunk) {
            continue;
        }
        for (auto& cell : chunk->lighting_chunks()) {
            cell.lighting.has_runtime_average      = false;
            cell.lighting.runtime_average_strength = 0.0f;
            cell.lighting.runtime_average_color    = SDL_Color{255, 255, 255, 255};
            cell.lighting.runtime_average_raw_intensity = 0.0f;
            cell.lighting.runtime_average_direction     = SDL_FPoint{0.0f, 0.0f};
            cell.lighting.has_runtime_direction         = false;
        }
    }

    if (frame.empty()) {
        return;
    }

    world::Grid& grid = assets_->world_grid();
    std::unordered_set<world::Chunk*> updated_chunks;
    updated_chunks.reserve(frame.samples.size());

    for (const auto& sample : frame.samples) {
        world::Chunk::LightingChunk* cell =
            find_lighting_chunk_from_global(grid, sample.global_i, sample.global_j);
        if (!cell) {
            continue;
        }

        const float brightness = std::clamp(sample.brightness, 0.0f, 1.0f);
        cell->lighting.runtime_average_strength = brightness;
        cell->lighting.runtime_average_color    = sample.color;
        cell->lighting.has_runtime_average      = true;
        cell->lighting.needs_update             = true;
        cell->lighting.is_active                = true;
        cell->lighting.runtime_average_raw_intensity = std::max(sample.raw_intensity, 0.0f);
        if (sample.has_direction) {
            SDL_FPoint dir = sample.direction;
            const float mag = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (mag > 1e-4f) {
                dir.x /= mag;
                dir.y /= mag;
                cell->lighting.runtime_average_direction = dir;
                cell->lighting.has_runtime_direction     = true;
            }
        }

        if (cell->parent) {
            updated_chunks.insert(cell->parent);
        }
    }

    for (world::Chunk* chunk : updated_chunks) {
        if (!chunk) {
            continue;
        }
        chunk->update_aggregate_from_lighting_chunks();
    }
}

LightMap::SampledBrightness LightMap::sample_lighting(int world_x,
                                                      int world_y,
                                                      float static_weight,
                                                      float dynamic_weight) const {
    std::scoped_lock lock(mutex_);
    SampledBrightness sample{};
    const auto        weights = resolve_sampling_weights(static_weight, dynamic_weight);

    world::Chunk* chunk = ensure_chunk_from_world(SDL_Point{world_x, world_y});
    if (!chunk) {
        vibble::log::warn("[LightMap] sample_lighting missing chunk for world point (" +
                          std::to_string(world_x) + ", " + std::to_string(world_y) + ")");
        sample.blended =
            blend_light_components(sample.static_component, sample.dynamic_component, weights.first, weights.second);
        return sample;
    }

    const world::Chunk::LightingChunk* cell =
        chunk->lighting_chunk_from_world(SDL_Point{world_x, world_y});
    if (cell) {
        sample.static_component  = std::clamp(cell->lighting.static_strength, 0.0f, 1.0f);
        sample.dynamic_component = std::clamp(cell->lighting.dynamic_strength, 0.0f, 1.0f);
        sample.has_color         = cell->lighting.has_runtime_average;
        sample.color             = cell->lighting.runtime_average_color;
    } else {
        sample.static_component  = std::clamp(chunk->lighting.static_strength, 0.0f, 1.0f);
        sample.dynamic_component = std::clamp(chunk->lighting.dynamic_strength, 0.0f, 1.0f);
        sample.has_color         = chunk->lighting.has_runtime_average;
        sample.color             = chunk->lighting.runtime_average_color;
    }

    sample.blended = blend_light_components(sample.static_component,
                                            sample.dynamic_component,
                                            weights.first,
                                            weights.second);
    return sample;
}

LightMap::SampledBrightness LightMap::sample_lighting_bilinear(float world_x,
                                                               float world_y,
                                                               float static_weight,
                                                               float dynamic_weight) const {
    const int x0 = static_cast<int>(std::floor(world_x));
    const int y0 = static_cast<int>(std::floor(world_y));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;

    const float tx = world_x - static_cast<float>(x0);
    const float ty = world_y - static_cast<float>(y0);

    const SampledBrightness s00 = sample_lighting(x0, y0, static_weight, dynamic_weight);
    const SampledBrightness s10 = sample_lighting(x1, y0, static_weight, dynamic_weight);
    const SampledBrightness s01 = sample_lighting(x0, y1, static_weight, dynamic_weight);
    const SampledBrightness s11 = sample_lighting(x1, y1, static_weight, dynamic_weight);

    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

    SampledBrightness result{};
    result.static_component = std::clamp(lerp(lerp(s00.static_component, s10.static_component, tx),
                                             lerp(s01.static_component, s11.static_component, tx),
                                             ty),
                                         0.0f,
                                         1.0f);
    result.dynamic_component = std::clamp(lerp(lerp(s00.dynamic_component, s10.dynamic_component, tx),
                                              lerp(s01.dynamic_component, s11.dynamic_component, tx),
                                              ty),
                                          0.0f,
                                          1.0f);

    const auto weights = resolve_sampling_weights(static_weight, dynamic_weight);
    result.blended = blend_light_components(result.static_component,
                                            result.dynamic_component,
                                            weights.first,
                                            weights.second);

    const float w00 = (1.0f - tx) * (1.0f - ty);
    const float w10 = tx * (1.0f - ty);
    const float w01 = (1.0f - tx) * ty;
    const float w11 = tx * ty;

    float accum_r = 0.0f;
    float accum_g = 0.0f;
    float accum_b = 0.0f;
    float color_weight = 0.0f;
    auto accumulate_color = [&](const SampledBrightness& s, float weight) {
        if (!s.has_color || weight <= 0.0f) {
            return;
        }
        accum_r += static_cast<float>(s.color.r) * weight;
        accum_g += static_cast<float>(s.color.g) * weight;
        accum_b += static_cast<float>(s.color.b) * weight;
        color_weight += weight;
    };

    accumulate_color(s00, w00);
    accumulate_color(s10, w10);
    accumulate_color(s01, w01);
    accumulate_color(s11, w11);

    if (color_weight > 1e-5f) {
        const float inv = 1.0f / color_weight;
        const float r   = std::clamp(accum_r * inv, 0.0f, 255.0f);
        const float g   = std::clamp(accum_g * inv, 0.0f, 255.0f);
        const float b   = std::clamp(accum_b * inv, 0.0f, 255.0f);
        result.color.r  = static_cast<Uint8>(std::lround(r));
        result.color.g  = static_cast<Uint8>(std::lround(g));
        result.color.b  = static_cast<Uint8>(std::lround(b));
        result.has_color = true;
    } else {
        result.color     = SDL_Color{255, 255, 255, 255};
        result.has_color = false;
    }

    return result;
}

float LightMap::sample_brightness(int world_x,
                                  int world_y,
                                  float static_weight,
                                  float dynamic_weight) const {
    return sample_lighting(world_x, world_y, static_weight, dynamic_weight).blended;
}

float LightMap::sample_brightness_bilinear(float world_x,
                                           float world_y,
                                           float static_weight,
                                           float dynamic_weight) const {
    return sample_lighting_bilinear(world_x, world_y, static_weight, dynamic_weight).blended;
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
        runtime_shadow_mask_blend_    = compute_runtime_light_blend_mode();

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
        runtime_shadow_mask_blend_ = compute_runtime_light_blend_mode();
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

void LightMap::collect_runtime_shadow_masks(const SDL_Rect& view_rect,
                                            float alpha_multiplier,
                                            const SDL_Color& color_mod,
                                            std::vector<RuntimeShadowMaskRender>& out) const {
    out.clear();
    if (!assets_) {
        return;
    }
    if (view_rect.w <= 0 || view_rect.h <= 0) {
        return;
    }

    const camera& cam        = assets_->getView();
    SDL_Rect      world_view = world_rect_from_screen(cam, view_rect);

    auto combine_color = [](Uint8 base, Uint8 tint) -> Uint8 {
        const float base_norm = static_cast<float>(base) / 255.0f;
        const float tint_norm = static_cast<float>(tint) / 255.0f;
        const float value     = std::clamp(base_norm * tint_norm, 0.0f, 1.0f);
        return static_cast<Uint8>(std::lround(value * 255.0f));
    };

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

        float  brightness    = std::clamp(chunk->lighting.current_strength, 0.0f, 1.0f);
        double visible_sum   = 0.0;
        int    visible_count = 0;
        double color_r_sum   = 0.0;
        double color_g_sum   = 0.0;
        double color_b_sum   = 0.0;
        double color_weight  = 0.0;
        for (const auto& cell : chunk->lighting_chunks()) {
            if (!intersects(cell.world_bounds, world_view)) {
                continue;
            }
            visible_sum += static_cast<double>(std::clamp(cell.lighting.current_strength, 0.0f, 1.0f));
            ++visible_count;
            if (cell.lighting.has_runtime_average) {
                const float weight = std::clamp(cell.lighting.runtime_average_strength, 0.0f, 1.0f);
                if (weight > 1e-5f) {
                    color_r_sum += static_cast<double>(cell.lighting.runtime_average_color.r) * weight;
                    color_g_sum += static_cast<double>(cell.lighting.runtime_average_color.g) * weight;
                    color_b_sum += static_cast<double>(cell.lighting.runtime_average_color.b) * weight;
                    color_weight += static_cast<double>(weight);
                }
            }
        }
        if (visible_count > 0) {
            brightness = static_cast<float>(visible_sum / static_cast<double>(visible_count));
        }

        SDL_Color runtime_color = chunk->lighting.runtime_average_color;
        if (color_weight > 1e-5) {
            const double inv = 1.0 / color_weight;
            runtime_color.r = static_cast<Uint8>(std::lround(std::clamp(color_r_sum * inv, 0.0, 255.0)));
            runtime_color.g = static_cast<Uint8>(std::lround(std::clamp(color_g_sum * inv, 0.0, 255.0)));
            runtime_color.b = static_cast<Uint8>(std::lround(std::clamp(color_b_sum * inv, 0.0, 255.0)));
        } else if (!chunk->lighting.has_runtime_average) {
            runtime_color = SDL_Color{255, 255, 255, 255};
        }

        SDL_Color applied_color{combine_color(color_mod.r, runtime_color.r),
                                combine_color(color_mod.g, runtime_color.g),
                                combine_color(color_mod.b, runtime_color.b),
                                255};

        float alpha = (1.0f - brightness) * alpha_multiplier;
        alpha *= std::clamp(chunk->shadow.opacity, 0.0f, 1.0f);
        alpha = std::clamp(alpha, 0.0f, 1.0f);
        if (alpha <= 1e-4f) {
            continue;
        }

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

        RuntimeShadowMaskRender render{};
        render.dest_rect = dest;
        render.color     = applied_color;
        render.alpha     = alpha;
        out.push_back(render);
    }
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

    std::vector<RuntimeShadowMaskRender> renders;
    renders.reserve(active_chunks().size());
    collect_runtime_shadow_masks(view_rect, alpha_multiplier, color_mod, renders);
    if (renders.empty()) {
        rendered_in_current_tick_ = true;
        return;
    }

    Uint8 saved_r = 255;
    Uint8 saved_g = 255;
    Uint8 saved_b = 255;
    Uint8 saved_a = 255;
    SDL_GetTextureColorMod(mask_texture, &saved_r, &saved_g, &saved_b);
    SDL_GetTextureAlphaMod(mask_texture, &saved_a);
    SDL_BlendMode saved_blend = SDL_BLENDMODE_BLEND;
    SDL_GetTextureBlendMode(mask_texture, &saved_blend);

    SDL_SetTextureBlendMode(mask_texture, runtime_shadow_mask_blend_);

    for (const RuntimeShadowMaskRender& entry : renders) {
        SDL_SetTextureColorMod(mask_texture, entry.color.r, entry.color.g, entry.color.b);
        SDL_SetTextureAlphaMod(mask_texture, clamp_alpha(entry.alpha));
        if (SDL_RenderCopy(renderer, mask_texture, nullptr, &entry.dest_rect) != 0) {
            vibble::log::warn(std::string{"[LightMap] Failed to render shadow mask: "} + SDL_GetError());
        }
    }

    SDL_SetTextureColorMod(mask_texture, saved_r, saved_g, saved_b);
    SDL_SetTextureAlphaMod(mask_texture, saved_a);
    SDL_SetTextureBlendMode(mask_texture, saved_blend);

    rendered_in_current_tick_ = true;
}

void LightMap::subtract_runtime_shadow_from_texture(SDL_Renderer* renderer,
                                                    SDL_Texture* target_texture,
                                                    const SDL_Rect& target_rect,
                                                    const SDL_Rect& screen_rect,
                                                    float alpha_multiplier) const {
    std::scoped_lock lock(mutex_);
    if (!renderer || !target_texture || !assets_) {
        return;
    }
    if (screen_rect.w <= 0 || screen_rect.h <= 0) {
        return;
    }
    if (target_rect.w <= 0 || target_rect.h <= 0) {
        return;
    }

    alpha_multiplier = std::clamp(alpha_multiplier, 0.0f, 1.0f);
    if (alpha_multiplier <= 1e-4f) {
        return;
    }

    SDL_Texture* mask_texture = ensure_runtime_shadow_mask(renderer);
    if (!mask_texture) {
        return;
    }

    std::vector<RuntimeShadowMaskRender> renders;
    renders.reserve(active_chunks().size());
    collect_runtime_shadow_masks(screen_rect, alpha_multiplier, SDL_Color{255, 255, 255, 255}, renders);
    if (renders.empty()) {
        return;
    }

    int mask_w = runtime_shadow_mask_w_;
    int mask_h = runtime_shadow_mask_h_;
    if (mask_w <= 0 || mask_h <= 0) {
        SDL_QueryTexture(mask_texture, nullptr, nullptr, &mask_w, &mask_h);
        mask_w = std::max(1, mask_w);
        mask_h = std::max(1, mask_h);
    }

    const SDL_BlendMode subtract_blend = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO,
                                                                    SDL_BLENDFACTOR_ONE,
                                                                    SDL_BLENDOPERATION_ADD,
                                                                    SDL_BLENDFACTOR_ZERO,
                                                                    SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                                                    SDL_BLENDOPERATION_ADD);
    if (subtract_blend == SDL_BLENDMODE_INVALID) {
        return;
    }

    Uint8 saved_r = 255;
    Uint8 saved_g = 255;
    Uint8 saved_b = 255;
    Uint8 saved_a = 255;
    SDL_BlendMode saved_blend = SDL_BLENDMODE_BLEND;
    SDL_GetTextureColorMod(mask_texture, &saved_r, &saved_g, &saved_b);
    SDL_GetTextureAlphaMod(mask_texture, &saved_a);
    SDL_GetTextureBlendMode(mask_texture, &saved_blend);

    SDL_SetTextureColorMod(mask_texture, 255, 255, 255);
    SDL_SetTextureBlendMode(mask_texture, subtract_blend);

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, target_texture);

    const float inv_screen_w = 1.0f / static_cast<float>(screen_rect.w);
    const float inv_screen_h = 1.0f / static_cast<float>(screen_rect.h);

    for (const RuntimeShadowMaskRender& entry : renders) {
        if (entry.dest_rect.w <= 0 || entry.dest_rect.h <= 0) {
            continue;
        }

        SDL_Rect overlap{};
        if (SDL_IntersectRect(&entry.dest_rect, &screen_rect, &overlap) != SDL_TRUE) {
            continue;
        }

        const float src_scale_x = static_cast<float>(mask_w) / static_cast<float>(entry.dest_rect.w);
        const float src_scale_y = static_cast<float>(mask_h) / static_cast<float>(entry.dest_rect.h);
        SDL_Rect    src{};
        src.x = static_cast<int>(std::floor((overlap.x - entry.dest_rect.x) * src_scale_x));
        src.y = static_cast<int>(std::floor((overlap.y - entry.dest_rect.y) * src_scale_y));
        src.w = static_cast<int>(std::ceil(overlap.w * src_scale_x));
        src.h = static_cast<int>(std::ceil(overlap.h * src_scale_y));

        auto clamp_dimension = [](int& pos, int& size, int max) {
            if (pos < 0) {
                size += pos;
                pos = 0;
            }
            if (pos + size > max) {
                size = max - pos;
            }
        };

        clamp_dimension(src.x, src.w, mask_w);
        clamp_dimension(src.y, src.h, mask_h);
        if (src.w <= 0 || src.h <= 0) {
            continue;
        }

        const float start_ratio_x = static_cast<float>(overlap.x - screen_rect.x) * inv_screen_w;
        const float end_ratio_x   = static_cast<float>((overlap.x - screen_rect.x) + overlap.w) * inv_screen_w;
        const float start_ratio_y = static_cast<float>(overlap.y - screen_rect.y) * inv_screen_h;
        const float end_ratio_y   = static_cast<float>((overlap.y - screen_rect.y) + overlap.h) * inv_screen_h;

        const float clamped_start_x = std::clamp(start_ratio_x, 0.0f, 1.0f);
        const float clamped_end_x   = std::clamp(end_ratio_x, clamped_start_x, 1.0f);
        const float clamped_start_y = std::clamp(start_ratio_y, 0.0f, 1.0f);
        const float clamped_end_y   = std::clamp(end_ratio_y, clamped_start_y, 1.0f);

        const int dest_x0 = target_rect.x +
                             static_cast<int>(std::lround(clamped_start_x * static_cast<float>(target_rect.w)));
        const int dest_y0 = target_rect.y +
                             static_cast<int>(std::lround(clamped_start_y * static_cast<float>(target_rect.h)));
        const int dest_x1 = target_rect.x +
                             static_cast<int>(std::lround(clamped_end_x * static_cast<float>(target_rect.w)));
        const int dest_y1 = target_rect.y +
                             static_cast<int>(std::lround(clamped_end_y * static_cast<float>(target_rect.h)));

        SDL_Rect dest{};
        dest.x = dest_x0;
        dest.y = dest_y0;
        dest.w = std::max(1, dest_x1 - dest_x0);
        dest.h = std::max(1, dest_y1 - dest_y0);

        SDL_SetTextureAlphaMod(mask_texture, clamp_alpha(entry.alpha));
        if (SDL_RenderCopy(renderer, mask_texture, &src, &dest) != 0) {
            vibble::log::warn(std::string{"[LightMap] Failed to subtract shadow mask: "} + SDL_GetError());
        }
    }

    SDL_SetRenderTarget(renderer, previous_target);

    SDL_SetTextureColorMod(mask_texture, saved_r, saved_g, saved_b);
    SDL_SetTextureAlphaMod(mask_texture, saved_a);
    SDL_SetTextureBlendMode(mask_texture, saved_blend);
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


