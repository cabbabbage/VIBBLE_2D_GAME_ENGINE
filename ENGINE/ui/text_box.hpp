#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>

// Simple text input box with label.
// - Click to focus, type to edit. Enter or click outside to commit.
// - Returns true from handle_event when text changes.
class TextBox {
public:
    TextBox(const std::string& label, const std::string& value);
    void set_position(int x, int y);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const;
    void set_label(const std::string& s);
    const std::string& label() const;
    void set_value(const std::string& v);
    const std::string& value() const;
    bool is_editing() const { return editing_; }
    void set_editing(bool e);
    // Returns true if the underlying value changed this event
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    static int width();
    static int height();
private:
    void draw_text(SDL_Renderer* r, const std::string& s, int x, int y, SDL_Color col) const;
private:
    SDL_Rect rect_{0,0,420,36};
    std::string label_;
    std::string text_;
    bool hovered_ = false;
    bool editing_ = false;
};
