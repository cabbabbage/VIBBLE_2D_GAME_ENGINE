#include "main.hpp"
#include "utils/rebuild_assets.hpp"
#include "utils/text_style.hpp"
#include "ui/main_menu.hpp"
#include "ui/menu_ui.hpp"
#include "ui/tinyfiledialogs.h"
#include "ui/loading_screen.hpp"
#include "core/manifest/manifest_loader.hpp"
#include "asset_loader.hpp"
#include "asset/asset_types.hpp"
#include "asset/asset_library.hpp"
#include "scene_renderer.hpp"
#include "AssetsManager.hpp"
#include "input.hpp"
#include "core/manifest/manifest_loader.hpp"
#include "audio/audio_engine.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "utils/loading_status_notifier.hpp"
#include "render/precomputed_light_map.hpp"
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_ttf.h>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <optional>
#include <iomanip>
#include <cctype>
#include <system_error>
#include <utility>
#include <stdexcept>
namespace fs = std::filesystem;

#if defined(_WIN32)
extern "C" {
        __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
        __declspec(dllexport) int NvOptimusEnablement                = 0x00000001;
}
#endif

MainApp::MainApp(MapDescriptor map,
                 SDL_Renderer* renderer,
                 int screen_w,
                 int screen_h,
                 LoadingScreen* loading_screen,
                 AssetLibrary* asset_library)
: map_descriptor_(std::move(map)),
  map_path_(map_descriptor_.id),
  renderer_(renderer),
  screen_w_(screen_w),
  screen_h_(screen_h),
  loading_screen_(loading_screen),
  asset_library_(asset_library) {}

MainApp::~MainApp() {
        AudioEngine::instance().shutdown();
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
        std::unique_ptr<loading_status::ScopedNotifier> notifier;
        if (loading_screen_) {
                loading_screen_->set_status("Loading assets");
                loading_screen_->draw_frame();
                SDL_RenderPresent(renderer_);
                SDL_PumpEvents();
                notifier = std::make_unique<loading_status::ScopedNotifier>([this](const std::string& status) {
                        if (!loading_screen_ || !renderer_) {
                                return;
                        }
                        loading_screen_->set_status(status);
                        loading_screen_->draw_frame();
                        SDL_RenderPresent(renderer_);
                        SDL_PumpEvents();
                });
        }
        try {
                nlohmann::json map_manifest_json = nlohmann::json::object();
                std::string content_root;
                const std::string map_identifier = map_path_;

                manifest::ManifestData manifest_data = manifest::load_manifest();
                auto map_it = manifest_data.maps.find(map_identifier);
                if (map_it == manifest_data.maps.end() || !map_it.value().is_object()) {
                        throw std::runtime_error("Map '" + map_identifier + "' not found in manifest.");
                }

                map_manifest_json = map_it.value();

                auto root_it = map_manifest_json.find("content_root");
                if (root_it != map_manifest_json.end() && root_it->is_string()) {
                        fs::path resolved_root = root_it->get<std::string>();
                        if (resolved_root.is_relative()) {
                                fs::path manifest_root = fs::path(manifest::manifest_path()).parent_path();
                                resolved_root = manifest_root / resolved_root;
                        }
                        content_root = resolved_root.lexically_normal().string();
                }

                if (!map_manifest_json.is_object()) {
                        map_manifest_json = nlohmann::json::object();
                }

                if (!content_root.empty()) {
                        fs::path resolved_root = fs::path(content_root);
                        if (resolved_root.is_relative()) {
                                fs::path manifest_root = fs::path(manifest::manifest_path()).parent_path();
                                resolved_root = manifest_root / resolved_root;
                        }
                        content_root = resolved_root.lexically_normal().string();
                }

                loader_ = std::make_unique<AssetLoader>(map_identifier,
                                                       map_manifest_json,
                                                       renderer_,
                                                       content_root,
                                                       nullptr,
                                                       asset_library_);
                loading_status::notify("Spawning assets");
                auto spawn_begin = std::chrono::steady_clock::now();
                auto all_assets = loader_->createAssets();
                const auto asset_count = all_assets.size();
                const auto room_count = loader_->getRooms().size();
                Asset* player_ptr = nullptr;
                for (auto& a : all_assets) {
                        Asset* candidate = a.get();
                        if (candidate && candidate->info && candidate->info->type == asset_types::player) { player_ptr = candidate; break; }
                }
                int start_px = player_ptr ? player_ptr->pos.x : static_cast<int>(loader_->getMapRadius());
                int start_py = player_ptr ? player_ptr->pos.y : static_cast<int>(loader_->getMapRadius());
                AssetLibrary* active_library = loader_->getAssetLibrary();
                if (!active_library) {
                        throw std::runtime_error("Asset library unavailable during game setup.");
                }
                std::unique_ptr<PrecomputedLightMap> precomputed_light_map = loader_->take_precomputed_light_map();
                game_assets_ = new Assets(std::move(all_assets),
                                          *active_library,
                                          player_ptr,
                                          loader_->getRooms(),
                                          screen_w_,
                                          screen_h_,
                                          start_px,
                                          start_py,
                                          static_cast<int>(loader_->getMapRadius() * 1.2),
                                          renderer_,
                                          loader_->map_identifier(),
                                          loader_->map_manifest(),
                                          loader_->content_root(),
                                          std::move(precomputed_light_map));
                const double spawn_seconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - spawn_begin).count() / 1000.0;
                std::ostringstream init_summary;
                init_summary << "[Init] Assets initialized: " << asset_count
                             << " assets across " << room_count << " rooms in "
                             << std::fixed << std::setprecision(2) << spawn_seconds << "s";
                std::cout << init_summary.str() << "\n";
                input_ = new Input();
                game_assets_->set_input(input_);
                if (!player_ptr) {
                        dev_mode_ = true;
                        std::cout << "[MainApp] No player asset found. Launching in Dev Mode.\n";
                }
                if (game_assets_) {
                        game_assets_->set_dev_mode(dev_mode_);
                }
                AudioEngine::instance().update();
        } catch (const std::exception& e) {
                std::cerr << "[MainApp] Setup error: " << e.what() << "\n";
                throw;
        }
}

