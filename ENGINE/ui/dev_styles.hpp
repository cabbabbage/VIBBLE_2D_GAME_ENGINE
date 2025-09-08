#pragma once

#include <SDL.h>
#include "ui/styles.hpp"
#include "utils/text_style.hpp"

// Lightweight, professional-looking styles for development/editor UIs.
// Intentionally flatter and more neutral than the in-game Styles.

// Forward declare SliderStyle for use by Slider (defined in slider.hpp)
struct SliderStyle;

class DevStyles {
public:
    // Buttons
    static const ButtonStyle& PrimaryButton();
    static const ButtonStyle& SecondaryButton();
    // Sliders
    static const SliderStyle& DefaultSlider();
    // Optional neutrals for editor panels
    static const SDL_Color& PanelBG();
    static const SDL_Color& Outline();
    static const SDL_Color& Accent();
};

