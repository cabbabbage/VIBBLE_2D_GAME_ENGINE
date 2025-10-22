#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include "styles.hpp"

// Hammered / pebbled glass style. The button draws no fill/outline;
// its bounds are visible only because it distorts what's behind it.
struct GlassButtonStyle {
    // Shape
    int   radius = 20;          // corner radius (px)

    // Base convex-lens refraction (pixels of warp scaled by size)
    float refraction_strength = 0.055f; // try 0.04–0.09

    // "Rough" hammered glass controls
    float rough_scale   = 0.035f; // noise frequency (larger = more bumps)
    float rough_ampl_px = 3.50f;  // micro facet offset amplitude (px)

    // Diffusion sampling (number of taps & radius for the multi-sample blur-in-place)
    int   diffusion_taps    = 9;   // 6, 9, or 12 are fine
    float diffusion_radius  = 2.8f; // px around the refracted UV

    // Chromatic aberration strength (subtle)
    float chroma_strength   = 0.70f;

    // Distortion mix with original background (no tint/fill involved)
    float mix_normal   = 0.50f;   // idle
    float mix_hover    = 0.70f;   // hover (more distortion)
    float mix_pressed  = 0.35f;   // pressed (flatter)

    // Fresnel-style rim (increases distortion mix near the edge; does NOT draw a border)
    float fresnel_power     = 2.20f;
    float fresnel_intensity = 0.60f; // amount of extra mix near rim

    // Optional blur buffer (only for luminance-driven flares if you decide to add later)
    int   blur_px         = 0;
    int   blur_px_hover   = 0;
    int   blur_px_pressed = 0;

    // Text
    SDL_Color text_color  = SDL_Color{245,245,245,255}; // muted white
    SDL_Color text_stroke = SDL_Color{0,0,0,128};       // soft shadow/stroke

    // Legacy / compatibility fields (ignored by the glass pass; retained so code compiles)
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
    SDL_Color highlight_glow_color{255,255,255,200};
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

    // Default preset for hammered/pebbled glass.
    static const GlassButtonStyle& default_glass_style();

    // Enable the hammered-glass pipeline for this button (use for Main/Pause menus).
    void enable_glass_style(bool enabled);
    void set_glass_style(const GlassButtonStyle& style);

private:
    // Legacy/decorative (not used by glass pass)
    void draw_deco(SDL_Renderer* r, const SDL_Rect& rect, bool hovered) const;

    // Hammered-glass: replaces the region with a rough refraction of the background
    void draw_glass(SDL_Renderer* renderer, const SDL_Rect& rect) const;

    // Label on top of the glass
    void draw_glass_text(SDL_Renderer* renderer, const SDL_Rect& rect) const;

private:
    SDL_Rect        rect_{0,0,520,64};
    std::string     label_;
    bool            hovered_ = false;
    bool            pressed_ = false;
    const ButtonStyle* style_ = nullptr;

    bool            glass_enabled_ = false;
    GlassButtonStyle glass_style_{};

    // optional telemetry
    mutable float   glass_luminance_ = 0.0f;
    mutable bool    glass_has_luminance_ = false;
};
