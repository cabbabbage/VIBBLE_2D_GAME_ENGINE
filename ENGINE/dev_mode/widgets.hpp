#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <memory>
#include <string>
#include <vector>

#include "dm_styles.hpp"

class DMButton {
public:
    DMButton(const std::string& text, const DMButtonStyle* style, int w, int h);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const { return rect_; }
    void set_text(const std::string& t) { text_ = t; }
    const std::string& text() const { return text_; }
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    bool is_hovered() const { return hovered_; }
    static int height() { return 28; }
private:
    void draw_label(SDL_Renderer* r, SDL_Color col) const;
    SDL_Rect rect_{0,0,200,28};
    std::string text_;
    bool hovered_ = false;
    bool pressed_ = false;
    const DMButtonStyle* style_ = nullptr;
};

class DMTextBox {
public:
    DMTextBox(const std::string& label, const std::string& value);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const { return rect_; }
    void set_value(const std::string& v) { text_ = v; }
    const std::string& value() const { return text_; }
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    bool is_editing() const { return editing_; }
    static int height() { return 32; }
private:
    void draw_text(SDL_Renderer* r, const std::string& s, int x, int y, const DMLabelStyle& ls) const;
    SDL_Rect rect_{0,0,200,32};
    std::string label_;
    std::string text_;
    bool hovered_ = false;
    bool editing_ = false;
};

class DMCheckbox {
public:
    DMCheckbox(const std::string& label, bool value);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const { return rect_; }
    void set_value(bool v) { value_ = v; }
    bool value() const { return value_; }
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    static int height() { return 28; }
private:
    void draw_label(SDL_Renderer* r) const;
    SDL_Rect rect_{0,0,200,28};
    std::string label_;
    bool value_ = false;
    bool hovered_ = false;
};

class DMTextBox; // forward

class DMSlider {
public:
    DMSlider(const std::string& label, int min_val, int max_val, int value);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const { return rect_; }
    void set_value(int v);
    int value() const { return value_; }
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    static int height() { return 40; }
private:
    SDL_Rect track_rect() const;
    SDL_Rect knob_rect() const;
    int value_for_x(int x) const;
    void draw_text(SDL_Renderer* r, const std::string& s, int x, int y) const;
    SDL_Rect rect_{0,0,200,40};
    std::string label_;
    int min_ = 0;
    int max_ = 100;
    int value_ = 0;
    bool dragging_ = false;
    bool knob_hovered_ = false;
    std::unique_ptr<DMTextBox> edit_box_;
};

class DMDropdown {
public:
    DMDropdown(const std::string& label, const std::vector<std::string>& options, int idx = 0);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const { return rect_; }
    int selected() const { return index_; }
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    static int height() { return 32; }
private:
    SDL_Rect rect_{0,0,200,32};
    std::string label_;
    std::vector<std::string> options_;
    int index_ = 0;
    bool hovered_ = false;
    bool expanded_ = false;
};

