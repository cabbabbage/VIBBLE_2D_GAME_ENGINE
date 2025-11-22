#pragma once

#include <SDL.h>
#include <cstdint>
#include <utility>
#include <nlohmann/json.hpp>
#include <optional>

#include "utils/area.hpp"
#include "utils/transform_smoothing_settings.hpp"
#include "render/image_effect_settings.hpp"


// Forward declarations to avoid heavy includes in the header.
class Asset;
class Room;
class CurrentRoomFinder;

/**
 * Camera with realism settings and optional perspective style effects.
 *
 * This version does not depend on camera_effects at all.
 */

class camera {
public:
    static constexpr float kMinPitchDegrees = 0.0f;
    static constexpr float kMaxPitchDegrees = 359.0f;
    static constexpr float kMinZoomAnchors = 0.0f;
    static constexpr float kMaxZoomAnchors = 20.0f;

    // Define BlurFalloffMethod here to avoid an extra include and ensure
    // BlurFalloffMethod::Linear is a valid name at compile time.
    enum class BlurFalloffMethod {
        Linear = 0,
        Quadratic = 1,
        Cubic = 2,
        Logarithmic = 3,
        Exponential = 4
    };
    struct RealismSettings {
        // Parallax and perspective sliders
        float foreshorten_strength         = 0.0f;
        float distance_scale_strength      = 0.0f;
        float min_visible_screen_ratio     = 0.015f;

        // Zoom boundaries; also drive pitch mapping.
        float zoom_low                     = 0.75f;
        float zoom_high                    = 3.0f;

        // Single baseline height that scales with zoom for perspective depth.
        float base_height_px               = 720.0f;

        // Pitch mapping: zooming in lifts the horizon, zooming out tilts downward.
        float tilt_zoom_in_degrees        = 345.0f;
        float tilt_zoom_out_degrees       = 310.0f;

        int   render_quality_percent       = 100;
        bool  smooth_motion_zoom           = true;

        // Motion smoothing
        TransformSmoothingMethod  motion_smoothing_method      = TransformSmoothingMethod::Lerp;
        TransformSmoothingParams  parallax_smoothing{};
        float parallax_smoothing_snap_threshold = 0.0f;
        float motion_smoothing_tau             = 0.0f;
        float motion_smoothing_spring_frequency = 0.0f;
        float motion_smoothing_max_step        = 0.0f;
        float motion_smoothing_snap_threshold  = 0.0f;

        float scale_variant_hysteresis_margin = 0.05f;

        // Depth cue texture blending
        int   foreground_texture_max_opacity  = 255;
        int   background_texture_max_opacity  = 255;
        float foreground_plane_screen_y       = 1080.0f;
        float background_plane_screen_y       = 0.0f;
        BlurFalloffMethod texture_opacity_falloff_method = BlurFalloffMethod::Linear;

        // Grid depth parameters
        // Distance (in screen px) to place the depth offset below the bottom of the screen.
        float grid_depth_offset_px           = 240.0f;

        // Per-zoom interpolation values
        float foreshorten_at_zoom_low  = 0.0f;
        float foreshorten_at_zoom_high = 0.0f;
        float distance_scale_at_zoom_low  = 0.0f;
        float distance_scale_at_zoom_high = 0.0f;
        float depth_offset_at_zoom_low  = 240.0f;
        float depth_offset_at_zoom_high = 240.0f;
        float base_height_at_zoom_low  = 720.0f;
        float base_height_at_zoom_high = 720.0f;

        // Image effects for foreground and background
        camera_effects::ImageEffectSettings foreground_effects{};
        camera_effects::ImageEffectSettings background_effects{};

    };

    struct RenderEffects {
        SDL_FPoint screen_position{0.0f, 0.0f};
        float      vertical_scale  = 1.0f;
        float      distance_scale  = 1.0f;
    };

    using RenderSmoothingKey = std::uintptr_t;

    struct FloorDepthParams {
        double horizon_screen_y   = 0.0;
        double bottom_screen_y    = 0.0;
        double base_world_y       = 0.0;
        double camera_height      = 0.0;
        double pitch_radians      = 0.0;
        double focus_ndc_offset   = 0.0;
        double pitch_norm         = 0.0;
        double strength           = 0.0;
        bool   enabled            = false;
    };

    camera(int screen_width, int screen_height, const Area& starting_zoom);

    void set_realism_settings(const RealismSettings& settings);
    const RealismSettings& realism_settings() const { return settings_; }

    void set_screen_center(SDL_Point p);
    SDL_Point get_screen_center() const { return screen_center_; }
    SDL_FPoint get_view_center_f() const;

    void set_scale(float s);
    float get_scale() const;

    void zoom_to_scale(double target_scale, int duration_steps);
    void zoom_to_area(const Area& target_area, int duration_steps);

    void update(float dt);

    void set_up_rooms(CurrentRoomFinder* finder);
    double default_zoom_for_room(const Room* room) const {
        return compute_room_scale_from_area(room);
    }

    void update_zoom(Room* cur,
                     CurrentRoomFinder* finder,
                     Asset* player,
                     bool refresh_requested,
                     float dt);

    const Area& get_camera_area() const { return current_view_; }

    SDL_FPoint map_to_screen_f(SDL_FPoint world) const;
    SDL_FPoint map_to_screen(SDL_Point world) const;
    SDL_FPoint screen_to_map(SDL_Point screen) const;

    RenderEffects compute_render_effects(
        SDL_Point world,
        float asset_screen_height,
        float reference_screen_height,
        RenderSmoothingKey smoothing_key) const;

    RenderEffects compute_render_effects(
        SDL_Point world,
        float asset_screen_height,
        float reference_screen_height) const {
        return compute_render_effects(world, asset_screen_height, reference_screen_height, 0);
    }

