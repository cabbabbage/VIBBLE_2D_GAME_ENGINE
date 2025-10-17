#include "render/light_map_manager.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "render/global_light_source.hpp"
#include "render/light_map.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

using render_pipeline::shading::ReactiveShadowSettings;
using render_pipeline::shading::clampf;
using render_pipeline::shading::sanitize_reactive_shadow_settings;

constexpr std::uint32_t kRecomputeIntervalMs = 16u;
constexpr float         kSignatureEpsilon    = 1e-4f;

struct ShadowResponseSample {
    float opacity = 1.0f;
    float offset  = 0.0f;
    float scale   = 1.0f;
};

ShadowResponseSample evaluate_shadow_response(const ReactiveShadowSettings& settings, float brightness) {
    auto apply_strength = [&](ShadowResponseSample sample) {
        sample.opacity *= settings.opacity_strength;
        sample.offset  *= settings.parallax_strength;
        sample.scale   *= settings.scale_strength;
        return sample;
    };

    ShadowResponseSample result{};
    const auto&          entries = settings.response_lut.entries;
    if (entries.empty()) {
        return apply_strength(result);
    }

    const float clamped_brightness = clampf(brightness, 0.0f, 1.0f);
    if (entries.size() == 1 || clamped_brightness <= entries.front().brightness) {
        const auto& entry = entries.front();
        result.opacity    = entry.opacity;
        result.offset     = entry.offset;
        result.scale      = entry.scale;
        return apply_strength(result);
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
            return apply_strength(result);
        }
    }

    const auto& tail = entries.back();
    result.opacity    = tail.opacity;
    result.offset     = tail.offset;
    result.scale      = tail.scale;
    return apply_strength(result);
}

ReactiveShadowSettings sanitized_settings(const Assets* assets) {
    if (!assets) {
        return sanitize_reactive_shadow_settings(ReactiveShadowSettings{});
    }
    if (const ReactiveShadowSettings* raw = assets->reactive_shadow_settings()) {
        return sanitize_reactive_shadow_settings(*raw);
    }
    return sanitize_reactive_shadow_settings(ReactiveShadowSettings{});
}

float falloff_weight(int dx, int dy, float horizontal_falloff, float vertical_falloff) {
    const float abs_x = std::abs(static_cast<float>(dx));
    const float abs_y = std::abs(static_cast<float>(dy));
    float       weight = 1.0f;
    if (horizontal_falloff > 0.0f) {
        weight *= std::exp(-abs_x * horizontal_falloff);
    }
    if (vertical_falloff > 0.0f) {
        weight *= std::exp(-abs_y * vertical_falloff);
    }
    return weight;
}

}  // namespace

LightMapManager::LightMapManager(Assets* assets) : assets_(assets) {}

void LightMapManager::begin_frame() {
    ++frame_counter_;
    for (auto& cache : quadrant_caches_) {
        cache.attempted_this_frame = false;
    }
}

const LightMap* LightMapManager::light_map() const {
    return assets_ ? assets_->light_map() : nullptr;
}

void LightMapManager::ensure_cache_size(const LightMap* map) const {
    if (!map) {
        if (!quadrant_caches_.empty()) {
            quadrant_caches_.clear();
        }
        return;
    }
    const int count = map->quadrant_count();
    if (count <= 0) {
        quadrant_caches_.clear();
        return;
    }
    if (static_cast<int>(quadrant_caches_.size()) != count) {
        quadrant_caches_.assign(static_cast<std::size_t>(count), QuadrantCache{});
    }
}

void LightMapManager::mark_all_dirty() const {
    for (auto& cache : quadrant_caches_) {
        cache.dirty                = true;
        cache.attempted_this_frame = false;
    }
}

