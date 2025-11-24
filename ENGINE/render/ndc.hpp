#pragma once

#include <cstdint>

/**
 * NDC (Normalized Device Coordinates) calculator for camera transformations.
 * 
 * This class encapsulates all NDC-related math for converting between world coordinates
 * and screen space, including perspective projection, pitch calculations, and floor depth warping.
 */
class ndc {
public:
    // Camera geometry data computed from zoom scale
    struct CameraGeometry {
        double camera_height = 0.0;
        double focus_depth   = 0.0;
        double anchor_world_y = 0.0;
        double camera_world_y = 0.0;
        double focus_ndc_offset = 0.0;
        double pitch_radians = 0.0;
        float  pitch_degrees = 0.0f;
        bool   valid         = false;
    };

    // Floor depth parameters for NDC warping
    struct FloorDepthParams {
        double horizon_screen_y   = 0.0;
        double bottom_screen_y    = 0.0;
        double base_world_y       = 0.0; // focus point on the floor (world y that maps to screen center)
        double camera_world_y     = 0.0; // camera projection origin on the floor plane
        double horizon_ndc        = 0.0;
        double near_ndc           = -1.0;
        double ndc_scale          = 1.0; // scales NDC so the nearest visible point reaches the bottom of the screen
        double camera_height      = 0.0;
        double pitch_radians      = 0.0;
        double focus_ndc_offset   = 0.0;
        double pitch_norm         = 0.0;
        double strength           = 0.0;
        bool   enabled            = false;
    };

    // Settings required for NDC calculations
    struct Settings {
        // Zoom boundaries for pitch mapping
        float zoom_low  = 0.75f;
        float zoom_high = 3.0f;

        // Baseline height that scales with zoom for perspective depth
        float base_height_px = 720.0f;

        // Pitch mapping: zooming in lifts horizon, zooming out tilts downward
        float tilt_zoom_in_degrees  = 75.0f;
        float tilt_zoom_out_degrees = 60.0f;

        // Per-zoom interpolation values
        float base_height_at_zoom_low  = 720.0f;
        float base_height_at_zoom_high = 720.0f;
    };

    ndc(int screen_width, int screen_height);

    // Update screen dimensions (e.g., on window resize)
    void set_screen_dimensions(int width, int height);

    // Update settings used for NDC calculations
    void set_settings(const Settings& settings);
    const Settings& settings() const { return settings_; }

    // Compute camera geometry for a given scale and anchor point
    CameraGeometry compute_geometry_for_scale(
        double scale_value,
        double anchor_world_y,
        bool realism_enabled) const;

    // Compute floor depth parameters from camera geometry
    FloorDepthParams compute_floor_depth_params_for_geometry(
        const CameraGeometry& geom,
        double scale_value,
        bool realism_enabled) const;

    // Convenience method: compute floor depth params directly from scale
    FloorDepthParams compute_floor_depth_params_for_scale(
        double scale_value,
        double anchor_world_y,
        bool realism_enabled) const;

    // Warp a world Y coordinate to screen Y using NDC transformation
    float warp_floor_screen_y(
        float world_y,
        float linear_screen_y,
        const FloorDepthParams& params) const;

    // Calculate horizon screen Y position for a given scale
    double horizon_screen_y_for_scale_value(
        double scale_value,
        double anchor_world_y,
        bool realism_enabled) const;

    // Get screen dimensions
    int screen_width() const { return screen_width_; }
    int screen_height() const { return screen_height_; }

private:
    // Helper: compute camera height from scale
    double camera_height_from_scale(double scale_value) const;

    // Helper: compute pitch from scale
    double pitch_from_scale(double scale_value) const;

    // Helper: compute zoom interpolation factor
    double zoom_lerp_t_for_scale(double scale_value) const;

    int screen_width_  = 0;
    int screen_height_ = 0;
    Settings settings_{};
};
