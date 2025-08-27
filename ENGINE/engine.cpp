
#include "engine.hpp"

#include <iostream>
#include <filesystem>
#include <random>
#include <ctime>

#include "ui/tinyfiledialogs.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

Engine::Engine(const std::string& map_path,
               SDL_Renderer* renderer,
               int screen_w,
               int screen_h)
    : map_path(map_path),
      renderer(renderer),
      SCREEN_WIDTH(screen_w),
      SCREEN_HEIGHT(screen_h),
      boundary_color({20, 33, 21, 150}),
      overlay_texture(nullptr),
      minimap_texture_(nullptr),
      game_assets(nullptr),
      scene(nullptr),
      menu_ui(nullptr),
      menu_active(false),
      dev_mode(false)
{}

Engine::~Engine() {
    if (overlay_texture) SDL_DestroyTexture(overlay_texture);
    if (minimap_texture_) SDL_DestroyTexture(minimap_texture_);
    delete game_assets;
    delete scene;
    delete menu_ui;
}

void Engine::init() {
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    try {
        loader_ = std::make_unique<AssetLoader>(map_path, renderer);
        roomTrailAreas = loader_->getAllRoomAndTrailAreas();
        minimap_texture_ = loader_->createMinimap(200, 200);

        auto assets_uptr = loader_->createAssets(SCREEN_WIDTH, SCREEN_HEIGHT);
        game_assets = assets_uptr.release();

        
        game_assets->set_mouse_input(&mouse_input);
    }
    catch (const std::exception& e) {
        std::cerr << "[Engine] Error: " << e.what() << "\n";
        return;
    }

    
    scene = new SceneRenderer(renderer, game_assets, SCREEN_WIDTH, SCREEN_HEIGHT, map_path);

    rebuild_menu_ui();

    std::cout << "\n\nENTERING GAME LOOP\n\n";
    game_loop();
}

void Engine::game_loop() {
    const int FRAME_MS = 1000 / 30;
    bool quit = false;
    SDL_Event e;
    std::unordered_set<SDL_Keycode> keys;
    int frame_count = 0;

    while (!quit) {
        Uint32 start = SDL_GetTicks();

        
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
            }
            else if (e.type == SDL_KEYDOWN) {
                keys.insert(e.key.keysym.sym);
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    menu_active = !menu_active;
                    std::cout << "[Engine] ESC -> menu_active="
                            << (menu_active ? "true" : "false") << "\n";
                }
            }
            else if (e.type == SDL_KEYUP) {
                keys.erase(e.key.keysym.sym);
            }

            
            mouse_input.handleEvent(e);

            if (menu_active && menu_ui) {
                menu_ui->handle_event(e);
            }
        }

        
        int px = game_assets->player->pos_X;
        int py = game_assets->player->pos_Y;
        game_assets->update(keys, px, py);

        
        if (frame_count >= 80) {
            scene->render();

            if (menu_active && menu_ui) {
                menu_ui->update(dev_mode);
                menu_ui->render();

                switch (menu_ui->consumeAction()) {
                    case MenuUI::MenuAction::EXIT:
                        handleExit();
                        quit = true;
                        break;
                    case MenuUI::MenuAction::RESTART:
                        handleRestart();
                        frame_count = 0;
                        break;
                    case MenuUI::MenuAction::SETTINGS:
                        handleSettings();
                        break;
                    case MenuUI::MenuAction::DEV_MODE_TOGGLE:
                        handleDevMode();
                        break;
                    case MenuUI::MenuAction::SAVE_ROOM:
                        save_current_room();
                        break;
                    default:
                        break;
                }
            }

            SDL_RenderPresent(renderer);
        }

        ++frame_count;

        
        mouse_input.update();

        
        Uint32 elapsed = SDL_GetTicks() - start;
        if (elapsed < FRAME_MS) SDL_Delay(FRAME_MS - elapsed);
    }

}

