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
#include "render/render.hpp"
#include "AssetsManager.hpp"
#include "input.hpp"
#include "core/manifest/manifest_loader.hpp"
#include "audio/audio_engine.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "utils/loading_status_notifier.hpp"
#include "utils/rebuild_queue.hpp"
#include "world/world_grid.hpp"
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_ttf.h>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
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
#include <cmath>
#include <cstdlib>
#include "utils/log.hpp"

namespace fs = std::filesystem;

namespace {

nlohmann::json build_default_map_manifest(const std::string& map_name);
}

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
        vibble::log::info("[MainApp] Loading pipeline complete. Entering main loop...");
        game_loop();
}

void MainApp::setup() {
        std::srand(static_cast<unsigned int>(std::time(nullptr)));

        try {

                struct ScopedLogLevel {
                        vibble::log::Level prev;
                        explicit ScopedLogLevel(vibble::log::Level next)
                        : prev(vibble::log::level()) {
                                vibble::log::set_level(next);
                        }
                        ~ScopedLogLevel() {
                                vibble::log::set_level(prev);
                        }
};

                std::unique_ptr<ScopedLogLevel> loader_debug_guard;
                if (const char* v = std::getenv("VIBBLE_LOADER_DEBUG")) {
                        if (*v && (*v == '1' || *v == 'y' || *v == 'Y' ||
                                   *v == 't' || *v == 'T' ||
                                   std::tolower(static_cast<unsigned char>(*v)) == 'd')) {
                                loader_debug_guard = std::make_unique<ScopedLogLevel>(vibble::log::Level::Debug);
                                vibble::log::info("[MainApp] VIBBLE_LOADER_DEBUG enabled; log level set to DEBUG during loading.");
                        }
                }

                std::unique_ptr<loading_status::ScopedNotifier> scoped_loading_notifier;
                if (loading_screen_ && renderer_) {
                        scoped_loading_notifier = std::make_unique<loading_status::ScopedNotifier>(
                                [this](const std::string& status) {
                                        try {
                                                loading_screen_->set_status(status);
                                                loading_screen_->draw_frame();
                                                SDL_RenderPresent(renderer_);
                                        } catch (...) {

                                        }
                                        SDL_Event ev;
                                        while (SDL_PollEvent(&ev)) {

                                        }
                                });

                        loading_screen_->set_status("Preparing...");
                        loading_screen_->draw_frame();
                        SDL_RenderPresent(renderer_);
                        SDL_Event ev;
                        while (SDL_PollEvent(&ev)) {}
                }

                nlohmann::json map_manifest_json = nlohmann::json::object();
                std::string content_root;
                const std::string map_identifier = map_descriptor_.id.empty() ? map_path_ : map_descriptor_.id;

                manifest::ManifestData manifest_data = manifest::load_manifest();
                bool manifest_entry_found = false;
                if (manifest_data.maps.is_object()) {
                        auto map_it = manifest_data.maps.find(map_identifier);
                        if (map_it != manifest_data.maps.end() && map_it.value().is_object()) {
                                map_manifest_json = map_it.value();
                                manifest_entry_found = true;
                        }
                }

                if (!manifest_entry_found) {
                        if (map_descriptor_.data.is_object() && !map_descriptor_.data.empty()) {
                                vibble::log::warn(std::string("[MainApp] Map '") + map_identifier + "' missing from manifest. Using descriptor payload.");
                                map_manifest_json = map_descriptor_.data;
                        } else {
                                vibble::log::warn(std::string("[MainApp] Map '") + map_identifier + "' missing from manifest. Generating default map manifest.");
                                map_manifest_json = build_default_map_manifest(map_identifier);
                        }
                }

                if (!map_manifest_json.is_object()) {
                        map_manifest_json = nlohmann::json::object();
                }

                fs::path manifest_root = fs::path(manifest::manifest_path()).parent_path();
                fs::path relative_content_root;
                auto root_it = map_manifest_json.find("content_root");
                if (root_it != map_manifest_json.end() && root_it->is_string()) {
                        const std::string& value = root_it->get_ref<const std::string&>();
                        if (!value.empty()) {
                                relative_content_root = fs::path(value);
                        }
                }

                bool manifest_updated = !manifest_entry_found;
                if (relative_content_root.empty()) {
                        relative_content_root = fs::path("content") / map_identifier;
                        map_manifest_json["content_root"] = relative_content_root.generic_string();
                        manifest_updated = true;
                        vibble::log::warn(std::string("[MainApp] No content_root for map '") + map_identifier + "'. Using default '" + relative_content_root.generic_string() + "'.");
                }

                fs::path resolved_root = relative_content_root;
                if (resolved_root.is_relative()) {
                        resolved_root = manifest_root / resolved_root;
                }
                resolved_root = resolved_root.lexically_normal();

                std::error_code dir_error;
                fs::create_directories(resolved_root, dir_error);
                if (dir_error) {
                        std::ostringstream oss;
                        oss << "Failed to prepare content root '" << resolved_root.string() << "': " << dir_error.message();
                        throw std::runtime_error(oss.str());
                }
                content_root = resolved_root.string();

                if (manifest_updated) {
                        try {
                                devmode::core::ManifestStore store;
                                store.reload();
                                if (!store.update_map_entry(map_identifier, map_manifest_json)) {
                                        vibble::log::warn(std::string("[MainApp] Failed to persist manifest entry for '") + map_identifier + "'.");
                                } else {
                                        store.flush();
                                }
                        } catch (const std::exception& ex) {
                                vibble::log::warn(std::string("[MainApp] Unable to persist manifest entry for '") + map_identifier + "': " + ex.what());
                        }
                }

                render_pipeline::ScalingProfileBuildOptions scaling_options;
                if (screen_w_ > 0 && screen_h_ > 0) {
                        scaling_options.screen_aspect =
                                static_cast<double>(screen_w_) / static_cast<double>(screen_h_);
                }
                scaling_options.asset_library = const_cast<const AssetLibrary*>(asset_library_);
                try {

                        const bool has_any_assets = asset_library_ && !asset_library_->all().empty();
                        if (has_any_assets) {
                                render_pipeline::BuildScalingProfiles(scaling_options);
                        } else {
                                vibble::log::info("[MainApp] No assets detected; skipping scaling profile build.");
                        }
                } catch (const std::exception& ex) {
                        vibble::log::warn(std::string("[MainApp] Scaling profile build skipped due to error: ") + ex.what());
                } catch (...) {
                        vibble::log::warn("[MainApp] Scaling profile build skipped due to unknown error.");
                }

                vibble::log::info("[MainApp] Constructing AssetLoader...");
                auto loader_begin = std::chrono::steady_clock::now();
                loader_ = std::make_unique<AssetLoader>( map_identifier, map_manifest_json, renderer_, content_root, nullptr, asset_library_);
                auto loader_end = std::chrono::steady_clock::now();
                vibble::log::info( std::string("[MainApp] AssetLoader constructed in ") + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>( loader_end - loader_begin) .count()) + "ms");

                loading_status::notify("Spawning assets");
                auto spawn_begin = std::chrono::steady_clock::now();
                world::WorldGrid world_grid{};
                loader_->createAssets(world_grid);
                auto all_assets = world_grid.all_assets();
                vibble::log::info(std::string("[MainApp] Asset spawning finished for map '") + map_identifier + "'.");
                vibble::log::info(std::string("[MainApp] ") + std::to_string(all_assets.size()) + " assets created and cached.");

                if (all_assets.size() > 200000) {
                        vibble::log::error(std::string("[MainApp] Asset count ") + std::to_string(all_assets.size()) + " exceeds limit (200000). Aborting to avoid instability.");
                        throw std::runtime_error("Asset count exceeds 200000; aborting.");
                }

                const auto asset_count = all_assets.size();
                const auto room_count = loader_->getRooms().size();

                Asset* player_ptr = nullptr;
                for (Asset* candidate : all_assets) {
                        if (candidate && candidate->info &&
                            candidate->info->type == asset_types::player) {
                                player_ptr = candidate;
                                break;
                        }
                }

                int start_px = player_ptr ? player_ptr->pos.x
                                          : static_cast<int>(loader_->getMapRadius());
                int start_py = player_ptr ? player_ptr->pos.y
                                          : static_cast<int>(loader_->getMapRadius());

                AssetLibrary* active_library = loader_->getAssetLibrary();
                if (!active_library) {
                        throw std::runtime_error("Asset library unavailable during game setup.");
                }

                game_assets_ = new Assets( *active_library, player_ptr, loader_->getRooms(), screen_w_, screen_h_, start_px, start_py, static_cast<int>(loader_->getMapRadius() * 1.2), renderer_, loader_->map_identifier(), loader_->map_manifest(), loader_->content_root(), std::move(world_grid));

                const double spawn_seconds =
                        std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - spawn_begin) .count() / 1000.0;

                std::ostringstream init_summary;
                init_summary << "[Init] Assets initialized: " << asset_count
                             << " assets across " << room_count << " rooms in "
                             << std::fixed << std::setprecision(2) << spawn_seconds << "s";
                vibble::log::info(init_summary.str());

                input_ = new Input();
                game_assets_->set_input(input_);
                if (!player_ptr) {
                        dev_mode_ = true;
                        vibble::log::warn("[MainApp] No player asset found. Launching in Dev Mode.");
                }
                if (game_assets_) {
                        game_assets_->set_dev_mode(dev_mode_);
                }
                AudioEngine::instance().update();
        } catch (const std::exception& e) {
                vibble::log::error(std::string("[MainApp] Setup error: ") + e.what());
                throw;
        }
}

