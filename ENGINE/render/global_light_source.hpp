
#pragma once

#include <SDL.h>
#include <vector>
#include <utility>
#include <string>

class Global_Light_Source {
public:
    Global_Light_Source(SDL_Renderer* renderer,
                        int screen_center_x,
                        int screen_center_y,
                        int screen_width,
                        SDL_Color fallback_base_color,
                        const std::string& map_path);

    ~Global_Light_Source();

    void update();

    std::pair<int,int> get_position() const;
    float              get_angle() const;
    SDL_Texture*       get_texture() const;
    SDL_Color          get_tint() const;

    
    SDL_Color apply_tint_to_color(const SDL_Color& base, int alpha_mod) const;

    SDL_Color get_current_color() const;
    int       get_brightness() const;

    
    int get_cached_w() const { return cached_w_; }
    int get_cached_h() const { return cached_h_; }

private:
    struct KeyEntry {
        float degree;
        SDL_Color color;
    };

    void build_texture();
    void set_light_brightness();
    SDL_Color compute_color_from_horizon() const;

private:
    SDL_Renderer* renderer_;
    SDL_Texture*  texture_;

    SDL_Color base_color_;
    SDL_Color current_color_;
    SDL_Color tint_;

    int   center_x_;
    int   center_y_;
    float angle_;
    bool  initialized_;

    int pos_x_;
    int pos_y_;

    int  frame_counter_;
    int  light_brightness;

    
    float radius_;
    float intensity_;
    float mult_;
    float fall_off_;
    int   orbit_radius;
    int   update_interval_;

    std::vector<KeyEntry> key_colors_;

    
    int cached_w_ = 0;
    int cached_h_ = 0;
};
