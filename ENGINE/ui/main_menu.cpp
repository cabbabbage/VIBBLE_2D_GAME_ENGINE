// === File: ui/main_menu.cpp ===
#include "main_menu.hpp"
#include "text_style.hpp"
#include <SDL_image.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

static inline SDL_Color make_color(Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255) {
    return SDL_Color{r,g,b,a};
}

MainMenu::MainMenu(SDL_Renderer* renderer, int screen_w, int screen_h)
    : renderer_(renderer), screen_w_(screen_w), screen_h_(screen_h)
{
    if (TTF_WasInit() == 0) {
        if (TTF_Init() < 0) {
            std::cerr << "TTF_Init failed: " << TTF_GetError() << "\n";
        }
    }
    loadBackgroundFromMisc();
    buildButtons();
}

MainMenu::~MainMenu() {
    if (background_tex_) {
        SDL_DestroyTexture(background_tex_);
        background_tex_ = nullptr;
    }
}

void MainMenu::buildButtons() {
    buttons_.clear();

    const int btn_w = 520;
    const int btn_h = 64;
    int start_y = (screen_h_ / 2) - 140;
    const int gap = 18;

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

    SDL_Rect quit_rect{ (screen_w_ - btn_w) / 2, start_y + 12, btn_w, btn_h };
    buttons_.push_back({quit_rect, "QUIT GAME", false});
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
                std::cout << "[MainMenu] Button clicked: " << b.label << "\n";
                if (b.label == "QUIT GAME") {
                    return "QUIT";
                }
                return "MAPS/" + b.label;
            }
        }
    }
    return "";
}

void MainMenu::render() {
    renderBackground();

    // Vignette overlay for mood
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 120);
    SDL_Rect vignette{0, 0, screen_w_, screen_h_};
    SDL_RenderFillRect(renderer_, &vignette);

    // Title header
    const std::string title = "DEPARTED AFFAIRS & CO.";
    TTF_Font* title_font = TextStyles::Title().open_font();
    if (title_font) {
        SDL_Color gold = TextStyles::Title().color;
        SDL_Surface* surf = TTF_RenderText_Blended(title_font, title.c_str(), gold);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
            int tw = surf->w, th = surf->h;
            SDL_FreeSurface(surf);
            if (tex) {
                SDL_Rect dst{ (screen_w_ - tw)/2, 60, tw, th };
                SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
        }
        TTF_CloseFont(title_font);
    }

    // Buttons
    for (auto& b : buttons_) {
        drawDecoButton(b.rect, b.hovered);
        drawTextCentered(b.label, b.rect, TextStyles::MediumMain(), b.hovered);
    }
}

void MainMenu::drawTextCentered(const std::string& text,
                                const SDL_Rect& rect,
                                const TextStyle& style,
                                bool hovered) {
    TTF_Font* font = style.open_font();
    if (!font) return;

    SDL_Color base = style.color;              // gold
    SDL_Color col  = hovered ? make_color(
                        Uint8(std::min(255, int(base.r + 20))),
                        Uint8(std::min(255, int(base.g + 20))),
                        Uint8(std::min(255, int(base.b + 10))),
                        255)
                              : base;

    // Subtle drop shadow for readability
    SDL_Surface* surf_shadow = TTF_RenderText_Blended(font, text.c_str(), make_color(0,0,0,255));
    SDL_Surface* surf_text   = TTF_RenderText_Blended(font, text.c_str(), col);

    if (!surf_text) { if (surf_shadow) SDL_FreeSurface(surf_shadow); TTF_CloseFont(font); return; }

    SDL_Texture* tex_shadow = surf_shadow ? SDL_CreateTextureFromSurface(renderer_, surf_shadow) : nullptr;
    SDL_Texture* tex_text   = SDL_CreateTextureFromSurface(renderer_, surf_text);
    int tw = surf_text->w, th = surf_text->h;

    if (surf_shadow) SDL_FreeSurface(surf_shadow);
    SDL_FreeSurface(surf_text);

    SDL_Rect dst {
        rect.x + (rect.w - tw) / 2,
        rect.y + (rect.h - th) / 2,
        tw, th
    };

    if (tex_shadow) {
        SDL_Rect shadow_dst = dst; shadow_dst.x += 2; shadow_dst.y += 2;
        SDL_SetTextureAlphaMod(tex_shadow, 130);
        SDL_RenderCopy(renderer_, tex_shadow, nullptr, &shadow_dst);
        SDL_DestroyTexture(tex_shadow);
    }

    if (tex_text) {
        SDL_RenderCopy(renderer_, tex_text, nullptr, &dst);
        SDL_DestroyTexture(tex_text);
    }

    TTF_CloseFont(font);
}

