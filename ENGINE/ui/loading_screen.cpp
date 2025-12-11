#include "loading_screen.hpp"
#include <SDL_image.h>
#include <fstream>
#include <sstream>
#include <random>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <cctype>
#include "font_paths.hpp"
#include "core/manifest/manifest_loader.hpp"
namespace fs = std::filesystem;

LoadingScreen::LoadingScreen(SDL_Renderer* renderer, int screen_w, int screen_h)
: renderer_(renderer), screen_w_(screen_w), screen_h_(screen_h) {}

LoadingScreen::~LoadingScreen() {
        if (current_texture_) {
                SDL_DestroyTexture(current_texture_);
                current_texture_ = nullptr;
        }
}

fs::path LoadingScreen::project_root() const {
    try {
        fs::path manifest = manifest::manifest_path();
        if (!manifest.empty()) {
            return fs::absolute(fs::path(manifest)).parent_path();
        }
    } catch (...) {
    }
    return fs::current_path();
}

fs::path LoadingScreen::loading_content_root() const {
    return project_root() / "SRC" / "LOADING CONTENT";
}

std::vector<fs::path> LoadingScreen::list_images_in(const fs::path& dir, bool recursive) const {
    std::vector<fs::path> out;
    if (dir.empty() || !fs::exists(dir)) return out;

    auto try_add = [&](const fs::path& file) {
        std::string ext = file.extension().string();
        for (auto& c : ext) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
            out.push_back(file);
        }
};

    if (recursive) {
        for (const auto& entry : fs::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                try_add(entry.path());
            }
        }
    } else {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                try_add(entry.path());
            }
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

void LoadingScreen::render_scaled_center(SDL_Texture* tex, int target_w, int target_h, int cx, int cy, double angle) {
        if (!tex) return; int w,h; SDL_QueryTexture(tex,nullptr,nullptr,&w,&h);
        if(w<=0||h<=0)return; double ar=(double)w/h;
        int dw=target_w; int dh=(int)(dw/ar);
        if(dh>target_h){dh=target_h; dw=(int)(dh*ar);}
        SDL_Rect dst{cx-dw/2, cy-dh/2, dw, dh};
        SDL_RenderCopyEx(renderer_,tex,nullptr,&dst,angle,nullptr,SDL_FLIP_NONE);
}

void LoadingScreen::init() {
        if (current_texture_) {
                SDL_DestroyTexture(current_texture_);
                current_texture_ = nullptr;
                current_texture_path_.clear();
        }

        selected_image_path_.clear();
        message_.clear();

        fs::path active_root = loading_content_root();
        auto images = list_images_in(active_root, true);

        if (!images.empty()) {
                std::mt19937 rng{std::random_device{}()};
                std::uniform_int_distribution<size_t> dist(0, images.size() - 1);
                selected_image_path_ = images[dist(rng)];
                message_ = pick_random_message_from_csv(selected_image_path_.parent_path() / "messages.csv");
        }

        status_text_.clear();
        rotation_angle_ = 0.0;
        last_frame_time_ = SDL_GetTicks();
}

void LoadingScreen::set_status(std::string status) {
        status_text_ = std::move(status);
}

void LoadingScreen::draw_frame() {

        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);

        const double rotation_speed = 20.0;
        Uint32 now = SDL_GetTicks();
        Uint32 delta = last_frame_time_ ? (now - last_frame_time_) : 0;
        last_frame_time_ = now;
        rotation_angle_ = std::fmod(rotation_angle_ + (delta * rotation_speed) / 1000.0, 360.0);

        if (!selected_image_path_.empty()) {
                if (!current_texture_ || selected_image_path_ != current_texture_path_) {
                        if (current_texture_) {
                                SDL_DestroyTexture(current_texture_);
                                current_texture_ = nullptr;
                                current_texture_path_.clear();
                        }
                        SDL_Surface* surf = IMG_Load(selected_image_path_.string().c_str());
                        if (surf) {
                                current_texture_ = SDL_CreateTextureFromSurface(renderer_, surf);
                                SDL_FreeSurface(surf);
                                if (current_texture_) {
                                        current_texture_path_ = selected_image_path_;
                                }
                        }
                }
        } else if (current_texture_) {
                SDL_DestroyTexture(current_texture_);
                current_texture_ = nullptr;
                current_texture_path_.clear();
        }

        const std::string mono_font = ui_fonts::monospace();
        TTF_Font* title_font = TTF_OpenFont(mono_font.c_str(), 48);
        SDL_Color white{255, 255, 255, 255};
        int title_height = 0;
        if (title_font) {
                int tw = 0, th = 0;
                TTF_SizeText(title_font, "LOADING...", &tw, &th);
                int tx = (screen_w_ - tw) / 2;
                draw_text(title_font, "LOADING...", tx, 40, white);
                title_height = th;
                TTF_CloseFont(title_font);
        }
        if (!status_text_.empty()) {
                TTF_Font* status_font = TTF_OpenFont(mono_font.c_str(), 28);
                if (status_font) {
                        int sw = 0;
                        int sh = 0;
                        TTF_SizeText(status_font, status_text_.c_str(), &sw, &sh);
                        int sx = (screen_w_ - sw) / 2;
                        int sy = 40 + title_height + 12;
                        draw_text(status_font, status_text_, sx, sy, white);
                        TTF_CloseFont(status_font);
                }
        }

        if (current_texture_) {
                render_scaled_center(current_texture_, screen_w_ / 3, screen_h_ / 3, screen_w_ / 2, screen_h_ / 2, rotation_angle_);
        }

        const bool has_message = !message_.empty();
        if (has_message) {
                TTF_Font* body_font = TTF_OpenFont(mono_font.c_str(), 26);
                SDL_Rect msg_rect{screen_w_ / 3, (screen_h_ * 2) / 3, screen_w_ / 3, screen_h_ / 4};
                if (body_font) {
                        render_justified_text(body_font, message_, msg_rect, white);
                        TTF_CloseFont(body_font);
                }
        }
}
