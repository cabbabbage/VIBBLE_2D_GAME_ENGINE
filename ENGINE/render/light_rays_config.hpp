#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>

#include "render/light_rays.hpp"

enum class BrightnessMetric {
    Luma709,
    MaxRGB,
    AvgRGB,
    EnergyRGB,
};

struct LightRaysPerLightConfig {
    bool use_alpha_in_mask = true;
    BrightnessMetric metric = BrightnessMetric::MaxRGB;
    float gamma_comp = 1.0f;
    float min_luma_threshold = 0.80f;
    float bright_percentile = 0.985f;
    int   samples = 64;
    float density = 0.90f;
    float decay = 0.97f;
    float weight = 0.75f;
    float exposure = 1.10f;
    int   downsample_log2 = 2;
};

struct LightRaysConfig {
    bool enabled = true;
    bool per_light_enabled = true;
    LightRaysPerLightConfig per_light{};

    static LightRaysConfig defaults();
    static LightRaysConfig from_json(const nlohmann::json& json);
    nlohmann::json to_json() const;
};

LightRaysParams to_light_rays_params(const LightRaysPerLightConfig& config);

std::string brightness_metric_to_string(BrightnessMetric metric);
BrightnessMetric brightness_metric_from_string(const std::string& value);

