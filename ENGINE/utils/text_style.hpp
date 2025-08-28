#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>

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

    static const TextStyle& Title()              { return title_; }
    static const TextStyle& MediumMain()         { return medium_main_; }
    static const TextStyle& MediumSecondary()    { return medium_secondary_; }
    static const TextStyle& SmallMain()          { return small_main_; }
    static const TextStyle& SmallSecondary()     { return small_secondary_; }

private:
    static inline TextStyle title_ = {
        "C:/Windows/Fonts/consola.ttf", 48, {255,255,255,255}
    };
    static inline TextStyle medium_main_ = {
        "C:/Windows/Fonts/consola.ttf", 28, {220,220,220,255}
    };
    static inline TextStyle medium_secondary_ = {
        "C:/Windows/Fonts/consola.ttf", 28, {150,150,150,255}
    };
    static inline TextStyle small_main_ = {
        "C:/Windows/Fonts/consola.ttf", 20, {200,200,200,255}
    };
    static inline TextStyle small_secondary_ = {
        "C:/Windows/Fonts/consola.ttf", 20, {120,120,120,255}
    };
};
