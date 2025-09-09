#include "ui/menu_ui.hpp"

#include "ui/tinyfiledialogs.h"
#include "asset_loader.hpp"
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
	dev_mode_local_ = dev_mode_;
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
													game_assets_->close_asset_info_editor();
													esc_consumed = true;
								} else if (game_assets_->is_asset_library_open()) {
													game_assets_->close_asset_library();
													esc_consumed = true;
								}
					}
					if (!esc_consumed) {
								toggleMenu();
					}
			}
			if (input_) input_->handleEvent(e);
			if (game_assets_) game_assets_->handle_sdl_event(e);
			if (menu_active_) handle_event(e);
		}
		if (game_assets_ && game_assets_->player) {
			const int px = game_assets_->player->pos.x;
			const int py = game_assets_->player->pos.y;
			game_assets_->update(*input_, px, py);
		}
		if (menu_active_) {
			update(dev_mode_local_);
			render();
			switch (consumeAction()) {
					case MenuAction::EXIT:            doExit();         quit = true;        break;
					case MenuAction::RESTART:         doRestart();      frame_count = 0;    break;
					case MenuAction::SETTINGS:        doSettings();                         break;
					case MenuAction::DEV_MODE_TOGGLE: doToggleDevMode(); rebuildButtons();   break;
					case MenuAction::SAVE_ROOM:       doSaveCurrentRoom();                   break;
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

void MenuUI::update(bool dev_mode_now) {
	if (dev_mode_local_ != dev_mode_now) {
		dev_mode_local_ = dev_mode_now;
		rebuildButtons();
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
	addButton(dev_mode_local_ ? "Switch to Player Mode" : "Switch to Dev Mode", MenuAction::DEV_MODE_TOGGLE);
	addButton("Save Current Room",  MenuAction::SAVE_ROOM);
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
	if (minimap_texture_)  { SDL_DestroyTexture(minimap_texture_); minimap_texture_ = nullptr; }
	if (game_assets_)      { delete game_assets_; game_assets_ = nullptr; }
	try {
		minimap_texture_ = loader_->createMinimap(200, 200);
		auto all_assets = loader_->createAssets(screen_w_, screen_h_);
		Asset* player_ptr = nullptr;
		for (auto& a : all_assets) {
			if (a.info && a.info->type == "Player") { player_ptr = &a; break; }
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
	dev_mode_local_ = !dev_mode_local_;
	dev_mode_ = dev_mode_local_;
	if (game_assets_) game_assets_->set_dev_mode(dev_mode_);
	std::cout << "[MenuUI] Dev Mode = " << (dev_mode_ ? "ON" : "OFF") << "\n";
}

void MenuUI::doSaveCurrentRoom() {
	std::cout << "[MenuUI] Save Current Room requested\n";
	std::string save_path;
	std::string room_name;
	std::string abs_map_path = fs::absolute(map_path_).string();
	const char* folder = tinyfd_selectFolderDialog( "Select folder to save room copy", abs_map_path.c_str() );
	if (!folder) {
		std::cout << "[MenuUI] No folder selected.\n";
		return;
	}
	const char* new_name = tinyfd_inputBox("Room Name", "Enter a name for the room copy:", "");
	if (!new_name || std::string(new_name).empty()) {
		std::cout << "[MenuUI] No room name entered.\n";
		return;
	}
	room_name = new_name;
	save_path = std::string(folder) + "/" + room_name + ".json";
	std::cout << "[MenuUI] Saving room '" << room_name << "' to " << save_path << "\n";
	try {
		nlohmann::json room_json = game_assets_->save_current_room(room_name);
		std::ofstream out(save_path, std::ios::trunc);
		if (!out.is_open()) {
			throw std::runtime_error("Failed to open file for writing: " + save_path);
		}
		out << room_json.dump(4);
		out.close();
		std::cout << "[MenuUI] Room saved successfully.\n";
	} catch (const std::exception& e) {
		std::cerr << "[MenuUI] Failed to save room: " << e.what() << "\n";
	}
}
