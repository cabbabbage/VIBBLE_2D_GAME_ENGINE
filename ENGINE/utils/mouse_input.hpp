#pragma once
#include <SDL.h>
#include <array>
// mouse_input.hpp
class MouseInput {
public:
    enum Button { LEFT, RIGHT, MIDDLE, X1, X2, COUNT };

    void handleEvent(const SDL_Event& e);
    void update();

    bool isDown(Button b) const { return buttons[b]; }
    bool wasPressed(Button b) const { return pressed[b]; }
    bool wasReleased(Button b) const { return released[b]; }
    bool wasClicked(Button b) const; // ✅ new — survives short taps

    int getX() const { return x; }
    int getY() const { return y; }

private:
    bool buttons[COUNT]   = {false};
    bool prevButtons[COUNT] = {false};
    bool pressed[COUNT]   = {false};
    bool released[COUNT]  = {false};

    int clickBuffer[COUNT] = {0}; // ✅ counts down frames to keep clicks alive

    int x = 0, y = 0;
    int dx = 0, dy = 0;
    int scrollX = 0, scrollY = 0;
};
