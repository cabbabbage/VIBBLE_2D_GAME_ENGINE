#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"

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
    directionality.enable_offsets     = read_bool(section, "enable_offsets", directionality.enable_offsets);
    directionality.gradient_deadzone  = read_float(section, "gradient_deadzone", directionality.gradient_deadzone);
    directionality.gradient_max       = read_float(section, "gradient_max", directionality.gradient_max);
    directionality.offset_ratio_x     = read_float(section, "offset_ratio_x", directionality.offset_ratio_x);
    directionality.offset_ratio_y     = read_float(section, "offset_ratio_y", directionality.offset_ratio_y);
    directionality.offset_x_bias      = read_float(section, "offset_x_bias", directionality.offset_x_bias);
    directionality.offset_y_bias      = read_float(section, "offset_y_bias", directionality.offset_y_bias);
    directionality.offset_max_ratio_x = read_float(section, "offset_max_ratio_x", directionality.offset_max_ratio_x);
    directionality.offset_max_ratio_y = read_float(section, "offset_max_ratio_y", directionality.offset_max_ratio_y);
}

void populate_from_section(ReactiveShadowSettings::Response& response, const nlohmann::json& section) {
    response.enable_scale          = read_bool(section, "enable_scale", response.enable_scale);
    response.enable_opacity        = read_bool(section, "enable_opacity", response.enable_opacity);
    response.scale_strength        = read_float(section, "scale_strength", response.scale_strength);
    response.scale_front_limit     = read_float(section, "scale_front_limit", response.scale_front_limit);
    response.scale_back_limit      = read_float(section, "scale_back_limit", response.scale_back_limit);
    response.scale_min             = read_float(section, "scale_min", response.scale_min);
    response.scale_max             = read_float(section, "scale_max", response.scale_max);
    response.opacity_gamma         = read_float(section, "opacity_gamma", response.opacity_gamma);
    response.opacity_min_factor    = read_float(section, "opacity_min_factor", response.opacity_min_factor);
    response.opacity_max_factor    = read_float(section, "opacity_max_factor", response.opacity_max_factor);
    response.absolute_opacity_min  = read_float(section, "absolute_opacity_min", response.absolute_opacity_min);
    response.absolute_opacity_max  = read_float(section, "absolute_opacity_max", response.absolute_opacity_max);
    response.brightness_floor      = read_float(section, "brightness_floor", response.brightness_floor);
}

void populate_from_section(ReactiveShadowSettings::Stability& stability, const nlohmann::json& section) {
    stability.enable_temporal_smoothing = read_bool(section, "enable_temporal_smoothing", stability.enable_temporal_smoothing);
    stability.temporal_smoothing        = read_float(section, "temporal_smoothing", stability.temporal_smoothing);
}

void populate_from_flat(ReactiveShadowSettings& settings, const nlohmann::json& json) {
    populate_from_section(settings.sampling, json);
    populate_from_section(settings.directionality, json);
    populate_from_section(settings.response, json);
    populate_from_section(settings.stability, json);
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
        { "gradient_deadzone", sanitized.directionality.gradient_deadzone },
        { "gradient_max", sanitized.directionality.gradient_max },
        { "offset_ratio_x", sanitized.directionality.offset_ratio_x },
        { "offset_ratio_y", sanitized.directionality.offset_ratio_y },
        { "offset_x_bias", sanitized.directionality.offset_x_bias },
        { "offset_y_bias", sanitized.directionality.offset_y_bias },
        { "offset_max_ratio_x", sanitized.directionality.offset_max_ratio_x },
        { "offset_max_ratio_y", sanitized.directionality.offset_max_ratio_y }
    });

    json["response"] = nlohmann::json::object({
        { "enable_scale", sanitized.response.enable_scale },
        { "enable_opacity", sanitized.response.enable_opacity },
        { "scale_strength", sanitized.response.scale_strength },
        { "scale_front_limit", sanitized.response.scale_front_limit },
        { "scale_back_limit", sanitized.response.scale_back_limit },
        { "scale_min", sanitized.response.scale_min },
        { "scale_max", sanitized.response.scale_max },
        { "opacity_gamma", sanitized.response.opacity_gamma },
        { "opacity_min_factor", sanitized.response.opacity_min_factor },
        { "opacity_max_factor", sanitized.response.opacity_max_factor },
        { "absolute_opacity_min", sanitized.response.absolute_opacity_min },
        { "absolute_opacity_max", sanitized.response.absolute_opacity_max },
        { "brightness_floor", sanitized.response.brightness_floor }
    });

    json["stability"] = nlohmann::json::object({
        { "enable_temporal_smoothing", sanitized.stability.enable_temporal_smoothing },
        { "temporal_smoothing", sanitized.stability.temporal_smoothing }
    });
}

}  // namespace render_pipeline::shading

