#include "input.hpp"

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
        int idx = -1;
        switch (e.button.button) {
            case SDL_BUTTON_LEFT:   idx = LEFT;   break;
            case SDL_BUTTON_RIGHT:  idx = RIGHT;  break;
            case SDL_BUTTON_MIDDLE: idx = MIDDLE; break;
            case SDL_BUTTON_X1:     idx = X1;     break;
            case SDL_BUTTON_X2:     idx = X2;     break;
        }
        if (idx >= 0) {
            buttons_[idx] = down;
            if (!down) {
                // small click window
                clickBuffer_[idx] = 3;
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

