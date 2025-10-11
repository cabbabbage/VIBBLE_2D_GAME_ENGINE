#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <array>
#include <string>

class Input;
class Assets;

class DisplayStats {
public:
    explicit DisplayStats(SDL_Renderer* renderer);
    ~DisplayStats();

    DisplayStats(const DisplayStats&) = delete;
    DisplayStats& operator=(const DisplayStats&) = delete;

    void handle_input(const Input& input);
    void update(const Assets& assets);
    void render(SDL_Renderer* renderer);

private:
    struct Row {
        std::string line;
        bool found = false;
    };

    void ensure_font();

    TTF_Font* font_ = nullptr;
    bool visible_ = false;
    std::array<Row, 2> rows_{};
    const std::array<std::string, 2> tracked_names_{"Vibble", "Tree"};
    SDL_Color text_color_{255, 255, 255, 255};
    SDL_Color bg_color_{0, 0, 0, 180};
    SDL_Color border_color_{255, 255, 255, 200};
    int padding_ = 8;
    int line_spacing_ = 4;
    int margin_ = 10;
};