void LightMapManager::refresh_global_state() const {
    const LightMap* map = light_map();
    ensure_cache_size(map);

    const ReactiveShadowSettings settings = sanitized_settings(assets_);
    if (!settings_initialized_ || settings != cached_settings_) {
        cached_settings_       = settings;
        settings_initialized_  = true;
        mark_all_dirty();
    }

    const Global_Light_Source* light = assets_ ? assets_->map_light_source() : nullptr;
    SDL_Point reference{0, 0};
    SDL_Point target{0, 0};
    int       brightness = -1;
    SDL_Color color{0, 0, 0, 0};
    if (light) {
        reference  = light->get_direction_reference();
        target     = light->get_direction_target();
        brightness = light->get_brightness();
        color      = light->get_current_color();
    }

    if (!light_state_initialized_ ||
        reference.x != last_light_reference_.x || reference.y != last_light_reference_.y ||
        target.x != last_light_target_.x || target.y != last_light_target_.y ||
        brightness != last_light_brightness_ ||
        color.r != last_light_color_.r || color.g != last_light_color_.g ||
        color.b != last_light_color_.b || color.a != last_light_color_.a) {
        last_light_reference_      = reference;
        last_light_target_         = target;
        last_light_brightness_     = brightness;
        last_light_color_          = color;
        light_state_initialized_   = true;
        mark_all_dirty();
    }
}

LightMapManager::QuadrantSignature LightMapManager::signature_for(const LightMapQuadrant& quadrant) const {
    QuadrantSignature signature{};
    signature.world_rect     = quadrant.world_rect();
    signature.grid_w         = quadrant.grid_width();
    signature.grid_h         = quadrant.grid_height();
    signature.base_brightness = quadrant.base_brightness();

    const auto static_stats = quadrant.static_grid_stats();
    signature.static_empty   = static_stats.empty;
    signature.static_average = static_stats.empty ? 0.0f : static_stats.average;

    // Dynamic grid removed; preserve static-only signature.
    return signature;
}

bool LightMapManager::signature_changed(const QuadrantSignature& a, const QuadrantSignature& b) const {
    if (a.world_rect.x != b.world_rect.x || a.world_rect.y != b.world_rect.y ||
        a.world_rect.w != b.world_rect.w || a.world_rect.h != b.world_rect.h) {
        return true;
    }
    if (a.grid_w != b.grid_w || a.grid_h != b.grid_h) {
        return true;
    }
    auto diff = [](float lhs, float rhs) {
        return std::abs(lhs - rhs) > kSignatureEpsilon;
    };
    if (diff(a.base_brightness, b.base_brightness) || diff(a.static_average, b.static_average)) {
        return true;
    }
    if (a.static_empty != b.static_empty) {
        return true;
    }
    return false;
}

SDL_FPoint LightMapManager::current_light_direction() const {
    const float dx = static_cast<float>(last_light_target_.x - last_light_reference_.x);
    const float dy = static_cast<float>(last_light_target_.y - last_light_reference_.y);
    const float len = std::sqrt((dx * dx) + (dy * dy));
    if (!(len > 1e-3f)) {
        return SDL_FPoint{0.0f, 0.0f};
    }
    return SDL_FPoint{dx / len, dy / len};
}

float LightMapManager::direction_factor_for_rect(const SDL_Rect& rect, const SDL_FPoint& light_dir) const {
    if (!light_state_initialized_) {
        return 1.0f;
    }
    const float dir_len_sq = (light_dir.x * light_dir.x) + (light_dir.y * light_dir.y);
    if (!(dir_len_sq > 1e-6f)) {
        return 1.0f;
    }
    SDL_FPoint center{static_cast<float>(rect.x) + static_cast<float>(rect.w) * 0.5f,
                      static_cast<float>(rect.y) + static_cast<float>(rect.h) * 0.5f};
    SDL_FPoint asset_vec{center.x - static_cast<float>(last_light_reference_.x),
                         center.y - static_cast<float>(last_light_reference_.y)};
    const float asset_len = std::sqrt((asset_vec.x * asset_vec.x) + (asset_vec.y * asset_vec.y));
    if (!(asset_len > 1e-3f)) {
        return 1.0f;
    }
    asset_vec.x /= asset_len;
    asset_vec.y /= asset_len;
    const float dot = std::clamp(asset_vec.x * light_dir.x + asset_vec.y * light_dir.y, -1.0f, 1.0f);
    return (dot + 1.0f) * 0.5f;
}

