#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>
#include <filesystem>

#include "styles.hpp"
#include "button.hpp"

class MainMenu {
public:
    MainMenu(SDL_Renderer* renderer, int screen_w, int screen_h);
    ~MainMenu();
    void buildButtons();
    std::string handle_event(const SDL_Event& e);
    void render();
    void showLoadingScreen();
private:
    SDL_Texture* loadTexture(const std::string& abs_utf8_path);
    SDL_Texture* loadTexture(const std::filesystem::path& p);
    std::filesystem::path firstImageIn(const std::filesystem::path& folder) const;
    SDL_Rect coverDst(SDL_Texture* tex) const;
    SDL_Rect fitCenter(SDL_Texture* tex, int max_w, int max_h, int cx, int cy) const;
    SDL_Point measureText(const LabelStyle& style, const std::string& s) const;
    void blitText(SDL_Renderer* r, const LabelStyle& style, const std::string& s, int x, int y, bool shadow, SDL_Color override_col) const;
    void blitTextCentered(SDL_Renderer* r, const LabelStyle& style, const std::string& s, const SDL_Rect& rect, bool shadow, SDL_Color override_col) const;
    std::string pickRandomLine(const std::filesystem::path& csv_path) const;
    void drawVignette(Uint8 alpha) const;
private:
    SDL_Renderer* renderer_ = nullptr;
    int screen_w_ = 0;
    int screen_h_ = 0;
    SDL_Texture* background_tex_ = nullptr;
    std::vector<Button> buttons_;
};
