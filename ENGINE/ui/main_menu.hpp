// === File: main_menu.hpp ===
#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>

class MainMenu {
public:
    MainMenu(SDL_Renderer* renderer, int screen_w, int screen_h);
    ~MainMenu();

    // Call this once per SDL_Event
    std::string handle_event(const SDL_Event& e);

    // Draw menu
    void render();

private:
    struct Button {
        SDL_Rect rect;
        std::string label;
        bool hovered = false;
    };

    SDL_Renderer* renderer_;
    int screen_w_;
    int screen_h_;
    TTF_Font* font_;
    std::vector<Button> buttons_;

    void buildButtons();
    void drawTextCentered(const std::string& text, const SDL_Rect& rect, SDL_Color color);
};
