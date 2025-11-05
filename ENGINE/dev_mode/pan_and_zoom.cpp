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
        // Interpret wheel: negative = zoom IN (towards), positive = zoom OUT (straight)
        const double step = (zoom_scale_factor_ > 0.0) ? zoom_scale_factor_ : 1.0;
        const int ticks = std::abs(wheel_y);
        const bool zoom_in = (wheel_y < 0);
        const bool zoom_out = !zoom_in;

        // eff > 1 => zoom in; eff < 1 => zoom out
        const double mag = std::pow(step, ticks);
        const double eff = zoom_in ? mag : (1.0 / mag);

        const int dur = 10; // small, smooth transition for wheel zoom
        if (zoom_in) {
            // Zooming in: pan towards mouse cursor
            cam.animate_zoom_towards_point(eff, SDL_Point{ input.getX(), input.getY() }, dur);
        } else { // zoom_out
            // Zooming out: just zoom straight out without panning
            cam.animate_zoom_multiply(eff, dur);
        }
    }

    if (input.wasReleased(Input::LEFT)) {
        panning_ = false;
    }

    if (input.wasPressed(Input::LEFT)) {
        if (!pan_blocked) {
            panning_ = true;
            pan_start_mouse_screen_ = SDL_Point{ input.getX(), input.getY() };
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

    const double scale = std::max(0.0001, static_cast<double>(cam.get_scale()));
    const int dx = input.getX() - pan_start_mouse_screen_.x;
    const int dy = input.getY() - pan_start_mouse_screen_.y;
    SDL_Point new_center{
        static_cast<int>(std::lround(static_cast<double>(pan_start_center_.x) - static_cast<double>(dx) * scale)),
        static_cast<int>(std::lround(static_cast<double>(pan_start_center_.y) - static_cast<double>(dy) * scale))
    };
    cam.set_focus_override(new_center);
    cam.set_screen_center(new_center);
}

void PanAndZoom::cancel(camera& cam) {
    if (!panning_) {
        return;
    }
    panning_ = false;
    cam.set_manual_zoom_override(false);
    cam.clear_focus_override();
}