void Engine::handleExit() {
    std::cout << "[Engine] Handling Exit...\n";
}

void Engine::handleRestart() {
    std::cout << "[Engine] Handling Restart...\n";
    menu_active = false;

    
    if (scene) { delete scene; scene = nullptr; }
    if (minimap_texture_) { SDL_DestroyTexture(minimap_texture_); minimap_texture_ = nullptr; }
    if (game_assets) { delete game_assets; game_assets = nullptr; }

    
    try {
        minimap_texture_ = loader_->createMinimap(200, 200);
        auto assets_uptr = loader_->createAssets(SCREEN_WIDTH, SCREEN_HEIGHT);
        game_assets = assets_uptr.release();
        game_assets->set_mouse_input(&mouse_input);
    } catch (const std::exception& ex) {
        std::cerr << "[Engine] Restart failed: " << ex.what() << "\n";
        return;
    }


    scene = new SceneRenderer(renderer, game_assets, SCREEN_WIDTH, SCREEN_HEIGHT, map_path);

    rebuild_menu_ui();
}

void Engine::handleSettings() {
    std::cout << "[Engine] Handling Settings...\n";
}

void Engine::handleDevMode() {
    dev_mode = !dev_mode;
    game_assets->set_dev_mode(dev_mode);
    std::cout << "[Engine] Dev Mode is now " << (dev_mode ? "ON" : "OFF") << "\n";
    rebuild_menu_ui();
}



void Engine::save_current_room() {
    std::cout << "[Engine] Save Current Room requested\n";

    
    int choice = tinyfd_messageBox(
        "Save Room",
        "Do you want to:\nYES = Save as new room\nNO = Update existing room\nCANCEL = Abort",
        "yesnocancel",   
        "question",      
        0                
    );

    if (choice == 0) { 
        std::cout << "[Engine] User canceled save.\n";
        return;
    }

    std::string save_path;
    std::string room_name;

    
    std::string abs_map_path = std::filesystem::absolute(map_path).string();

    if (choice == 1) { 
        const char* folder = tinyfd_selectFolderDialog(
            "Select folder to save new room",
            abs_map_path.c_str()   
        );
        if (!folder) {
            std::cout << "[Engine] No folder selected.\n";
            return;
        }

        const char* new_name = tinyfd_inputBox("Room Name", "Enter a name for the new room:", "");
        if (!new_name || std::string(new_name).empty()) {
            std::cout << "[Engine] No room name entered.\n";
            return;
        }

        room_name = new_name;
        save_path = std::string(folder) + "/" + room_name + ".json"; 
    }
    else if (choice == 2) { 
        const char* filterPatterns[1] = { "*.json" }; 

        const char* file = tinyfd_openFileDialog(
            "Select existing room JSON",
            abs_map_path.c_str(),   
            1,
            filterPatterns,
            "JSON files",
            0
        );
        if (!file) {
            std::cout << "[Engine] No file selected.\n";
            return;
        }

        save_path = file;
        room_name = std::filesystem::path(file).stem().string();
    }

    std::cout << "[Engine] Saving room '" << room_name << "' to " << save_path << "\n";

    try {
        
        nlohmann::json room_json = game_assets->save_current_room(room_name);

        
        std::ofstream out(save_path, std::ios::trunc);
        if (!out.is_open()) {
            throw std::runtime_error("Failed to open file for writing: " + save_path);
        }
        out << room_json.dump(4); 
        out.close();

        std::cout << "[Engine] Room saved successfully.\n";
    }
    catch (const std::exception& e) {
        std::cerr << "[Engine] Failed to save room: " << e.what() << "\n";
    }
}



void Engine::rebuild_menu_ui() {
    if (menu_ui) { delete menu_ui; menu_ui = nullptr; }
    menu_ui = new MenuUI(renderer, SCREEN_WIDTH, SCREEN_HEIGHT, dev_mode);
}
