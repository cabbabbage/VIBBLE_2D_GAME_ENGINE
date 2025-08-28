// === File: main_menu.hpp ===
#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>
#include "text_style.hpp"   // for TextStyle and TextStyles

class MainMenu {
public:
    MainMenu(SDL_Renderer* renderer, int screen_w, int screen_h);
    ~MainMenu();

    std::string handle_event(const SDL_Event& e);
    void render();

private:
    struct Button {
        SDL_Rect rect;
        std::string label;
        bool hovered;
    };

    SDL_Renderer* renderer_;
    int screen_w_;
    int screen_h_;
    std::vector<Button> buttons_;

    void buildButtons();
    void drawTextCentered(const std::string& text,
                          const SDL_Rect& rect,
                          const TextStyle& style,
                          bool hovered);
};
