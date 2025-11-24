#pragma once

#include "utils/transform_smoothing_settings.hpp"

// Forward declarations to avoid heavy includes
class camera_grid;
namespace world {
class Grid;
}

/**
 * Parallax controller for camera effects.
 * Handles parallax settings, updates, and configuration.
 */
class parallax {
public:
    // Settings specific to parallax functionality
    struct ParallaxSettings {
        // Parallax smoothing parameters
        float parallax_smoothing_lerp_rate = 0.0f;
        float parallax_smoothing_spring_frequency = 0.0f;
        float parallax_smoothing_max_step = 0.0f;
        float parallax_smoothing_snap_threshold = 0.0f;
        TransformSmoothingMethod parallax_smoothing_method = TransformSmoothingMethod::Lerp;
    };

    parallax();

    // Set parallax settings
    void set_settings(const ParallaxSettings& settings);
    const ParallaxSettings& settings() const { return settings_; }

    // Update parallax calculations for the given camera and grid
    void update_parallax(camera_grid& camera, world::Grid& world_grid, float dt_seconds);

    // Check if parallax is enabled (based on camera realism)
    bool is_enabled(const camera_grid& camera) const;

    // Apply parallax smoothing sanitization
    void sanitize_smoothing_params();

private:
    ParallaxSettings settings_{};
};
