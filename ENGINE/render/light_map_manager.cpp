#include "render/light_map_manager.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "render/light_map.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"

#include <algorithm>
#include <cmath>

namespace {

using render_pipeline::shading::ReactiveShadowSettings;
using render_pipeline::shading::sanitize_reactive_shadow_settings;

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

    const float clamped_brightness = render_pipeline::shading::clampf(brightness, 0.0f, 1.0f);
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

ReactiveShadowSettings sanitized_settings(const Assets* assets) {
    if (!assets) {
        return sanitize_reactive_shadow_settings(ReactiveShadowSettings{});
    }
    if (const ReactiveShadowSettings* raw = assets->reactive_shadow_settings()) {
        return sanitize_reactive_shadow_settings(*raw);
    }
    return sanitize_reactive_shadow_settings(ReactiveShadowSettings{});
}

}  // namespace

LightMapManager::LightMapManager(Assets* assets) : assets_(assets) {}

const LightMap* LightMapManager::light_map() const {
    return assets_ ? assets_->light_map() : nullptr;
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
    const auto dynamic_stats = quadrant->dynamic_grid_stats();
    snapshot.static_average  = static_stats.empty ? 0.0f : static_stats.average;
    snapshot.dynamic_average = dynamic_stats.empty ? 0.0f : dynamic_stats.average;
    snapshot.dynamic_min     = dynamic_stats.empty ? 0.0f : dynamic_stats.min;
    snapshot.dynamic_max     = dynamic_stats.empty ? 0.0f : dynamic_stats.max;

    const ReactiveShadowSettings settings = sanitized_settings(assets_);
    const float static_weight             = settings.sampling_weights.static_weight;
    const float dynamic_weight            = settings.sampling_weights.dynamic_weight;

    snapshot.combined_brightness = quadrant->combined_average(static_weight, dynamic_weight);

    const float min_brightness = render_pipeline::shading::clampf(snapshot.base_brightness +
                                                                  (snapshot.static_average * static_weight) +
                                                                  (snapshot.dynamic_min * dynamic_weight),
                                                                  0.0f,
                                                                  1.0f);
    const float max_brightness = render_pipeline::shading::clampf(snapshot.base_brightness +
                                                                  (snapshot.static_average * static_weight) +
                                                                  (snapshot.dynamic_max * dynamic_weight),
                                                                  0.0f,
                                                                  1.0f);

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

