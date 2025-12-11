#include "EditorUIPrimitives.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>

#include "dev_mode/draw_utils.hpp"
#include "dev_mode/dm_styles.hpp"

namespace animation_editor::ui {

bool WidgetRegistry::handle_event(const SDL_Event& e) const {
    bool handled = false;
    for (const auto& handler : handlers_) {
        if (handler && handler(e)) {
            handled = true;
        }
    }
    return handled;
}

void ScrollController::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    clamp();
}

void ScrollController::set_content_height(int height) {
    content_height_ = std::max(0, height);
    clamp();
}

void ScrollController::set_step_pixels(int step) {
    step_pixels_ = std::max(1, step);
}

void ScrollController::set_scroll(int value) {
    scroll_ = value;
    clamp();
}

SDL_Rect ScrollController::apply(const SDL_Rect& rect) const {
    SDL_Rect result = rect;
    result.y -= scroll_;
    return result;
}

bool ScrollController::handle_wheel(const SDL_Event& e) {
    if (e.type != SDL_MOUSEWHEEL) {
        return false;
    }
    int mx = 0;
    int my = 0;
    SDL_GetMouseState(&mx, &my);
    SDL_Point mouse{mx, my};
    if (!SDL_PointInRect(&mouse, &bounds_)) {
        return false;
    }

    int delta = e.wheel.y;
    if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
        delta = -delta;
    }
#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (delta == 0) {
        float precise = e.wheel.preciseY;
        if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            precise = -precise;
        }
        delta = static_cast<int>(std::round(precise));
        if (delta == 0 && precise != 0.0f) {
            delta = precise > 0.0f ? 1 : -1;
        }
    }
#endif
    return apply_wheel_delta(delta);
}

void ScrollController::clamp() {
    int max_scroll = std::max(0, content_height_ - bounds_.h);
    if (scroll_ < 0) {
        scroll_ = 0;
    } else if (scroll_ > max_scroll) {
        scroll_ = max_scroll;
    }
}

bool ScrollController::apply_wheel_delta(int delta_lines) {
    if (delta_lines == 0) {
        return false;
    }
    int max_scroll = std::max(0, content_height_ - bounds_.h);
    int new_scroll = std::clamp(scroll_ - delta_lines * step_pixels_, 0, max_scroll);
    bool changed = (new_scroll != scroll_);
    scroll_ = new_scroll;
    return changed;
}

void draw_panel_background(SDL_Renderer* renderer, const SDL_Rect& bounds) {
    dm_draw::DrawBeveledRect(renderer, bounds, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
}

}
