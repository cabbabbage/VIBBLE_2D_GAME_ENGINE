#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include "styles.hpp"

class Button {
public:
    static Button get_main_button(const std::string& text);
    static Button get_exit_button(const std::string& text);
public:
    Button();
    Button(const std::string& text, const ButtonStyle* style, int w, int h);
    void set_position(int x, int y);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const;
    void set_text(const std::string& text);
    const std::string& text() const;
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;
    bool is_hovered() const;
    bool is_pressed() const;
    static int width();
    static int height();
private:
    void draw_deco(SDL_Renderer* r, const SDL_Rect& rect, bool hovered) const;
private:
    SDL_Rect rect_{0,0,520,64};
    std::string label_;
    bool hovered_ = false;
    bool pressed_ = false;
    const ButtonStyle* style_ = nullptr;
};
