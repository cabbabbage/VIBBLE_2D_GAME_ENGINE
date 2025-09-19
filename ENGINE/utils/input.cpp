#include "input.hpp"

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
} // namespace

void Input::handleEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_MOUSEMOTION:
        dx_ = e.motion.xrel;
        dy_ = e.motion.yrel;
        x_ = e.motion.x;
        y_ = e.motion.y;
        break;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        bool down = (e.type == SDL_MOUSEBUTTONDOWN);
        Button button = to_button(e.button.button);
        if (button != COUNT) {
            buttons_[button] = down;
            if (!down) {
                // small click window
                clickBuffer_[button] = 3;
            }
        }
        break;
    }

    case SDL_MOUSEWHEEL:
        scrollX_ += e.wheel.x;
        scrollY_ += e.wheel.y;
        break;

    case SDL_KEYDOWN:
        keys_down_[e.key.keysym.scancode] = true;
        break;
    case SDL_KEYUP:
        keys_down_[e.key.keysym.scancode] = false;
        break;

    default:
        break;
    }
}

void Input::update() {
    // Mouse button transitions
    for (int i = 0; i < COUNT; ++i) {
        pressed_[i]   = (!prevButtons_[i] && buttons_[i]);
        released_[i]  = (prevButtons_[i] && !buttons_[i]);
        prevButtons_[i] = buttons_[i];
        if (clickBuffer_[i] > 0) clickBuffer_[i]--;
    }

    // Keyboard transitions
    for (int i = 0; i < SDL_NUM_SCANCODES; ++i) {
        keys_pressed_[i]  = (!prev_keys_down_[i] && keys_down_[i]);
        keys_released_[i] = (prev_keys_down_[i] && !keys_down_[i]);
        prev_keys_down_[i] = keys_down_[i];
    }

    // Reset per-frame deltas
    dx_ = dy_ = 0;
    scrollX_ = scrollY_ = 0;
}

bool Input::wasClicked(Button b) const {
    return clickBuffer_[b] > 0;
}

void Input::clearClickBuffer() {
    for (int i = 0; i < COUNT; ++i) {
        clickBuffer_[i] = 0;
    }
}

void Input::consumeMouseButton(Button b) {
    if (b < 0 || b >= COUNT) return;
    buttons_[b] = prevButtons_[b];
    pressed_[b] = false;
    released_[b] = false;
    clickBuffer_[b] = 0;
}

void Input::consumeAllMouseButtons() {
    for (int i = 0; i < COUNT; ++i) {
        consumeMouseButton(static_cast<Button>(i));
    }
}

void Input::consumeScroll() {
    scrollX_ = 0;
    scrollY_ = 0;
}

void Input::consumeMotion() {
    dx_ = 0;
    dy_ = 0;
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

