// === File: ui/styles.hpp ===
#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>

// ---------- Label (text) styling ----------
struct LabelStyle {
    std::string font_path;
    int         font_size;
    SDL_Color   color;

    TTF_Font* open_font() const {
        return TTF_OpenFont(font_path.c_str(), font_size);
    }
};

// ---------- Button (deco) styling ----------
struct ButtonStyle {
    // Text to render on the button
    LabelStyle label;

    // Colors for deco button rendering (match main_menu look)
    SDL_Color  fill_base;     // lower half/base fill (slate)
    SDL_Color  fill_top;      // top-half overlay (coal)
    SDL_Color  outline;       // gold outline (normal)
    SDL_Color  outline_dim;   // dimmed gold outline (non-hover)
    SDL_Color  accent;        // teal inner lines
    SDL_Color  glow;          // hover glow (gold, low alpha)

    // Text colors (normal vs hover)
    SDL_Color  text_normal;
    SDL_Color  text_hover;
};

// ---------- Global styles/palette access ----------
class Styles {
public:
    // Palette (named colors)
    static const SDL_Color& Gold();        // primary brand gold
    static const SDL_Color& GoldDim();     // dimmed gold (frame when idle)
    static const SDL_Color& Teal();        // accent lines
    static const SDL_Color& Slate();       // button base fill
    static const SDL_Color& Coal();        // button top-half overlay
    static const SDL_Color& Night();       // screen background
    static const SDL_Color& Fog();         // neutral light text
    static const SDL_Color& Mist();        // muted secondary text
    static const SDL_Color& Ivory();       // cool light for main label

    // Labels (rename of previous text styles)
    static const LabelStyle& LabelTitle();
    static const LabelStyle& LabelMain();
    static const LabelStyle& LabelSecondary();
    static const LabelStyle& LabelSmallMain();
    static const LabelStyle& LabelSmallSecondary();
    static const LabelStyle& LabelExit();          // for EXIT buttons

    // Buttons
    static const ButtonStyle& MainDecoButton();    // standard main button
    static const ButtonStyle& ExitDecoButton();    // exit variant
};
