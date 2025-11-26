#pragma once

#include "utils/transform_smoothing_settings.hpp"

#include <algorithm>
#include <cmath>

namespace camera_ndc_utils {

// Constants
constexpr float  kMinTau    = 1e-4f;
constexpr double SCALE_EPS  = 1e-4;
constexpr double BASE_RATIO = 1.1;
constexpr double PI_D       = 3.14159265358979323846;
constexpr double HALF_FOV_Y = PI_D / 4.0; // 45 deg half FOV (90 total)
constexpr double RAD_TO_DEG = 180.0 / PI_D;
constexpr float  kDefaultPitchDegrees   = 60.0f;
constexpr float  kMaxForeshortenSliderStrength = 2.0f;
constexpr float  kFixedDepthOffsetPx    = 4000.0f;
constexpr double kMinZoomRange = 1e-4;

struct ZoomInterpolator {
    double t = 0.0;
    ZoomInterpolator(float zoom_low, float zoom_high, double scale_value) {
        const double safe_low = std::max(0.0f, zoom_low);
        const double safe_high = std::max(safe_low + static_cast<double>(kMinZoomRange),
                                           static_cast<double>(zoom_high));
        const double span = std::max(kMinZoomRange, safe_high - safe_low);
        t = std::clamp((scale_value - safe_low) / span, 0.0, 1.0);
    }
    template <typename V>
    V lerp(V low, V high) const {
        return static_cast<V>(low + (high - low) * t);
    }
};

inline double wrap_degrees_0_360(double raw_value) {
    if (!std::isfinite(raw_value)) {
        return static_cast<double>(kDefaultPitchDegrees);
    }
    double wrapped = std::fmod(raw_value, 360.0);
    if (wrapped < 0.0) wrapped += 360.0;
    if (wrapped >= 360.0 || !std::isfinite(wrapped)) {
        wrapped = std::fmod(wrapped, 360.0);
        if (wrapped < 0.0) wrapped += 360.0;
    }
    return std::isfinite(wrapped) ? wrapped : static_cast<double>(kDefaultPitchDegrees);
}

inline float wrap_degrees_0_360(float raw_value) {
    return static_cast<float>(wrap_degrees_0_360(static_cast<double>(raw_value)));
}

inline double shortest_delta_degrees(double from_deg, double to_deg) {
    return std::remainder(to_deg - from_deg, 360.0);
}

inline double lerp_angle(double from_deg, double to_deg, double t) {
    const double delta = shortest_delta_degrees(from_deg, to_deg);
    return wrap_degrees_0_360(from_deg + delta * t);
}

inline float clamp_slider(float raw_value, float max_value) {
    const float safe = std::isfinite(raw_value) ? std::max(0.0f, raw_value) : 0.0f;
    return std::clamp(safe, 0.0f, max_value);
}

inline double signed_radians_from_degrees(double degrees) {
    const double wrapped_deg = wrap_degrees_0_360(degrees);
    const double signed_deg  = (wrapped_deg > 180.0) ? wrapped_deg - 360.0 : wrapped_deg;
    return signed_deg * (PI_D / 180.0);
}

inline float sanitize_pitch_degrees(float raw_value, bool* clamped = nullptr) {
    if (clamped) *clamped = false;
    const float wrapped = wrap_degrees_0_360(std::isfinite(raw_value)
        ? raw_value
        : kDefaultPitchDegrees);
    const float clamped_value = std::clamp(
        wrapped,
        0.0f, // kMinPitchDegrees
        89.0f); // kMaxPitchDegrees
    if (clamped && clamped_value != raw_value) {
        *clamped = true;
    }
    return clamped_value;
}

inline TransformSmoothingParams sanitize_params(const TransformSmoothingParams& params) {
    TransformSmoothingParams out = params;
    if (!std::isfinite(out.lerp_rate) || out.lerp_rate < 0.0f) {
        out.lerp_rate = 0.0f;
    }
    if (!std::isfinite(out.spring_frequency) || out.spring_frequency < 0.0f) {
        out.spring_frequency = 0.0f;
    }
    if (!std::isfinite(out.max_step) || out.max_step < 0.0f) {
        out.max_step = 0.0f;
    }
    if (!std::isfinite(out.snap_threshold) || out.snap_threshold < 0.0f) {
        out.snap_threshold = 0.0f;
    }
    switch (out.method) {
    case TransformSmoothingMethod::None:
    case TransformSmoothingMethod::Lerp:
    case TransformSmoothingMethod::CriticallyDampedSpring:
        break;
    default:
        out.method = TransformSmoothingMethod::None;
        break;
    }
    return out;
}

inline float rate_from_tau(float tau_seconds) {
    if (!std::isfinite(tau_seconds) || tau_seconds <= kMinTau) {
        return 0.0f;
    }
    return 1.0f / tau_seconds;
}

inline float tau_from_rate(float rate) {
    if (!std::isfinite(rate) || rate <= kMinTau) {
        return 0.0f;
    }
    return 1.0f / rate;
}

inline double clamp_zoom_scale(double value) {
    return std::clamp(
        value,
        0.0001,
        20.0); // camera_grid::kMaxZoomAnchors
}

} // namespace camera_ndc_utils
