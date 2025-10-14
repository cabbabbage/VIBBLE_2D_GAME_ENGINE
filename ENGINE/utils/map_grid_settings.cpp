#include "map_grid_settings.hpp"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "utils/area.hpp"

namespace {
constexpr int kMinSpacing = 8;
constexpr int kMaxSpacing = 2048;
constexpr int kMinJitter = 0;
constexpr int kMaxJitter = 1024;
}

MapGridSettings MapGridSettings::defaults() {
    return MapGridSettings{100, 0};
}

MapGridSettings MapGridSettings::from_json(const nlohmann::json* obj) {
    MapGridSettings settings = defaults();
    if (!obj || !obj->is_object()) {
        return settings;
    }
    try {
        if (obj->contains("spacing") && (*obj)["spacing"].is_number_integer()) {
            settings.spacing = (*obj)["spacing"].get<int>();
        }
    } catch (...) {
        settings.spacing = defaults().spacing;
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
    spacing = std::clamp(spacing, kMinSpacing, kMaxSpacing);
    const int jitter_max = std::clamp(spacing, kMinSpacing, kMaxJitter);
    jitter = std::clamp(jitter, kMinJitter, jitter_max);
}

void MapGridSettings::apply_to_json(nlohmann::json& obj) const {
    if (!obj.is_object()) {
        obj = nlohmann::json::object();
    }
    obj["spacing"] = spacing;
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
