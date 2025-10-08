#include "render/light_rays_config.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#include <nlohmann/json.hpp>

namespace {

template <typename T>
T clamp_value(T value, T min_v, T max_v) {
    return std::max(min_v, std::min(max_v, value));
}

using MetricPair = std::pair<std::string_view, BrightnessMetric>;

constexpr std::array<MetricPair, 4> kMetricPairs = { {
    {"Luma709",  BrightnessMetric::Luma709},
    {"MaxRGB",   BrightnessMetric::MaxRGB},
    {"AvgRGB",   BrightnessMetric::AvgRGB},
    {"EnergyRGB",BrightnessMetric::EnergyRGB},
} };

BrightnessMetric metric_from_json_value(const nlohmann::json& value, BrightnessMetric fallback) {
    if (value.is_string()) {
        return brightness_metric_from_string(value.get<std::string>());
    }
    if (value.is_number_integer()) {
        int idx = clamp_value(value.get<int>(), 0, static_cast<int>(kMetricPairs.size() - 1));
        return kMetricPairs[static_cast<std::size_t>(idx)].second;
    }
    return fallback;
}

void assign_if_bool(const nlohmann::json& src, const char* key, bool& out_value) {
    auto it = src.find(key);
    if (it != src.end() && it->is_boolean()) {
        out_value = it->get<bool>();
    }
}

void assign_if_float(const nlohmann::json& src, const char* key, float& out_value, float min_v, float max_v) {
    auto it = src.find(key);
    if (it != src.end() && it->is_number()) {
        out_value = clamp_value(static_cast<float>(it->get<double>()), min_v, max_v);
    }
}

void assign_if_int(const nlohmann::json& src, const char* key, int& out_value, int min_v, int max_v) {
    auto it = src.find(key);
    if (it != src.end() && it->is_number_integer()) {
        out_value = clamp_value(it->get<int>(), min_v, max_v);
    }
}

} // namespace

LightRaysConfig LightRaysConfig::defaults() {
    LightRaysConfig config;
    config.enabled = true;
    config.per_light_enabled = true;
    config.per_light.use_alpha_in_mask = true;
    config.per_light.metric = BrightnessMetric::MaxRGB;
    config.per_light.gamma_comp = 1.0f;
    config.per_light.min_luma_threshold = 0.80f;
    config.per_light.bright_percentile = 0.985f;
    config.per_light.samples = 64;
    config.per_light.density = 0.90f;
    config.per_light.decay = 0.97f;
    config.per_light.weight = 0.75f;
    config.per_light.exposure = 1.10f;
    config.per_light.downsample_log2 = 2;
    return config;
}

LightRaysConfig LightRaysConfig::from_json(const nlohmann::json& json) {
    LightRaysConfig config = LightRaysConfig::defaults();
    if (!json.is_object()) {
        return config;
    }

    assign_if_bool(json, "enabled", config.enabled);
    assign_if_bool(json, "final_blur_enabled", config.enabled); // legacy name
    assign_if_bool(json, "per_light_enabled", config.per_light_enabled);

    const nlohmann::json* per_light_section = &json;
    auto per_it = json.find("per_light");
    if (per_it != json.end() && per_it->is_object()) {
        per_light_section = &(*per_it);
    }

    assign_if_bool(*per_light_section, "use_alpha_in_mask", config.per_light.use_alpha_in_mask);

    if (auto metric_it = per_light_section->find("metric"); metric_it != per_light_section->end()) {
        config.per_light.metric = metric_from_json_value(*metric_it, config.per_light.metric);
    }

    assign_if_float(*per_light_section, "gamma_comp", config.per_light.gamma_comp, 0.01f, 8.0f);
    assign_if_float(*per_light_section, "min_luma_threshold", config.per_light.min_luma_threshold, 0.0f, 1.0f);
    assign_if_float(*per_light_section, "bright_percentile", config.per_light.bright_percentile, 0.0f, 1.0f);
    assign_if_int(*per_light_section, "samples", config.per_light.samples, 1, 512);
    assign_if_float(*per_light_section, "density", config.per_light.density, 0.0f, 8.0f);
    assign_if_float(*per_light_section, "decay", config.per_light.decay, 0.0f, 1.0f);
    assign_if_float(*per_light_section, "weight", config.per_light.weight, 0.0f, 8.0f);
    assign_if_float(*per_light_section, "exposure", config.per_light.exposure, 0.0f, 8.0f);
    assign_if_int(*per_light_section, "downsample_log2", config.per_light.downsample_log2, 0, 8);

    return config;
}

nlohmann::json LightRaysConfig::to_json() const {
    nlohmann::json json = nlohmann::json::object();
    json["enabled"] = enabled;
    json["per_light_enabled"] = per_light_enabled;

    json["use_alpha_in_mask"] = per_light.use_alpha_in_mask;
    json["metric"] = brightness_metric_to_string(per_light.metric);
    json["gamma_comp"] = per_light.gamma_comp;
    json["min_luma_threshold"] = per_light.min_luma_threshold;
    json["bright_percentile"] = per_light.bright_percentile;
    json["samples"] = per_light.samples;
    json["density"] = per_light.density;
    json["decay"] = per_light.decay;
    json["weight"] = per_light.weight;
    json["exposure"] = per_light.exposure;
    json["downsample_log2"] = per_light.downsample_log2;
    return json;
}

LightRaysParams to_light_rays_params(const LightRaysPerLightConfig& config) {
    LightRaysParams params;
    params.min_luma_threshold = config.min_luma_threshold;
    params.bright_percentile = config.bright_percentile;
    params.samples = config.samples;
    params.density = config.density;
    params.decay = config.decay;
    params.weight = config.weight;
    params.exposure = config.exposure;
    params.downsample_log2 = config.downsample_log2;
    return params;
}

std::string brightness_metric_to_string(BrightnessMetric metric) {
    for (const auto& pair : kMetricPairs) {
        if (pair.second == metric) {
            return std::string(pair.first);
        }
    }
    return std::string(kMetricPairs.front().first);
}

BrightnessMetric brightness_metric_from_string(const std::string& value) {
    for (const auto& pair : kMetricPairs) {
        if (pair.first == value) {
            return pair.second;
        }
    }
    return BrightnessMetric::MaxRGB;
}

