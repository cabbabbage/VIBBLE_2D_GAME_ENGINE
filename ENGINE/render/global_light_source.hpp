#pragma once

#include <SDL.h>
#include <vector>
#include <string>
#include <string_view>
#include <optional>
#include <nlohmann/json.hpp>

#include "utils/ranged_color.hpp"

class Global_Light_Source {

        public:
    Global_Light_Source(SDL_Renderer* renderer, SDL_Point screen_center, int screen_width, SDL_Color fallback_base_color);
    void apply_config(const nlohmann::json& data);
    ~Global_Light_Source() = default;
    void update(const std::optional<SDL_FPoint>& target_world, const std::optional<SDL_FPoint>& average_direction);
    SDL_Point get_position() const;
    float     get_angle() const;
    SDL_Color get_current_color() const;
    int       get_brightness() const;
    SDL_Point get_orbit_center() const { return center_; }
    SDL_Point get_direction_reference() const { return map_reference_center_; }
    SDL_Point get_direction_target() const;
    bool      initialize_from_map_manifest(const nlohmann::json& map_info, std::string_view map_id);
    void      set_screen_orbit_center(SDL_Point screen_center);
    void      set_direction_reference_world(SDL_Point world_point);
    void      set_direction_target_world(SDL_Point world_point);
    // Temporarily override the alpha channel of the current map light color
    // without modifying underlying map configuration. Pass std::nullopt to clear.
    void      set_alpha_override(std::optional<Uint8> alpha);

        private:
    struct KeyEntry {
        float degree = 0.0f;
        utils::color::RangedColor range{{255,255},{255,255},{255,255},{255,255}};
        SDL_Color color{255, 255, 255, 255};
        bool needs_resolve = false;
};
    bool load_from_map_manifest(const nlohmann::json& map_info, std::string_view map_id);
    void set_defaults(int screen_width, SDL_Color fallback_base_color);
    void set_light_brightness();
    Uint8 clamp_alpha(Uint8 value) const;
    SDL_Color clamp_color_alpha(SDL_Color color) const;
    SDL_Color compute_color_from_horizon(float degree) const;
    void update_active_segment(float degree);
    void resolve_key_entry(KeyEntry& entry);
    void      recalc_position();

        private:
    SDL_Renderer* renderer_;
    utils::color::RangedColor base_color_range_{{255,255},{255,255},{255,255},{255,255}};
    SDL_Color base_color_;
    SDL_Color current_color_;
    SDL_Point default_center_;
    SDL_Point default_map_center_;
    SDL_Point center_;
    SDL_Point map_reference_center_;
    SDL_Point direction_target_world_;
    SDL_FPoint smoothed_target_world_{0.0f, 0.0f};
    SDL_FPoint smoothed_direction_{1.0f, 0.0f};
    bool smoothed_target_valid_    = false;
    bool smoothed_direction_valid_ = false;
    SDL_Point pos_;
    float angle_;
    bool  initialized_;
    bool  orbit_initialized_ = false;
    int   frame_counter_;
    int   light_brightness;
    float radius_;
    float intensity_;
    float mult_;
    float fall_off_;
    int   orbit_radius_x_;
    int   orbit_radius_y_;
    int   update_interval_;
    std::vector<KeyEntry> key_colors_;
    bool resolve_each_orbit_ = false;
    bool base_pending_resolve_ = false;
    float last_degree_ = 0.0f;
    size_t active_segment_start_ = 0;
    size_t active_segment_end_ = 0;

    // Optional override for alpha channel applied in get_current_color()
    std::optional<Uint8> alpha_override_{};
};
