#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <optional>
#include "utils/text_style.hpp"

// Reusable horizontal slider UI element.
// Construct with label, min, max, and optional starting value.
// Usage pattern mirrors Button: call handle_event() each frame,
// check if it returned true (value changed), then read value().
//
// Example:
//   Slider volume("Volume", 0, 100, 50);
//   volume.set_position(100, 200);
//   if (volume.handle_event(e)) {
//       int v = volume.value();
//       // apply v
//   }
//   volume.render(renderer);
// Style for a flat, professional slider look.
struct SliderStyle {
    SDL_Color frame_normal{200,200,200,255};
    SDL_Color frame_hover{160,160,160,255};
    SDL_Color track_bg{235,238,241,255};
    SDL_Color track_fill{59,130,246,255};  // blue
    SDL_Color knob_fill{248,249,251,255};
    SDL_Color knob_fill_hover{241,243,245,255};
    SDL_Color knob_frame{180,185,190,255};
    SDL_Color knob_frame_hover{120,130,140,255};
    TextStyle label_style{ "C:/Windows/Fonts/segoeui.ttf", 16, SDL_Color{75,85,99,255} }; // gray-600
    TextStyle value_style{ "C:/Windows/Fonts/segoeui.ttf", 16, SDL_Color{31,41,55,255} }; // gray-800
};

class Slider {
public:
    // Constructors
    Slider(const std::string& label, int min_val, int max_val);
    Slider(const std::string& label, int min_val, int max_val, int current_val);

    // Geometry
    void set_position(int x, int y);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const;

    // Label
    void set_label(const std::string& text);
    const std::string& label() const;

    // Range & value
    void set_range(int min_val, int max_val);
    int  min() const;
    int  max() const;
    void set_value(int v);
    int  value() const;

    // Input & render
    // Returns true if the slider value changed due to this event
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;

    // Default dimensions (matches Button proportions by default)
    static int width();
    static int height();

    // Style
    void set_style(const SliderStyle* style) { style_ = style; }
    const SliderStyle* style() const { return style_; }

private:
    // Helpers
    SDL_Rect track_rect() const;          // inner track area within rect_
    SDL_Rect knob_rect_for_value(int v) const;
    int      value_for_x(int mouse_x) const; // map mouse x to slider value
    void     draw_track(SDL_Renderer* r) const;
    void     draw_knob(SDL_Renderer* r, const SDL_Rect& krect, bool hovered) const;
    void     draw_text(SDL_Renderer* r) const;

private:
    SDL_Rect rect_{0,0,520,64};
    std::string label_;
    int min_ = 0;
    int max_ = 100;
    int value_ = 0;
    bool dragging_ = false;
    bool knob_hovered_ = false;
    const SliderStyle* style_ = nullptr; // optional custom style
};