    void apply_camera_settings(const nlohmann::json& data);
    nlohmann::json camera_settings_to_json() const;

    TransformSmoothingParams motion_smoothing_params() const;

    bool parallax_enabled() const {
        return realism_enabled_;
    }

    bool realism_enabled() const { return realism_enabled_; }

    void pan_and_zoom_to_point(SDL_Point world_pos, double zoom_scale_factor, int duration_steps);
    void pan_and_zoom_to_asset(const Asset* a, double zoom_scale_factor, int duration_steps);
    void animate_zoom_multiply(double factor, int duration_steps);
    void animate_zoom_towards_point(double factor, SDL_Point screen_point, int duration_steps);

    // New floor depth helpers for the warped grid
    FloorDepthParams compute_floor_depth_params() const;
    float warp_floor_screen_y(float world_y, float linear_screen_y) const;
    double horizon_screen_y_for_scale() const;
    double horizon_screen_y_for_scale_value(double scale_value) const;

    // Runtime camera geometry helpers (derived from zoom + offsets).
    float  current_pitch_degrees() const { return runtime_pitch_deg_; }
    double current_camera_height() const { return runtime_camera_height_; }
    double current_anchor_world_y() const { return runtime_anchor_world_y_; }
    float  current_foreshorten_strength() const { return runtime_foreshorten_strength_; }
    float  current_distance_scale_strength() const { return runtime_distance_scale_strength_; }
    float  current_depth_offset_px() const { return runtime_depth_offset_px_; }
    const FloorDepthParams& current_floor_depth_params() const { return runtime_floor_params_; }

    // Override controls for dev mode
    void set_manual_zoom_override(bool enabled) { manual_zoom_override_ = enabled; }
    void set_focus_override(SDL_Point focus) { focus_override_ = true; focus_point_ = focus; }
    void clear_focus_override() { focus_override_ = false; }
    bool is_zooming() const { return zooming_; }
    void set_render_areas_enabled(bool enabled) { /* stub for compatibility */ }
    void set_realism_enabled(bool enabled) { realism_enabled_ = enabled; }
    void set_parallax_enabled(bool enabled) { /* deprecated - use set_realism_enabled */ }
    bool is_manual_zoom_override() const { return manual_zoom_override_; }
    bool has_focus_override() const { return focus_override_; }
    SDL_Point get_focus_override_point() const { return focus_point_; }
    void recompute_current_view();
    Area convert_area_to_aspect(const Area& in) const;
    double compute_room_scale_from_area(const Room* room) const;

private:
    struct CameraGeometry {
        double camera_height = 0.0;
        double focus_depth   = 0.0;
        double anchor_world_y = 0.0;
        double focus_ndc_offset = 0.0;
        double pitch_radians = 0.0;
        float  pitch_degrees = 0.0f;
        bool   valid         = false;
    };

    double view_height_world() const;
    double anchor_world_y() const;
    double zoom_lerp_t_for_scale(double scale_value) const;
    CameraGeometry compute_geometry_for_scale(double scale_value) const;
    float foreshorten_for_scale(double scale_value) const;
    float distance_scale_for_scale(double scale_value) const;
    float depth_offset_for_scale(double scale_value) const;

    struct TransformSmoother1D {
        TransformSmoothingParams params{};
        float current  = 0.0f;
        float target   = 0.0f;
        float velocity = 0.0f;

        void set_params(const TransformSmoothingParams& p);
        void reset(float value);
        void advance(float dt);
        float value_for_render() const { return current; }
    };

public:
    CameraGeometry compute_geometry() const;
    FloorDepthParams compute_floor_depth_params_for_geometry(const CameraGeometry& geom, double scale_value) const;
    FloorDepthParams compute_floor_depth_params_for_scale(double scale_value) const;
    void update_geometry_cache(const CameraGeometry& g);

    int    screen_width_  = 0;
    int    screen_height_ = 0;
    double aspect_        = 1.0;

    Area base_zoom_;
    Area current_view_;

    SDL_Point  screen_center_{0, 0};
    bool       screen_center_initialized_ = false;
    double     pan_offset_x_ = 0.0;
    double     pan_offset_y_ = 0.0;

    float  scale_        = 1.0f;
    bool   zooming_      = false;
    int    steps_total_  = 0;
    int    steps_done_   = 0;
    double start_scale_  = 1.0;
    double target_scale_ = 1.0;

    TransformSmoother1D center_smoothing_x_;
    TransformSmoother1D center_smoothing_y_;
    TransformSmoother1D zoom_smoothing_;

    SDL_FPoint smoothed_center_{0.0f, 0.0f};
    float      smoothed_scale_ = 1.0f;
    double     runtime_camera_height_ = 0.0;
    double     runtime_focus_depth_   = 0.0;
    double     runtime_anchor_world_y_ = 0.0;
    double     runtime_focus_ndc_offset_ = 0.0;
    double     runtime_pitch_rad_     = 0.0;
    float      runtime_pitch_deg_     = 0.0f;
    float      runtime_foreshorten_strength_ = 0.0f;
    float      runtime_distance_scale_strength_ = 0.0f;
    float      runtime_depth_offset_px_ = 0.0f;
    FloorDepthParams runtime_floor_params_{};
    bool       geometry_valid_        = false;

    RealismSettings settings_{};
    bool realism_enabled_ = true;

    bool pan_override_          = false;
    bool manual_zoom_override_  = false;
    bool focus_override_        = false;

    SDL_Point focus_point_{0, 0};
    SDL_Point start_center_{0, 0};
    SDL_Point target_center_{0, 0};

    Room*  starting_room_ = nullptr;
    double starting_area_ = 1.0;
};