void MainApp::game_loop() {
        constexpr double TARGET_FPS = 60.0;
        constexpr double TARGET_FRAME_SECONDS = 1.0 / TARGET_FPS;
        const double perf_frequency = static_cast<double>(SDL_GetPerformanceFrequency());
        const double target_counts  = TARGET_FRAME_SECONDS * perf_frequency;

        double idle_counts_accum = 0.0;
        int idle_frame_counter   = 0;
        constexpr int IDLE_REPORT_INTERVAL = 120;
        bool quit = false;
        SDL_Event e;

        vibble::log::info("[MainApp] Game loop started.");

        if (game_assets_) {
                if (LightMap* map = game_assets_->light_map()) {
                        map->present_static_previews(renderer_);
                }
        }

        while (!quit) {
                const Uint64 frame_begin = SDL_GetPerformanceCounter();

                while (SDL_PollEvent(&e)) {
                        if (e.type == SDL_QUIT) {
                                quit = true;
                        }
                        if (input_) {
                                input_->handleEvent(e);
                        }
                        if (game_assets_) {
                                game_assets_->handle_sdl_event(e);
                        }
                }

                if (game_assets_ && input_) {
                        game_assets_->update(*input_);
                }
                if (input_) {
                        input_->update();
                }

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
                        const double total_idle_ms =
                                (idle_counts_accum * 1000.0) / perf_frequency;
                        const double average_idle_ms =
                                total_idle_ms / static_cast<double>(idle_frame_counter);
                        vibble::log::debug( "[MainApp] Idle pacing: total " + std::to_string(total_idle_ms) + "ms over " + std::to_string(idle_frame_counter) + " frame(s); avg " + std::to_string(average_idle_ms) + "ms.");
                        idle_counts_accum = 0.0;
                        idle_frame_counter = 0;
                }
        }
}

