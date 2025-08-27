#include "view.hpp"
#include <cmath>

view::view(int screen_width, int screen_height, const Bounds& starting_bounds)
{
    // Store the exact starting bounds (global, unscaled)
    current_bounds_ = starting_bounds;

    // Set base bounds to half-width/height of screen
    base_bounds_.left   = -screen_width  ;
    base_bounds_.right  =  screen_width  ;
    base_bounds_.top    = -screen_height ;
    base_bounds_.bottom =  screen_height ;

    // Expand base bounds by 10%
    int extra_w = static_cast<int>((base_bounds_.right - base_bounds_.left) * 1.0f / 2.0f);
    int extra_h = static_cast<int>((base_bounds_.bottom - base_bounds_.top) * 1.0f / 2.0f);
    base_bounds_.left   -= extra_w;
    base_bounds_.right  += extra_w;
    base_bounds_.top    -= extra_h;
    base_bounds_.bottom += extra_h + 100;

    // Initial scale is ratio of current bounds to base bounds (using width ratio)
    int base_w = base_bounds_.right - base_bounds_.left;
    int curr_w = current_bounds_.right - current_bounds_.left;
    scale_ = (base_w != 0) ? static_cast<float>(curr_w) / static_cast<float>(base_w) : 1.0f;

    zooming_ = false;
    steps_total_ = steps_done_ = 0;
    start_scale_ = target_scale_ = scale_;
}

void view::set_scale(float s) {
    scale_ = (s > 0.0f) ? s : 0.0001f;
    zooming_ = false;
    steps_total_ = steps_done_ = 0;
    start_scale_ = target_scale_ = scale_;
}

float view::get_scale() const {
    return scale_;
}

view::Bounds view::get_base_bounds() const {
    return base_bounds_;
}

view::Bounds view::get_current_bounds() const {
    Bounds b;
    b.left   = static_cast<int>(std::round(base_bounds_.left   * scale_));
    b.right  = static_cast<int>(std::round(base_bounds_.right  * scale_));
    b.top    = static_cast<int>(std::round(base_bounds_.top    * scale_));
    b.bottom = static_cast<int>(std::round(base_bounds_.bottom * scale_));
    return b;
}

SDL_Rect view::to_world_rect(int cx, int cy) const {
    Bounds b = get_current_bounds();
    int x = cx + b.left;
    int y = cy + b.top;
    int w = (b.right - b.left);
    int h = (b.bottom - b.top);
    return SDL_Rect{ x, y, w, h };
}

bool view::is_point_in_bounds(int x, int y, int cx, int cy) const {
    SDL_Rect vr = to_world_rect(cx, cy);
    return (x >= vr.x && x < vr.x + vr.w &&
            y >= vr.y && y < vr.y + vr.h);
}

bool view::is_asset_in_bounds(const Asset& a, int cx, int cy) const {
    SDL_Texture* tex = a.get_current_frame();
    int tw = 0, th = 0;
    if (tex) {
        SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    }

    int ax = a.pos_X - tw / 2;
    int ay = a.pos_Y - th;
    SDL_Rect asset_rect{ ax, ay, std::max(1, tw), std::max(1, th) };

    SDL_Rect view_rect = to_world_rect(cx, cy);
    return aabb_intersect(asset_rect, view_rect);
}

void view::zoom_scale(double target_scale, int duration_steps) {
    double clamped = (target_scale > 0.0) ? target_scale : 0.0001;
    if (duration_steps <= 0) {
        set_scale(static_cast<float>(clamped));
        return;
    }
    start_scale_  = scale_;
    target_scale_ = clamped;
    steps_total_  = duration_steps;
    steps_done_   = 0;
    zooming_      = true;
}

void view::zoom_bounds(const Bounds& target_bounds, int duration_steps) {
    const int base_w = base_bounds_.right - base_bounds_.left;
    const int base_h = base_bounds_.bottom - base_bounds_.top;
    const int tgt_w  = target_bounds.right - target_bounds.left;
    const int tgt_h  = target_bounds.bottom - target_bounds.top;

    double sx = base_w != 0 ? static_cast<double>(tgt_w) / static_cast<double>(base_w) : 1.0;
    double sy = base_h != 0 ? static_cast<double>(tgt_h) / static_cast<double>(base_h) : 1.0;

    double target = sx;
    if (std::abs(sx - sy) > 0.001) {
        target = (sx + sy) * 0.5;
    }

    zoom_scale(target, duration_steps);
}

void view::update() {
    if (!zooming_) {
        intro = false;
        return;}

    ++steps_done_;
    if (steps_done_ >= steps_total_) {
        scale_ = static_cast<float>(target_scale_);
        zooming_ = false;
        steps_total_ = steps_done_ = 0;
        start_scale_ = target_scale_;
        return;
    }

    double t = static_cast<double>(steps_done_) / static_cast<double>(steps_total_);
    double s = start_scale_ + (target_scale_ - start_scale_) * t;
    scale_ = static_cast<float>(std::max(0.0001, s));
}
