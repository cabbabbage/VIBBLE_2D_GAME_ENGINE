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
    settings.virtual_light_map.horizontal_falloff =
        read_float(json, "horizontal_falloff", settings.virtual_light_map.horizontal_falloff);
    settings.virtual_light_map.vertical_falloff =
        read_float(json, "vertical_falloff", settings.virtual_light_map.vertical_falloff);
    settings.virtual_light_map.max_offset_x =
        read_float(json, "max_offset_x", settings.virtual_light_map.max_offset_x);
    settings.virtual_light_map.max_offset_y =
        read_float(json, "max_offset_y", settings.virtual_light_map.max_offset_y);
    settings.virtual_light_map.shadow_scale =
        read_float(json, "shadow_scale", settings.virtual_light_map.shadow_scale);
    settings.virtual_light_map.min_scale_percent =
        read_int(json, "min_scale_percent", settings.virtual_light_map.min_scale_percent);
    settings.virtual_light_map.max_scale_percent =
        read_int(json, "max_scale_percent", settings.virtual_light_map.max_scale_percent);
    settings.virtual_light_map.map_light_dir_offset_strength = read_float(
        json,
        "map_light_dir_offset_strength",
        settings.virtual_light_map.map_light_dir_offset_strength);
    settings.virtual_light_map.parallax_percent =
        read_float(json, "parallax_percent", settings.virtual_light_map.parallax_percent);
    settings.virtual_light_map.search_radius =
        read_int(json, "search_radius", settings.virtual_light_map.search_radius);
    settings.opacity_sensitivity_percent =
        read_float(json, "opacity_sensitivity_percent", settings.opacity_sensitivity_percent);
    settings.opacity_strength = read_float(json, "opacity_strength", settings.opacity_strength);
    settings.parallax_strength = read_float(json, "parallax_strength", settings.parallax_strength);
    settings.scale_strength = read_float(json, "scale_strength", settings.scale_strength);

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

}  // namespace

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
        { "horizontal_falloff", sanitized.virtual_light_map.horizontal_falloff },
        { "vertical_falloff", sanitized.virtual_light_map.vertical_falloff },
        { "max_offset_x", sanitized.virtual_light_map.max_offset_x },
        { "max_offset_y", sanitized.virtual_light_map.max_offset_y },
        { "shadow_scale", sanitized.virtual_light_map.shadow_scale },
        { "min_scale_percent", sanitized.virtual_light_map.min_scale_percent },
        { "max_scale_percent", sanitized.virtual_light_map.max_scale_percent },
        { "map_light_dir_offset_strength", sanitized.virtual_light_map.map_light_dir_offset_strength },
        { "parallax_percent", sanitized.virtual_light_map.parallax_percent },
        { "search_radius", sanitized.virtual_light_map.search_radius }
    });
    json["opacity_sensitivity_percent"] = sanitized.opacity_sensitivity_percent;
    json["opacity_strength"]  = sanitized.opacity_strength;
    json["parallax_strength"] = sanitized.parallax_strength;
    json["scale_strength"]    = sanitized.scale_strength;

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

}  // namespace render_pipeline::shading

