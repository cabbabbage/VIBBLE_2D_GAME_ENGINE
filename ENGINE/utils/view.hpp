#pragma once

#include <SDL.h>
#include <algorithm>
#include "asset/Asset.hpp"

class view {
public:
    struct Bounds {
        int left;
        int right;
        int top;
        int bottom;
    };

    view(int screen_width, int screen_height, const Bounds& starting_bounds);

    void set_scale(float s);
    float get_scale() const;

    Bounds get_base_bounds() const;
    Bounds get_current_bounds() const;

    SDL_Rect to_world_rect(int cx, int cy) const;

    bool is_point_in_bounds(int x, int y, int cx, int cy) const;
    bool is_asset_in_bounds(const Asset& a, int cx, int cy) const;

    void zoom_scale(double target_scale, int duration_steps);
    void zoom_bounds(const Bounds& target_bounds, int duration_steps);
    void update();
    bool intro = true;
    bool   zooming_      = false;
private:
    Bounds base_bounds_{};      
    Bounds current_bounds_{};   
    float  scale_ = 1.0f;

    double start_scale_  = 1.0;
    double target_scale_ = 1.0;
    int    steps_total_  = 0;
    int    steps_done_   = 0;


    static inline bool aabb_intersect(const SDL_Rect& A, const SDL_Rect& B) {
        return !(A.x + A.w <= B.x || B.x + B.w <= A.x ||
                 A.y + A.h <= B.y || B.y + B.h <= A.y);
    }
};