void MainApp::game_loop() {
        constexpr double TARGET_FPS = 60.0;
        constexpr double TARGET_FRAME_SECONDS = 1.0 / TARGET_FPS;
        const double perf_frequency = static_cast<double>(SDL_GetPerformanceFrequency());
        const double target_counts  = TARGET_FRAME_SECONDS * perf_frequency;
        bool quit = false;
        SDL_Event e;
        std::cout << "game loop started!\n";
        while (!quit) {
                const Uint64 frame_begin = SDL_GetPerformanceCounter();
                while (SDL_PollEvent(&e)) {
                        if (e.type == SDL_QUIT) quit = true;
			if (input_) input_->handleEvent(e);
			if (game_assets_) game_assets_->handle_sdl_event(e);
		}
                if (game_assets_ && input_) {
                        game_assets_->update(*input_);
                }
                if (input_) input_->update();
                const Uint64 frame_end = SDL_GetPerformanceCounter();
                const double work_counts = static_cast<double>(frame_end - frame_begin);
                if (work_counts < target_counts) {
                        double remaining_counts = target_counts - work_counts;
                        double remaining_ms = (remaining_counts * 1000.0) / perf_frequency;
                        if (remaining_ms >= 1.0) {
                                SDL_Delay(static_cast<Uint32>(remaining_ms));
                        }
                        while (static_cast<double>(SDL_GetPerformanceCounter() - frame_begin) < target_counts) {
                                SDL_Delay(0);
                        }
                }
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

    nlohmann::json layer;
    layer["level"] = 0;
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
        {"orbit_x", 0},
        {"orbit_y", 0},
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
    spawn_room["radius"] = kSpawnRadius;
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
    map_info["map_grid_settings"] = nlohmann::json::object({
        {"spacing", 100},
        {"jitter", 0}
    });
    map_info["map_name"] = map_name;

    return map_info;
}

std::optional<MapDescriptor> create_new_map_interactively() {
    const fs::path maps_root{"MAPS"};
    devmode::core::ManifestStore manifest_store;
    try {
        manifest_store.reload();
    } catch (const std::exception& ex) {
        std::string msg = std::string("Failed to load manifest:\n") + ex.what();
        tinyfd_messageBox("Error", msg.c_str(), "ok", "error", 0);
        return std::nullopt;
    }
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
            tinyfd_messageBox("Invalid Map Name", "Map names may only contain letters, numbers, underscores, or hyphens.", "ok", "error", 0);
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

        nlohmann::json map_info = build_default_map_info(*sanitized);
        map_info["content_root"] = map_dir.generic_string();

        if (!manifest_store.update_map_entry(*sanitized, map_info)) {
            tinyfd_messageBox("Error Creating Map", "Failed to update manifest for new map.", "ok", "error", 0);
            std::error_code ec;
            fs::remove_all(map_dir, ec);
            continue;
        }

        manifest_store.flush();

        MapDescriptor descriptor;
        descriptor.id   = *sanitized;
        descriptor.data = std::move(map_info);
        return descriptor;
    }
}

}

