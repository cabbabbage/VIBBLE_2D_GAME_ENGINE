#include "dev_mode/pan_and_zoom.hpp"

#include "render/warped_screen_grid.hpp"
#include "utils/input.hpp"

#include <algorithm>
#include <cmath>

void PanAndZoom::set_zoom_scale_factor(double factor) {
    zoom_scale_factor_ = (factor > 0.0) ? factor : 1.0;
}

void PanAndZoom::handle_input(WarpedScreenGrid& cam, const Input& input, bool pan_blocked) {
    const SDL_Point mouse{ input.getX(), input.getY() };
    const int wheel_y = input.getScrollY();
    if (wheel_y != 0) {

        const double step = (zoom_scale_factor_ > 0.0) ? zoom_scale_factor_ : 1.0;
        const int ticks = std::abs(wheel_y);
        const bool zoom_in = (wheel_y < 0);
        const double mag = std::pow(step, ticks);
        const double eff = zoom_in ? mag : (1.0 / mag);
        const int dur = 10;

        const double base_scale = std::max(0.0001, static_cast<double>(cam.get_scale()));
        const double unclamped_target = base_scale * eff;
        const double target_scale = std::clamp( unclamped_target, 0.0001, static_cast<double>(WarpedScreenGrid::kMaxZoomAnchors));
        const double adjusted_eff = target_scale / base_scale;

        if (std::abs(adjusted_eff - 1.0) > 1e-6) {

            if (panning_) {
                cam.set_manual_zoom_override(true);
                cam.set_focus_override(cam.get_screen_center());
                cam.animate_zoom_multiply(adjusted_eff, dur);
            } else {

                cam.animate_zoom_towards_point(adjusted_eff, mouse, dur);
            }
        }
    }

    if (input.wasReleased(Input::LEFT)) {
        panning_ = false;
        pan_drag_pending_ = false;
    }

    if (input.wasPressed(Input::LEFT)) {
        if (!pan_blocked) {
            pan_drag_pending_ = true;
            pan_start_mouse_screen_ = mouse;
            pan_start_center_ = cam.get_screen_center();
        } else {
            panning_ = false;
            pan_drag_pending_ = false;
        }
    }

    const bool left_down = input.isDown(Input::LEFT);

    if (!left_down) {
        pan_drag_pending_ = false;
    }

    if (pan_blocked && !panning_) {
        pan_drag_pending_ = false;
    }

    if (!panning_ && pan_drag_pending_ && left_down) {
        const int dx = mouse.x - pan_start_mouse_screen_.x;
        const int dy = mouse.y - pan_start_mouse_screen_.y;
        if (dx != 0 || dy != 0) {
            panning_ = true;
            pan_drag_pending_ = false;
            cam.set_manual_zoom_override(true);
            cam.set_focus_override(pan_start_center_);
        }
    }

    if (!panning_ || !left_down) {
        return;
    }

    const double scale = std::max(0.0001, static_cast<double>(cam.get_scale()));
    const int dx = mouse.x - pan_start_mouse_screen_.x;
    const int dy = mouse.y - pan_start_mouse_screen_.y;
    SDL_Point new_center{
        static_cast<int>(std::lround(static_cast<double>(pan_start_center_.x) - static_cast<double>(dx) * scale)),  static_cast<int>(std::lround(static_cast<double>(pan_start_center_.y) - static_cast<double>(dy) * scale)) };
    cam.set_focus_override(new_center);
    cam.set_screen_center(new_center);
}

void PanAndZoom::cancel(WarpedScreenGrid& cam) {
    pan_drag_pending_ = false;
    if (!panning_) {
        return;
    }
    panning_ = false;
    cam.set_manual_zoom_override(false);
    cam.clear_focus_override();
}
