#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace render_pipeline::shading {

namespace {

float read_float(const nlohmann::json& obj, const char* key, float fallback) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return fallback;
    }
    try {
        if (it->is_number_float()) {
            return static_cast<float>(it->get<double>());
        }
        if (it->is_number_integer()) {
            return static_cast<float>(it->get<int>());
        }
        if (it->is_string()) {
            const std::string text = it->get<std::string>();
            size_t idx = 0;
            float parsed = std::stof(text, &idx);
            if (idx == text.size()) {
                return parsed;
            }
        }
    } catch (...) {
    }
    return fallback;
}

int read_int(const nlohmann::json& obj, const char* key, int fallback) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return fallback;
    }
    try {
        if (it->is_number_integer()) {
            return it->get<int>();
        }
        if (it->is_number_float()) {
            return static_cast<int>(std::lround(it->get<double>()));
        }
        if (it->is_string()) {
            const std::string text = it->get<std::string>();
            size_t            idx  = 0;
            int               parsed = static_cast<int>(std::lround(std::stof(text, &idx)));
            if (idx == text.size()) {
                return parsed;
            }
        }
    } catch (...) {
    }
    return fallback;
}

void populate(ReactiveShadowSettings& settings, const nlohmann::json& json) {
    // Legacy shared falloffs
    const bool has_legacy_h = json.contains("horizontal_falloff");
    const bool has_legacy_v = json.contains("vertical_falloff");
    settings.virtual_light_map.horizontal_falloff =
        read_float(json, "horizontal_falloff", settings.virtual_light_map.horizontal_falloff);
    settings.virtual_light_map.vertical_falloff =
        read_float(json, "vertical_falloff", settings.virtual_light_map.vertical_falloff);
    // New split falloffs
    settings.virtual_light_map.offset_horizontal_falloff =
        read_float(json, "offset_horizontal_falloff", settings.virtual_light_map.offset_horizontal_falloff);
    settings.virtual_light_map.offset_vertical_falloff =
        read_float(json, "offset_vertical_falloff", settings.virtual_light_map.offset_vertical_falloff);
    settings.virtual_light_map.opacity_horizontal_falloff =
        read_float(json, "opacity_horizontal_falloff", settings.virtual_light_map.opacity_horizontal_falloff);
    settings.virtual_light_map.opacity_vertical_falloff =
        read_float(json, "opacity_vertical_falloff", settings.virtual_light_map.opacity_vertical_falloff);
    // If new fields weren't provided here, propagate legacy values
    if (!json.contains("offset_horizontal_falloff") && has_legacy_h) {
        settings.virtual_light_map.offset_horizontal_falloff = settings.virtual_light_map.horizontal_falloff;
    }
    if (!json.contains("opacity_horizontal_falloff") && has_legacy_h) {
        settings.virtual_light_map.opacity_horizontal_falloff = settings.virtual_light_map.horizontal_falloff;
    }
    if (!json.contains("offset_vertical_falloff") && has_legacy_v) {
        settings.virtual_light_map.offset_vertical_falloff = settings.virtual_light_map.vertical_falloff;
    }
    if (!json.contains("opacity_vertical_falloff") && has_legacy_v) {
        settings.virtual_light_map.opacity_vertical_falloff = settings.virtual_light_map.vertical_falloff;
    }
    settings.virtual_light_map.max_offset_x =
        read_float(json, "max_offset_x", settings.virtual_light_map.max_offset_x);
    settings.virtual_light_map.max_offset_y =
        read_float(json, "max_offset_y", settings.virtual_light_map.max_offset_y);
    settings.virtual_light_map.map_light_dir_offset_strength = read_float( json, "map_light_dir_offset_strength", settings.virtual_light_map.map_light_dir_offset_strength);
    // Legacy shared search radius
    const bool has_legacy_radius = json.contains("search_radius");
    settings.virtual_light_map.search_radius =
        read_int(json, "search_radius", settings.virtual_light_map.search_radius);
    // New split radii
    settings.virtual_light_map.offset_search_radius =
        read_int(json, "offset_search_radius", settings.virtual_light_map.offset_search_radius);
    settings.virtual_light_map.opacity_search_radius =
        read_int(json, "opacity_search_radius", settings.virtual_light_map.opacity_search_radius);
    if (!json.contains("offset_search_radius") && has_legacy_radius) {
        settings.virtual_light_map.offset_search_radius = settings.virtual_light_map.search_radius;
    }
    if (!json.contains("opacity_search_radius") && has_legacy_radius) {
        settings.virtual_light_map.opacity_search_radius = settings.virtual_light_map.search_radius;
    }
    settings.virtual_light_map.grid_subdivide =
        read_int(json, "grid_subdivide", settings.virtual_light_map.grid_subdivide);
    settings.virtual_light_map.light_grid_subdivide =
        read_int(json, "light_grid_subdivide", settings.virtual_light_map.light_grid_subdivide);
    settings.opacity_sensitivity_percent =
        read_float(json, "opacity_sensitivity_percent", settings.opacity_sensitivity_percent);
    settings.opacity_strength = read_float(json, "opacity_strength", settings.opacity_strength);
    settings.frame_blend_falloff_frames =
        read_int(json, "frame_blend_falloff_frames", settings.frame_blend_falloff_frames);

    // New toggles and opacity controls
    settings.virtual_light_map.enable_offset = json.contains("enable_offset")
        ? (json["enable_offset"].is_boolean() ? json["enable_offset"].get<bool>()
                                               : (read_int(json, "enable_offset", settings.virtual_light_map.enable_offset ? 1 : 0) != 0))
        : settings.virtual_light_map.enable_offset;
    settings.virtual_light_map.enable_opacity = json.contains("enable_opacity")
        ? (json["enable_opacity"].is_boolean() ? json["enable_opacity"].get<bool>()
                                                : (read_int(json, "enable_opacity", settings.virtual_light_map.enable_opacity ? 1 : 0) != 0))
        : settings.virtual_light_map.enable_opacity;
    settings.virtual_light_map.min_opacity = read_float(json, "min_opacity", settings.virtual_light_map.min_opacity);
    settings.virtual_light_map.max_opacity = read_float(json, "max_opacity", settings.virtual_light_map.max_opacity);
    // Opacity boost accepts -1..1 or -100..100, try both
    {
        float boost = settings.virtual_light_map.opacity_boost;
        if (json.contains("opacity_boost")) {
            boost = read_float(json, "opacity_boost", boost);
            // Heuristic: if boost seems like percentage, map to -1..1
            if (std::abs(boost) > 1.0f) {
                boost = boost / 100.0f;
            }
        }
        settings.virtual_light_map.opacity_boost = boost;
    }

    auto parse_lut = [&](const nlohmann::json& source) {
        if (!source.is_array()) {
            return;
        }
        std::vector<ReactiveShadowSettings::ShadowResponseLutEntry> entries;
        entries.reserve(source.size());
        for (const auto& entry : source) {
            if (!entry.is_object()) {
                continue;
            }
            ReactiveShadowSettings::ShadowResponseLutEntry node{};
            node.brightness = read_float(entry, "brightness", node.brightness);
            node.opacity    = read_float(entry, "opacity", node.opacity);
            node.offset     = read_float(entry, "offset", node.offset);
            node.scale      = read_float(entry, "scale", node.scale);
            entries.push_back(node);
        }
        if (!entries.empty()) {
            settings.response_lut.entries = std::move(entries);
        }
};

    if (auto lut_it = json.find("shadow_lut"); lut_it != json.end()) {
        parse_lut(*lut_it);
    } else if (auto alt_it = json.find("brightness_lut"); alt_it != json.end()) {
        parse_lut(*alt_it);
    }

    if (auto weights_it = json.find("sampling_weights"); weights_it != json.end() && weights_it->is_object()) {
        settings.sampling_weights.static_weight =
            read_float(*weights_it, "static_weight", settings.sampling_weights.static_weight);
        settings.sampling_weights.static_weight =
            read_float(*weights_it, "static", settings.sampling_weights.static_weight);
        settings.sampling_weights.dynamic_weight =
            read_float(*weights_it, "dynamic_weight", settings.sampling_weights.dynamic_weight);
        settings.sampling_weights.dynamic_weight =
            read_float(*weights_it, "dynamic", settings.sampling_weights.dynamic_weight);
    }
}

}

