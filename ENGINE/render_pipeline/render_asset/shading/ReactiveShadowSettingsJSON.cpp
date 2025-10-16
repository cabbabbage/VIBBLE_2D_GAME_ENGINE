#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"

#include <algorithm>
#include <cmath>
#include <string>

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
    settings.virtual_light_map.size_scale_factor =
        read_float(json, "size_scale_factor", settings.virtual_light_map.size_scale_factor);
    settings.virtual_light_map.map_light_factor =
        read_float(json, "map_light_factor", settings.virtual_light_map.map_light_factor);
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
        { "size_scale_factor", sanitized.virtual_light_map.size_scale_factor },
        { "map_light_factor", sanitized.virtual_light_map.map_light_factor }
    });
}

}  // namespace render_pipeline::shading

