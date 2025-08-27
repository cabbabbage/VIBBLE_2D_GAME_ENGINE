
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_set>
#include <SDL.h>
#include "utils/mouse_input.hpp"
#include "utils/area.hpp"
#include "asset/asset.hpp"
#include "core/active_assets_manager.hpp"
#include "render/scene_renderer.hpp"
#include "core/asset_loader.hpp"
#include "ui\menu_ui.hpp"

class Assets;

class SceneRenderer;

class Engine {
public:
    Engine(const std::string& map_path,
           SDL_Renderer* renderer,
           int screen_w,
           int screen_h);
    ~Engine();

    void init();
    void game_loop();

private:
    
    void handleExit();
    void handleRestart();
    void handleSettings();
    void handleDevMode();
    void save_current_room();

    
    void rebuild_menu_ui();   

private:
    MouseInput mouse_input; 
    std::string                  map_path;
    SDL_Renderer*                renderer;
    const int                    SCREEN_WIDTH;
    const int                    SCREEN_HEIGHT;

    SDL_Color                    boundary_color;
    SDL_Texture*                 overlay_texture;
    SDL_Texture*                 minimap_texture_;

    std::unique_ptr<AssetLoader> loader_;
    Assets*                      game_assets;
    SceneRenderer*               scene;
    std::vector<Area>            roomTrailAreas;

    
    MenuUI*                      menu_ui;
    bool                         menu_active;
    bool                         dev_mode = false; 
};