LightMapManager::QuadrantParams LightMapManager::compute_params(const LightMapQuadrant& quadrant,
                                                               const QuadrantSignature&) const {
    QuadrantParams params{};

    const float static_weight  = cached_settings_.sampling_weights.static_weight;
    const float dynamic_weight = cached_settings_.sampling_weights.dynamic_weight;
    const int   grid_w         = quadrant.grid_width();
    const int   grid_h         = quadrant.grid_height();

    float accumulated_brightness = 0.0f;
    float accumulated_weight     = 0.0f;

    if (grid_w > 0 && grid_h > 0) {
        const int max_dimension = std::max(grid_w, grid_h);
        const int radius = std::clamp(cached_settings_.virtual_light_map.search_radius, 0, std::max(0, max_dimension - 1));
        const int center_x = grid_w / 2;
        const int center_y = grid_h / 2;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) > radius) {
                    continue;
                }
                const int sample_x = std::clamp(center_x + dx, 0, grid_w - 1);
                const int sample_y = std::clamp(center_y + dy, 0, grid_h - 1);
                const float weight = falloff_weight(dx,
                                                    dy,
                                                    cached_settings_.virtual_light_map.horizontal_falloff,
                                                    cached_settings_.virtual_light_map.vertical_falloff);
                const float sample = quadrant.sample_brightness(static_cast<float>(sample_x),
                                                                static_cast<float>(sample_y),
                                                                static_weight,
                                                                0.0f,
                                                                false);
                accumulated_brightness += sample * weight;
                accumulated_weight     += weight;
            }
        }
    }

    float average_brightness = 0.0f;
    if (accumulated_weight > 1e-6f) {
        average_brightness = accumulated_brightness / accumulated_weight;
    } else {
        average_brightness = quadrant.combined_average(static_weight, 0.0f);
    }
    average_brightness = clampf(average_brightness, 0.0f, 1.0f);

    const ShadowResponseSample response = evaluate_shadow_response(cached_settings_, average_brightness);
    const SDL_FPoint           light_dir = current_light_direction();
    const float direction_factor = direction_factor_for_rect(quadrant.world_rect(), light_dir);
    const float direction_weight = clampf(cached_settings_.virtual_light_map.map_light_factor, 0.0f, 1.0f);
    const float direction_mix    = 1.0f + (direction_factor - 1.0f) * direction_weight;

    params.opacity_q = clampf(response.opacity * direction_mix, 0.0f, 10.0f);

    const float offset_magnitude = response.offset * direction_mix;
    params.offset_x_q = light_dir.x * offset_magnitude;
    params.offset_y_q = light_dir.y * offset_magnitude;

    params.scale_q = std::max(response.scale * cached_settings_.virtual_light_map.shadow_scale, 0.0f);

    const float max_offset_x = std::max(cached_settings_.virtual_light_map.max_offset_x, 0.0f);
    const float max_offset_y = std::max(cached_settings_.virtual_light_map.max_offset_y, 0.0f);
    params.offset_x_q        = clampf(params.offset_x_q, -max_offset_x, max_offset_x);
    params.offset_y_q        = clampf(params.offset_y_q, -max_offset_y, max_offset_y);

    return params;
}

std::optional<int> LightMapManager::find_quadrant_index(SDL_FPoint world_or_screen_pos) const {
    const LightMap* map = light_map();
    if (!map) {
        return std::nullopt;
    }
    int index = map->quadrant_for_point(world_or_screen_pos.x, world_or_screen_pos.y);
    if (index >= 0) {
        return index;
    }
    if (!assets_) {
        return std::nullopt;
    }
    SDL_Point world_point{static_cast<int>(std::lround(world_or_screen_pos.x)),
                           static_cast<int>(std::lround(world_or_screen_pos.y))};
    SDL_Point screen_point = assets_->getView().map_to_screen(world_point);
    index = map->quadrant_for_point(static_cast<float>(screen_point.x), static_cast<float>(screen_point.y));
    if (index < 0) {
        return std::nullopt;
    }
    return index;
}

std::optional<LightMapManager::QuadrantParams> LightMapManager::get_quadrant_params(SDL_FPoint world_or_screen_pos) const {
    if (auto index = find_quadrant_index(world_or_screen_pos)) {
        return get_quadrant_params_for_index(*index);
    }
    return std::nullopt;
}

