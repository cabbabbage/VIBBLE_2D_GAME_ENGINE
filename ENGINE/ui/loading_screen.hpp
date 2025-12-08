#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>
#include <filesystem>

class LoadingScreen {

	public:
    LoadingScreen(SDL_Renderer* renderer, int screen_w, int screen_h);
    ~LoadingScreen();
    void init();
    void draw_frame();
    void set_status(std::string status);

        private:
    SDL_Renderer* renderer_;
    int screen_w_;
    int screen_h_;
    std::filesystem::path selected_image_path_;
    std::string message_;
    std::string status_text_;
    SDL_Texture* current_texture_ = nullptr;
    std::filesystem::path current_texture_path_;
    std::filesystem::path project_root() const;
    std::filesystem::path loading_content_root() const;
    std::vector<std::filesystem::path> list_images_in(const std::filesystem::path& dir, bool recursive) const;
    std::string pick_random_message_from_csv(const std::filesystem::path& csv_path);
    void draw_text(TTF_Font* font, const std::string& txt, int x, int y, SDL_Color col);
    void render_justified_text(TTF_Font* font, const std::string& text, const SDL_Rect& rect, SDL_Color col);
    void render_scaled_center(SDL_Texture* tex, int target_w, int target_h, int cx, int cy, double angle = 0.0);

    double rotation_angle_ = 0.0;
    Uint32 last_frame_time_ = 0;
};
