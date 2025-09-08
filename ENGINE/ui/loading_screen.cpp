#include "loading_screen.hpp"
#include <SDL_image.h>
#include <fstream>
#include <sstream>
#include <random>
#include <iostream>
namespace fs = std::filesystem;

LoadingScreen::LoadingScreen(SDL_Renderer* renderer, int screen_w, int screen_h)
    : renderer_(renderer), screen_w_(screen_w), screen_h_(screen_h) {}

fs::path LoadingScreen::pick_random_loading_folder() {
    std::vector<fs::path> folders;
    if (!fs::exists("loading") || !fs::is_directory("loading")) return "";
    for (auto& p : fs::directory_iterator("loading")) {
        if (p.is_directory()) folders.push_back(p.path());
    }
    if (folders.empty()) return "";
    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, folders.size() - 1);
    return folders[dist(rng)];
}

std::vector<fs::path> LoadingScreen::list_images_in(const fs::path& dir) {
    std::vector<fs::path> out;
    if (!fs::exists(dir) || !fs::is_directory(dir)) return out;
    for (auto& p : fs::directory_iterator(dir)) {
        if (p.is_regular_file()) {
            auto ext = p.path().extension().string();
            for (auto& c : ext) c = (char)tolower(c);
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") out.push_back(p.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string LoadingScreen::pick_random_message_from_csv(const fs::path& csv_path) {
    std::vector<std::string> lines;
    std::ifstream in(csv_path);
    if (!in.is_open()) return "";
    std::string line;
    while (std::getline(in, line)) if (!line.empty()) lines.push_back(line);
    if (lines.empty()) return "";
    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, lines.size() - 1);
    return lines[dist(rng)];
}

void LoadingScreen::draw_text(TTF_Font* font, const std::string& txt, int x, int y, SDL_Color col) {
    SDL_Surface* surf = TTF_RenderText_Blended(font, txt.c_str(), col);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    int tw = surf->w, th = surf->h;
    SDL_FreeSurface(surf);
    if (!tex) return;
    SDL_Rect dst{ x, y, tw, th };
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}

void LoadingScreen::render_justified_text(TTF_Font* font, const std::string& text, const SDL_Rect& rect, SDL_Color col) {
    if (!font || text.empty()) return;
    std::istringstream iss(text);
    std::vector<std::string> words; std::string w;
    while (iss >> w) words.push_back(w);
    if (words.empty()) return;
    int space_w; int space_h; TTF_SizeText(font, " ", &space_w, &space_h);
    std::vector<std::vector<std::string>> lines;
    std::vector<std::string> cur;
    auto width_of = [&](const std::vector<std::string>& ws) {
        int wsum = 0;
        for (size_t i=0;i<ws.size();++i){
            int w=0,h=0; TTF_SizeText(font,ws[i].c_str(),&w,&h);
            wsum+=w; if(i+1<ws.size()) wsum+=space_w;
        }
        return wsum;
    };
    for (auto& word:words){
        auto test=cur; test.push_back(word);
        if(width_of(test)<=rect.w || cur.empty()) cur=std::move(test);
        else{ lines.push_back(cur); cur.clear(); cur.push_back(word);}
    }
    if(!cur.empty()) lines.push_back(cur);
    int line_y=rect.y;
    for(auto& l:lines){
        int words_total_w=0,word_h=0; std::vector<int> ww(l.size());
        for(size_t i=0;i<l.size();++i){int w=0,h=0; TTF_SizeText(font,l[i].c_str(),&w,&h); ww[i]=w; words_total_w+=w; word_h=std::max(word_h,h);}
        int gaps=l.size()-1; int x=rect.x;
        if(gaps<=0){x=rect.x+(rect.w-words_total_w)/2;}
        for(size_t i=0;i<l.size();++i){
            SDL_Surface* surf=TTF_RenderText_Blended(font,l[i].c_str(),col);
            if(!surf)continue; SDL_Texture* tex=SDL_CreateTextureFromSurface(renderer_,surf);
            int tw=surf->w,th=surf->h; SDL_FreeSurface(surf);
            if(!tex)continue; SDL_Rect dst{x,line_y,tw,th}; SDL_RenderCopy(renderer_,tex,nullptr,&dst); SDL_DestroyTexture(tex);
            x+=ww[i]+space_w;
        }
        line_y+=word_h; if(line_y>=rect.y+rect.h) break;
    }
}

void LoadingScreen::render_scaled_center(SDL_Texture* tex, int target_w, int target_h, int cx, int cy) {
    if (!tex) return; int w,h; SDL_QueryTexture(tex,nullptr,nullptr,&w,&h);
    if(w<=0||h<=0)return; double ar=(double)w/h;
    int dw=target_w; int dh=(int)(dw/ar);
    if(dh>target_h){dh=target_h; dw=(int)(dh*ar);}
    SDL_Rect dst{cx-dw/2, cy-dh/2, dw, dh};
    SDL_RenderCopy(renderer_,tex,nullptr,&dst);
}

void LoadingScreen::init() {
    fs::path folder = pick_random_loading_folder();
    if (folder.empty()) return;
    images_ = list_images_in(folder);
    message_ = pick_random_message_from_csv(folder / "messages.csv");
    current_index_ = 0;
    last_switch_time_ = SDL_GetTicks();
}

void LoadingScreen::draw_frame() {
    if (images_.empty()) return;
    Uint32 now = SDL_GetTicks();
    if (now - last_switch_time_ > 250) {
        current_index_ = (current_index_ + 1) % images_.size();
        last_switch_time_ = now;
    }
    SDL_Surface* surf = IMG_Load(images_[current_index_].string().c_str());
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    SDL_FreeSurface(surf);
    if (!tex) return;
    SDL_SetRenderDrawColor(renderer_,0,0,0,255); SDL_RenderClear(renderer_);
    TTF_Font* title_font=TTF_OpenFont("C:/Windows/Fonts/consola.ttf",48);
    SDL_Color white={255,255,255,255};
    if(title_font){int tw,th; TTF_SizeText(title_font,"LOADING...",&tw,&th); int tx=(screen_w_-tw)/2;
        draw_text(title_font,"LOADING...",tx,40,white); TTF_CloseFont(title_font);}
    render_scaled_center(tex,screen_w_/3,screen_h_/3,screen_w_/2,screen_h_/2);
    TTF_Font* body_font=TTF_OpenFont("C:/Windows/Fonts/consola.ttf",26);
    SDL_Rect msg_rect{screen_w_/3,(screen_h_*2)/3,screen_w_/3,screen_h_/4};
    if(body_font && !message_.empty()){render_justified_text(body_font,message_,msg_rect,white); TTF_CloseFont(body_font);}
    SDL_DestroyTexture(tex);
}
