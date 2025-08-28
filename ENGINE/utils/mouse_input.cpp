#include "mouse_input.hpp"

void MouseInput::handleEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_MOUSEMOTION:
        dx = e.motion.xrel;
        dy = e.motion.yrel;
        x = e.motion.x;
        y = e.motion.y;
        break;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        bool down = (e.type == SDL_MOUSEBUTTONDOWN);
        int idx = -1;
        switch (e.button.button) {
            case SDL_BUTTON_LEFT:   idx = LEFT; break;
            case SDL_BUTTON_RIGHT:  idx = RIGHT; break;
            case SDL_BUTTON_MIDDLE: idx = MIDDLE; break;
            case SDL_BUTTON_X1:     idx = X1; break;
            case SDL_BUTTON_X2:     idx = X2; break;
        }
        if (idx >= 0) {
            buttons[idx] = down;
            if (!down) {
                
                clickBuffer[idx] = 3; 
            }
        }
        break;
    }

    case SDL_MOUSEWHEEL:
        scrollX += e.wheel.x;
        scrollY += e.wheel.y;
        break;
    }
}

void MouseInput::update() {
    for (int i = 0; i < COUNT; ++i) {
        pressed[i]  = (!prevButtons[i] && buttons[i]);
        released[i] = (prevButtons[i] && !buttons[i]);
        prevButtons[i] = buttons[i];

        if (clickBuffer[i] > 0) clickBuffer[i]--; 
    }

    dx = dy = 0;
    scrollX = scrollY = 0;
}

bool MouseInput::wasClicked(Button b) const {
    return clickBuffer[b] > 0;
}
