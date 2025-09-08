#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <optional>
#include "utils/text_style.hpp"

struct SliderStyle {
	SDL_Color frame_normal{200,200,200,255};
	SDL_Color frame_hover{160,160,160,255};
	SDL_Color track_bg{235,238,241,255};
	SDL_Color track_fill{59,130,246,255};
	SDL_Color knob_fill{248,249,251,255};
	SDL_Color knob_fill_hover{241,243,245,255};
	SDL_Color knob_frame{180,185,190,255};
	SDL_Color knob_frame_hover{120,130,140,255};
	TextStyle label_style{ "C:/Windows/Fonts/segoeui.ttf", 16, SDL_Color{75,85,99,255} };
	TextStyle value_style{ "C:/Windows/Fonts/segoeui.ttf", 16, SDL_Color{31,41,55,255} };
};

class Slider {

	public:
    Slider(const std::string& label, int min_val, int max_val);
    Slider(const std::string& label, int min_val, int max_val, int current_val);
    void set_position(int x, int y);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const;
    void set_label(const std::string& text);
    const std::string& label() const;
    void set_range(int min_val, int max_val);
    int  min() const;
    int  max() const;
    void set_value(int v);
    int  value() const;
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;
    static int width();
    static int height();
    void set_style(const SliderStyle* style) { style_ = style; }
    const SliderStyle* style() const { return style_; }

	private:
    SDL_Rect track_rect() const;
    SDL_Rect knob_rect_for_value(int v) const;
    int      value_for_x(int mouse_x) const;
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
    const SliderStyle* style_ = nullptr;
};
