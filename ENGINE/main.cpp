#include "main.hpp"
#include "utils/rebuild_assets.hpp"
#include "utils/text_style.hpp"
#include "ui/main_menu.hpp"
#include "ui/menu_ui.hpp"
#include "asset_loader.hpp"
#include "scene_renderer.hpp"
#include "AssetsManager.hpp"
#include "input.hpp"
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_ttf.h>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
#include <algorithm>
namespace fs = std::filesystem;

extern "C" {
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
	__declspec(dllexport) int NvOptimusEnablement                = 0x00000001;
}

MainApp::MainApp(const std::string& map_path, SDL_Renderer* renderer, int screen_w, int screen_h)
: map_path_(map_path), renderer_(renderer), screen_w_(screen_w), screen_h_(screen_h) {}

MainApp::~MainApp() {
	if (overlay_texture_)  SDL_DestroyTexture(overlay_texture_);
	delete game_assets_;
	delete input_;
}

void MainApp::init() {
	setup();
	game_loop();
}

void MainApp::setup() {
	std::srand(static_cast<unsigned int>(std::time(nullptr)));
	try {
		loader_ = std::make_unique<AssetLoader>(map_path_, renderer_);
		auto all_assets = loader_->createAssets();
		Asset* player_ptr = nullptr;
		for (auto& a : all_assets) {
			if (a.info && a.info->type == "Player") { player_ptr = &a; break; }
		}
		if (!player_ptr) throw std::runtime_error("[Main] No player asset found");
		game_assets_ = new Assets(std::move(all_assets), *loader_->getAssetLibrary(), player_ptr, loader_->getRooms(), screen_w_, screen_h_, player_ptr->pos.x, player_ptr->pos.y, static_cast<int>(loader_->getMapRadius() * 1.2), renderer_, map_path_);
		input_ = new Input();
		game_assets_->set_input(input_);
	} catch (const std::exception& e) {
		std::cerr << "[MainApp] Setup error: " << e.what() << "\n";
		throw;
	}
}

void MainApp::game_loop() {
	constexpr int FRAME_MS = 1000 / 30;
	bool quit = false;
	SDL_Event e;
	int frame_count = 0;
	while (!quit) {
		Uint32 start = SDL_GetTicks();
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) quit = true;
			if (input_) input_->handleEvent(e);
			if (game_assets_) game_assets_->handle_sdl_event(e);
		}
		if (game_assets_ && game_assets_->player) {
			const int px = game_assets_->player->pos.x;
			const int py = game_assets_->player->pos.y;
			game_assets_->update(*input_, px, py);
		}
		++frame_count;
		if (input_) input_->update();
		Uint32 elapsed = SDL_GetTicks() - start;
		if (elapsed < FRAME_MS) SDL_Delay(FRAME_MS - elapsed);
	}
}

static void run(SDL_Window* window, SDL_Renderer* renderer, int screen_w, int screen_h, bool rebuild_cache) {
    (void)window;
    while (true) {
        MainMenu menu(renderer, screen_w, screen_h);
        std::string chosen_map;
        SDL_Event e;
        bool choosing = true;
        while (choosing) {
            while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_QUIT) { chosen_map = "QUIT"; choosing = false; break; }
                    std::string result = menu.handle_event(e);
                    if (result == "QUIT") { chosen_map = "QUIT"; choosing = false; break; }
                    if (!result.empty()) { chosen_map = result; choosing = false; break; }
            }
            SDL_SetRenderTarget(renderer, nullptr);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            menu.render();
            SDL_RenderPresent(renderer);
            SDL_Delay(16);
        }
        if (chosen_map == "QUIT" || chosen_map.empty()) break;
        menu.showLoadingScreen();
        if (rebuild_cache) {
            std::cout << "[Main] Rebuilding asset cache...\n";
            RebuildAssets* rebuilder = new RebuildAssets(renderer, chosen_map);
            delete rebuilder;
            std::cout << "[Main] Asset cache rebuild complete.\n";
        }
        MenuUI app(renderer, screen_w, screen_h, chosen_map);
        app.init();
        if (app.wants_return_to_main_menu()) continue;
        break;
    }
}

int main(int argc, char* argv[]) {
	std::cout << "[Main] Starting game engine...\n";
	const bool rebuild_cache = (argc > 1 && argv[1] && std::string(argv[1]) == "-r");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
		std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n"; return 1;
	}
	if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
		std::cerr << "Mix_OpenAudio failed: " << Mix_GetError() << "\n"; SDL_Quit(); return 1;
	}
	if (TTF_Init() < 0) {
		std::cerr << "TTF_Init failed: " << TTF_GetError() << "\n"; SDL_Quit(); return 1;
	}
	if (!(IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_TIF | IMG_INIT_WEBP) &
	(IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_TIF | IMG_INIT_WEBP))) {
		std::cerr << "IMG_Init failed: " << IMG_GetError() << "\n"; SDL_Quit(); return 1;
	}
	SDL_Window* window = SDL_CreateWindow("Game Window", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP);
	if (!window) {
		std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
		IMG_Quit(); TTF_Quit(); SDL_Quit(); return 1;
	}
	SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if (!renderer) {
		std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
		SDL_DestroyWindow(window); IMG_Quit(); TTF_Quit(); SDL_Quit(); return 1;
	}
	SDL_RendererInfo info; SDL_GetRendererInfo(renderer, &info);
	std::cout << "[Main] Renderer: " << (info.name ? info.name : "Unknown") << "\n";
	int screen_width = 0, screen_height = 0;
	SDL_GetRendererOutputSize(renderer, &screen_width, &screen_height);
	std::cout << "[Main] Screen resolution: " << screen_width << "x" << screen_height << "\n";
	run(window, renderer, screen_width, screen_height, rebuild_cache);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	IMG_Quit(); TTF_Quit(); SDL_Quit();
	std::cout << "[Main] Game exited cleanly.\n";
	return 0;
}
