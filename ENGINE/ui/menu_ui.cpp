#include "ui/menu_ui.hpp"

#include "ui/tinyfiledialogs.h"
#include "asset_loader.hpp"
#include "asset/asset_types.hpp"
#include "AssetsManager.hpp"
#include "input.hpp"
#include "world/world_grid.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <memory>
#include <random>
#include <sstream>
#include <utility>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace fs = std::filesystem;

MenuUI::MenuUI(SDL_Renderer* renderer,
               int screen_w,
               int screen_h,
               MapDescriptor map,
               LoadingScreen* loading_screen,
               AssetLibrary* asset_library)
: MainApp(std::move(map), renderer, screen_w, screen_h, loading_screen, asset_library)
{
        if (TTF_WasInit() == 0) {
                if (TTF_Init() < 0) {
                        std::cerr << "TTF_Init failed: " << TTF_GetError() << "\n";
                }
	}
	menu_active_ = false;
}

MenuUI::~MenuUI() = default;

void MenuUI::init() {
        setup();
        rebuildButtons();
        game_loop();
}

bool MenuUI::wants_return_to_main_menu() const {
	return return_to_main_menu_;
}

void MenuUI::game_loop() {

        constexpr double TARGET_FPS = 60.0;
        constexpr double TARGET_FRAME_SECONDS = 1.0 / TARGET_FPS;
        const double perf_frequency = static_cast<double>(SDL_GetPerformanceFrequency());
        const double target_counts  = TARGET_FRAME_SECONDS * perf_frequency;

	bool quit = false;
	SDL_Event e;
        return_to_main_menu_ = false;
        double idle_counts_accum = 0.0;
        int idle_frame_counter   = 0;
        constexpr int IDLE_REPORT_INTERVAL = 120;

	while (!quit) {
                const Uint64 frame_begin = SDL_GetPerformanceCounter();
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) {
					quit = true;
			}
                        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && e.key.repeat == 0) {
                                bool esc_consumed = false;
                                if (game_assets_) {
                                        if (game_assets_->is_asset_info_editor_open()) {
                                                game_assets_->close_asset_info_editor();
                                                esc_consumed = true;
                                        }
                                }
                                if (!esc_consumed) {
                                        toggleMenu();
                                }
                        }
                        if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                                const bool ctrl_down = (e.key.keysym.mod & KMOD_CTRL) != 0;
                                if (ctrl_down && e.key.keysym.sym == SDLK_d) {
                                        doToggleDevMode();
                                }
                        }
                        if (input_) input_->handleEvent(e);
                        if (game_assets_) game_assets_->handle_sdl_event(e);
                        if (menu_active_) handle_event(e);
                }

                if (game_assets_ && input_) {
                        game_assets_->update(*input_);
                }

                if (game_assets_) {
                        static bool opened_asset_info_once = false;
                        if (!opened_asset_info_once) {
                                const auto& active = game_assets_->getActive();
                                if (!active.empty()) {
                                        game_assets_->open_asset_info_editor_for_asset(active.front());
                                        opened_asset_info_once = true;
                                }
                        }
                }

                if (menu_active_) {
                        render();
                        switch (consumeAction()) {
                                case MenuAction::EXIT:     doExit();    quit = true; break;
                                case MenuAction::RESTART:  doRestart();               break;
                                case MenuAction::SETTINGS: doSettings();             break;
                                default: break;
                        }
                }

                bool scene_presents_itself = (game_assets_ && game_assets_->scene_light_map_only_mode());
                if (menu_active_ || !scene_presents_itself) {
                        SDL_RenderPresent(renderer_);
                }

                if (input_) input_->update();

                const Uint64 frame_end = SDL_GetPerformanceCounter();
                const double work_counts = static_cast<double>(frame_end - frame_begin);
                if (work_counts < target_counts) {
                        const double remaining_counts = target_counts - work_counts;
                        idle_counts_accum += remaining_counts;
                        ++idle_frame_counter;
                        const double remaining_ms = (remaining_counts * 1000.0) / perf_frequency;
                        if (remaining_ms >= 1.0) {
                                SDL_Delay(static_cast<Uint32>(remaining_ms));
                        }
                }

                if (idle_frame_counter >= IDLE_REPORT_INTERVAL) {

                        idle_counts_accum = 0.0;
                        idle_frame_counter = 0;
                }
	}
}

void MenuUI::toggleMenu() {
        menu_active_ = !menu_active_;
        std::cout << "[MenuUI] ESC -> menu_active=" << (menu_active_ ? "true" : "false") << "\n";
        if (menu_active_) Button::refresh_glass_overlay();
        if (game_assets_) game_assets_->set_render_suppressed(menu_active_);
}

void MenuUI::handle_event(const SDL_Event& e) {
	for (auto& mb : buttons_) {
		if (mb.button.handle_event(e)) {
			last_action_ = mb.action;
			std::cout << "[MenuUI] Button clicked: " << mb.button.text() << "\n";
		}
	}
}

