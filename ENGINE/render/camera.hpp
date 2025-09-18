#pragma once

#include <SDL.h>
#include <algorithm>
#include <vector>
#include <nlohmann/json.hpp>
#include "utils/area.hpp"

class Asset;
class Room;
class CurrentRoomFinder;

class camera {

        public:
    struct RealismSettings {
        float render_distance = 800.0f;           // world-space margin for activating assets
        float parallax_strength = 12.0f;          // multiplier for parallax offset
        float squash_strength = 0.35f;            // multiplier for vertical squashing
        float distance_scale_strength = 0.3f;     // multiplier for distance-based scaling
        float camera_angle_degrees = 55.0f;       // camera pitch relative to the ground plane
        float camera_height_at_zoom0 = 18.0f;     // camera height above ground at base zoom
        float camera_vertical_offset = 0.0f;      // world-space offset from screen center for distance math
    };

    struct RenderEffects {
        SDL_Point screen_position{0, 0};
        float vertical_scale = 1.0f;
        float distance_scale = 1.0f;
    };

    // Construct the camera with a starting zoom Area (map-space).
    // The starting area is adjusted to match the screen aspect ratio (cover).
    camera(int screen_width, int screen_height, const Area& starting_zoom);

    // Scale API
    void  set_scale(float s);
    float get_scale() const;
    void  zoom_to_scale(double target_scale, int duration_steps);

    // Area-based API
    Area  convert_area_to_aspect(const Area& in) const; // cover-fit to screen aspect
    void  zoom_to_area(const Area& target_area, int duration_steps);

    // Dev/utility: focus and zoom helpers
    void  set_manual_zoom_override(bool enabled) { manual_zoom_override_ = enabled; }
    bool  is_manual_zoom_override() const { return manual_zoom_override_; }
    void  set_focus_override(SDL_Point p) { focus_override_ = true; focus_point_ = p; }
    bool  has_focus_override() const { return focus_override_; }
    SDL_Point get_focus_override_point() const { return focus_point_; }
    void  clear_focus_override() { focus_override_ = false; }
    void  pan_and_zoom_to_point(SDL_Point world_pos, double zoom_scale_factor, int duration_steps);
    void  pan_and_zoom_to_asset(const Asset* a, double zoom_scale_factor, int duration_steps);
    // Zoom animation without panning: multiply current target by factor over duration
    void  animate_zoom_multiply(double factor, int duration_steps);

    // View accessors
    const Area& get_base_zoom() const { return base_zoom_; }
    const Area& get_current_view() const { return current_view_; }

    // Coordinate mapping
    SDL_Point map_to_screen(SDL_Point world, float parallax_x = 0.0f, float parallax_y = 0.0f) const;
    SDL_Point screen_to_map(SDL_Point screen, float parallax_x = 0.0f, float parallax_y = 0.0f) const;

    // Asset rendering helpers
    RenderEffects compute_render_effects(SDL_Point world,
                                         float asset_screen_height,
                                         float reference_screen_height) const;

    // Parallax global toggle (dev mode convenience)
    void set_parallax_enabled(bool e) { parallax_enabled_ = e; }
    bool parallax_enabled() const { return parallax_enabled_; }

    void set_realism_enabled(bool enabled) { realism_enabled_ = enabled; }
    bool realism_enabled() const { return realism_enabled_; }

    void set_realism_settings(const RealismSettings& settings) { settings_ = settings; }
    RealismSettings& realism_settings() { return settings_; }
    const RealismSettings& realism_settings() const { return settings_; }

    void apply_camera_settings(const nlohmann::json& data);
    nlohmann::json camera_settings_to_json() const;

    int get_render_distance_world_margin() const;

    // Area-first helpers
    Area     get_camera_area() const { return current_view_; }

    // Screen center is the map-space focal point (e.g., player position).
    void      set_screen_center(SDL_Point p) { screen_center_ = p; }
    SDL_Point get_screen_center() const { return screen_center_; }

    // Animation update
    void update();
    void set_up_rooms(CurrentRoomFinder* finder);
    void update_zoom(Room* cur, CurrentRoomFinder* finder, Asset* player);

    // Extra effects (stubs for now)
    void pan(const std::vector<SDL_Point>& /*targets*/, int /*hold_time*/) {}
    void shake(double /*intensity*/, double /*speed*/, int /*duration*/) {}

    void set_overscan_pixels(int px) { overscan_px_ = std::max(0, px); }
    bool intro = true;
    bool zooming_ = false;

	private:
    // Screen and aspect
    int        screen_width_  = 0;
    int        screen_height_ = 0;
    double     aspect_        = 1.0;

    // Area model
    Area       base_zoom_{"base_zoom"};   // Exactly screen_w x screen_h at zoom = 1
    Area       current_view_{"current_view"}; // Current map-space view
    SDL_Point  screen_center_{0, 0};

    // Zoom state
    float      scale_        = 1.0f; // current_view width / base_zoom width
    int        overscan_px_  = 200;  // extra margin beyond screen, in pixels
    double     start_scale_  = 1.0;
    double     target_scale_ = 1.0;
    int        steps_total_  = 0;
    int        steps_done_   = 0;

    // Room-based zooming support
    Room*      starting_room_ = nullptr;
    double     starting_area_ = 1.0;
    double     compute_room_scale_from_area(const Room* room) const;

    // Internal helpers
    void       recompute_current_view();

    // Overrides (Dev Mode / focus)
    bool       manual_zoom_override_ = false; // when true, update_zoom will not change target scale
    bool       focus_override_ = false;       // when true, use focus_point_ instead of player position
    SDL_Point  focus_point_{0, 0};

    // Pan/zoom animation state
    bool       pan_override_ = false;         // when true during animation, center is animated to target
    SDL_Point  start_center_{0, 0};
    SDL_Point  target_center_{0, 0};

    // Global parallax toggle
    bool       parallax_enabled_ = true;
    bool       realism_enabled_ = true;
    RealismSettings settings_{};
};

