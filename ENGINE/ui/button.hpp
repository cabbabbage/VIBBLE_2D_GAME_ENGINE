#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include "styles.hpp"

// Pure-glass style: the button only distorts what's behind it.
// No solid fills or outlines are drawn by the glass renderer.
struct GlassButtonStyle {
    // Shape
    int       radius = 20;          // rounded corner radius (px)

    // Refraction (convex lens)
    float     refraction_strength = 0.040f;  // base warp magnitude (scaled by size)
    float     refraction_falloff  = 0.0f;    // kept for compat (not used strongly)

    // Chromatic micro-aberration (gives "thick" glass feel)
    float     chroma_strength     = 0.65f;   // radial RGB split amount

    // Fresnel rim (edge brightening, no outline)
    float     fresnel_power       = 2.20f;   // curve for edge emphasis
    float     fresnel_intensity   = 0.55f;   // how bright the rim can get

    // Background blur buffer (used only to read luminance for flares/highlights)
    int       blur_px             = 22;
    int       blur_px_hover       = 30;
    int       blur_px_pressed     = 18;

    // Highlights & flares
    float     highlight_intensity = 0.22f;   // specular pop from local luminance
    float     glow_intensity      = 1.40f;   // broad highlight/glow
    float     flare_gain          = 1.80f;   // directional flare boost
    float     prism_gain          = 1.10f;   // small extra RGB spread on bright lines

    // Text
    SDL_Color text_color          = SDL_Color{245,245,245,255}; // muted white
    SDL_Color text_stroke         = SDL_Color{0,0,0,128};       // soft stroke for legibility

    // Legacy/compat fields (ignored by the glass pass; safe defaults)
    SDL_Color border_light{0,0,0,0};
    SDL_Color border_dark{0,0,0,0};
    SDL_Color inner_shadow{0,0,0,0};
    SDL_Color outer_shadow{0,0,0,0};
    SDL_Color tint{0,0,0,0};           // keep at 0 alpha for clear glass
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

    // Controls the extra blurred capture (used only to read background luminance for flares).
    static void set_glass_blur_enabled(bool enabled);

    // Default preset tuned for “bubbly, thick” glass with no visible fill/outline.
    static const GlassButtonStyle& default_glass_style();

    // Enable the glass pipeline for this button (Main/Pause menus). Dev-mode UI should leave this off.
    void enable_glass_style(bool enabled);
    void set_glass_style(const GlassButtonStyle& style);

private:
    // Fallback decorated button (non-glass path)
    void draw_deco(SDL_Renderer* r, const SDL_Rect& rect, bool hovered) const;

    // Glass rendering: replaces the button bounds with a refracted copy of the scene beneath it.
    void draw_glass(SDL_Renderer* renderer, const SDL_Rect& rect) const;

    // Text on top of the glass
    void draw_glass_text(SDL_Renderer* renderer, const SDL_Rect& rect) const;

private:
    SDL_Rect        rect_{0,0,520,64};
    std::string     label_;
    bool            hovered_ = false;
    bool            pressed_ = false;
    const ButtonStyle* style_ = nullptr;

    bool            glass_enabled_ = false;
    GlassButtonStyle glass_style_{};

    // Optional telemetry
    mutable float   glass_luminance_ = 0.0f;
    mutable bool    glass_has_luminance_ = false;

    static bool     glass_blur_enabled_;
};
