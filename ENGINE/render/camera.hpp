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
        float render_distance = 800.0f;
        float parallax_strength = 12.0f;
        float foreshorten_strength = 0.35f;
        float distance_scale_strength = 0.3f;
        float height_at_zoom1 = 18.0f;
        float tripod_distance_y = 0.0f;
};

    struct RenderEffects {
        SDL_Point screen_position{0, 0};
        float vertical_scale = 1.0f;
        float distance_scale = 1.0f;
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

    const Area& get_base_zoom() const { return base_zoom_; }
    const Area& get_current_view() const { return current_view_; }

    SDL_Point map_to_screen(SDL_Point world, float parallax_x = 0.0f, float parallax_y = 0.0f) const;
    SDL_Point screen_to_map(SDL_Point screen, float parallax_x = 0.0f, float parallax_y = 0.0f) const;

    RenderEffects compute_render_effects(SDL_Point world, float asset_screen_height, float reference_screen_height) const;

    void set_parallax_enabled(bool e) { parallax_enabled_ = e; }
    bool parallax_enabled() const { return parallax_enabled_; }

    void set_realism_enabled(bool enabled) { realism_enabled_ = enabled; }
    bool realism_enabled() const { return realism_enabled_; }

    void set_realism_settings(const RealismSettings& settings) { settings_ = settings; }
    RealismSettings& realism_settings() { return settings_; }
    const RealismSettings& realism_settings() const { return settings_; }

    void set_render_areas_enabled(bool enabled) { render_areas_enabled_ = enabled; }
    bool render_areas_enabled() const { return render_areas_enabled_; }

    void apply_camera_settings(const nlohmann::json& data);
    nlohmann::json camera_settings_to_json() const;

    int get_render_distance_world_margin() const;

    Area     get_camera_area() const { return current_view_; }

    void      set_screen_center(SDL_Point p);
    SDL_Point get_screen_center() const { return screen_center_; }

    void update();
    void set_up_rooms(CurrentRoomFinder* finder);
    void update_zoom(Room* cur, CurrentRoomFinder* finder, Asset* player);

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
    bool       render_areas_enabled_ = false;
};

