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
    SDL_Color  fill_base;
    SDL_Color  fill_top;
    SDL_Color  outline;
    SDL_Color  outline_dim;
    SDL_Color  accent;
    SDL_Color  glow;
    // Text colors (normal vs hover)
    SDL_Color  text_normal;
    SDL_Color  text_hover;
};

// ---------- Global styles/palette access ----------
class Styles {
public:
    // Palette (named colors)
    static const SDL_Color& Gold();
    static const SDL_Color& GoldDim();
    static const SDL_Color& Teal();
    static const SDL_Color& Slate();
    static const SDL_Color& Coal();
    static const SDL_Color& Night();
    static const SDL_Color& Fog();
    static const SDL_Color& Mist();
    static const SDL_Color& Ivory();
    // Labels (rename of previous text styles)
    static const LabelStyle& LabelTitle();
    static const LabelStyle& LabelMain();
    static const LabelStyle& LabelSecondary();
    static const LabelStyle& LabelSmallMain();
    static const LabelStyle& LabelSmallSecondary();
    static const LabelStyle& LabelExit();
    // Buttons
    static const ButtonStyle& MainDecoButton();
    static const ButtonStyle& ExitDecoButton();
};
