
#include "main_menu.hpp"

#include <SDL_image.h>
#include <SDL_ttf.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

namespace fs = std::filesystem;



MainMenu::MainMenu(SDL_Renderer* renderer, int screen_w, int screen_h)
    : renderer_(renderer), screen_w_(screen_w), screen_h_(screen_h)
{
    if (TTF_WasInit() == 0 && TTF_Init() < 0) {
        std::cerr << "TTF_Init failed: " << TTF_GetError() << "\n";
    }
    const fs::path bg_folder = "./MISC_CONTENT/backgrounds";
    if (fs::exists(bg_folder) && fs::is_directory(bg_folder)) {
        const fs::path first = firstImageIn(bg_folder);
        if (!first.empty()) background_tex_ = loadTexture(first);
    }
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
    const int btn_w = Button::width();
    const int btn_h = Button::height();
    const int gap   = 18;
    int y = (screen_h_ / 2) - 140;
    const int x = (screen_w_ - btn_w) / 2;
    try {
        if (fs::exists("MAPS") && fs::is_directory("MAPS")) {
            for (const auto& e : fs::directory_iterator("MAPS")) {
                if (!e.is_directory()) continue;
                const std::string label = e.path().filename().string();
                Button b = Button::get_main_button(label);
                b.set_rect(SDL_Rect{ x, y, btn_w, btn_h });
                buttons_.push_back(std::move(b));
                y += btn_h + gap;
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "[MainMenu] MAPS scan failed: " << ex.what() << "\n";
    }
    Button quit = Button::get_exit_button("QUIT GAME");
    quit.set_rect(SDL_Rect{ x, y + 12, btn_w, btn_h });
    buttons_.push_back(std::move(quit));
}

std::string MainMenu::handle_event(const SDL_Event& e) {
    for (auto& b : buttons_) {
        if (b.handle_event(e)) {
            const std::string lbl = b.text();
            if (lbl == "QUIT GAME") return "QUIT";
            return "MAPS/" + lbl;
        }
    }
    return "";
}

void MainMenu::render() {
    
    if (background_tex_) {
        SDL_Rect dst = coverDst(background_tex_);
        SDL_RenderCopy(renderer_, background_tex_, nullptr, &dst);
    } else {
        SDL_Color night = Styles::Night();
        SDL_SetRenderDrawColor(renderer_, night.r, night.g, night.b, night.a);
        SDL_RenderClear(renderer_);
    }
    drawVignette(120);
    
    const std::string title = "DEPARTED AFFAIRS & CO.";
    SDL_Rect trect{ 0, 60, screen_w_, 80 };
    blitTextCentered(renderer_, Styles::LabelTitle(), title, trect, true, SDL_Color{0,0,0,0});
    
    for (auto& b : buttons_) {
        b.render(renderer_);
    }
}

void MainMenu::showLoadingScreen() {
    SDL_SetRenderTarget(renderer_, nullptr);
    
    SDL_Texture* bg = background_tex_;
    bool temp_bg = false;
    if (!bg) {
        const fs::path bg_folder = "./MISC_CONTENT/backgrounds";
        const fs::path first = firstImageIn(bg_folder);
        if (!first.empty()) {
            bg = loadTexture(first);
            temp_bg = (bg != nullptr);
        }
    }
    
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    if (bg) {
        SDL_Rect bgdst = coverDst(bg);
        SDL_RenderCopy(renderer_, bg, nullptr, &bgdst);
    }
    drawVignette(110);
    
    std::vector<fs::path> folders;
    if (fs::exists("loading") && fs::is_directory("loading")) {
        for (const auto& e : fs::directory_iterator("loading")) {
            if (e.is_directory()) folders.push_back(e.path());
        }
    }
    SDL_Texture* tarot = nullptr;
    std::string msg;
    if (!folders.empty()) {
        std::mt19937 rng{std::random_device{}()};
        const fs::path folder = folders[ std::uniform_int_distribution<size_t>(0, folders.size()-1)(rng) ];
        const fs::path img = firstImageIn(folder);
        if (!img.empty()) tarot = loadTexture(img);
        msg = pickRandomLine(folder / "messages.csv");
    }
    
    const std::string loading = "LOADING...";
    SDL_Point tsize = measureText(Styles::LabelTitle(), loading);
    const int title_x = (screen_w_ - tsize.x) / 2;
    const int title_y = std::max(0, (screen_h_ / 2) - screen_h_ / 6 - tsize.y - 24);
    blitText(renderer_, Styles::LabelTitle(), loading, title_x, title_y, true, SDL_Color{0,0,0,0});
    
    if (tarot) {
        SDL_Rect dst = fitCenter(tarot, screen_w_/3, screen_h_/3, screen_w_/2, screen_h_/2);
        SDL_RenderCopy(renderer_, tarot, nullptr, &dst);
        SDL_DestroyTexture(tarot);
        tarot = nullptr;
    }
    
    if (!msg.empty()) {
        const int pad = 24;
        const int mw  = screen_w_/3;
        const int mx  = (screen_w_ - mw)/2;
        const int my  = (screen_h_/2) + screen_h_/6 + pad;
        const int mh  = std::max(0, screen_h_ - my - pad);
        SDL_Rect mrect{ mx, my, mw, mh };
        const LabelStyle& L = Styles::LabelSmallSecondary();
        TTF_Font* f = L.open_font();
        if (f) {
            int space_w=0, line_h=0;
            TTF_SizeText(f, " ", &space_w, &line_h);
            TTF_CloseFont(f);
            std::istringstream iss(msg);
            std::string word, line;
            int y = mrect.y;
            while (iss >> word) {
                std::string test = line.empty()? word : line + " " + word;
                SDL_Point sz = measureText(L, test);
                if (sz.x > mrect.w && !line.empty()) {
                    blitText(renderer_, L, line, mrect.x, y, false, SDL_Color{0,0,0,0});
                    y += line_h;
                    line = word;
                } else {
                    line = std::move(test);
                }
                if (y >= mrect.y + mrect.h) break;
            }
            if (!line.empty() && y < mrect.y + mrect.h) {
                blitText(renderer_, L, line, mrect.x, y, false, SDL_Color{0,0,0,0});
            }
        }
    }
    SDL_RenderPresent(renderer_);
    SDL_PumpEvents();
    if (temp_bg && bg) SDL_DestroyTexture(bg);
}



SDL_Texture* MainMenu::loadTexture(const std::string& abs_utf8_path) {
    SDL_Texture* t = IMG_LoadTexture(renderer_, abs_utf8_path.c_str());
    if (!t) {
        std::cerr << "[MainMenu] IMG_LoadTexture failed: " << abs_utf8_path << " | " << IMG_GetError() << "\n";
    }
    return t;
}

SDL_Texture* MainMenu::loadTexture(const fs::path& p) {
    if (p.empty()) return nullptr;
    return loadTexture(fs::absolute(p).u8string());
}

std::filesystem::path MainMenu::firstImageIn(const fs::path& folder) const {
    if (!fs::exists(folder) || !fs::is_directory(folder)) return {};
    for (const auto& e : fs::directory_iterator(folder)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        for (auto& c : ext) c = char(::tolower(c));
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") return e.path();
    }
    return {};
}

SDL_Rect MainMenu::coverDst(SDL_Texture* tex) const {
    if (!tex) return SDL_Rect{0,0,screen_w_,screen_h_};
    int tw=0, th=0;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    if (tw<=0 || th<=0) return SDL_Rect{0,0,screen_w_,screen_h_};
    const double ar = double(tw)/double(th);
    int w = screen_w_;
    int h = int(w / ar);
    if (h < screen_h_) {
        h = screen_h_;
        w = int(h * ar);
    }
    return SDL_Rect{ (screen_w_ - w)/2, (screen_h_ - h)/2, w, h };
}

SDL_Rect MainMenu::fitCenter(SDL_Texture* tex, int max_w, int max_h, int cx, int cy) const {
    if (!tex) return SDL_Rect{ cx - max_w/2, cy - max_h/2, max_w, max_h };
    int tw=0, th=0;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    if (tw<=0 || th<=0) return SDL_Rect{ cx - max_w/2, cy - max_h/2, max_w, max_h };
    const double ar = double(tw)/double(th);
    int w = max_w;
    int h = int(w / ar);
    if (h > max_h) {
        h = max_h;
        w = int(h * ar);
    }
    return SDL_Rect{ cx - w/2, cy - h/2, w, h };
}

SDL_Point MainMenu::measureText(const LabelStyle& style, const std::string& s) const {
    SDL_Point sz{0,0};
    if (s.empty()) return sz;
    TTF_Font* f = style.open_font();
    if (!f) return sz;
    TTF_SizeText(f, s.c_str(), &sz.x, &sz.y);
    TTF_CloseFont(f);
    return sz;
}

void MainMenu::blitText(SDL_Renderer* r,
                        const LabelStyle& style,
                        const std::string& s,
                        int x, int y,
                        bool shadow,
                        SDL_Color override_col) const
{
    if (s.empty()) return;
    TTF_Font* f = style.open_font();
    if (!f) return;
    const SDL_Color coal = Styles::Coal();
    const SDL_Color col  = override_col.a ? override_col : style.color;
    SDL_Surface* surf_text = TTF_RenderText_Blended(f, s.c_str(), col);
    SDL_Surface* surf_shadow = shadow ? TTF_RenderText_Blended(f, s.c_str(), coal) : nullptr;
    if (surf_text) {
        SDL_Texture* tex_text = SDL_CreateTextureFromSurface(r, surf_text);
        if (surf_shadow) {
            SDL_Texture* tex_shadow = SDL_CreateTextureFromSurface(r, surf_shadow);
            if (tex_shadow) {
                SDL_Rect dsts { x+2, y+2, surf_shadow->w, surf_shadow->h };
                SDL_SetTextureAlphaMod(tex_shadow, 130);
                SDL_RenderCopy(r, tex_shadow, nullptr, &dsts);
                SDL_DestroyTexture(tex_shadow);
            }
        }
        if (tex_text) {
            SDL_Rect dst { x, y, surf_text->w, surf_text->h };
            SDL_RenderCopy(r, tex_text, nullptr, &dst);
            SDL_DestroyTexture(tex_text);
        }
    }
    if (surf_shadow) SDL_FreeSurface(surf_shadow);
    if (surf_text)   SDL_FreeSurface(surf_text);
    TTF_CloseFont(f);
}

void MainMenu::blitTextCentered(SDL_Renderer* r,
                                const LabelStyle& style,
                                const std::string& s,
                                const SDL_Rect& rect,
                                bool shadow,
                                SDL_Color override_col) const
{
    SDL_Point sz = measureText(style, s);
    const int x = rect.x + (rect.w - sz.x)/2;
    const int y = rect.y + (rect.h - sz.y)/2;
    blitText(r, style, s, x, y, shadow, override_col);
}

std::string MainMenu::pickRandomLine(const fs::path& csv_path) const {
    std::ifstream in(csv_path);
    if (!in.is_open()) return {};
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.size()>=3 &&
            (unsigned char)line[0]==0xEF && (unsigned char)line[1]==0xBB && (unsigned char)line[2]==0xBF) {
            line.erase(0,3);
        }
        if (!line.empty() && line.back()=='\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    if (lines.empty()) return {};
    std::mt19937 rng{std::random_device{}()};
    return lines[ std::uniform_int_distribution<size_t>(0, lines.size()-1)(rng) ];
}

void MainMenu::drawVignette(Uint8 alpha) const {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, alpha);
    SDL_Rect v{0,0,screen_w_,screen_h_};
    SDL_RenderFillRect(renderer_, &v);
}