std::optional<LightMapManager::QuadrantParams> LightMapManager::get_quadrant_params_for_index(int index) const {
    const LightMap* map = light_map();
    if (!map) {
        return std::nullopt;
    }
    refresh_global_state();
    ensure_cache_size(map);
    if (index < 0 || index >= map->quadrant_count()) {
        return std::nullopt;
    }

    QuadrantCache& cache = quadrant_caches_[static_cast<std::size_t>(index)];
    const LightMapQuadrant* quadrant = map->quadrant(index);
    if (!quadrant) {
        return std::nullopt;
    }

    const QuadrantSignature signature = signature_for(*quadrant);
    if (!cache.valid || signature_changed(signature, cache.signature)) {
        cache.dirty = true;
    }
    cache.signature = signature;

    if (cache.last_frame != frame_counter_) {
        cache.attempted_this_frame = false;
    }

    if (cache.dirty) {
        const std::uint32_t now = SDL_GetTicks();
        const bool          can_recompute = !cache.attempted_this_frame &&
                                            (!cache.valid || (now - cache.last_compute_ticks) >= kRecomputeIntervalMs);
        if (can_recompute) {
            cache.params             = compute_params(*quadrant, signature);
            cache.valid              = true;
            cache.dirty              = false;
            cache.last_compute_ticks = now;
        }
        cache.attempted_this_frame = true;
        cache.last_frame           = frame_counter_;
    } else {
        cache.last_frame = frame_counter_;
    }

    if (!cache.valid) {
        return std::nullopt;
    }
    return cache.params;
}

std::optional<LightMapManager::QuadrantSnapshot> LightMapManager::snapshot_for_quadrant(int index) const {
    const LightMap* map = light_map();
    if (!map) {
        return std::nullopt;
    }
    const LightMapQuadrant* quadrant = map->quadrant(index);
    if (!quadrant) {
        return std::nullopt;
    }

    QuadrantSnapshot snapshot;
    snapshot.index           = index;
    snapshot.world_rect      = quadrant->world_rect();
    snapshot.active          = quadrant->active();
    snapshot.dirty           = quadrant->dirty();
    snapshot.base_brightness = quadrant->base_brightness();

    const auto static_stats  = quadrant->static_grid_stats();
    snapshot.static_average  = static_stats.empty ? 0.0f : static_stats.average;

    const ReactiveShadowSettings settings = sanitized_settings(assets_);
    const float static_weight             = settings.sampling_weights.static_weight;
    const float dynamic_weight            = settings.sampling_weights.dynamic_weight;

    snapshot.combined_brightness = quadrant->combined_average(static_weight, dynamic_weight);

    const float min_brightness = clampf(snapshot.base_brightness +
                                        (snapshot.static_average * static_weight),
                                        0.0f,
                                        1.0f);
    const float max_brightness = min_brightness;

    const ShadowResponseSample min_response = evaluate_shadow_response(settings, min_brightness);
    const ShadowResponseSample max_response = evaluate_shadow_response(settings, max_brightness);
    snapshot.shadow_opacity_min             = std::min(min_response.opacity, max_response.opacity);
    snapshot.shadow_opacity_max             = std::max(min_response.opacity, max_response.opacity);

    return snapshot;
}

std::vector<LightMapManager::QuadrantSnapshot> LightMapManager::all_snapshots() const {
    std::vector<QuadrantSnapshot> snapshots;
    const LightMap*               map = light_map();
    if (!map) {
        return snapshots;
    }
    const int count = map->quadrant_count();
    snapshots.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (auto snapshot = snapshot_for_quadrant(i)) {
            snapshots.push_back(*snapshot);
        }
    }
    return snapshots;
}

std::vector<std::string> LightMapManager::assets_sampling_quadrant(int index) const {
    std::vector<std::string> names;
    const LightMap*          map = light_map();
    if (!assets_ || !map) {
        return names;
    }
    if (index < 0 || index >= map->quadrant_count()) {
        return names;
    }

    const auto& active_assets = assets_->getActiveLitAssets();
    camera&      cam          = assets_->getView();

    names.reserve(active_assets.size());
    for (Asset* asset : active_assets) {
        if (!asset) {
            continue;
        }
        SDL_Point world_pos{ asset->pos.x, asset->pos.y };
        SDL_Point screen_pos = cam.map_to_screen(world_pos);
        const int asset_quadrant = map->quadrant_for_point(static_cast<float>(screen_pos.x),
                                                           static_cast<float>(screen_pos.y));
        if (asset_quadrant == index && asset->info) {
            names.push_back(asset->info->name);
        }
    }

    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());

    return names;
}