ReactiveShadowSettings reactive_shadow_settings_from_json(const nlohmann::json& json,
                                                          const ReactiveShadowSettings& defaults) {
    ReactiveShadowSettings result = defaults;
    if (!json.is_object()) {
        return sanitize_reactive_shadow_settings(result);
    }

    if (auto it = json.find("virtual_light_map"); it != json.end() && it->is_object()) {
        populate(result, *it);
    }

    populate(result, json);

    return sanitize_reactive_shadow_settings(result);
}

void assign_reactive_shadow_settings(nlohmann::json& json, const ReactiveShadowSettings& settings) {
    const ReactiveShadowSettings sanitized = sanitize_reactive_shadow_settings(settings);

    json = nlohmann::json::object();
    json["virtual_light_map"] = nlohmann::json::object({
        // Keep legacy fields populated to ease migration, though new fields are authoritative
        { "horizontal_falloff", sanitized.virtual_light_map.horizontal_falloff },
        { "vertical_falloff", sanitized.virtual_light_map.vertical_falloff },
        { "offset_horizontal_falloff", sanitized.virtual_light_map.offset_horizontal_falloff },
        { "offset_vertical_falloff", sanitized.virtual_light_map.offset_vertical_falloff },
        { "opacity_horizontal_falloff", sanitized.virtual_light_map.opacity_horizontal_falloff },
        { "opacity_vertical_falloff", sanitized.virtual_light_map.opacity_vertical_falloff },
        { "max_offset_x", sanitized.virtual_light_map.max_offset_x },
        { "max_offset_y", sanitized.virtual_light_map.max_offset_y },
        { "map_light_dir_offset_strength", sanitized.virtual_light_map.map_light_dir_offset_strength },
        { "search_radius", sanitized.virtual_light_map.search_radius },
        { "offset_search_radius", sanitized.virtual_light_map.offset_search_radius },
        { "opacity_search_radius", sanitized.virtual_light_map.opacity_search_radius },
        { "enable_offset", sanitized.virtual_light_map.enable_offset },
        { "enable_opacity", sanitized.virtual_light_map.enable_opacity },
        { "min_opacity", sanitized.virtual_light_map.min_opacity },
        { "max_opacity", sanitized.virtual_light_map.max_opacity },
        { "opacity_boost", sanitized.virtual_light_map.opacity_boost },
        { "grid_subdivide", sanitized.virtual_light_map.grid_subdivide },
        { "light_grid_subdivide", sanitized.virtual_light_map.light_grid_subdivide }
    });
    json["opacity_sensitivity_percent"] = sanitized.opacity_sensitivity_percent;
    json["opacity_strength"]  = sanitized.opacity_strength;
    json["frame_blend_falloff_frames"] = sanitized.frame_blend_falloff_frames;

    nlohmann::json lut = nlohmann::json::array();
    for (const auto& entry : sanitized.response_lut.entries) {
        lut.push_back(nlohmann::json::object({
            { "brightness", entry.brightness },
            { "opacity", entry.opacity },
            { "offset", entry.offset },
            { "scale", entry.scale }
        }));
    }
    json["shadow_lut"] = std::move(lut);

    json["sampling_weights"] = nlohmann::json::object({
        { "static_weight", sanitized.sampling_weights.static_weight },
        { "dynamic_weight", sanitized.sampling_weights.dynamic_weight }
    });
}

}

