#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include "styles.hpp"

struct GlassButtonStyle {

    int   radius = 20;

    float refraction_strength = 0.055f;

    float rough_scale   = 0.035f;
    float rough_ampl_px = 3.50f;

    int   diffusion_taps    = 9;
    float diffusion_radius  = 2.8f;

    float chroma_strength   = 0.70f;

    float mix_normal   = 0.50f;
    float mix_hover    = 0.70f;
    float mix_pressed  = 0.35f;

    float fresnel_power     = 2.20f;
    float fresnel_intensity = 0.60f;

    bool  overlay_enabled                = true;
    float overlay_opacity                = 0.65f;
    float overlay_bright_to_alpha_gamma  = 1.0f;

    float ray_threshold   = 0.55f;
    float ray_intensity   = 1.10f;
    float ray_length      = 0.45f;
    int   ray_steps       = 8;

    int   motion_blur_radius = 8;
    float motion_blur_mix    = 0.68f;

    int   blur_px         = 0;
    int   blur_px_hover   = 0;
    int   blur_px_pressed = 0;

    SDL_Color text_color  = SDL_Color{252,252,252,255};
    SDL_Color text_stroke = SDL_Color{0,0,0,110};

    SDL_Color border_light{0,0,0,0};
    SDL_Color border_dark{0,0,0,0};
    SDL_Color inner_shadow{0,0,0,0};
    SDL_Color outer_shadow{0,0,0,0};
    SDL_Color tint{0,0,0,0};
    SDL_Color tint_hover{0,0,0,0};
    SDL_Color tint_pressed{0,0,0,0};
    float     noise_opacity = 0.0f;
    float     smudge_opacity = 0.0f;
    SDL_Color highlight_color{255,255,255,255};
    SDL_Color highlight_glow_color{255,255,255,235};
    SDL_Color focus_ring_inner{0,0,0,0};
    SDL_Color focus_ring_outer{0,0,0,0};
    SDL_Color disabled_text{200,200,200,200};
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

    static int  width();
    static int  height();

    static const GlassButtonStyle& default_glass_style();

    static void refresh_glass_overlay();

    void enable_glass_style(bool enabled);
    void set_glass_style(const GlassButtonStyle& style);

private:

    void draw_deco(SDL_Renderer* r, const SDL_Rect& rect, bool hovered) const;

    void draw_glass(SDL_Renderer* renderer, const SDL_Rect& rect) const;

    void draw_glass_text(SDL_Renderer* renderer, const SDL_Rect& rect) const;

private:
    SDL_Rect        rect_{0,0,520,64};
    std::string     label_;
    bool            hovered_ = false;
    bool            pressed_ = false;
    const ButtonStyle* style_ = nullptr;

    bool            glass_enabled_ = false;
    GlassButtonStyle glass_style_{};

    mutable float   glass_luminance_ = 0.0f;
    mutable bool    glass_has_luminance_ = false;
};
