// === File: menu_ui.cpp ===
#include "menu_ui.hpp"
#include <iostream>

MenuUI::MenuUI(SDL_Renderer* renderer, int screen_w, int screen_h, bool dev_mode)
    : renderer_(renderer), screen_w_(screen_w), screen_h_(screen_h), dev_mode_(dev_mode)
{
    if (TTF_WasInit() == 0) {
        if (TTF_Init() < 0) {
            std::cerr << "TTF_Init failed: " << TTF_GetError() << "\n";
        }
    }

    // choose a common Windows font
    font_ = TTF_OpenFont("C:/Windows/Fonts/consola.ttf", 24);
    if (!font_) {
        std::cerr << "Failed to load font: " << TTF_GetError() << "\n";
    }

    rebuildButtons();
}

MenuUI::~MenuUI() {
    if (font_) {
        TTF_CloseFont(font_);
    }
}

void MenuUI::setDevMode(bool enabled) {
    if (dev_mode_ == enabled) return;
    dev_mode_ = enabled;
    rebuildButtons();
}

void MenuUI::rebuildButtons() {
    buttons_.clear();

    const int btn_w = 340;
    const int btn_h = 44;
    int start_y = 150;
    const int gap = 16;

    auto addButton = [&](const std::string& label, MenuAction action) {
        SDL_Rect r{ (screen_w_ - btn_w) / 2, start_y, btn_w, btn_h };
        start_y += btn_h + gap;
        buttons_.push_back({r, label, false, action});
    };

    addButton("Exit Game",            MenuAction::EXIT);
    addButton("Restart Run",          MenuAction::RESTART);
    addButton("Settings",             MenuAction::SETTINGS);
    addButton(dev_mode_ ? "Switch to Player Mode" : "Switch to Dev Mode",
              MenuAction::DEV_MODE_TOGGLE);
    addButton("Save Current Room",    MenuAction::SAVE_ROOM);
}

void MenuUI::handle_event(const SDL_Event& e) {
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
                last_action_ = b.action;
                std::cout << "[MenuUI] Button clicked: " << b.label << "\n";
            }
        }
    }
}


void MenuUI::update(bool dev_mode) {
    if (dev_mode_ != dev_mode) {
        dev_mode_ = dev_mode;
        rebuildButtons();  
    }
}


void MenuUI::render() {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 180);
    SDL_Rect bg{0, 0, screen_w_, screen_h_};
    SDL_RenderFillRect(renderer_, &bg);

    for (auto& b : buttons_) {
        // body
        SDL_SetRenderDrawColor(renderer_, b.hovered ? 60 : 40, b.hovered ? 60 : 40, b.hovered ? 60 : 40, 255);
        SDL_RenderFillRect(renderer_, &b.rect);
        // border
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
        SDL_RenderDrawRect(renderer_, &b.rect);
        // label
        drawTextCentered(b.label, b.rect, b.hovered ? SDL_Color{255,255,255,255}
                                                    : SDL_Color{220,220,220,255});
    }
}

void MenuUI::drawTextCentered(const std::string& text, const SDL_Rect& rect, SDL_Color color) {
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
