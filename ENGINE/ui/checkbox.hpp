#pragma once

#include <SDL.h>
#include <string>

// Simple reusable checkbox UI element.
// Initialized with a label and initial value.
class Checkbox {
public:
    Checkbox(const std::string& label, bool value);

    void set_position(int x, int y);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const;

    void set_label(const std::string& s);
    const std::string& label() const;

    void set_value(bool v);
    bool value() const;

    // Returns true when toggled
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;

    static int width();
    static int height();

private:
    SDL_Rect rect_{0,0,300,28};
    std::string label_;
    bool value_ = false;
    bool hovered_ = false;
};
