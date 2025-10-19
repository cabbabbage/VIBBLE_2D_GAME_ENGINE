#include "map_grid_settings.hpp"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

#include "utils/area.hpp"
#include "util/grid.hpp"

namespace {
constexpr int kMinResolution = 0;
constexpr int kMaxResolution = vibble::grid::kMaxResolution;
constexpr int kMinJitter = 0;
}

MapGridSettings MapGridSettings::defaults() {
    return MapGridSettings{7, 0};
}

MapGridSettings MapGridSettings::from_json(const nlohmann::json* obj) {
    MapGridSettings settings = defaults();
    if (!obj || !obj->is_object()) {
        return settings;
    }
    try {
        if (obj->contains("resolution") && (*obj)["resolution"].is_number_integer()) {
            settings.resolution = (*obj)["resolution"].get<int>();
        } else if (obj->contains("spacing") && (*obj)["spacing"].is_number_integer()) {
            const int spacing = std::max(1, (*obj)["spacing"].get<int>());
            const double log_value = std::log2(static_cast<double>(spacing));
            settings.resolution = static_cast<int>(std::lround(log_value));
        }
    } catch (...) {
        settings.resolution = defaults().resolution;
    }
    try {
        if (obj->contains("jitter") && (*obj)["jitter"].is_number_integer()) {
            settings.jitter = (*obj)["jitter"].get<int>();
        }
    } catch (...) {
        settings.jitter = defaults().jitter;
    }
    settings.clamp();
    return settings;
}

void MapGridSettings::clamp() {
    resolution = std::clamp(resolution, kMinResolution, kMaxResolution);
    const int step = spacing();
    const int jitter_max = std::max(kMinJitter, step / 2);
    jitter = std::clamp(jitter, kMinJitter, jitter_max);
}

void MapGridSettings::apply_to_json(nlohmann::json& obj) const {
    if (!obj.is_object()) {
        obj = nlohmann::json::object();
    }
    obj["resolution"] = resolution;
    obj["jitter"] = jitter;
}

void ensure_map_grid_settings(nlohmann::json& map_info) {
    if (!map_info.is_object()) {
        map_info = nlohmann::json::object();
    }
    nlohmann::json& section = map_info["map_grid_settings"];
    if (!section.is_object()) {
        section = nlohmann::json::object();
    }
    MapGridSettings settings = MapGridSettings::from_json(&section);
    settings.apply_to_json(section);
}

SDL_Point apply_map_grid_jitter(const MapGridSettings& settings,
                                SDL_Point base,
                                std::mt19937& rng,
                                const Area& area) {
    if (settings.jitter <= 0) {
        return base;
    }
    std::uniform_int_distribution<int> dist(-settings.jitter, settings.jitter);
    SDL_Point candidate = base;
    constexpr int kMaxAttempts = 4;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        candidate.x = base.x + dist(rng);
        candidate.y = base.y + dist(rng);
        if (area.contains_point(candidate)) {
            return candidate;
        }
    }
    return base;
}

int MapGridSettings::spacing() const {
    return vibble::grid::delta(resolution);
}
