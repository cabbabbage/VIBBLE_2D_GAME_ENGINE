#include "main.hpp"
#include "utils/rebuild_assets.hpp"
#include "utils/text_style.hpp"
#include "ui/main_menu.hpp"
#include "ui/menu_ui.hpp"
#include "ui/tinyfiledialogs.h"
#include "asset_loader.hpp"
#include "asset/asset_types.hpp"
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
#include <optional>
#include <cctype>
#include <system_error>
namespace fs = std::filesystem;

#if defined(_WIN32)
extern "C" {
        __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
        __declspec(dllexport) int NvOptimusEnablement                = 0x00000001;
}
#endif

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
                        if (a.info && a.info->type == asset_types::player) { player_ptr = &a; break; }
                }
                int start_px = player_ptr ? player_ptr->pos.x : static_cast<int>(loader_->getMapRadius());
                int start_py = player_ptr ? player_ptr->pos.y : static_cast<int>(loader_->getMapRadius());
                game_assets_ = new Assets(std::move(all_assets), *loader_->getAssetLibrary(), player_ptr, loader_->getRooms(), screen_w_, screen_h_, start_px, start_py, static_cast<int>(loader_->getMapRadius() * 1.2), renderer_, map_path_);
                input_ = new Input();
                game_assets_->set_input(input_);
                if (!player_ptr) {
                        dev_mode_ = true;
                        std::cout << "[MainApp] No player asset found. Launching in Dev Mode.\n";
                }
                if (game_assets_) {
                        game_assets_->set_dev_mode(dev_mode_);
                }
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
		++frame_count;
		if (input_) input_->update();
		Uint32 elapsed = SDL_GetTicks() - start;
        if (elapsed < FRAME_MS) SDL_Delay(FRAME_MS - elapsed);
        }
}

namespace {

std::string trim_copy(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::optional<std::string> sanitize_map_name(const std::string& input) {
    std::string trimmed = trim_copy(input);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    std::string result;
    result.reserve(trimmed.size());
    for (char ch : trimmed) {
        unsigned char uc = static_cast<unsigned char>(ch);
        if (std::isalnum(uc) || ch == '_' || ch == '-') {
            result.push_back(ch);
        } else if (std::isspace(uc)) {
            return std::nullopt;
        } else {
            return std::nullopt;
        }
    }
    return result;
}

nlohmann::json build_default_map_info(const std::string& map_name) {
    constexpr int kSpawnRadius = 1500;
    const int diameter = kSpawnRadius * 2;

    nlohmann::json map_info;
    map_info["map_radius"] = kSpawnRadius;

    nlohmann::json layer;
    layer["level"] = 0;
    layer["radius"] = kSpawnRadius;
    layer["max_rooms"] = 1;
    nlohmann::json spawn_spec;
    spawn_spec["name"] = "spawn";
    spawn_spec["max_instances"] = 1;
    spawn_spec["required_children"] = nlohmann::json::array();
    layer["rooms"] = nlohmann::json::array({spawn_spec});
    map_info["map_layers"] = nlohmann::json::array({layer});

    nlohmann::json default_light = nlohmann::json::object({
        {"radius", 0},
        {"intensity", 255},
        {"orbit_radius", 0},
        {"update_interval", 10},
        {"mult", 0.0},
        {"fall_off", 100},
        {"min_opacity", 0},
        {"max_opacity", 255},
        {"base_color", nlohmann::json::array({255, 255, 255, 255})},
        {"keys", nlohmann::json::array({
            nlohmann::json::array({0.0, nlohmann::json::array({255, 255, 255, 255})})
        })}
    });

    map_info["map_assets_data"] = nlohmann::json::object();
    map_info["map_boundary_data"] = nlohmann::json::object();
    map_info["map_light_data"] = std::move(default_light);
    map_info["trails_data"] = nlohmann::json::object();

    nlohmann::json spawn_room;
    spawn_room["name"] = "spawn";
    spawn_room["geometry"] = "Circle";
    spawn_room["min_width"] = diameter;
    spawn_room["max_width"] = diameter;
    spawn_room["min_height"] = diameter;
    spawn_room["max_height"] = diameter;
    spawn_room["edge_smoothness"] = 2;
    spawn_room["is_spawn"] = true;
    spawn_room["is_boss"] = false;
    spawn_room["inherits_map_assets"] = false;
    spawn_room["spawn_groups"] = nlohmann::json::array();

    map_info["rooms_data"] = nlohmann::json::object();
    map_info["rooms_data"]["spawn"] = std::move(spawn_room);
    map_info["camera_settings"] = nlohmann::json::object();
    map_info["map_name"] = map_name;

    return map_info;
}

std::optional<std::string> create_new_map_interactively() {
    const fs::path maps_root{"MAPS"};
    try {
        if (!fs::exists(maps_root)) {
            fs::create_directories(maps_root);
        }
    } catch (const std::exception& ex) {
        std::string msg = std::string("Failed to access MAPS directory:\n") + ex.what();
        tinyfd_messageBox("Error", msg.c_str(), "ok", "error", 0);
        return std::nullopt;
    }

    while (true) {
        const char* response = tinyfd_inputBox("Create New Map", "Enter the name for your new map:", "");
        if (!response) {
            return std::nullopt;
        }

        auto sanitized = sanitize_map_name(response);
        if (!sanitized) {
            tinyfd_messageBox("Invalid Map Name",
                              "Map names may only contain letters, numbers, underscores, or hyphens.",
                              "ok",
                              "error",
                              0);
            continue;
        }

        fs::path map_dir = maps_root / *sanitized;
        if (fs::exists(map_dir)) {
            tinyfd_messageBox("Map Exists", "A map with that name already exists.", "ok", "error", 0);
            continue;
        }

        try {
            fs::create_directories(map_dir);
        } catch (const std::exception& ex) {
            std::string msg = std::string("Failed to create map directory:\n") + ex.what();
            tinyfd_messageBox("Error Creating Map", msg.c_str(), "ok", "error", 0);
            continue;
        }

        const nlohmann::json map_info = build_default_map_info(*sanitized);
        try {
            std::ofstream out(map_dir / "map_info.json");
            if (!out.is_open()) {
                throw std::runtime_error("Unable to open map_info.json for writing.");
            }
            out << map_info.dump(2);
        } catch (const std::exception& ex) {
            std::string msg = std::string("Failed to write map_info.json:\n") + ex.what();
            tinyfd_messageBox("Error Creating Map", msg.c_str(), "ok", "error", 0);
            std::error_code ec;
            fs::remove_all(map_dir, ec);
            continue;
        }

        return map_dir.string();
    }
}

} // namespace

void run(SDL_Window* window, SDL_Renderer* renderer, int screen_w, int screen_h, bool rebuild_cache) {
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
                    if (result == "CREATE_NEW_MAP") {
                        auto created = create_new_map_interactively();
                        if (created) {
                            chosen_map = *created;
                            choosing = false;
                        }
                        continue;
                    }
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
        if (SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2") != SDL_TRUE) {
                SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
        }
        std::cout << "[Main] Requested high quality texture filtering.\n";
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
