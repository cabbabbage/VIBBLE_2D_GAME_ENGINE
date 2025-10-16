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
            size_t idx = 0;
            int parsed = std::stoi(text, &idx);
            if (idx == text.size()) {
                return parsed;
            }
        }
    } catch (...) {
    }
    return fallback;
}

bool read_bool(const nlohmann::json& obj, const char* key, bool fallback) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return fallback;
    }
    try {
        if (it->is_boolean()) {
            return it->get<bool>();
        }
        if (it->is_number_integer()) {
            return it->get<int>() != 0;
        }
        if (it->is_string()) {
            const std::string text = it->get<std::string>();
            if (text == "true" || text == "1") {
                return true;
            }
            if (text == "false" || text == "0") {
                return false;
            }
        }
    } catch (...) {
    }
    return fallback;
}

void populate_from_section(ReactiveShadowSettings::Sampling& sampling, const nlohmann::json& section) {
    sampling.kernel_radius     = read_int(section, "kernel_radius", sampling.kernel_radius);
    sampling.outer_ring_weight = read_float(section, "outer_ring_weight", sampling.outer_ring_weight);
    sampling.diagonal_weight   = read_float(section, "diagonal_weight", sampling.diagonal_weight);
}

void populate_from_section(ReactiveShadowSettings::Directionality& directionality, const nlohmann::json& section) {
    directionality.enable_offsets = read_bool(section, "enable_offsets", directionality.enable_offsets);
    if (section.contains("gradient_sensitivity")) {
        directionality.gradient_sensitivity =
            read_float(section, "gradient_sensitivity", directionality.gradient_sensitivity);
    } else {
        directionality.gradient_sensitivity =
            read_float(section, "gradient_deadzone", directionality.gradient_sensitivity);
    }

    if (section.contains("offset_strength")) {
        directionality.offset_strength =
            read_float(section, "offset_strength", directionality.offset_strength);
    } else {
        const float ratio_x = read_float(section, "offset_ratio_x", directionality.offset_strength);
        const float ratio_y = read_float(section, "offset_ratio_y", directionality.offset_strength);
        const float bias_x  = read_float(section, "offset_x_bias", 1.0f);
        const float bias_y  = read_float(section, "offset_y_bias", 1.0f);
        directionality.offset_strength = std::max(ratio_x * bias_x, ratio_y * bias_y);
    }

    if (section.contains("max_offset_ratio")) {
        directionality.max_offset_ratio =
            read_float(section, "max_offset_ratio", directionality.max_offset_ratio);
    } else {
        const float max_ratio_x = read_float(section, "offset_max_ratio_x", directionality.max_offset_ratio);
        const float max_ratio_y = read_float(section, "offset_max_ratio_y", directionality.max_offset_ratio);
        directionality.max_offset_ratio = std::max(max_ratio_x, max_ratio_y);
    }

    directionality.front_weight = read_float(section, "front_weight", directionality.front_weight);
    directionality.side_weight  = read_float(section, "side_weight", directionality.side_weight);
    directionality.back_weight  = read_float(section, "back_weight", directionality.back_weight);
}

void populate_from_section(ReactiveShadowSettings::Response& response, const nlohmann::json& section) {
    response.enable_opacity = read_bool(section, "enable_opacity", response.enable_opacity);
    if (section.contains("opacity_strength")) {
        response.opacity_strength = read_float(section, "opacity_strength", response.opacity_strength);
    } else {
        response.opacity_strength = read_float(section, "opacity_gamma", response.opacity_strength);
    }

    if (section.contains("min_opacity")) {
        response.min_opacity = read_float(section, "min_opacity", response.min_opacity);
    } else {
        response.min_opacity = read_float(section, "absolute_opacity_min", response.min_opacity);
    }

    if (section.contains("max_opacity")) {
        response.max_opacity = read_float(section, "max_opacity", response.max_opacity);
    } else {
        response.max_opacity = read_float(section, "absolute_opacity_max", response.max_opacity);
    }

    response.front_opacity_boost = read_float(section, "front_opacity_boost", response.front_opacity_boost);
}

void populate_from_section(ReactiveShadowSettings::Stability& stability, const nlohmann::json& section) {
    stability.enable_temporal_smoothing = read_bool(section, "enable_temporal_smoothing", stability.enable_temporal_smoothing);
    stability.temporal_smoothing        = read_float(section, "temporal_smoothing", stability.temporal_smoothing);
    stability.reuse_similarity_threshold =
        read_float(section, "reuse_similarity_threshold", stability.reuse_similarity_threshold);
}

void populate_from_section(ReactiveShadowSettings::Output& output, const nlohmann::json& section) {
    output.scale_factor      = read_float(section, "scale_factor", output.scale_factor);
    output.map_line_weight   = read_float(section, "map_line_weight", output.map_line_weight);
    output.parallax_strength = read_float(section, "parallax_strength", output.parallax_strength);
}

