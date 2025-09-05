#pragma once
#include <SDL.h>
#include <unordered_set>

class Input {
public:
    enum Button { LEFT, RIGHT, MIDDLE, X1, X2, COUNT };

    // Feed all SDL events here (keyboard + mouse)
    void handleEvent(const SDL_Event& e);
    // Call once per frame to update pressed/released states
    void update();

    // Mouse queries
    bool isDown(Button b) const { return buttons_[b]; }
    bool wasPressed(Button b) const { return pressed_[b]; }
    bool wasReleased(Button b) const { return released_[b]; }
    bool wasClicked(Button b) const; // short-lived click buffer

    // Clear all pending click buffers (prevents stale clicks being processed
    // for multiple frames, e.g., when switching UI modes)
    void clearClickBuffer();

    int getX() const { return x_; }
    int getY() const { return y_; }

    int getDX() const { return dx_; }
    int getDY() const { return dy_; }
    int getScrollX() const { return scrollX_; }
    int getScrollY() const { return scrollY_; }

    // Keyboard queries
    bool isKeyDown(SDL_Keycode key) const { return keys_down_.count(key) != 0; }
    bool wasKeyPressed(SDL_Keycode key) const { return keys_pressed_.count(key) != 0; }
    bool wasKeyReleased(SDL_Keycode key) const { return keys_released_.count(key) != 0; }

private:
    // Mouse state
    bool buttons_[COUNT]      = {false};
    bool prevButtons_[COUNT]  = {false};
    bool pressed_[COUNT]      = {false};
    bool released_[COUNT]     = {false};
    int  clickBuffer_[COUNT]  = {0};
    int  x_ = 0, y_ = 0;
    int  dx_ = 0, dy_ = 0;
    int  scrollX_ = 0, scrollY_ = 0;

    // Keyboard state
    std::unordered_set<SDL_Keycode> keys_down_;
    std::unordered_set<SDL_Keycode> prev_keys_down_;
    std::unordered_set<SDL_Keycode> keys_pressed_;
    std::unordered_set<SDL_Keycode> keys_released_;
};
