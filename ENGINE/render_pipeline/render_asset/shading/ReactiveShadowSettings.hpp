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
        float gradient_sensitivity  = 0.1f;
        float offset_strength       = 0.5f;
        float max_offset_ratio      = 0.3f;

        bool operator==(const Directionality& other) const {
            return enable_offsets == other.enable_offsets &&
                   gradient_sensitivity == other.gradient_sensitivity &&
                   offset_strength == other.offset_strength &&
                   max_offset_ratio == other.max_offset_ratio;
        }
        bool operator!=(const Directionality& other) const { return !(*this == other); }
    } directionality;

    struct Response {
        bool  enable_opacity        = true;
        float opacity_strength      = 0.6f;
        float min_opacity           = 0.1f;
        float max_opacity           = 1.0f;

        bool operator==(const Response& other) const {
            return enable_opacity == other.enable_opacity &&
                   opacity_strength == other.opacity_strength &&
                   min_opacity == other.min_opacity &&
                   max_opacity == other.max_opacity;
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

    struct Output {
        float scale_factor      = 1.0f;
        float map_line_weight   = 0.0f;
        float parallax_strength = 0.0f;

        bool operator==(const Output& other) const {
            return scale_factor == other.scale_factor &&
                   map_line_weight == other.map_line_weight &&
                   parallax_strength == other.parallax_strength;
        }
        bool operator!=(const Output& other) const { return !(*this == other); }
    } output;

    bool operator==(const ReactiveShadowSettings& other) const {
        return sampling == other.sampling &&
               directionality == other.directionality &&
               response == other.response &&
               stability == other.stability &&
               output == other.output;
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

    out.directionality.gradient_sensitivity = clampf(out.directionality.gradient_sensitivity, 0.0f, 2.0f);
    out.directionality.offset_strength      = clampf(out.directionality.offset_strength, 0.0f, 2.0f);
    out.directionality.max_offset_ratio     = clampf(out.directionality.max_offset_ratio, 0.0f, 1.0f);

    out.response.opacity_strength = clampf(out.response.opacity_strength, 0.0f, 3.0f);
    out.response.min_opacity      = clampf(out.response.min_opacity, 0.0f, 1.0f);
    out.response.max_opacity      = clampf(out.response.max_opacity,
                                           std::max(out.response.min_opacity, 0.0f),
                                           1.0f);

    out.stability.temporal_smoothing = clampf(out.stability.temporal_smoothing, 0.0f, 0.999f);

    out.output.scale_factor      = clampf(out.output.scale_factor, 0.1f, 4.0f);
    out.output.map_line_weight   = clampf(out.output.map_line_weight, 0.0f, 5.0f);
    out.output.parallax_strength = clampf(out.output.parallax_strength, 0.0f, 5.0f);

    return out;
}

}  // namespace render_pipeline::shading

