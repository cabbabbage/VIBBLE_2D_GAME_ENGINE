#pragma once

#include <SDL.h>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>
#include "utils/area.hpp"
#include "utils/transform_smoothing.hpp"

class Asset;
class Room;
class CurrentRoomFinder;

class camera {

        public:
    enum class BlurFalloffMethod : int {
        Linear = 0,
        Quadratic = 1,
        Cubic = 2,
        Logarithmic = 3,
        Exponential = 4
    };
    struct RealismSettings {
        float render_distance = 800.0f;
        // Screen-space offset to bias the render radius vertically.
        // Positive values move the render range downward on screen.
        float render_radius_y_offset_px = 0.0f;
        float parallax_strength = 12.0f;
        float foreshorten_strength = 0.35f;
        float distance_scale_strength = 0.3f;
        float height_at_zoom1 = 18.0f;
        float tripod_distance_y = 0.0f;
        float min_visible_screen_ratio = 0.015f;
        int   render_quality_percent = 100;
        bool  smooth_motion_zoom = true;
        TransformSmoothingMethod motion_smoothing_method = TransformSmoothingMethod::CriticallyDampedSpring;
        float motion_smoothing_tau = 0.12f;
        float motion_smoothing_spring_frequency = 5.0f;
        float motion_smoothing_max_step = 8000.0f;
        float motion_smoothing_snap_threshold = 0.25f;
        float scale_variant_hysteresis_margin = 0.05f;
        float min_zoom_multiplier = 0.7f;
        float max_zoom_multiplier = 1.3f;
        // Perspective Colors (UI-only; not used in render yet)
        float distance_saturation_factor_min = 0.0f;
        float distance_saturation_factor_max = 0.0f;
        float primary_color_boost_min = 0.0f;
        float primary_color_boost_max = 0.0f;
        float ground_brightness_factor = 0.0f;
        float background_brightness = 0.0f;
        // Perspective Blur (Aperture Blur)
        float max_foreground_blur = 0.0f;
        float max_background_blur = 0.0f;
        float blur_foreground_screen_y = 1080.0f;
        float blur_background_screen_y = 0.0f;
        BlurFalloffMethod blur_falloff_method = BlurFalloffMethod::Linear;
        TransformSmoothingParams parallax_smoothing{
            TransformSmoothingMethod::CriticallyDampedSpring,
            0.0f,
            6.0f,
            8000.0f,
            0.5f};
    };

    struct RenderEffects {
        SDL_FPoint screen_position{0.0f, 0.0f};
        float      vertical_scale = 1.0f;
        float      distance_scale = 1.0f;
    };

    camera(int screen_width, int screen_height, const Area& starting_zoom);

    void  set_scale(float s);
    float get_scale() const;
    void  zoom_to_scale(double target_scale, int duration_steps);

    Area  convert_area_to_aspect(const Area& in) const;
    void  zoom_to_area(const Area& target_area, int duration_steps);

    void  set_manual_zoom_override(bool enabled) { manual_zoom_override_ = enabled; }
    bool  is_manual_zoom_override() const { return manual_zoom_override_; }
    void  set_focus_override(SDL_Point p) { focus_override_ = true; focus_point_ = p; }
    bool  has_focus_override() const { return focus_override_; }
    SDL_Point get_focus_override_point() const { return focus_point_; }
    void  clear_focus_override() { focus_override_ = false; }
    void  pan_and_zoom_to_point(SDL_Point world_pos, double zoom_scale_factor, int duration_steps);
    void  pan_and_zoom_to_asset(const Asset* a, double zoom_scale_factor, int duration_steps);

    void  animate_zoom_multiply(double factor, int duration_steps);
    void  animate_zoom_towards_point(double factor, SDL_Point screen_point, int duration_steps);

    const Area& get_base_zoom() const { return base_zoom_; }
    const Area& get_current_view() const { return current_view_; }

    SDL_FPoint map_to_screen(SDL_Point world) const;
    SDL_FPoint map_to_screen_f(SDL_FPoint world) const;
    SDL_FPoint screen_to_map(SDL_Point screen) const;

    // Returns the actual rendered view center in world space.
    // Uses the smoothed center when motion smoothing is enabled.
    SDL_FPoint get_view_center_f() const;

    using RenderSmoothingKey = std::uintptr_t;

    RenderEffects compute_render_effects(SDL_Point world,
                                         float asset_screen_height,
                                         float reference_screen_height,
                                         RenderSmoothingKey smoothing_key = 0) const;

    void set_parallax_enabled(bool e) { parallax_enabled_ = e; }
    bool parallax_enabled() const { return parallax_enabled_; }

    void set_realism_enabled(bool enabled) { realism_enabled_ = enabled; }
    bool realism_enabled() const { return realism_enabled_; }

    void set_render_areas_enabled(bool enabled) { render_areas_enabled_ = enabled; }
    bool render_areas_enabled() const { return render_areas_enabled_; }

    void set_realism_settings(const RealismSettings& settings);
    RealismSettings& realism_settings() { return settings_; }
    const RealismSettings& realism_settings() const { return settings_; }
    TransformSmoothingParams motion_smoothing_params() const;

    void apply_camera_settings(const nlohmann::json& data);
    nlohmann::json camera_settings_to_json() const;

    int get_render_distance_world_margin() const;
    int get_render_radius_world_y_offset() const;

    Area     get_camera_area() const { return current_view_; }

    void      set_screen_center(SDL_Point p);
    SDL_Point get_screen_center() const { return screen_center_; }

    void update(float dt);
    void set_up_rooms(CurrentRoomFinder* finder);
    double default_zoom_for_room(const Room* room) const;
    void update_zoom(Room* cur, CurrentRoomFinder* finder, Asset* player, bool refresh_requested, float dt);

    void pan(const std::vector<SDL_Point>& , int ) {}
    void shake(double , double , int ) {}

    void set_overscan_pixels(int px) { overscan_px_ = std::max(0, px); }
    bool intro = true;
    bool zooming_ = false;

	private:

    int        screen_width_  = 0;
    int        screen_height_ = 0;
    double     aspect_        = 1.0;

    Area       base_zoom_{"base_zoom"};
    Area       current_view_{"current_view"};
    SDL_Point  screen_center_{0, 0};
    bool       screen_center_initialized_ = false;
    double     pan_offset_x_ = 0.0;
    double     pan_offset_y_ = 0.0;

    float      scale_        = 1.0f;
    int        overscan_px_  = 200;
    double     start_scale_  = 1.0;
    double     target_scale_ = 1.0;
    int        steps_total_  = 0;
    int        steps_done_   = 0;

    Room*      starting_room_ = nullptr;
    double     starting_area_ = 1.0;
    double     compute_room_scale_from_area(const Room* room) const;

    void       recompute_current_view();

    bool       manual_zoom_override_ = false;
    bool       focus_override_ = false;
    SDL_Point  focus_point_{0, 0};

    bool       pan_override_ = false;
    SDL_Point  start_center_{0, 0};
    SDL_Point  target_center_{0, 0};

    bool       parallax_enabled_ = true;
    bool       realism_enabled_ = true;
    RealismSettings settings_{};
    bool       render_areas_enabled_ = true;

    TransformSmoothingState center_smoothing_x_{};
    TransformSmoothingState center_smoothing_y_{};
    TransformSmoothingState zoom_smoothing_{};
    SDL_FPoint smoothed_center_{0.0f, 0.0f};
    float      smoothed_scale_ = 1.0f;

};
