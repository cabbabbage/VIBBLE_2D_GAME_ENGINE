#pragma once

#include <SDL.h>
#include <memory>
#include <string>
#include <unordered_set>

class Assets;
class SceneRenderer;
class AssetLoader;
class Input;

class MainApp {

	public:
    MainApp(const std::string& map_path, SDL_Renderer* renderer, int screen_w, int screen_h);
    virtual ~MainApp();
    virtual void init();
    virtual void game_loop();
    virtual void setup();
	protected:
	protected:
    std::string   map_path_;
    SDL_Renderer* renderer_   = nullptr;
    int           screen_w_   = 0;
    int           screen_h_   = 0;
    std::unique_ptr<AssetLoader> loader_;
    Assets*        game_assets_      = nullptr;
    SceneRenderer* scene_            = nullptr;
    Input*         input_            = nullptr;
    SDL_Texture* overlay_texture_    = nullptr;
    SDL_Texture* minimap_texture_    = nullptr;
    bool dev_mode_ = false;
};

void run(SDL_Window* window, SDL_Renderer* renderer, int screen_w, int screen_h, bool rebuild_cache);