namespace {

std::string trim_copy(const std::string& value) {
    size_t start = 0;
    while (start < value.size() &&
           std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
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

nlohmann::json build_default_map_manifest(const std::string& map_name) {
    constexpr int kSpawnRadius = 1500;
    const int diameter = kSpawnRadius * 2;

    auto spawn_id_for = [&](const std::string& suffix) {
        std::string cleaned = map_name;
        for (char& ch : cleaned) {
            if (std::isspace(static_cast<unsigned char>(ch))) {
                ch = '_';
            }
        }
        return std::string("spn-") + cleaned + "-" + suffix;
};

    auto make_room_spawn_group = [&](const std::string& display_name,
                                     const std::string& asset_name) {
        nlohmann::json group;
        group["display_name"] = display_name;
        group["spawn_id"] = spawn_id_for(display_name);
        group["position"] = "Exact";
        group["priority"] = 0;
        group["dx"] = 0;
        group["dy"] = 0;
        group["enforce_spacing"] = false;
        group["explicit_flip"] = false;
        group["force_flipped"] = false;
        group["locked"] = false;
        group["min_number"] = 1;
        group["max_number"] = 1;
        group["origional_height"] = diameter;
        group["origional_width"] = diameter;
        group["resolution"] = 6;
        group["resolve_geometry_to_room_size"] = true;
        group["resolve_quantity_to_room_size"] = false;
        group["candidates"] = nlohmann::json::array({
            nlohmann::json::object({{"name", "null"},   {"chance", 0}}),
            nlohmann::json::object({{"name", asset_name}, {"chance", 100}})
        });
        return group;
};

    auto make_batch_spawn_group = [&](const std::string& suffix,
                                      const std::string& display_name) {
        nlohmann::json group;
        group["display_name"] = display_name;
        group["spawn_id"] = spawn_id_for(suffix);
        group["position"] = "Random";
        group["priority"] = 0;
        group["min_number"] = 0;
        group["max_number"] = 0;
        group["enforce_spacing"] = false;
        group["grid_resolution"] = 6;
        group["resolution"] = 0;
        group["resolve_geometry_to_room_size"] = false;
        group["resolve_quantity_to_room_size"] = false;
        group["candidates"] = nlohmann::json::array({
            nlohmann::json::object({{"name", "null"}, {"chance", 100}})
        });
        return group;
};

    nlohmann::json map_info;

    nlohmann::json layer;
    layer["name"] = "layer_0";
    layer["level"] = 0;
    layer["min_rooms"] = 1;
    layer["max_rooms"] = 1;
    nlohmann::json spawn_spec;
    spawn_spec["name"] = "spawn";
    spawn_spec["min_instances"] = 1;
    spawn_spec["max_instances"] = 1;
    spawn_spec["required_children"] = nlohmann::json::array();
    layer["rooms"] = nlohmann::json::array({spawn_spec});
    map_info["map_layers"] = nlohmann::json::array({layer});

    map_info["map_assets_data"] = nlohmann::json::object({
        {"spawn_groups",
         nlohmann::json::array({ make_batch_spawn_group("map_assets",
                                                        "batch_map_assets") })}
    });
    map_info["map_boundary_data"] = nlohmann::json::object({
        {"inherits_map_assets", false},
        {"spawn_groups",
         nlohmann::json::array({ make_batch_spawn_group("map_boundary",
                                                        "batch_map_boundary") })}
    });
    map_info["reactive_shadows"] = nlohmann::json::object({
        {"frame_blend_falloff_frames", 15},
        {"opacity_sensitivity_percent", 100.0},
        {"opacity_strength", 1.0},
        {"sampling_weights", nlohmann::json::object({
            {"dynamic_weight", 1.0},
            {"static_weight", 0.0}
        })},
        {"shadow_lut", nlohmann::json::array({
            nlohmann::json::object({
                {"brightness", 0.0},
                {"offset", 0.0},
                {"opacity", 1.0},
                {"scale", 1.0}
            })
        })}
    });
    map_info["trails_data"] = nlohmann::json::object({
        {"basic", nlohmann::json::object({
            {"name", "basic"},
            {"display_color", nlohmann::json::array({85, 242, 143, 255})},
            {"edge_smoothness", 2},
            {"geometry", "Line"},
            {"inherits_map_assets", false},
            {"is_spawn", false},
            {"is_boss", false},
            {"min_width", 400},
            {"max_width", 800},
            {"min_height", 400},
            {"max_height", 800},
            {"spawn_groups", nlohmann::json::array()}
        })}
    });

    map_info["map_layers_settings"] = nlohmann::json::object({
        {"min_edge_distance", 200}
    });

    nlohmann::json spawn_room;
    spawn_room["name"] = "spawn";
    spawn_room["geometry"] = "Circle";
    spawn_room["radius"] = kSpawnRadius;
    spawn_room["min_radius"] = kSpawnRadius;
    spawn_room["max_radius"] = kSpawnRadius;
    spawn_room["min_width"] = diameter;
    spawn_room["max_width"] = diameter;
    spawn_room["min_height"] = diameter;
    spawn_room["max_height"] = diameter;
    spawn_room["edge_smoothness"] = 2;
    spawn_room["curvyness"] = 2;
    spawn_room["is_spawn"] = true;
    spawn_room["is_boss"] = false;
    spawn_room["inherits_map_assets"] = true;
    spawn_room["display_color"] = nlohmann::json::array({120, 170, 235, 255});
    spawn_room["areas"] = nlohmann::json::array({
        nlohmann::json::object({
            {"name", "spawn_center"},
            {"type", "spawning"},
            {"kind", "Spawn"},
            {"resolution", 3},
            {"points", nlohmann::json::array({
                nlohmann::json::object({{"x", -256}, {"y", -256}}),
                nlohmann::json::object({{"x", 256}, {"y", -256}}),
                nlohmann::json::object({{"x", 256}, {"y", 256}}),
                nlohmann::json::object({{"x", -256}, {"y", 256}})
            })}
        })
    });
    spawn_room["spawn_groups"] = nlohmann::json::array({
        make_room_spawn_group("Vibble", "Vibble")
    });

    map_info["rooms_data"] = nlohmann::json::object();
    map_info["rooms_data"]["spawn"] = std::move(spawn_room);
    map_info["camera_settings"] = nlohmann::json::object({
        {"render_quality_percent", 80},
        {"smooth_motion_zoom", true},
        {"zoom_low", 0.75},
        {"zoom_high", 3.0},
        {"base_height_px", 720.0},
        {"motion_smoothing_method", 1},
        {"motion_smoothing_max_step", 8000.0},
        {"motion_smoothing_snap_threshold", 0.25},
        {"min_visible_screen_ratio", 0.01}
    });
    map_info["map_grid_settings"] = nlohmann::json::object({
        {"resolution", 6},
        {"spacing", 100},
        {"jitter", 0},
        {"r_chunk", 6},
        {"chunk_size", 64}
    });
    map_info["audio"] = nlohmann::json::object({
        {"music", nlohmann::json::object({
            {"content_root", (fs::path("content") / map_name / "music").generic_string()},
            {"tracks", nlohmann::json::array()}
        })}
    });
    map_info["map_name"] = map_name;

    return map_info;
}

std::optional<MapDescriptor> create_new_map_interactively() {
    devmode::core::ManifestStore manifest_store;
    try {
        manifest_store.reload();
    } catch (const std::exception& ex) {
        std::string msg = std::string("Failed to load manifest:\n") + ex.what();
        tinyfd_messageBox("Error", msg.c_str(), "ok", "error", 0);
        return std::nullopt;
    }

    while (true) {
        const char* response =
            tinyfd_inputBox("Create New Map", "Enter the name for your new map:", "");
        if (!response) {
            return std::nullopt;
        }

        auto sanitized = sanitize_map_name(response);
        if (!sanitized) {
            tinyfd_messageBox("Invalid Map Name", "Map names may only contain letters, numbers, underscores, or hyphens.", "ok", "error", 0);
            continue;
        }

        if (manifest_store.find_map_entry(*sanitized)) {
            tinyfd_messageBox("Map Exists", "A map with that name already exists.", "ok", "error", 0);
            continue;
        }

        nlohmann::json map_info = build_default_map_manifest(*sanitized);

        fs::path manifest_root;
        try {
            manifest_root = fs::absolute(fs::path(manifest::manifest_path()).parent_path());
        } catch (const std::exception& ex) {
            std::string msg = std::string("Unable to determine project root: ") + ex.what();
            tinyfd_messageBox("Error", msg.c_str(), "ok", "error", 0);
            continue;
        }

        std::error_code dir_error;
        fs::path content_root = manifest_root / "content";
        fs::create_directories(content_root, dir_error);
        if (dir_error) {
            std::string msg = std::string("Failed to prepare content folder: ") + dir_error.message();
            tinyfd_messageBox("Error", msg.c_str(), "ok", "error", 0);
            continue;
        }

        fs::path map_dir = content_root / *sanitized;
        dir_error.clear();
        fs::create_directories(map_dir, dir_error);
        if (dir_error) {
            std::string msg = std::string("Failed to create map folder: ") + dir_error.message();
            tinyfd_messageBox("Error", msg.c_str(), "ok", "error", 0);
            continue;
        }
        dir_error.clear();
        fs::create_directories(map_dir / "music", dir_error);

        map_info["content_root"] =
            (fs::path("content") / *sanitized).generic_string();
        auto audio_it = map_info.find("audio");
        if (audio_it == map_info.end() || !audio_it->is_object()) {
            map_info["audio"] = nlohmann::json::object();
            audio_it = map_info.find("audio");
        }
        nlohmann::json& audio_section = (*audio_it);
        nlohmann::json& music_section = audio_section["music"];
        if (!music_section.is_object()) {
            music_section = nlohmann::json::object();
        }
        music_section["content_root"] =
            (fs::path(map_info["content_root"].get<std::string>()) / "music") .generic_string();
        if (!music_section.contains("tracks") ||
            !music_section["tracks"].is_array()) {
            music_section["tracks"] = nlohmann::json::array();
        }

        if (!manifest_store.update_map_entry(*sanitized, map_info)) {
            tinyfd_messageBox("Error Creating Map", "Failed to update manifest for new map.", "ok", "error", 0);
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

void run(SDL_Window* window,
         SDL_Renderer* renderer,
         int screen_w,
         int screen_h,
         bool rebuild_cache) {
    (void)window;

    if (renderer) {
        SDL_SetRenderTarget(renderer, nullptr);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {}
    }

    manifest::ManifestData manifest_data;
    try {
        manifest_data = manifest::load_manifest();

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {}
    } catch (const std::exception& ex) {
        vibble::log::error(std::string("[Main] Failed to load manifest: ") + ex.what());
        return;
    }

    std::shared_ptr<AssetLibrary> shared_asset_library =
        std::make_shared<AssetLibrary>(false);
    vibble::log::info("[Main] Preparing asset metadata cache...");
    shared_asset_library->load_all_from_SRC();
    { SDL_Event ev; while (SDL_PollEvent(&ev)) {} }
    vibble::log::info(std::string("[Main] Asset metadata cache ready for ") + std::to_string(shared_asset_library->all().size()) + " asset(s).");
    vibble::log::info("[Main] Loading cached asset resources...");
    shared_asset_library->loadAllAnimations(renderer);
    { SDL_Event ev; while (SDL_PollEvent(&ev)) {} }
    vibble::log::info("[Main] Cached asset resources loaded.");

    while (true) {
        MainMenu menu(renderer, screen_w, screen_h, manifest_data.maps);
        vibble::log::info("[Main] Main menu displayed.");
        std::optional<MapDescriptor> chosen_map;
        bool quit_requested = false;
        bool should_show_loading_screen = false;
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
                        vibble::log::info(std::string("[Main] New map created and selected: ") + chosen_map->id);
                        choosing = false;
                        should_show_loading_screen = true;
                    }
                    continue;
                }
                MapDescriptor descriptor;
                descriptor.id   = result->id;
                descriptor.data = result->data;
                chosen_map = std::move(descriptor);
                vibble::log::info(std::string("[Main] Map selected: ") + chosen_map->id);
                choosing = false;
                should_show_loading_screen = true;
                break;
            }
            if (!choosing) {
                break;
            }
            SDL_SetRenderTarget(renderer, nullptr);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            menu.render();
            SDL_RenderPresent(renderer);
            SDL_Delay(16);
        }

        if (should_show_loading_screen) {
            menu.showLoadingScreen();
        }
        if (quit_requested || !chosen_map) {
            break;
        }

        MapDescriptor selected_map = std::move(*chosen_map);
        LoadingScreen loading_screen(renderer, screen_w, screen_h);
        loading_screen.init();

        if (rebuild_cache) {
            vibble::log::info("[Main] Rebuilding asset cache...");
            RebuildAssets* rebuilder = new RebuildAssets(renderer, selected_map.id);
            delete rebuilder;
            vibble::log::info("[Main] Asset cache rebuild complete.");
            vibble::log::info("[Main] Refreshing shared asset library after cache rebuild...");
            shared_asset_library->load_all_from_SRC();
            shared_asset_library->loadAllAnimations(renderer);
            vibble::log::info("[Main] Shared asset library refreshed.");
        }

        MenuUI app(renderer, screen_w, screen_h, std::move(selected_map), &loading_screen, shared_asset_library.get());
        app.init();
        if (app.wants_return_to_main_menu()) {
            continue;
        }
        break;
    }
}

int main(int argc, char* argv[]) {
        vibble::log::info("[Main] Starting game engine...");
        const bool rebuild_cache =
                (argc > 1 && argv[1] && std::string(argv[1]) == "-r");

        vibble::RebuildQueueCoordinator rebuild_queue;
        if (rebuild_cache) {
                vibble::log::info("[Main] -r detected; queueing full asset/light rebuild.");
                rebuild_queue.request_full_asset_rebuild();
                rebuild_queue.request_full_light_rebuild();
        }

        if (!rebuild_queue.validate_manifest_cache()) {
                vibble::log::warn("[Main] Cache validation step failed.");
        }

        if (rebuild_queue.has_pending_asset_work()) {
                vibble::log::info("[Main] Processing queued asset rebuilds via asset_tool.py...");
                if (rebuild_queue.run_asset_tool()) {
                        vibble::log::info("[Main] Asset rebuilds completed.");
                } else {
                        vibble::log::warn("[Main] asset_tool.py reported an error.");
                }
        } else {
                vibble::log::info("[Main] No queued asset rebuilds detected.");
        }

        if (rebuild_queue.has_pending_light_work()) {
                vibble::log::info("[Main] Processing queued light rebuilds via light_tool.py...");
                if (rebuild_queue.run_light_tool()) {
                        vibble::log::info("[Main] Light rebuilds completed.");
                } else {
                        vibble::log::warn("[Main] light_tool.py reported an error.");
                }
        } else {
                vibble::log::info("[Main] No queued light rebuilds detected.");
        }

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
                vibble::log::error(std::string("SDL_Init failed: ") + SDL_GetError());
                return 1;
        }

        if (SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best") != SDL_TRUE) {
                if (SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2") != SDL_TRUE) {
                        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
                }
        }
        vibble::log::info("[Main] Requested high quality texture filtering.");

        if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
                vibble::log::error(std::string("Mix_OpenAudio failed: ") + Mix_GetError());
                SDL_Quit();
                return 1;
        }