void populate_from_section(ReactiveShadowSettings::VirtualLightMapSettings& map_cfg,
                           const nlohmann::json& section) {
    map_cfg.map_light_factor   = read_float(section, "map_light_factor", map_cfg.map_light_factor);
    map_cfg.horizontal_falloff = read_float(section, "horizontal_falloff", map_cfg.horizontal_falloff);
    map_cfg.vertical_falloff   = read_float(section, "vertical_falloff", map_cfg.vertical_falloff);
    map_cfg.max_offset_x       = read_float(section, "max_offset_x", map_cfg.max_offset_x);
    map_cfg.max_offset_y       = read_float(section, "max_offset_y", map_cfg.max_offset_y);
    map_cfg.shadow_scale       = read_float(section, "shadow_scale", map_cfg.shadow_scale);
}

void populate_from_flat(ReactiveShadowSettings& settings, const nlohmann::json& json) {
    populate_from_section(settings.sampling, json);
    populate_from_section(settings.directionality, json);
    populate_from_section(settings.response, json);
    populate_from_section(settings.stability, json);
    populate_from_section(settings.output, json);
    populate_from_section(settings.virtual_light_map, json);
}

}  // namespace

ReactiveShadowSettings reactive_shadow_settings_from_json(const nlohmann::json& json,
                                                          const ReactiveShadowSettings& defaults) {
    ReactiveShadowSettings result = defaults;
    if (!json.is_object()) {
        return sanitize_reactive_shadow_settings(result);
    }

    if (auto sampling_it = json.find("sampling"); sampling_it != json.end() && sampling_it->is_object()) {
        populate_from_section(result.sampling, *sampling_it);
    }
    if (auto direction_it = json.find("directionality"); direction_it != json.end() && direction_it->is_object()) {
        populate_from_section(result.directionality, *direction_it);
    }
    if (auto response_it = json.find("response"); response_it != json.end() && response_it->is_object()) {
        populate_from_section(result.response, *response_it);
    }
    if (auto stability_it = json.find("stability"); stability_it != json.end() && stability_it->is_object()) {
        populate_from_section(result.stability, *stability_it);
    }
    if (auto output_it = json.find("output"); output_it != json.end() && output_it->is_object()) {
        populate_from_section(result.output, *output_it);
    }
    if (auto vlm_it = json.find("virtual_light_map"); vlm_it != json.end() && vlm_it->is_object()) {
        populate_from_section(result.virtual_light_map, *vlm_it);
    }

    // Support flat legacy layout if present.
    populate_from_flat(result, json);

    return sanitize_reactive_shadow_settings(result);
}

void assign_reactive_shadow_settings(nlohmann::json& json, const ReactiveShadowSettings& settings) {
    const ReactiveShadowSettings sanitized = sanitize_reactive_shadow_settings(settings);
    json = nlohmann::json::object();
    json["sampling"] = nlohmann::json::object({
        { "kernel_radius", sanitized.sampling.kernel_radius },
        { "outer_ring_weight", sanitized.sampling.outer_ring_weight },
        { "diagonal_weight", sanitized.sampling.diagonal_weight }
    });

    json["directionality"] = nlohmann::json::object({
        { "enable_offsets", sanitized.directionality.enable_offsets },
        { "gradient_sensitivity", sanitized.directionality.gradient_sensitivity },
        { "offset_strength", sanitized.directionality.offset_strength },
        { "max_offset_ratio", sanitized.directionality.max_offset_ratio },
        { "front_weight", sanitized.directionality.front_weight },
        { "side_weight", sanitized.directionality.side_weight },
        { "back_weight", sanitized.directionality.back_weight }
    });

    json["response"] = nlohmann::json::object({
        { "enable_opacity", sanitized.response.enable_opacity },
        { "opacity_strength", sanitized.response.opacity_strength },
        { "min_opacity", sanitized.response.min_opacity },
        { "max_opacity", sanitized.response.max_opacity },
        { "front_opacity_boost", sanitized.response.front_opacity_boost }
    });

    json["stability"] = nlohmann::json::object({
        { "enable_temporal_smoothing", sanitized.stability.enable_temporal_smoothing },
        { "temporal_smoothing", sanitized.stability.temporal_smoothing },
        { "reuse_similarity_threshold", sanitized.stability.reuse_similarity_threshold }
    });

    json["output"] = nlohmann::json::object({
        { "scale_factor", sanitized.output.scale_factor },
        { "map_line_weight", sanitized.output.map_line_weight },
        { "parallax_strength", sanitized.output.parallax_strength }
    });

    json["virtual_light_map"] = nlohmann::json::object({
        { "map_light_factor", sanitized.virtual_light_map.map_light_factor },
        { "horizontal_falloff", sanitized.virtual_light_map.horizontal_falloff },
        { "vertical_falloff", sanitized.virtual_light_map.vertical_falloff },
        { "max_offset_x", sanitized.virtual_light_map.max_offset_x },
        { "max_offset_y", sanitized.virtual_light_map.max_offset_y },
        { "shadow_scale", sanitized.virtual_light_map.shadow_scale }
    });
}

}  // namespace render_pipeline::shading

