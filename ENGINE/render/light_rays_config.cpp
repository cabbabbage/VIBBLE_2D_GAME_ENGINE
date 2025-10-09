#include "render/light_rays_config.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include <nlohmann/json.hpp>

namespace {
std::string to_string_metric(BrightnessMetric metric) {
    switch (metric) {
        case BrightnessMetric::Luma709:   return "Luma709";
        case BrightnessMetric::MaxRGB:    return "MaxRGB";
        case BrightnessMetric::AvgRGB:    return "AvgRGB";
        case BrightnessMetric::EnergyRGB: return "EnergyRGB";
    }
    return "MaxRGB";
}

BrightnessMetric from_string_metric(std::string value) {
    auto to_lower = [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); };
    std::transform(value.begin(), value.end(), value.begin(), to_lower);
    if (value == "luma709") return BrightnessMetric::Luma709;
    if (value == "avgrgb") return BrightnessMetric::AvgRGB;
    if (value == "energyrgb") return BrightnessMetric::EnergyRGB;
    return BrightnessMetric::MaxRGB;
}

int clamp_int(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

float clamp_float(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}
} // namespace

LightRaysConfig LightRaysConfig::defaults() {
    LightRaysConfig config;
    config.enabled = false;
    config.per_light_enabled = true;
    config.per_light.metric = BrightnessMetric::MaxRGB;
    config.per_light.gamma_comp = 1.2f;
    config.per_light.min_luma_threshold = 0.90f;
    config.per_light.bright_percentile = 0.995f;
    config.per_light.samples = 64;
    config.per_light.density = 0.9f;
    config.per_light.decay = 0.97f;
    config.per_light.weight = 0.75f;
    config.per_light.exposure = 0.9f;
    config.per_light.downsample_log2 = 2;
    config.per_light.use_alpha_in_mask = true;
    return config;
}

LightRaysConfig LightRaysConfig::from_json(const nlohmann::json& data) {
    LightRaysConfig config = LightRaysConfig::defaults();
    if (!data.is_object()) {
        return config;
    }

    config.enabled = data.value("enabled", config.enabled);
    config.per_light_enabled = data.value("per_light_enabled", config.per_light_enabled);
    config.per_light.use_alpha_in_mask = data.value("use_alpha_in_mask", config.per_light.use_alpha_in_mask);
    config.per_light.gamma_comp = data.value("gamma_comp", config.per_light.gamma_comp);
    config.per_light.min_luma_threshold = clamp_float(data.value("min_luma_threshold", config.per_light.min_luma_threshold), 0.0f, 1.0f);
    config.per_light.bright_percentile = clamp_float(data.value("bright_percentile", config.per_light.bright_percentile), 0.0f, 1.0f);
    config.per_light.samples = clamp_int(data.value("samples", config.per_light.samples), 1, 512);
    config.per_light.density = clamp_float(data.value("density", config.per_light.density), 0.0f, 8.0f);
    config.per_light.decay = clamp_float(data.value("decay", config.per_light.decay), 0.0f, 0.999f);
    config.per_light.weight = clamp_float(data.value("weight", config.per_light.weight), 0.0f, 8.0f);
    config.per_light.exposure = clamp_float(data.value("exposure", config.per_light.exposure), 0.0f, 8.0f);
    config.per_light.downsample_log2 = clamp_int(data.value("downsample_log2", config.per_light.downsample_log2), 0, 8);

    if (auto it = data.find("metric"); it != data.end() && it->is_string()) {
        config.per_light.metric = from_string_metric(it->get<std::string>());
    }

    return config;
}

nlohmann::json LightRaysConfig::to_json() const {
    nlohmann::json out = nlohmann::json::object();
    out["enabled"] = enabled;
    out["per_light_enabled"] = per_light_enabled;
    out["use_alpha_in_mask"] = per_light.use_alpha_in_mask;
    out["metric"] = to_string_metric(per_light.metric);
    out["gamma_comp"] = per_light.gamma_comp;
    out["min_luma_threshold"] = per_light.min_luma_threshold;
    out["bright_percentile"] = per_light.bright_percentile;
    out["samples"] = per_light.samples;
    out["density"] = per_light.density;
    out["decay"] = per_light.decay;
    out["weight"] = per_light.weight;
    out["exposure"] = per_light.exposure;
    out["downsample_log2"] = per_light.downsample_log2;
    return out;
}

LightRaysParams LightRaysConfig::to_light_rays_params() const {
    LightRaysParams params;
    params.use_alpha_in_mask = per_light.use_alpha_in_mask;
    params.metric = per_light.metric;
    params.gamma_comp = per_light.gamma_comp;
    params.min_luma_threshold = per_light.min_luma_threshold;
    params.bright_percentile = per_light.bright_percentile;
    params.samples = per_light.samples;
    params.density = per_light.density;
    params.decay = per_light.decay;
    params.weight = per_light.weight;
    params.exposure = per_light.exposure;
    params.downsample_log2 = per_light.downsample_log2;
    return params;
}

