#pragma once

#include <SDL.h>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

class Global_Light_Source {

	public:
    Global_Light_Source(SDL_Renderer* renderer, SDL_Point screen_center, int screen_width, SDL_Color fallback_base_color, const std::string& map_path);
    void apply_config(const nlohmann::json& data);
    ~Global_Light_Source() = default;
    void update();
    SDL_Point get_position() const;
    float     get_angle() const;
    SDL_Color get_current_color() const;
    int       get_brightness() const;
    SDL_Point get_orbit_center() const { return center_; }
    SDL_Point get_direction_reference() const { return map_reference_center_; }
    SDL_Point get_direction_target() const;
    int       min_opacity() const { return min_opacity_; }
    int       max_opacity() const { return max_opacity_; }

        private:
    struct KeyEntry {
        float degree;
        SDL_Color color;
};
    bool load_from_map_light(const std::string& map_path);
    void set_defaults(int screen_width, SDL_Color fallback_base_color);
    void set_light_brightness();
    Uint8 clamp_alpha(Uint8 value) const;
    SDL_Color clamp_color_alpha(SDL_Color color) const;
    SDL_Color compute_color_from_horizon() const;
    void      recalc_position();

        private:
    SDL_Renderer* renderer_;
    SDL_Color base_color_;
    SDL_Color current_color_;
    SDL_Point default_center_;
    SDL_Point default_map_center_;
    SDL_Point center_;
    SDL_Point map_reference_center_;
    SDL_Point pos_;
    float angle_;
    bool  initialized_;
    int   frame_counter_;
    int   light_brightness;
    float radius_;
    float intensity_;
    float mult_;
    float fall_off_;
    int   orbit_radius_x_;
    int   orbit_radius_y_;
    int   update_interval_;
    int   min_opacity_;
    int   max_opacity_;
    std::vector<KeyEntry> key_colors_;
};