void MainMenu::drawDecoButton(const SDL_Rect& r, bool hovered) {
    // Palette
    const SDL_Color coal     = make_color(12, 16, 18, 255);
    const SDL_Color slate    = make_color(28, 32, 36, 255);
    const SDL_Color teal     = make_color(40, 110, 120, 255);
    const SDL_Color gold     = make_color(250, 195, 73, 255);
    const SDL_Color gold_dim = make_color(180, 135, 40, 255);

    // Fill (subtle gradient via two fills)
    SDL_SetRenderDrawColor(renderer_, slate.r, slate.g, slate.b, 230);
    SDL_RenderFillRect(renderer_, &r);
    SDL_Rect topHalf{ r.x, r.y, r.w, r.h / 2 };
    SDL_SetRenderDrawColor(renderer_, coal.r, coal.g, coal.b, 200);
    SDL_RenderFillRect(renderer_, &topHalf);

    // Outer gold frame
    SDL_SetRenderDrawColor(renderer_, (hovered?gold:gold_dim).r,
                                      (hovered?gold:gold_dim).g,
                                      (hovered?gold:gold_dim).b, 255);
// Outer gold frame
SDL_Rect outer{ r.x, r.y, r.w, r.h };
SDL_RenderDrawRect(renderer_, &outer);

SDL_Rect inner{ r.x+1, r.y+1, r.w-2, r.h-2 };
SDL_RenderDrawRect(renderer_, &inner);


    // Inner teal deco lines
    SDL_SetRenderDrawColor(renderer_, teal.r, teal.g, teal.b, 255);
    // Horizontal trims
    SDL_RenderDrawLine(renderer_, r.x + 10, r.y + 10, r.x + r.w - 10, r.y + 10);
    SDL_RenderDrawLine(renderer_, r.x + 10, r.y + r.h - 11, r.x + r.w - 10, r.y + r.h - 11);
    // Corner accents
    for (int i = 0; i < 3; ++i) {
        SDL_RenderDrawLine(renderer_, r.x + 10 + i, r.y + 10, r.x + 10 + i, r.y + 20);
        SDL_RenderDrawLine(renderer_, r.x + r.w - 11 - i, r.y + 10, r.x + r.w - 11 - i, r.y + 20);
        SDL_RenderDrawLine(renderer_, r.x + 10 + i, r.y + r.h - 21, r.x + 10 + i, r.y + r.h - 11);
        SDL_RenderDrawLine(renderer_, r.x + r.w - 11 - i, r.y + r.h - 21, r.x + r.w - 11 - i, r.y + r.h - 11);
    }

    // Hover glow
    if (hovered) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
        SDL_SetRenderDrawColor(renderer_, gold.r, gold.g, gold.b, 45);
        SDL_Rect glow{ r.x - 6, r.y - 6, r.w + 12, r.h + 12 };
        SDL_RenderFillRect(renderer_, &glow);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    }
}

void MainMenu::renderBackground() {
    if (background_tex_) {
        int texW = 0, texH = 0;
        SDL_QueryTexture(background_tex_, nullptr, nullptr, &texW, &texH);

        // Cover screen preserving AR
        double ar = (texW > 0 && texH > 0) ? double(texW) / double(texH) : 1.0;
        int w = screen_w_, h = int(screen_w_ / ar);
        if (h < screen_h_) { h = screen_h_; w = int(screen_h_ * ar); }
        SDL_Rect dst{ (screen_w_ - w)/2, (screen_h_ - h)/2, w, h };

        SDL_RenderCopy(renderer_, background_tex_, nullptr, &dst);
    } else {
        // Fallback: solid night-sky
        SDL_SetRenderDrawColor(renderer_, 8, 12, 18, 255);
        SDL_RenderClear(renderer_);
    }
}

void MainMenu::loadBackgroundFromMisc() {
    // Look for a PNG/JPG in ./MISC_CONTENT
    fs::path folder = "./MISC_CONTENT/backgrounds";
    if (!fs::exists(folder) || !fs::is_directory(folder)) return;

    fs::path pick;
    for (const auto& p : fs::directory_iterator(folder)) {
        if (!p.is_regular_file()) continue;
        auto ext = p.path().extension().string();
        for (auto& c : ext) c = (char)tolower(c);
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
            pick = p.path();
            break;
        }
    }
    if (pick.empty()) return;

    const std::string path = fs::absolute(pick).u8string();
    background_tex_ = IMG_LoadTexture(renderer_, path.c_str());
    if (!background_tex_) {
        std::cerr << "[MainMenu] Failed to load background " << path
                  << " : " << IMG_GetError() << "\n";
    }
}
