#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include "ui/font_paths.hpp"

struct TextStyle {
    std::string font_path;
    int font_size;
    SDL_Color color;
    TTF_Font* open_font() const {
        return TTF_OpenFont(font_path.c_str(), font_size);
    }
};

class TextStyles {
public:
    static const TextStyle& Title()           { return title_; }
    static const TextStyle& MediumMain()      { return medium_main_; }
    static const TextStyle& MediumSecondary() { return medium_secondary_; }
    static const TextStyle& SmallMain()       { return small_main_; }
    static const TextStyle& SmallSecondary()  { return small_secondary_; }

private:
    static inline TextStyle title_ = {
        ui_fonts::decorative_bold(),
        74,
        SDL_Color{250, 195, 73, 255}
};
    static inline TextStyle medium_main_ = {
        ui_fonts::decorative_bold(),
        32,
        SDL_Color{200, 200, 255, 200}
};
    static inline TextStyle medium_secondary_ = {
        ui_fonts::serif_regular(),
        30,
        SDL_Color{250, 195, 73, 255}
};
    static inline TextStyle small_main_ = {
        ui_fonts::serif_regular(),
        30,
        SDL_Color{220, 220, 200, 255}
};
    static inline TextStyle small_secondary_ = {
        ui_fonts::serif_italic(),
        30,
        SDL_Color{140, 160, 160, 255}
};
};