void MenuUI::render() {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 100);
	SDL_Rect bg{0, 0, screen_w_, screen_h_};
	SDL_RenderFillRect(renderer_, &bg);
	drawVignette(110);
	const std::string title = "PAUSE MENU";
	SDL_Rect trect{ 0, 60, screen_w_, 60 };
	blitTextCentered(renderer_, Styles::LabelTitle(), title, trect, true, SDL_Color{0,0,0,0});
	for (auto& mb : buttons_) {
		mb.button.render(renderer_);
	}
}

MenuUI::MenuAction MenuUI::consumeAction() {
	MenuAction a = last_action_;
	last_action_ = MenuAction::NONE;
	return a;
}

void MenuUI::rebuildButtons() {
        buttons_.clear();
        Button::refresh_glass_overlay();
        const int btn_w = Button::width();
        const int btn_h = Button::height();
        const int gap   = 16;
        int start_y = 150;
        const int x = (screen_w_ - btn_w) / 2;
        auto addButton = [&](const std::string& label, MenuAction action, bool is_exit=false) {
                Button b = is_exit ? Button::get_exit_button(label) : Button::get_main_button(label);
                b.set_glass_style(Button::default_glass_style());
                b.enable_glass_style(true);
                b.set_rect(SDL_Rect{ x, start_y, btn_w, btn_h });
                start_y += btn_h + gap;
                buttons_.push_back(MenuButton{ std::move(b), action });
};
        addButton("End Run",            MenuAction::EXIT, true);
        addButton("Restart Run",        MenuAction::RESTART);
        addButton("Settings",           MenuAction::SETTINGS);
}

SDL_Point MenuUI::measureText(const LabelStyle& style, const std::string& s) const {
	SDL_Point sz{0,0};
	if (s.empty()) return sz;
	TTF_Font* f = style.open_font();
	if (!f) return sz;
	TTF_SizeText(f, s.c_str(), &sz.x, &sz.y);
	TTF_CloseFont(f);
	return sz;
}

void MenuUI::blitText(SDL_Renderer* r,
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

void MenuUI::blitTextCentered(SDL_Renderer* r,
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

void MenuUI::drawVignette(Uint8 alpha) const {
	SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer_, 0, 0, 0, alpha);
	SDL_Rect v{0,0,screen_w_,screen_h_};
	SDL_RenderFillRect(renderer_, &v);
}

void MenuUI::doExit() {
	std::cout << "[MenuUI] End Run -> return to main menu\n";
	return_to_main_menu_ = true;
}

void MenuUI::doRestart() {
        std::cout << "[MenuUI] Restarting...\n";
        if (game_assets_)      { delete game_assets_; game_assets_ = nullptr; }
        try {
                if (loader_) {
                    nlohmann::json manifest_copy = loader_->map_manifest();
                    std::string content_root = loader_->content_root();
                    std::string map_id = loader_->map_identifier();
                    loader_ = std::make_unique<AssetLoader>(map_id, manifest_copy, renderer_, content_root, nullptr, asset_library_);
                }
                world::WorldGrid world_grid{};
                loader_->createAssets(world_grid);
                auto all_assets = world_grid.all_assets();
                Asset* player_ptr = nullptr;
                for (Asset* candidate : all_assets) {
                    if (candidate && candidate->info && candidate->info->type == asset_types::player) { player_ptr = candidate; break; }
                }
                int start_px = player_ptr ? player_ptr->pos.x : static_cast<int>(loader_->getMapRadius());
                int start_py = player_ptr ? player_ptr->pos.y : static_cast<int>(loader_->getMapRadius());
                AssetLibrary* restart_library = loader_->getAssetLibrary();
                if (!restart_library) {
                        throw std::runtime_error("Asset library unavailable during restart.");
                }
                game_assets_ = new Assets(*restart_library, player_ptr, loader_->getRooms(), screen_w_, screen_h_, start_px, start_py, static_cast<int>(loader_->getMapRadius() * 1.2), renderer_, loader_->map_identifier(), loader_->map_manifest(), loader_->content_root(), std::move(world_grid));
                if (!input_) input_ = new Input();
                game_assets_->set_input(input_);
                if (!player_ptr) {
                        dev_mode_ = true;
                        std::cout << "[MenuUI] No player asset found. Launching in Dev Mode.\n";
                }
                if (game_assets_) {
                        game_assets_->set_dev_mode(dev_mode_);
                }
        } catch (const std::exception& ex) {
                std::cerr << "[MenuUI] Restart failed: " << ex.what() << "\n";
                return;
        }
}

void MenuUI::doSettings() {
	std::cout << "[MenuUI] Settings opened\n";
}

void MenuUI::doToggleDevMode() {
        dev_mode_ = !dev_mode_;
        if (game_assets_) game_assets_->set_dev_mode(dev_mode_);
        std::cout << "[MenuUI] Dev Mode = " << (dev_mode_ ? "ON" : "OFF") << "\n";
        rebuildButtons();

        if (menu_active_) {
                menu_active_ = false;
                if (game_assets_) game_assets_->set_render_suppressed(false);
                std::cout << "[MenuUI] Closing menu after mode switch\n";
        }
}
