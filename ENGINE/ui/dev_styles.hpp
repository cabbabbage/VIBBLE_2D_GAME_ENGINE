#pragma once

#include <SDL.h>
#include "ui/styles.hpp"
#include "utils/text_style.hpp"





struct SliderStyle;

class DevStyles {
public:
    
    static const ButtonStyle& PrimaryButton();
    static const ButtonStyle& SecondaryButton();
    
    static const SliderStyle& DefaultSlider();
    
    static const SDL_Color& PanelBG();
    static const SDL_Color& Outline();
    static const SDL_Color& Accent();
};

