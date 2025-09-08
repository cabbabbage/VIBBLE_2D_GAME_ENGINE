#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>
#include <filesystem>

class LoadingScreen {
public:
    LoadingScreen(SDL_Renderer* renderer, int screen_w, int screen_h);
    void init();
    void draw_frame();
private:
    SDL_Renderer* renderer_;
    int screen_w_;
    int screen_h_;
    std::vector<std::filesystem::path> images_;
    std::string message_;
    size_t current_index_ = 0;
    Uint32 last_switch_time_ = 0;
    std::filesystem::path pick_random_loading_folder();
    std::vector<std::filesystem::path> list_images_in(const std::filesystem::path& dir);
    std::string pick_random_message_from_csv(const std::filesystem::path& csv_path);
    void draw_text(TTF_Font* font, const std::string& txt, int x, int y, SDL_Color col);
    void render_justified_text(TTF_Font* font, const std::string& text, const SDL_Rect& rect, SDL_Color col);
    void render_scaled_center(SDL_Texture* tex, int target_w, int target_h, int cx, int cy);
};
