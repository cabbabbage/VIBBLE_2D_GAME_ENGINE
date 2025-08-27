// === File: main_menu.cpp ===
#include "main_menu.hpp"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

MainMenu::MainMenu(SDL_Renderer* renderer, int screen_w, int screen_h)
    : renderer_(renderer), screen_w_(screen_w), screen_h_(screen_h), font_(nullptr)
{
    if (TTF_WasInit() == 0) {
        if (TTF_Init() < 0) {
            std::cerr << "TTF_Init failed: " << TTF_GetError() << "\n";
        }
    }

    font_ = TTF_OpenFont("C:/Windows/Fonts/consola.ttf", 28);
    if (!font_) {
        std::cerr << "Failed to load font: " << TTF_GetError() << "\n";
    }

    buildButtons();
}

MainMenu::~MainMenu() {
    if (font_) {
        TTF_CloseFont(font_);
    }
}

void MainMenu::buildButtons() {
    buttons_.clear();

    const int btn_w = 400;
    const int btn_h = 50;
    int start_y = 150;
    const int gap = 20;

    try {
        for (const auto& entry : fs::directory_iterator("MAPS")) {
            if (entry.is_directory()) {
                SDL_Rect r{ (screen_w_ - btn_w) / 2, start_y, btn_w, btn_h };
                start_y += btn_h + gap;
                buttons_.push_back({r, entry.path().filename().string(), false});
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[MainMenu] Failed to read MAPS folder: " << e.what() << "\n";
    }
}

std::string MainMenu::handle_event(const SDL_Event& e) {
    if (e.type == SDL_MOUSEMOTION) {
        SDL_Point p{ e.motion.x, e.motion.y };
        for (auto& b : buttons_) {
            b.hovered = SDL_PointInRect(&p, &b.rect);
        }
    }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        for (auto& b : buttons_) {
            if (SDL_PointInRect(&p, &b.rect)) {
                std::cout << "[MainMenu] Folder clicked: " << b.label << "\n";
                return "MAPS/" + b.label;
            }
        }
    }
    return "";
}

void MainMenu::render() {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 220);
    SDL_Rect bg{0, 0, screen_w_, screen_h_};
    SDL_RenderFillRect(renderer_, &bg);

    for (auto& b : buttons_) {
        // body
        SDL_SetRenderDrawColor(renderer_, b.hovered ? 70 : 40, b.hovered ? 70 : 40, b.hovered ? 70 : 40, 255);
        SDL_RenderFillRect(renderer_, &b.rect);
        // border
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
        SDL_RenderDrawRect(renderer_, &b.rect);
        // label
        drawTextCentered(b.label, b.rect, b.hovered ? SDL_Color{255,255,255,255}
                                                    : SDL_Color{200,200,200,255});
    }
}

void MainMenu::drawTextCentered(const std::string& text, const SDL_Rect& rect, SDL_Color color) {
    if (!font_) return;
    SDL_Surface* surf = TTF_RenderText_Blended(font_, text.c_str(), color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    int tw = surf->w, th = surf->h;
    SDL_FreeSurface(surf);
    if (!tex) return;

    SDL_Rect dst {
        rect.x + (rect.w - tw) / 2,
        rect.y + (rect.h - th) / 2,
        tw, th
    };
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}
