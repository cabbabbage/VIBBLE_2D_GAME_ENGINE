// === File: ui/menu_ui.cpp ===
#include "ui/menu_ui.hpp"

#include "ui/tinyfiledialogs.h"
#include "asset_loader.hpp"
#include "scene_renderer.hpp"
#include "assets.hpp"
#include "mouse_input.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

MenuUI::MenuUI(SDL_Renderer* renderer,
               int screen_w,
               int screen_h,
               const std::string& map_path)
    : MainApp(map_path, renderer, screen_w, screen_h) {
    if (TTF_WasInit() == 0) {
        if (TTF_Init() < 0) {
            std::cerr << "TTF_Init failed: " << TTF_GetError() << "\n";
        }
    }
}

MenuUI::~MenuUI() = default;

void MenuUI::init() {
    setup();
    dev_mode_local_ = dev_mode_;
    rebuildButtons();
    std::cout << "\n\nENTERING GAME LOOP (MenuUI)\n\n";
    game_loop();
}

void MenuUI::game_loop() {
    const int FRAME_MS = 1000 / 30;
    bool quit = false;
    SDL_Event e;
    std::unordered_set<SDL_Keycode> keys;
    int frame_count = 0;

    return_to_main_menu_ = false;

    while (!quit) {
        Uint32 start = SDL_GetTicks();

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true; 
            } else if (e.type == SDL_KEYDOWN) {
                keys.insert(e.key.keysym.sym);
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    toggleMenu();
                }
            } else if (e.type == SDL_KEYUP) {
                keys.erase(e.key.keysym.sym);
            }

            if (mouse_input_) mouse_input_->handleEvent(e);
            if (menu_active_) handle_event(e);
        }

        if (game_assets_ && game_assets_->player) {
            const int px = game_assets_->player->pos_X;
            const int py = game_assets_->player->pos_Y;
            game_assets_->update(keys, px, py);
        }

        if (frame_count >= 80) {
            if (scene_) scene_->render();

            if (menu_active_) {
                update(dev_mode_local_);
                render();

                switch (consumeAction()) {
                    case MenuAction::EXIT:         doExit();         quit = true; break;  
                    case MenuAction::RESTART:      doRestart();      frame_count = 0; break;
                    case MenuAction::SETTINGS:     doSettings();     break;
                    case MenuAction::DEV_MODE_TOGGLE:
                        doToggleDevMode();
                        rebuildButtons();
                        break;
                    case MenuAction::SAVE_ROOM:    doSaveCurrentRoom(); break;
                    default: break;
                }
            }

            SDL_RenderPresent(renderer_);
        }

        ++frame_count;
        if (mouse_input_) mouse_input_->update();

        Uint32 elapsed = SDL_GetTicks() - start;
        if (elapsed < FRAME_MS) SDL_Delay(FRAME_MS - elapsed);
    }
}

void MenuUI::toggleMenu() {
    menu_active_ = !menu_active_;
    std::cout << "[MenuUI] ESC -> menu_active=" << (menu_active_ ? "true" : "false") << "\n";
}

void MenuUI::handle_event(const SDL_Event& e) {
    if (e.type == SDL_MOUSEMOTION) {
        SDL_Point p{ e.motion.x, e.motion.y };
        for (auto& b : buttons_) b.hovered = SDL_PointInRect(&p, &b.rect);
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

void MenuUI::update(bool dev_mode_now) {
    if (dev_mode_local_ != dev_mode_now) {
        dev_mode_local_ = dev_mode_now;
        rebuildButtons();
    }
}

void MenuUI::render() {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 180);
    SDL_Rect bg{0, 0, screen_w_, screen_h_};
    SDL_RenderFillRect(renderer_, &bg);

    for (auto& b : buttons_) {
        SDL_SetRenderDrawColor(renderer_, b.hovered ? 60 : 40, b.hovered ? 60 : 40, b.hovered ? 60 : 40, 255);
        SDL_RenderFillRect(renderer_, &b.rect);

        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
        SDL_RenderDrawRect(renderer_, &b.rect);

        // Choose style per button
        const TextStyle& style = (b.action == MenuAction::EXIT)
                               ? TextStyles::MediumSecondary()
                               : TextStyles::MediumMain();

        drawTextCentered(b.label, b.rect, style, b.hovered);
    }
}

MenuUI::MenuAction MenuUI::consumeAction() {
    MenuAction a = last_action_;
    last_action_ = MenuAction::NONE;
    return a;
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

    addButton("End Run",            MenuAction::EXIT);
    addButton("Restart Run",        MenuAction::RESTART);
    addButton("Settings",           MenuAction::SETTINGS);
    addButton(dev_mode_local_ ? "Switch to Player Mode" : "Switch to Dev Mode",
              MenuAction::DEV_MODE_TOGGLE);
    addButton("Save Current Room",  MenuAction::SAVE_ROOM);
}

void MenuUI::drawTextCentered(const std::string& text,
                              const SDL_Rect& rect,
                              const TextStyle& style,
                              bool hovered) {
    TTF_Font* font = style.open_font();
    if (!font) return;

    SDL_Color col = hovered ? SDL_Color{255,255,255,255} : style.color;

    SDL_Surface* surf = TTF_RenderText_Blended(font, text.c_str(), col);
    if (!surf) { TTF_CloseFont(font); return; }

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    int tw = surf->w, th = surf->h;
    SDL_FreeSurface(surf);

    if (!tex) { TTF_CloseFont(font); return; }

    SDL_Rect dst {
        rect.x + (rect.w - tw) / 2,
        rect.y + (rect.h - th) / 2,
        tw, th
    };
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    TTF_CloseFont(font);
}

void MenuUI::doExit() {
    std::cout << "[MenuUI] End Run -> return to main menu\n";
    return_to_main_menu_ = true;
}

void MenuUI::doRestart() {
    std::cout << "[MenuUI] Restarting...\n";

    if (scene_)            { delete scene_; scene_ = nullptr; }
    if (minimap_texture_)  { SDL_DestroyTexture(minimap_texture_); minimap_texture_ = nullptr; }
    if (game_assets_)      { delete game_assets_; game_assets_ = nullptr; }

    try {
        minimap_texture_ = loader_->createMinimap(200, 200);
        auto assets_uptr = loader_->createAssets(screen_w_, screen_h_);
        game_assets_ = assets_uptr.release();
        if (!mouse_input_) mouse_input_ = new MouseInput();
        game_assets_->set_mouse_input(mouse_input_);
    } catch (const std::exception& ex) {
        std::cerr << "[MenuUI] Restart failed: " << ex.what() << "\n";
        return;
    }

    scene_ = new SceneRenderer(renderer_, game_assets_, screen_w_, screen_h_, map_path_);
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

    const char* folder = tinyfd_selectFolderDialog(
        "Select folder to save room copy",
        abs_map_path.c_str()
    );
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
