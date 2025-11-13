#pragma once

#include <algorithm>
#include <cmath>

#include "render/camera.hpp"

namespace depth_cue {

inline bool nearly_equal(float a, float b, float epsilon = 1e-6f) {
    return std::fabs(a - b) <= epsilon;
}

inline constexpr float kDepthCueDeadzonePx = 1.5f;

enum class DepthSide {
    None = 0,
    Foreground,
    Background
};

struct DepthSample {
    DepthSide side = DepthSide::None;
    float     t    = 0.0f;

    bool is_valid() const { return side != DepthSide::None; }
    bool is_foreground() const { return side == DepthSide::Foreground; }
    bool is_background() const { return side == DepthSide::Background; }
};

inline float evaluate_depth_curve(camera::BlurFalloffMethod method, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (method) {
        case camera::BlurFalloffMethod::Quadratic:
            return t * t;
        case camera::BlurFalloffMethod::Cubic:
            return t * t * t;
        case camera::BlurFalloffMethod::Logarithmic: {
            const float k = 4.0f;
            const float num = std::log1p(k * t);
            const float den = std::log1p(k);
            return (den > 0.0f) ? (num / den) : t;
        }
        case camera::BlurFalloffMethod::Exponential: {
            const float k = 3.0f;
            const float num = std::exp(k * t) - 1.0f;
            const float den = std::exp(k) - 1.0f;
            return (den > 0.0f) ? (num / den) : t;
        }
        case camera::BlurFalloffMethod::Linear:
        default:
            return t;
    }
}

inline float sample_signed_effect(const DepthSample& sample,
                                  float foreground_value,
                                  float background_value,
                                  camera::BlurFalloffMethod curve) {
    if (sample.is_foreground() && std::fabs(foreground_value) > 0.0f) {
        return evaluate_depth_curve(curve, sample.t) * foreground_value;
    }
    if (sample.is_background() && std::fabs(background_value) > 0.0f) {
        return evaluate_depth_curve(curve, sample.t) * background_value;
    }
    return 0.0f;
}

}  // namespace depth_cue

