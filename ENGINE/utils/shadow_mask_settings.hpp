#pragma once

#include <algorithm>

struct ShadowMaskSettings {
    float expansion_ratio = 0.8f;
    float blur_scale      = 1.0f;
    float falloff_start   = 0.0f;
    float falloff_exponent = 1.05f;
    float alpha_multiplier = 1.0f;
    int chunk_resolution = 3;
};

inline ShadowMaskSettings SanitizeShadowMaskSettings(const ShadowMaskSettings& settings) {
    ShadowMaskSettings sanitized = settings;
    sanitized.expansion_ratio = std::clamp(sanitized.expansion_ratio, 0.0f, 4.0f);
    sanitized.blur_scale = std::clamp(sanitized.blur_scale, 0.0f, 8.0f);
    sanitized.falloff_start = std::clamp(sanitized.falloff_start, 0.0f, 0.99f);
    sanitized.falloff_exponent = std::max(0.01f, sanitized.falloff_exponent);
    sanitized.alpha_multiplier = std::clamp(sanitized.alpha_multiplier, 0.0f, 4.0f);
    return sanitized;
}
