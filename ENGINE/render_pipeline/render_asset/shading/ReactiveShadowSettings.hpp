#pragma once

#include <algorithm>
#include <tuple>

namespace render_pipeline::shading {

struct ReactiveShadowSettings {
    struct Sampling {
        int   kernel_radius     = 2;
        float outer_ring_weight = 1.2f;
        float diagonal_weight   = 0.5f;

        bool operator==(const Sampling& other) const {
            return kernel_radius == other.kernel_radius &&
                   outer_ring_weight == other.outer_ring_weight &&
                   diagonal_weight == other.diagonal_weight;
        }
        bool operator!=(const Sampling& other) const { return !(*this == other); }
    } sampling;

    struct Directionality {
        bool  enable_offsets        = true;
        float gradient_deadzone     = 0.06f;
        float gradient_max          = 0.6f;
        float offset_ratio_x        = 0.25f;
        float offset_ratio_y        = 0.18f;
        float offset_x_bias         = 1.25f;
        float offset_y_bias         = 0.9f;
        float offset_max_ratio_x    = 0.35f;
        float offset_max_ratio_y    = 0.25f;

        bool operator==(const Directionality& other) const {
            return enable_offsets == other.enable_offsets &&
                   gradient_deadzone == other.gradient_deadzone &&
                   gradient_max == other.gradient_max &&
                   offset_ratio_x == other.offset_ratio_x &&
                   offset_ratio_y == other.offset_ratio_y &&
                   offset_x_bias == other.offset_x_bias &&
                   offset_y_bias == other.offset_y_bias &&
                   offset_max_ratio_x == other.offset_max_ratio_x &&
                   offset_max_ratio_y == other.offset_max_ratio_y;
        }
        bool operator!=(const Directionality& other) const { return !(*this == other); }
    } directionality;

    struct Response {
        bool  enable_scale          = true;
        bool  enable_opacity        = true;
        float scale_strength        = 0.3f;
        float scale_front_limit     = 0.35f;
        float scale_back_limit      = 0.22f;
        float scale_min             = 0.5f;
        float scale_max             = 1.6f;
        float opacity_gamma         = 1.4f;
        float opacity_min_factor    = 0.45f;
        float opacity_max_factor    = 1.35f;
        float absolute_opacity_min  = 0.1f;
        float absolute_opacity_max  = 1.0f;
        float brightness_floor      = 0.05f;

        bool operator==(const Response& other) const {
            return enable_scale == other.enable_scale &&
                   enable_opacity == other.enable_opacity &&
                   scale_strength == other.scale_strength &&
                   scale_front_limit == other.scale_front_limit &&
                   scale_back_limit == other.scale_back_limit &&
                   scale_min == other.scale_min &&
                   scale_max == other.scale_max &&
                   opacity_gamma == other.opacity_gamma &&
                   opacity_min_factor == other.opacity_min_factor &&
                   opacity_max_factor == other.opacity_max_factor &&
                   absolute_opacity_min == other.absolute_opacity_min &&
                   absolute_opacity_max == other.absolute_opacity_max &&
                   brightness_floor == other.brightness_floor;
        }
        bool operator!=(const Response& other) const { return !(*this == other); }
    } response;

    struct Stability {
        bool  enable_temporal_smoothing = true;
        float temporal_smoothing        = 0.6f;

        bool operator==(const Stability& other) const {
            return enable_temporal_smoothing == other.enable_temporal_smoothing &&
                   temporal_smoothing == other.temporal_smoothing;
        }
        bool operator!=(const Stability& other) const { return !(*this == other); }
    } stability;

    bool operator==(const ReactiveShadowSettings& other) const {
        return sampling == other.sampling &&
               directionality == other.directionality &&
               response == other.response &&
               stability == other.stability;
    }
    bool operator!=(const ReactiveShadowSettings& other) const { return !(*this == other); }
};

inline float clampf(float value, float min_value, float max_value) {
    return std::max(min_value, std::min(value, max_value));
}

inline ReactiveShadowSettings sanitize_reactive_shadow_settings(const ReactiveShadowSettings& raw) {
    ReactiveShadowSettings out = raw;
    out.sampling.kernel_radius = std::clamp(out.sampling.kernel_radius, 1, 16);
    out.sampling.outer_ring_weight = clampf(out.sampling.outer_ring_weight, 0.0f, 5.0f);
    out.sampling.diagonal_weight   = clampf(out.sampling.diagonal_weight, 0.0f, 5.0f);

    out.directionality.gradient_deadzone  = clampf(out.directionality.gradient_deadzone, 0.0f, 1.0f);
    out.directionality.gradient_max       = clampf(out.directionality.gradient_max,
                                                  out.directionality.gradient_deadzone + 1e-4f,
                                                  2.5f);
    out.directionality.offset_ratio_x     = clampf(out.directionality.offset_ratio_x, 0.0f, 1.0f);
    out.directionality.offset_ratio_y     = clampf(out.directionality.offset_ratio_y, 0.0f, 1.0f);
    out.directionality.offset_x_bias      = clampf(out.directionality.offset_x_bias, 0.0f, 3.0f);
    out.directionality.offset_y_bias      = clampf(out.directionality.offset_y_bias, 0.0f, 3.0f);
    out.directionality.offset_max_ratio_x = clampf(out.directionality.offset_max_ratio_x, 0.0f, 1.0f);
    out.directionality.offset_max_ratio_y = clampf(out.directionality.offset_max_ratio_y, 0.0f, 1.0f);

    out.response.scale_strength       = clampf(out.response.scale_strength, 0.0f, 1.0f);
    out.response.scale_front_limit    = clampf(out.response.scale_front_limit, 0.0f, 1.0f);
    out.response.scale_back_limit     = clampf(out.response.scale_back_limit, 0.0f, 1.0f);
    out.response.scale_min            = clampf(out.response.scale_min, 0.1f, 4.0f);
    out.response.scale_max            = clampf(out.response.scale_max,
                                               std::max(out.response.scale_min, 0.1f), 4.0f);
    out.response.opacity_gamma        = clampf(out.response.opacity_gamma, 0.0f, 4.0f);
    out.response.opacity_min_factor   = clampf(out.response.opacity_min_factor, 0.1f, 3.0f);
    out.response.opacity_max_factor   = clampf(out.response.opacity_max_factor,
                                               std::max(out.response.opacity_min_factor, 0.1f),
                                               3.0f);
    out.response.absolute_opacity_min = clampf(out.response.absolute_opacity_min, 0.0f, 1.0f);
    out.response.absolute_opacity_max = clampf(out.response.absolute_opacity_max,
                                               std::max(out.response.absolute_opacity_min, 0.0f),
                                               1.0f);
    out.response.brightness_floor     = clampf(out.response.brightness_floor, 0.0f, 1.0f);

    out.stability.temporal_smoothing = clampf(out.stability.temporal_smoothing, 0.0f, 0.999f);

    return out;
}

}  // namespace render_pipeline::shading

