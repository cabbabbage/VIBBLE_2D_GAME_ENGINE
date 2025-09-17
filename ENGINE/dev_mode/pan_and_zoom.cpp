#include "dev_mode/pan_and_zoom.hpp"

#include "render/camera.hpp"
#include "utils/input.hpp"

#include <algorithm>
#include <cmath>

void PanAndZoom::set_zoom_scale_factor(double factor) {
    zoom_scale_factor_ = (factor > 0.0) ? factor : 1.0;
}

void PanAndZoom::handle_input(camera& cam, const Input& input, bool pan_blocked) {
    const int wheel_y = input.getScrollY();
    if (wheel_y != 0) {
        const double step = (zoom_scale_factor_ > 0.0) ? zoom_scale_factor_ : 1.0;
        double eff = 1.0;
        if (wheel_y > 0) {
            eff = std::pow(step, wheel_y);
        } else if (wheel_y < 0) {
            eff = 1.0 / std::pow(step, -wheel_y);
        }
        const int base = 18;
        const int dur = std::max(6, base - 2 * std::min(6, std::abs(wheel_y)));
        cam.animate_zoom_multiply(eff, dur);
    }

    if (input.wasReleased(Input::LEFT)) {
        panning_ = false;
    }

    if (input.wasPressed(Input::LEFT)) {
        if (!pan_blocked) {
            panning_ = true;
            pan_start_mouse_map_ = cam.screen_to_map(SDL_Point{input.getX(), input.getY()});
            pan_start_center_ = cam.get_screen_center();
            cam.set_manual_zoom_override(true);
            cam.set_focus_override(pan_start_center_);
        } else {
            panning_ = false;
        }
    }

    if (!panning_ || !input.isDown(Input::LEFT)) {
        return;
    }

    SDL_Point current = cam.screen_to_map(SDL_Point{input.getX(), input.getY()});
    SDL_Point delta{pan_start_mouse_map_.x - current.x, pan_start_mouse_map_.y - current.y};
    SDL_Point new_center{pan_start_center_.x + delta.x, pan_start_center_.y + delta.y};
    cam.set_focus_override(new_center);
    cam.set_screen_center(new_center);
}