        if (TTF_Init() < 0) {
                vibble::log::error(std::string("TTF_Init failed: ") + TTF_GetError());
                SDL_Quit();
                return 1;
        }

        if (!(IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_TIF | IMG_INIT_WEBP) &
              (IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_TIF | IMG_INIT_WEBP))) {
                vibble::log::error(std::string("IMG_Init failed: ") + IMG_GetError());
                SDL_Quit();
                return 1;
        }

        SDL_Window* window = SDL_CreateWindow( "Game Window", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP);
        if (!window) {
                vibble::log::error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
                IMG_Quit();
                TTF_Quit();
                SDL_Quit();
                return 1;
        }

        SDL_Renderer* renderer =
                SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) {
                vibble::log::error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
                SDL_DestroyWindow(window);
                IMG_Quit();
                TTF_Quit();
                SDL_Quit();
                return 1;
        }

        SDL_RendererInfo info;
        SDL_GetRendererInfo(renderer, &info);
        vibble::log::info(std::string("[Main] Renderer: ") + (info.name ? info.name : "Unknown"));

        int screen_width = 0;
        int screen_height = 0;
        SDL_GetRendererOutputSize(renderer, &screen_width, &screen_height);
        vibble::log::info(std::string("[Main] Screen resolution: ") + std::to_string(screen_width) + "x" + std::to_string(screen_height));

        run(window, renderer, screen_width, screen_height, rebuild_cache);

        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        IMG_Quit();
        TTF_Quit();
        SDL_Quit();
        vibble::log::info("[Main] Game exited cleanly.");
        return 0;
}
