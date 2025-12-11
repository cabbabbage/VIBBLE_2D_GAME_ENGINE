#include "main_menu.hpp"

#include <SDL_image.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <array>
#include <utility>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <cmath>
#include <cctype>

#include "core/manifest/manifest_loader.hpp"

namespace fs = std::filesystem;

MainMenu::MainMenu(SDL_Renderer* renderer,
                   int screen_w,
                   int screen_h,
                   const nlohmann::json& maps)
: renderer_(renderer),
  screen_w_(screen_w),
  screen_h_(screen_h),
  maps_json_(&maps)
{
        if (TTF_WasInit() == 0 && TTF_Init() < 0) {
                std::cerr << "TTF_Init failed: " << TTF_GetError() << "\n";
        }
        animation_start_ticks_ = SDL_GetTicks64();
        try {
                manifest_root_ = fs::absolute(fs::path(manifest::manifest_path()).parent_path());
        } catch (const std::exception& ex) {
                std::cerr << "[MainMenu] Failed to determine project root: " << ex.what() << "\n";
                manifest_root_ = fs::current_path();
        }
        auto try_load_background = [&](const fs::path& candidate) -> bool {
                if (candidate.empty()) return false;
                try {
                        if (!fs::exists(candidate)) {
                                return false;
                        }
                        SDL_Texture* tex = loadTexture(candidate);
                        if (tex) {
                                background_tex_ = tex;
                                background_image_path_ = fs::absolute(candidate);
                                return true;
                        }
                        std::cerr << "[MainMenu] Failed to load menu background texture: "
                                  << candidate << "\n";
                } catch (const std::exception& ex) {
                        std::cerr << "[MainMenu] Error accessing menu background at '"
                                  << candidate << "': " << ex.what() << "\n";
                }
                return false;
};

        if (!try_load_background(pick_loading_image())) {
                const fs::path bg_folder = resolve_manifest_path("SRC/misc_content/backgrounds");
                if (fs::exists(bg_folder) && fs::is_directory(bg_folder)) {
                        const fs::path fallback = firstImageIn(bg_folder);
                        try_load_background(fallback);
                }
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
        map_lookup_.clear();
        Button::refresh_glass_overlay();
        const int btn_w = Button::width();
        const int btn_h = Button::height();
        const int gap   = 18;
        int y = (screen_h_ / 2) - 140;
        const int x = (screen_w_ - btn_w) / 2;
        auto configure_button = [](Button& button) {
                button.set_glass_style(Button::default_glass_style());
                button.enable_glass_style(true);
};
        if (maps_json_ && maps_json_->is_object()) {
                for (auto it = maps_json_->cbegin(); it != maps_json_->cend(); ++it) {
                        if (!it.value().is_object()) continue;
                        const std::string map_id = it.key();
                        map_lookup_.emplace(map_id, &it.value());
                        std::string label = map_id;
                        const auto name_it = it.value().find("map_name");
                        if (name_it != it.value().end() && name_it->is_string()) {
                                label = name_it->get<std::string>();
                        }
                        Button b = Button::get_main_button(label);
                        configure_button(b);
                        b.set_rect(SDL_Rect{ x, y, btn_w, btn_h });
                        buttons_.push_back(MenuEntry{ std::move(b), map_id, true });
                        y += btn_h + gap;
                }
        }

        Button create = Button::get_main_button("Create New Map");
        configure_button(create);
        create.set_rect(SDL_Rect{ x, y, btn_w, btn_h });
        buttons_.push_back(MenuEntry{ std::move(create), "CREATE_NEW_MAP", false });
        y += btn_h + gap;
        Button quit = Button::get_exit_button("QUIT GAME");
        configure_button(quit);
        quit.set_rect(SDL_Rect{ x, y + 12, btn_w, btn_h });
        buttons_.push_back(MenuEntry{ std::move(quit), "QUIT", false });
}

std::optional<MainMenu::Selection> MainMenu::handle_event(const SDL_Event& e) {
        for (auto& entry : buttons_) {
                if (entry.button.handle_event(e)) {
                        Selection selection;
                        selection.id = entry.action;
                        if (entry.is_map) {
                                auto it = map_lookup_.find(entry.action);
                                if (it != map_lookup_.end() && it->second) {
                                        selection.data = *(it->second);
                                }
                        }
                        return selection;
                }
        }
        return std::nullopt;
}

void MainMenu::render() {
        if (background_tex_) {
                renderAnimatedBackground(background_tex_);
        } else {
                SDL_Color night = Styles::Night();
                SDL_SetRenderDrawColor(renderer_, night.r, night.g, night.b, night.a);
                SDL_RenderClear(renderer_);
        }
	drawVignette(120);
	const std::string title = "DEPARTED AFFAIRS & CO.";
	SDL_Rect trect{ 0, 60, screen_w_, 80 };
	blitTextCentered(renderer_, Styles::LabelTitle(), title, trect, true, SDL_Color{0,0,0,0});
	for (auto& entry : buttons_) {
		entry.button.render(renderer_);
	}
}

void MainMenu::showLoadingScreen() {
	SDL_SetRenderTarget(renderer_, nullptr);
        SDL_Texture* bg = background_tex_;
        bool temp_bg = false;
        if (!bg && !background_image_path_.empty()) {
                try {
                        if (fs::exists(background_image_path_)) {
                                bg = loadTexture(background_image_path_);
                                temp_bg = (bg != nullptr);
                        }
                } catch (const std::exception& ex) {
                        std::cerr << "[MainMenu] Error loading menu background for loading screen: "
                                  << ex.what() << "\n";
                }
        }
        if (!bg) {
                const fs::path bg_folder = resolve_manifest_path("SRC/misc_content/backgrounds");
                const fs::path first = firstImageIn(bg_folder);
                if (!first.empty()) {
                        bg = loadTexture(first);
                        temp_bg = (bg != nullptr);
		}
	}
	SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
	SDL_RenderClear(renderer_);
        if (bg) {
                renderAnimatedBackground(bg);
        }
	drawVignette(110);
	SDL_Texture* tarot = nullptr;
	std::string msg;
	const fs::path image_path = pick_loading_image();
	if (!image_path.empty()) {
		tarot = loadTexture(image_path);
		msg = pickRandomLine(image_path.parent_path() / "messages.csv");
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

	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {

	}
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

std::filesystem::path MainMenu::resolve_manifest_path(const std::string& forward_path) const {
	fs::path base = manifest_root_;
	if (base.empty()) {
		base = fs::current_path();
	}
	fs::path relative;
	std::stringstream ss(forward_path);
	std::string segment;
	while (std::getline(ss, segment, '/')) {
		if (segment.empty() || segment == ".") continue;
		relative /= segment;
	}
	fs::path resolved = base / relative;
	return resolved.lexically_normal();
}

std::filesystem::path MainMenu::loading_content_root() const {
	return resolve_manifest_path("SRC/LOADING CONTENT");
}

std::vector<fs::path> MainMenu::list_loading_images(const fs::path& root, bool recursive) const {
	std::vector<fs::path> out;
	if (root.empty() || !fs::exists(root)) return out;

	auto try_add = [&](const fs::path& candidate) {
		std::string ext = candidate.extension().string();
		for (auto& c : ext) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
			out.push_back(candidate);
		}
};

	if (recursive) {
		for (const auto& entry : fs::recursive_directory_iterator(root)) {
			if (entry.is_regular_file()) {
				try_add(entry.path());
			}
		}
	} else {
		for (const auto& entry : fs::directory_iterator(root)) {
			if (entry.is_regular_file()) {
				try_add(entry.path());
			}
		}
	}

	std::sort(out.begin(), out.end());
	return out;
}

fs::path MainMenu::pick_loading_image() const {
	auto images = list_loading_images(loading_content_root(), true);
	if (images.empty()) {
		return {};
	}
	std::mt19937 rng{std::random_device{}()};
	std::uniform_int_distribution<size_t> dist(0, images.size() - 1);
	return images[dist(rng)];
}

std::filesystem::path MainMenu::firstImageIn(const fs::path& folder) const {
    if (!fs::exists(folder) || !fs::is_directory(folder)) return {};

    fs::path png_candidate;
    fs::path jpg_candidate;
    for (const auto& e : fs::directory_iterator(folder)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        for (auto& c : ext) c = char(::tolower(c));
        if (ext == ".png") {
            png_candidate = e.path();
            break;
        }
        if (ext == ".jpg" || ext == ".jpeg") {
            if (jpg_candidate.empty()) jpg_candidate = e.path();
        }
    }
    if (!png_candidate.empty()) return png_candidate;
    return jpg_candidate;
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

void MainMenu::renderAnimatedBackground(SDL_Texture* tex) const {
        if (!tex) return;

        int tex_w = 0;
        int tex_h = 0;
        SDL_QueryTexture(tex, nullptr, nullptr, &tex_w, &tex_h);
        if (tex_w <= 0 || tex_h <= 0) return;

        const double rpm = 0.5 / 5.0;
        const double degrees_per_second = rpm * 360.0 / 60.0;
        const Uint64 now = SDL_GetTicks64();
        const double elapsed_seconds = static_cast<double>(now - animation_start_ticks_) / 1000.0;
        const double angle = std::fmod(elapsed_seconds * degrees_per_second, 360.0);

        const double pivot_x = static_cast<double>(screen_w_) * 0.5;
        const double pivot_y = static_cast<double>(screen_h_) * 0.5;

        const double base_scale_x = static_cast<double>(screen_w_) / static_cast<double>(tex_w);
        const double base_scale_y = static_cast<double>(screen_h_) / static_cast<double>(tex_h);
        double required_scale = std::max(base_scale_x, base_scale_y);

        const double half_w = static_cast<double>(tex_w) * 0.5;
        const double half_h = static_cast<double>(tex_h) * 0.5;
        const double texture_radius = std::sqrt(half_w * half_w + half_h * half_h);
        if (texture_radius > 1e-6) {
                const std::array<std::pair<double, double>, 4> corners{{
                        {0.0, 0.0},
                        {static_cast<double>(screen_w_), 0.0},
                        {0.0, static_cast<double>(screen_h_)},
                        {static_cast<double>(screen_w_), static_cast<double>(screen_h_)}
                }};
                double max_corner_distance = 0.0;
                for (const auto& corner : corners) {
                        const double dx = pivot_x - corner.first;
                        const double dy = pivot_y - corner.second;
                        const double dist = std::hypot(dx, dy);
                        if (dist > max_corner_distance) max_corner_distance = dist;
                }
                const double needed_scale = max_corner_distance / texture_radius;
                if (needed_scale > required_scale) required_scale = needed_scale;
        }

        required_scale = std::max(required_scale, 1.0);

        required_scale *= 1.18;

        SDL_Rect dst{};
        dst.w = static_cast<int>(std::ceil(static_cast<double>(tex_w) * required_scale));
        dst.h = static_cast<int>(std::ceil(static_cast<double>(tex_h) * required_scale));
        dst.x = static_cast<int>(std::round(pivot_x - static_cast<double>(dst.w) * 0.5));
        dst.y = static_cast<int>(std::round(pivot_y - static_cast<double>(dst.h) * 0.5));

        SDL_Point center{};
        center.x = dst.w / 2;
        center.y = dst.h / 2;
        SDL_RenderCopyEx(renderer_, tex, nullptr, &dst, angle, &center, SDL_FLIP_NONE);
}

void MainMenu::drawVignette(Uint8 alpha) const {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, alpha);
        SDL_Rect v{0,0,screen_w_,screen_h_};
        SDL_RenderFillRect(renderer_, &v);
}
