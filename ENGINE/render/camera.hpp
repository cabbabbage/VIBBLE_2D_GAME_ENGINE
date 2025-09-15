#pragma once

#include <SDL.h>
#include <algorithm>
#include <vector>
#include "utils/area.hpp"

class Asset;
class Room;
class CurrentRoomFinder;

class camera {

	public:
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

    // Parallax global toggle (dev mode convenience)
    void set_parallax_enabled(bool e) { parallax_enabled_ = e; }
    bool parallax_enabled() const { return parallax_enabled_; }

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
};
