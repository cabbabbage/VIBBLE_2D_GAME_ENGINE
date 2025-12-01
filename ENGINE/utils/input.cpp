#include "input.hpp"

#include <utility>

namespace {
Input::Button to_button(Uint8 sdl_button) {
    switch (sdl_button) {
    case SDL_BUTTON_LEFT:   return Input::LEFT;
    case SDL_BUTTON_RIGHT:  return Input::RIGHT;
    case SDL_BUTTON_MIDDLE: return Input::MIDDLE;
    case SDL_BUTTON_X1:     return Input::X1;
    case SDL_BUTTON_X2:     return Input::X2;
    default:                return Input::COUNT;
    }
}
}

void Input::handleEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_MOUSEMOTION:
        dx_ = e.motion.xrel;
        dy_ = e.motion.yrel;
        x_ = e.motion.x;
        y_ = e.motion.y;
        mouse_motion_dirty_ = true;
        break;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        bool down = (e.type == SDL_MOUSEBUTTONDOWN);
        Button button = to_button(e.button.button);
        if (button != COUNT) {
            buttons_[button] = down;
            button_state_dirty_ = true;
            if (!down) {

                clickBuffer_[button] = 3;
                click_buffer_active_ = true;
            }
        }
        break;
    }

    case SDL_MOUSEWHEEL:
        scrollX_ += e.wheel.x;
        scrollY_ += e.wheel.y;
        scroll_dirty_ = true;
        break;

    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        SDL_Scancode sc = e.key.keysym.scancode;
        keys_down_[sc] = (e.type == SDL_KEYDOWN);
        if (!scancode_dirty_flags_[sc]) {
            scancode_dirty_flags_[sc] = true;
            dirty_scancodes_.push_back(sc);
        }
        break;
    }

    default:
        break;
    }
}

void Input::update() {
    if (button_state_dirty_ || click_buffer_active_ || button_transition_active_) {
        bool any_click_active = false;
        bool any_transition = false;
        for (int i = 0; i < COUNT; ++i) {
            pressed_[i]     = (!prevButtons_[i] && buttons_[i]);
            released_[i]    = (prevButtons_[i] && !buttons_[i]);
            if (pressed_[i] || released_[i]) {
                any_transition = true;
            }
            prevButtons_[i] = buttons_[i];
            if (clickBuffer_[i] > 0) {
                --clickBuffer_[i];
                if (clickBuffer_[i] > 0) {
                    any_click_active = true;
                }
            }
        }
        click_buffer_active_ = any_click_active;
        button_transition_active_ = any_transition;
        button_state_dirty_ = false;
    }

    if (!pressed_scancode_buffer_.empty()) {
        for (SDL_Scancode sc : pressed_scancode_buffer_) {
            keys_pressed_[sc] = false;
        }
        pressed_scancode_buffer_.clear();
    }

    if (!released_scancode_buffer_.empty()) {
        for (SDL_Scancode sc : released_scancode_buffer_) {
            keys_released_[sc] = false;
        }
        released_scancode_buffer_.clear();
    }

    if (!dirty_scancodes_.empty()) {
        for (SDL_Scancode sc : dirty_scancodes_) {
            const bool is_down   = keys_down_[sc];
            const bool was_down  = prev_keys_down_[sc];
            const bool pressed   = (!was_down && is_down);
            const bool released  = (was_down && !is_down);
            keys_pressed_[sc]    = pressed;
            keys_released_[sc]   = released;
            if (pressed) {
                pressed_scancode_buffer_.push_back(sc);
            }
            if (released) {
                released_scancode_buffer_.push_back(sc);
            }
            prev_keys_down_[sc]     = is_down;
            scancode_dirty_flags_[sc] = false;
        }
        dirty_scancodes_.clear();
    }

    if (mouse_motion_dirty_ || dx_ != 0 || dy_ != 0) {
        dx_ = dy_ = 0;
        mouse_motion_dirty_ = false;
    }

    if (scroll_dirty_ || scrollX_ != 0 || scrollY_ != 0) {
        scrollX_ = scrollY_ = 0;
        scroll_dirty_ = false;
    }
}

bool Input::wasClicked(Button b) const {
    return clickBuffer_[b] > 0;
}

void Input::clearClickBuffer() {
    for (int i = 0; i < COUNT; ++i) {
        clickBuffer_[i] = 0;
    }
    click_buffer_active_ = false;
}

void Input::consumeMouseButton(Button b) {
    if (b < 0 || b >= COUNT) return;
    prevButtons_[b] = buttons_[b];
    pressed_[b] = false;
    released_[b] = false;
    clickBuffer_[b] = 0;
    refresh_click_buffer_active();
    refresh_button_transition_active();
}

void Input::consumeAllMouseButtons() {
    for (int i = 0; i < COUNT; ++i) {
        consumeMouseButton(static_cast<Button>(i));
    }
}

void Input::consumeScroll() {
    scrollX_ = 0;
    scrollY_ = 0;
    scroll_dirty_ = false;
}

void Input::consumeMotion() {
    dx_ = 0;
    dy_ = 0;
    mouse_motion_dirty_ = false;
}

void Input::consumeEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        Button button = to_button(e.button.button);
        if (button != COUNT) {
            consumeMouseButton(button);
        }
        break;
    }
    case SDL_MOUSEWHEEL:
        consumeScroll();
        break;
    case SDL_MOUSEMOTION:
        consumeMotion();
        break;
    default:
        break;
    }
}

void Input::set_screen_to_world_mapper(ScreenToWorldFunction fn) {
    screen_to_world_fn_ = std::move(fn);
}

void Input::clear_screen_to_world_mapper() {
    screen_to_world_fn_ = {};
}

std::optional<SDL_Point> Input::screen_to_world(SDL_Point screen) const {
    if (!screen_to_world_fn_) {
        return std::nullopt;
    }
    return screen_to_world_fn_(screen);
}

std::optional<SDL_Point> Input::mouse_world_position() const {
    SDL_Point screen{x_, y_};
    return screen_to_world(screen);
}

void Input::refresh_click_buffer_active() {
    click_buffer_active_ = false;
    for (int i = 0; i < COUNT; ++i) {
        if (clickBuffer_[i] > 0) {
            click_buffer_active_ = true;
            break;
        }
    }
}

void Input::refresh_button_transition_active() {
    button_transition_active_ = false;
    for (int i = 0; i < COUNT; ++i) {
        if (pressed_[i] || released_[i]) {
            button_transition_active_ = true;
            break;
        }
    }
}

