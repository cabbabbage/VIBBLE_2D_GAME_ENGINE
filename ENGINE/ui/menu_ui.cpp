#include "ui/menu_ui.hpp"

#include "ui/tinyfiledialogs.h"
#include "asset_loader.hpp"
#include "asset/asset_types.hpp"
#include "scene_renderer.hpp"
#include "AssetsManager.hpp"
#include "input.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <random>
#include <sstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

MenuUI::MenuUI(SDL_Renderer* renderer,
               int screen_w,
               int screen_h,
               const std::string& map_path)
: MainApp(map_path, renderer, screen_w, screen_h)
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
	const int FRAME_MS = 1000 / 60;
	bool quit = false;
	SDL_Event e;
	int frame_count = 0;
	return_to_main_menu_ = false;
	while (!quit) {
		Uint32 start = SDL_GetTicks();
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) {
					quit = true;
			}
                        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && e.key.repeat == 0) {
                                bool esc_consumed = false;
                                if (game_assets_) {
                                                if (game_assets_->is_asset_info_editor_open()) {
                                                                // Close asset info editor; if it had closed the library, reopen it.
                                                                game_assets_->close_asset_info_editor();
                                                                esc_consumed = true;
                                                }
                                                // Note: ESC no longer closes the Asset Library.
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
                if (game_assets_) {
                        int px = 0;
                        int py = 0;
                        if (game_assets_->player) {
                                px = game_assets_->player->pos.x;
                                py = game_assets_->player->pos.y;
                        } else {
                                SDL_Point focus = game_assets_->getView().get_screen_center();
                                px = focus.x;
                                py = focus.y;
                        }
                        if (input_) {
                                game_assets_->update(*input_, px, py);
                        }
                }
                if (menu_active_) {
                        render();
                        switch (consumeAction()) {
                                        case MenuAction::EXIT:            doExit();         quit = true;        break;
                                        case MenuAction::RESTART:         doRestart();      frame_count = 0;    break;
                                        case MenuAction::SETTINGS:        doSettings();                         break;
                                        default: break;
                        }
                }
		if (menu_active_) SDL_RenderPresent(renderer_);
		++frame_count;
		if (input_) input_->update();
		Uint32 elapsed = SDL_GetTicks() - start;
		if (elapsed < FRAME_MS) SDL_Delay(FRAME_MS - elapsed);
	}
}

void MenuUI::toggleMenu() {
	menu_active_ = !menu_active_;
	std::cout << "[MenuUI] ESC -> menu_active=" << (menu_active_ ? "true" : "false") << "\n";
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
	const int btn_w = Button::width();
	const int btn_h = Button::height();
	const int gap   = 16;
	int start_y = 150;
	const int x = (screen_w_ - btn_w) / 2;
	auto addButton = [&](const std::string& label, MenuAction action, bool is_exit=false) {
		Button b = is_exit ? Button::get_exit_button(label) : Button::get_main_button(label);
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
		auto all_assets = loader_->createAssets();
		Asset* player_ptr = nullptr;
		for (auto& a : all_assets) {
                    if (a.info && a.info->type == asset_types::player) { player_ptr = &a; break; }
		}
		if (!player_ptr) throw std::runtime_error("[MenuUI] No player asset found");
		game_assets_ = new Assets(std::move(all_assets), *loader_->getAssetLibrary(), player_ptr, loader_->getRooms(), screen_w_, screen_h_, player_ptr->pos.x, player_ptr->pos.y, static_cast<int>(loader_->getMapRadius() * 1.2), renderer_, map_path_);
		if (!input_) input_ = new Input();
		game_assets_->set_input(input_);
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
        // Close the menu immediately after switching modes
        if (menu_active_) {
                menu_active_ = false;
                if (game_assets_) game_assets_->set_render_suppressed(false);
                std::cout << "[MenuUI] Closing menu after mode switch\n";
        }
}