void run(SDL_Window* window, SDL_Renderer* renderer, int screen_w, int screen_h, bool rebuild_cache) {
    (void)window;

    manifest::ManifestData manifest_data;
    try {
        manifest_data = manifest::load_manifest();
    } catch (const std::exception& ex) {
        std::cerr << "[Main] Failed to load manifest: " << ex.what() << "\n";
        return;
    }

    std::shared_ptr<AssetLibrary> shared_asset_library = std::make_shared<AssetLibrary>(false);
    std::cout << "[Main] Preparing asset metadata cache...\n";
    shared_asset_library->load_all_from_SRC();
    std::cout << "[Main] Asset metadata cache ready for "
              << shared_asset_library->all().size() << " asset(s).\n";
    std::cout << "[Main] Loading cached asset resources...\n";
    shared_asset_library->loadAllAnimations(renderer);
    std::cout << "[Main] Cached asset resources loaded.\n";

    while (true) {
        MainMenu menu(renderer, screen_w, screen_h, manifest_data.maps);
        std::optional<MapDescriptor> chosen_map;
        bool quit_requested = false;
        SDL_Event e;
        bool choosing = true;
        while (choosing) {
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) {
                    quit_requested = true;
                    choosing = false;
                    break;
                }
                auto result = menu.handle_event(e);
                if (!result) {
                    continue;
                }
                if (result->id == "QUIT") {
                    quit_requested = true;
                    choosing = false;
                    break;
                }
                if (result->id == "CREATE_NEW_MAP") {
                    auto created = create_new_map_interactively();
                    if (created) {
                        chosen_map = std::move(*created);
                        choosing = false;
                    }
                    continue;
                }
                MapDescriptor descriptor;
                descriptor.id   = result->id;
                descriptor.data = result->data;
                chosen_map = std::move(descriptor);
                choosing = false;
                break;
            }
            SDL_SetRenderTarget(renderer, nullptr);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            menu.render();
            SDL_RenderPresent(renderer);
            SDL_Delay(16);
        }
        if (quit_requested || !chosen_map) break;

        MapDescriptor selected_map = std::move(*chosen_map);
        LoadingScreen loading_screen(renderer, screen_w, screen_h);
        loading_screen.init();
        loading_screen.set_status("Loading assets");
        loading_screen.draw_frame();
        SDL_RenderPresent(renderer);
        SDL_PumpEvents();
        if (rebuild_cache) {
            std::cout << "[Main] Rebuilding asset cache...\n";
            RebuildAssets* rebuilder = new RebuildAssets(renderer, selected_map.id);
            delete rebuilder;
            std::cout << "[Main] Asset cache rebuild complete.\n";
            std::cout << "[Main] Refreshing shared asset library after cache rebuild...\n";
            shared_asset_library->load_all_from_SRC();
            shared_asset_library->loadAllAnimations(renderer);
            std::cout << "[Main] Shared asset library refreshed.\n";
        }
        MenuUI app(renderer,
                   screen_w,
                   screen_h,
                   std::move(selected_map),
                   &loading_screen,
                   shared_asset_library.get());
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
