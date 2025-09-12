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

    // View accessors
    const Area& get_base_zoom() const { return base_zoom_; }
    const Area& get_current_view() const { return current_view_; }

    // Coordinate mapping
    SDL_Point map_to_screen(SDL_Point world, float parallax_x = 0.0f, float parallax_y = 0.0f) const;
    SDL_Point screen_to_map(SDL_Point screen, float parallax_x = 0.0f, float parallax_y = 0.0f) const;

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
};
