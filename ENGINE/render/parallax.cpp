#include "parallax.hpp"

#include "camera_grid.hpp"
#include "world/grid.hpp"
#include "utils/transform_smoothing_settings.hpp"

#include <algorithm>

namespace {
    // Sanitize transform smoothing parameters
    TransformSmoothingParams parallax_sanitize_params(const TransformSmoothingParams& params) {
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
}

parallax::parallax() {
    // Set default parallax smoothing parameters
    settings_.parallax_smoothing_lerp_rate = 0.08f; // 1/0.08f ≈ 12.5 Hz effective
    settings_.parallax_smoothing_spring_frequency = 10.0f;
    settings_.parallax_smoothing_max_step = 0.0f;
    settings_.parallax_smoothing_snap_threshold = 0.0f;
    settings_.parallax_smoothing_method = TransformSmoothingMethod::Lerp;

    sanitize_smoothing_params();
}

void parallax::set_settings(const ParallaxSettings& settings) {
    settings_ = settings;
    sanitize_smoothing_params();
}

void parallax::update_parallax(camera_grid& camera, world::Grid& world_grid, float dt_seconds) {
    if (!is_enabled(camera)) {
        return;
    }

    // Create transform smoothing parameters from parallax settings
    TransformSmoothingParams parallax_params{};
    parallax_params.method = settings_.parallax_smoothing_method;
    parallax_params.lerp_rate = settings_.parallax_smoothing_lerp_rate;
    parallax_params.spring_frequency = settings_.parallax_smoothing_spring_frequency;
    parallax_params.max_step = settings_.parallax_smoothing_max_step;
    parallax_params.snap_threshold = settings_.parallax_smoothing_snap_threshold;
    parallax_params = parallax_sanitize_params(parallax_params);

    // Update parallax calculations in the grid
    world_grid.update_parallax(camera, dt_seconds);
}

bool parallax::is_enabled(const camera_grid& camera) const {
    return camera.realism_enabled();
}

void parallax::sanitize_smoothing_params() {
    // Apply default values for parallax smoothing if not properly set
    if (settings_.parallax_smoothing_method == TransformSmoothingMethod::Lerp &&
        settings_.parallax_smoothing_lerp_rate <= 0.0f) {
        settings_.parallax_smoothing_lerp_rate = 0.08f;
    } else if (settings_.parallax_smoothing_method == TransformSmoothingMethod::CriticallyDampedSpring &&
               settings_.parallax_smoothing_spring_frequency <= 0.0f) {
        settings_.parallax_smoothing_spring_frequency = 10.0f;
    }

    // Ensure all parameters are finite and reasonable
    if (!std::isfinite(settings_.parallax_smoothing_lerp_rate) ||
        settings_.parallax_smoothing_lerp_rate < 0.0f) {
        settings_.parallax_smoothing_lerp_rate = 0.0f;
    }
    if (!std::isfinite(settings_.parallax_smoothing_spring_frequency) ||
        settings_.parallax_smoothing_spring_frequency < 0.0f) {
        settings_.parallax_smoothing_spring_frequency = 0.0f;
    }
    if (!std::isfinite(settings_.parallax_smoothing_max_step) ||
        settings_.parallax_smoothing_max_step < 0.0f) {
        settings_.parallax_smoothing_max_step = 0.0f;
    }
    if (!std::isfinite(settings_.parallax_smoothing_snap_threshold) ||
        settings_.parallax_smoothing_snap_threshold < 0.0f) {
        settings_.parallax_smoothing_snap_threshold = 0.0f;
    }

    // Validate method is one of the allowed values
    switch (settings_.parallax_smoothing_method) {
    case TransformSmoothingMethod::None:
    case TransformSmoothingMethod::Lerp:
    case TransformSmoothingMethod::CriticallyDampedSpring:
        break;
    default:
        settings_.parallax_smoothing_method = TransformSmoothingMethod::Lerp;
        break;
    }
}
