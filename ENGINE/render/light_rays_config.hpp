#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

#include "render/light_rays.hpp"

struct LightRaysConfig {
    struct PerLight {
        bool use_alpha_in_mask = true;
        BrightnessMetric metric = BrightnessMetric::MaxRGB;
        float gamma_comp = 1.0f;
        float min_luma_threshold = 0.90f;
        float bright_percentile = 0.995f;
        int   samples = 64;
        float density = 0.9f;
        float decay = 0.97f;
        float weight = 0.75f;
        float exposure = 0.9f;
        int   downsample_log2 = 2;
};

    bool enabled = false;
    bool per_light_enabled = true;
    PerLight per_light{};

    static LightRaysConfig defaults();

    static LightRaysConfig from_json(const nlohmann::json& data);
    nlohmann::json to_json() const;

    LightRaysParams to_light_rays_params() const;
};

