#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include "styles.hpp"

struct GlassButtonStyle {
    SDL_Color tint;
    SDL_Color tint_hover;
    SDL_Color tint_pressed;
    float     noise_opacity = 0.0f;
    float     smudge_opacity = 0.0f;
    int       radius = 0;
    SDL_Color border_light;
    SDL_Color border_dark;
    SDL_Color inner_shadow;
    SDL_Color outer_shadow;
    int       blur_px = 0;
    int       blur_px_hover = 0;
    int       blur_px_pressed = 0;
    SDL_Color text_color;
    SDL_Color text_stroke;
    SDL_Color focus_ring_inner;
    SDL_Color focus_ring_outer;
    SDL_Color disabled_text;
};

class Button {

	public:
    static Button get_main_button(const std::string& text);
    static Button get_exit_button(const std::string& text);

	public:
    Button();
    Button(const std::string& text, const ButtonStyle* style, int w, int h);
    void set_position(SDL_Point p);
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
    static void set_glass_blur_enabled(bool enabled);
    static const GlassButtonStyle& default_glass_style();
    void enable_glass_style(bool enabled);
    void set_glass_style(const GlassButtonStyle& style);

        private:
    void draw_deco(SDL_Renderer* r, const SDL_Rect& rect, bool hovered) const;
    void draw_glass(SDL_Renderer* renderer, const SDL_Rect& rect) const;
    void draw_glass_text(SDL_Renderer* renderer, const SDL_Rect& rect) const;

        private:
    SDL_Rect rect_{0,0,520,64};
    std::string label_;
    bool hovered_ = false;
    bool pressed_ = false;
    const ButtonStyle* style_ = nullptr;
    bool glass_enabled_ = false;
    GlassButtonStyle glass_style_{};
    mutable float glass_luminance_ = 0.0f;
    mutable bool glass_has_luminance_ = false;
    static bool glass_blur_enabled_;
};
