#pragma once

#include <SDL.h>
#include <array>
#include <functional>
#include <optional>
#include <vector>

class Input {
public:
    enum Button { LEFT, RIGHT, MIDDLE, X1, X2, COUNT };

    void handleEvent(const SDL_Event& e);
    void update();

    bool isDown(Button b) const { return buttons_[b]; }
    bool wasPressed(Button b) const { return pressed_[b]; }
    bool wasReleased(Button b) const { return released_[b]; }
    bool wasClicked(Button b) const;
    void clearClickBuffer();

    void consumeMouseButton(Button b);
    void consumeAllMouseButtons();
    void consumeScroll();
    void consumeMotion();
    void consumeEvent(const SDL_Event& e);

    int getX() const { return x_; }
    int getY() const { return y_; }
    int getDX() const { return dx_; }
    int getDY() const { return dy_; }
    int getScrollX() const { return scrollX_; }
    int getScrollY() const { return scrollY_; }

    bool isKeyDown(SDL_Keycode key) const {
        SDL_Scancode sc = SDL_GetScancodeFromKey(key);
        return keys_down_[sc];
    }
    bool wasKeyPressed(SDL_Keycode key) const {
        SDL_Scancode sc = SDL_GetScancodeFromKey(key);
        return keys_pressed_[sc];
    }
    bool wasKeyReleased(SDL_Keycode key) const {
        SDL_Scancode sc = SDL_GetScancodeFromKey(key);
        return keys_released_[sc];
    }

    bool isScancodeDown(SDL_Scancode sc) const { return keys_down_[sc]; }
    bool wasScancodePressed(SDL_Scancode sc) const { return keys_pressed_[sc]; }
    bool wasScancodeReleased(SDL_Scancode sc) const { return keys_released_[sc]; }

    using ScreenToWorldFunction = std::function<SDL_Point(SDL_Point)>;
    void set_screen_to_world_mapper(ScreenToWorldFunction fn);
    void clear_screen_to_world_mapper();
    bool has_screen_to_world_mapper() const { return static_cast<bool>(screen_to_world_fn_); }
    std::optional<SDL_Point> screen_to_world(SDL_Point screen) const;
    std::optional<SDL_Point> mouse_world_position() const;

private:
    bool buttons_[COUNT] = {false};
    bool prevButtons_[COUNT] = {false};
    bool pressed_[COUNT] = {false};
    bool released_[COUNT] = {false};
    int  clickBuffer_[COUNT] = {0};

    int x_ = 0, y_ = 0;
    int dx_ = 0, dy_ = 0;
    int scrollX_ = 0, scrollY_ = 0;

    std::array<bool, SDL_NUM_SCANCODES> keys_down_{};
    std::array<bool, SDL_NUM_SCANCODES> prev_keys_down_{};
    std::array<bool, SDL_NUM_SCANCODES> keys_pressed_{};
    std::array<bool, SDL_NUM_SCANCODES> keys_released_{};

    std::vector<SDL_Scancode> dirty_scancodes_;
    std::vector<SDL_Scancode> pressed_scancode_buffer_;
    std::vector<SDL_Scancode> released_scancode_buffer_;
    std::array<bool, SDL_NUM_SCANCODES> scancode_dirty_flags_{};

    ScreenToWorldFunction screen_to_world_fn_{};

    void refresh_click_buffer_active();
    void refresh_button_transition_active();

    bool button_state_dirty_ = false;
    bool button_transition_active_ = false;
    bool mouse_motion_dirty_ = false;
    bool scroll_dirty_ = false;
    bool click_buffer_active_ = false;
};

