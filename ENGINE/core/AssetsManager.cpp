#include "AssetsManager.hpp"
#include "asset/initialize_assets.hpp"

#include "find_current_room.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_utils.hpp"
#include "audio/audio_engine.hpp"
#include "dev_mode/dev_controls.hpp"
#include "render/scene_renderer.hpp"
#include "render_pipeline/ScalingLogic.hpp"
#include "map_generation/room.hpp"
#include "utils/area.hpp"
#include "utils/input.hpp"
#include "utils/range_util.hpp"
#include "utils/text_style.hpp"
#include "utils/map_grid_settings.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>
#include <vector>
#include <unordered_set>
#include <SDL.h>
#include <SDL_ttf.h>

namespace {

void dev_mode_trace(const std::string& message) {
    try {
        std::ofstream log("dev_mode_trace.log", std::ios::app);
        log << message << '\n';
    } catch (...) {
        // Swallow logging errors; tracing must never throw.
    }
}

struct SDLTextureDeleter {
    void operator()(SDL_Texture* texture) const {
        if (texture) {
            SDL_DestroyTexture(texture);
        }
    }
};

struct SDLSurfaceDeleter {
    void operator()(SDL_Surface* surface) const {
        if (surface) {
            SDL_FreeSurface(surface);
        }
    }
};

TTF_Font* scaling_notice_font() {
    static std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)> font(nullptr, TTF_CloseFont);
    if (!font) {
        const TextStyle& style = TextStyles::MediumMain();
        font.reset(style.open_font());
    }
    return font.get();
}

}

Assets::Assets(std::vector<Asset>&& loaded,
               AssetLibrary& library,
               Asset*,
               std::vector<Room*> rooms,
               int screen_width_,
               int screen_height_,
               int screen_center_x,
               int screen_center_y,
               int map_radius,
               SDL_Renderer* renderer,
               const std::string& map_id,
               const nlohmann::json& map_manifest,
               std::string content_root)
    : camera_(
          screen_width_,
          screen_height_,
          Area(
              "starting_camera",
              std::vector<SDL_Point>{

                  SDL_Point{-10,-10},
                  SDL_Point{ 10,-10},
                  SDL_Point{ 10,10},
                  SDL_Point{-10, 10}
              })
      ),
      screen_width(screen_width_),
      screen_height(screen_height_),
      library_(library),
      map_id_(map_id),
      map_path_(std::move(content_root)),
      map_info_path_(map_path_.empty() ? std::string{} : (map_path_ + "/map_info.json"))
{
    map_info_json_ = map_manifest;
    if (!map_info_json_.is_object()) {
        map_info_json_ = nlohmann::json::object();
    }

    hydrate_map_info_sections();
    load_camera_settings_from_json();

    InitializeAssets::initialize(*this, std::move(loaded), std::move(rooms), screen_width_, screen_height_, screen_center_x, screen_center_y, map_radius);

    finder_ = new CurrentRoomFinder(rooms_, player);
    if (finder_) {
        camera_.set_up_rooms(finder_);
    }

    scene = new SceneRenderer(renderer, this, screen_width_, screen_height_, map_info_json_, map_id_);
    apply_map_light_config();
    for (Asset* a : all) {
        if (a) a->set_assets(this);
    }

    update_filtered_active_assets();

}

std::vector<const Room::NamedArea*> Assets::current_room_trigger_areas() const {
    std::vector<const Room::NamedArea*> result;
    if (!current_room_) {
        return result;
    }

    const auto is_trigger_string = [](const std::string& value) {
        if (value.empty()) {
            return false;
        }
        std::string lowered;
        lowered.reserve(value.size());
        for (unsigned char ch : value) {
            lowered.push_back(static_cast<char>(std::tolower(ch)));
        }
        if (lowered == "trigger") {
            return true;
        }
        return lowered.find("trigger") != std::string::npos;
};

    for (const auto& entry : current_room_->areas) {
        if (!entry.area) {
            continue;
        }
        if (is_trigger_string(entry.kind) ||
            is_trigger_string(entry.type) ||
            is_trigger_string(entry.name)) {
            result.push_back(&entry);
        }
    }

    return result;
}

void Assets::save_map_info_json() {
    if (map_info_path_.empty()) {
        return;
    }
    write_camera_settings_to_json();
    std::ofstream out(map_info_path_);
    if (!out.is_open()) {
        std::cerr << "[Assets] Failed to write map_info.json at " << map_info_path_ << "\n";
        return;
    }
    try {
        out << map_info_json_.dump(2);
    } catch (const std::exception& e) {
        std::cerr << "[Assets] Failed to serialize map_info.json: " << e.what() << "\n";
    }
}

void Assets::hydrate_map_info_sections() {
    if (!map_info_json_.is_object()) {
        return;
    }

    const auto ensure_object = [&](const char* key) {
        auto it = map_info_json_.find(key);
        if (it == map_info_json_.end()) {
            map_info_json_[key] = nlohmann::json::object();
            return;
        }
        if (!it->is_object()) {
            std::cerr << "[Assets] map_info." << key << " expected to be an object. Resetting." << "\n";
            *it = nlohmann::json::object();
        }
};

    ensure_object("map_assets_data");
    ensure_object("map_boundary_data");
    ensure_object("map_light_data");
    ensure_object("rooms_data");
    ensure_object("trails_data");

    ensure_map_grid_settings(map_info_json_);

    {
        nlohmann::json& L = map_info_json_["map_light_data"];
        if (!L.is_object()) {
            map_info_json_["map_light_data"] = nlohmann::json::object();
        }
        nlohmann::json& D = map_info_json_["map_light_data"];
        if (!D.contains("radius"))          D["radius"] = 0;
        if (!D.contains("intensity"))       D["intensity"] = 255;
        auto clamp_radius = [](int v) { return std::max(0, std::min(20000, v)); };
        int orbit_radius = 0;
        if (D.contains("orbit_radius")) {
            try {
                orbit_radius = clamp_radius(D["orbit_radius"].get<int>());
            } catch (...) {
                orbit_radius = 0;
            }
        }
        int orbit_x = orbit_radius;
        if (D.contains("orbit_x")) {
            try {
                orbit_x = clamp_radius(D["orbit_x"].get<int>());
            } catch (...) {
                orbit_x = orbit_radius;
            }
        }
        int orbit_y = orbit_x;
        if (D.contains("orbit_y")) {
            try {
                orbit_y = clamp_radius(D["orbit_y"].get<int>());
            } catch (...) {
                orbit_y = orbit_x;
            }
        }
        D["orbit_x"] = orbit_x;
        D["orbit_y"] = orbit_y;
        D["orbit_radius"] = std::max(orbit_x, orbit_y);
        if (!D.contains("update_interval")) D["update_interval"] = 10;
        if (!D.contains("mult"))            D["mult"] = 0.0;
        if (!D.contains("fall_off"))        D["fall_off"] = 100;
        if (!D.contains("min_opacity"))     D["min_opacity"] = 0;
        if (!D.contains("max_opacity"))     D["max_opacity"] = 255;
        if (!D.contains("base_color") || !D["base_color"].is_array() || D["base_color"].size() < 4) {
            D["base_color"] = nlohmann::json::array({255, 255, 255, 255});
        }
        if (!D.contains("keys") || !D["keys"].is_array() || D["keys"].empty()) {

            D["keys"] = nlohmann::json::array();
            D["keys"].push_back(nlohmann::json::array({ 0.0, D["base_color"] }));
        }
        auto clamp_component = [](int v) { return std::max(0, std::min(255, v)); };
        if (!D.contains("screen_light") || !D["screen_light"].is_object()) {
            D["screen_light"] = nlohmann::json::object();
        }
        nlohmann::json& screen = D["screen_light"];
        if (!screen.contains("color") || !screen["color"].is_array() || screen["color"].size() < 3) {
            screen["color"] = nlohmann::json::array({255, 255, 255});
        } else {
            for (std::size_t i = 0; i < 3; ++i) {
                if (i >= screen["color"].size()) {
                    screen["color"].push_back(255);
                } else {
                    try {
                        int comp = clamp_component(screen["color"][i].get<int>());
                        screen["color"][i] = comp;
                    } catch (...) {
                        screen["color"][i] = 255;
                    }
                }
            }
            while (screen["color"].size() > 3) {
                screen["color"].erase(screen["color"].size() - 1);
            }
        }
        int map_min = D.value("min_opacity", 0);
        int map_max = D.value("max_opacity", 255);
        map_min = std::max(0, std::min(255, map_min));
        map_max = std::max(0, std::min(255, map_max));
        if (map_min > map_max) std::swap(map_min, map_max);
        if (!screen.contains("min_opacity")) screen["min_opacity"] = map_min;
        if (!screen.contains("max_opacity")) screen["max_opacity"] = map_max;
        try {
            int smin = clamp_component(screen["min_opacity"].get<int>());
            int smax = clamp_component(screen["max_opacity"].get<int>());
            smin = std::max(map_min, std::min(map_max, smin));
            smax = std::max(map_min, std::min(map_max, smax));
            if (smin > smax) std::swap(smin, smax);
            screen["min_opacity"] = smin;
            screen["max_opacity"] = smax;
        } catch (...) {
            screen["min_opacity"] = map_min;
            screen["max_opacity"] = map_max;
        }
    }
}

void Assets::load_camera_settings_from_json() {
    if (!map_info_json_.is_object()) {
        return;
    }
    nlohmann::json& camera_settings = map_info_json_["camera_settings"];
    if (!camera_settings.is_object()) {
        camera_settings = nlohmann::json::object();
    }
    camera_.apply_camera_settings(camera_settings);
    camera_settings = camera_.camera_settings_to_json();
}

void Assets::write_camera_settings_to_json() {
    if (!map_info_json_.is_object()) {
        return;
    }
    map_info_json_["camera_settings"] = camera_.camera_settings_to_json();
}

void Assets::on_camera_settings_changed() {
    write_camera_settings_to_json();
    save_map_info_json();
}

void Assets::reload_camera_settings() {
    load_camera_settings_from_json();
}

void Assets::apply_map_light_config() {
    if (!scene) {
        return;
    }
    if (!map_info_json_.is_object()) {
        return;
    }
    auto it = map_info_json_.find("map_light_data");
    if (it != map_info_json_.end() && it->is_object()) {
        scene->apply_map_light_config(*it);
    }
}

bool Assets::on_map_light_changed() {
    apply_map_light_config();
    save_map_info_json();
    return true;
}

Assets::~Assets() {
    delete scene;
    delete finder_;
    delete dev_controls_;
}

AssetLibrary& Assets::library() {
    return library_;
}

const AssetLibrary& Assets::library() const {
    return library_;
}

void Assets::set_rooms(std::vector<Room*> rooms) {
    rooms_ = std::move(rooms);
}

std::vector<Room*>& Assets::rooms() {
    return rooms_;
}

const std::vector<Room*>& Assets::rooms() const {
    return rooms_;
}

void Assets::refresh_active_asset_lists() {
    rebuild_active_assets_if_needed();

    SDL_Point camera_focus = camera_.get_screen_center();
    auto update_audio_metrics = [&](Asset* asset) {
        if (!asset) return;
        const float dx = static_cast<float>(asset->pos.x - camera_focus.x);
        const float dy = static_cast<float>(asset->pos.y - camera_focus.y);
        asset->distance_from_camera = std::sqrt(dx * dx + dy * dy);
        asset->angle_from_camera = std::atan2(dy, dx);
};
    if (player) {
        update_audio_metrics(player);
    }
    for (Asset* asset : active_assets) {
        update_audio_metrics(asset);
    }

    AudioEngine::instance().update();
    update_filtered_active_assets();
}

void Assets::refresh_filtered_active_assets() {
    update_filtered_active_assets();
}

void Assets::update_filtered_active_assets() {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        filtered_active_assets = active_assets;
        dev_controls_->filter_active_assets(filtered_active_assets);
        return;
    }

    filtered_active_assets.clear();
}

void Assets::ensure_dev_controls() {
    if (dev_controls_) {
        return;
    }

    const char* msg_create = "[Assets] Creating Dev Controls";
    std::cout << msg_create << "\n";
    dev_mode_trace(msg_create);
    dev_controls_ = new DevControls(this, screen_width, screen_height);
    if (!dev_controls_) {
        const char* msg_fail = "[Assets] Failed to allocate Dev Controls";
        std::cout << msg_fail << "\n";
        dev_mode_trace(msg_fail);
        return;
    }
    const char* msg_constructed = "[Assets] Dev Controls constructed, wiring context";
    std::cout << msg_constructed << "\n";
    dev_mode_trace(msg_constructed);

    dev_mode_trace("[Assets] Dev Controls -> set_player");
    dev_controls_->set_player(player);
    dev_mode_trace("[Assets] Dev Controls -> set_active_assets");
    dev_controls_->set_active_assets(filtered_active_assets);
    dev_mode_trace("[Assets] Dev Controls -> set_current_room");
    dev_controls_->set_current_room(current_room_);
    dev_mode_trace("[Assets] Dev Controls -> set_screen_dimensions");
    dev_controls_->set_screen_dimensions(screen_width, screen_height);
    dev_mode_trace("[Assets] Dev Controls -> set_rooms");
    dev_controls_->set_rooms(&rooms_);
    dev_mode_trace("[Assets] Dev Controls -> set_input");
    dev_controls_->set_input(input);
    dev_mode_trace("[Assets] Dev Controls -> set_map_info");
    dev_controls_->set_map_info(&map_info_json_, [this]() { return on_map_light_changed(); });
    dev_mode_trace("[Assets] Dev Controls -> set_map_context");
    dev_controls_->set_map_context(&map_info_json_, map_path_);
    dev_mode_trace("[Assets] Dev Controls wiring complete");
}

void Assets::set_input(Input* m) {
    input = m;

    if (dev_controls_) {
        dev_controls_->set_input(m);
        if (dev_controls_->is_enabled()) {
            dev_controls_->set_player(player);
            dev_controls_->set_active_assets(filtered_active_assets);
            dev_controls_->set_current_room(current_room_);
            dev_controls_->set_screen_dimensions(screen_width, screen_height);
            dev_controls_->set_rooms(&rooms_);
            dev_controls_->set_map_context(&map_info_json_, map_path_);
        }
    }
}

void Assets::update(const Input& input)
{

    render_pipeline::ScalingLogic::TickUsageSampling();

    const bool ctrl_down = input.isScancodeDown(SDL_SCANCODE_LCTRL) || input.isScancodeDown(SDL_SCANCODE_RCTRL);
    if (scene && ctrl_down && input.wasScancodePressed(SDL_SCANCODE_M)) {
        scene->toggle_light_map_only_mode();
        std::cout << "[Assets] Light map-only view "
                  << (scene->light_map_only_mode() ? "enabled" : "disabled")
                  << " (Ctrl+M).\n";
    }
    if (ctrl_down && input.wasScancodePressed(SDL_SCANCODE_R)) {
        const bool enabled = render_pipeline::ScalingLogic::ToggleUsageTracking();
        if (!enabled) {
            render_pipeline::ScalingLogic::FlushUsageData();
        }
        std::cout << "[Assets] Scaling usage tracking " << (enabled ? "enabled" : "disabled") << " (Ctrl+R).\n";
        scaling_notice_ = ScalingNotice{
            enabled ? std::string("Recording scale") : std::string("Stopped recording"),
            SDL_GetTicks() + 2000u
        };
    }

    Room* detected_room = finder_ ? finder_->getCurrentRoom() : nullptr;
    Room* active_room = detected_room;
    if (dev_controls_ && dev_controls_->is_enabled()) {
        active_room = dev_controls_->resolve_current_room(detected_room);
    }
    current_room_ = active_room;

    camera_.update_zoom(active_room, finder_, player);

    update_active_assets(camera_.get_screen_center());
    rebuild_active_assets_if_needed();

    AudioEngine& audio_engine = AudioEngine::instance();
    audio_engine.set_effect_max_distance(static_cast<float>(std::max(1, camera_.get_render_distance_world_margin())));

    dx = dy = 0;

    int start_px = player ? player->pos.x : 0;
    int start_py = player ? player->pos.y : 0;

    if (!dev_mode) {
        if (player) player->update();
    }

    if (player) {
        dx = player->pos.x - start_px;
        dy = player->pos.y - start_py;
        if (dx != 0 || dy != 0) {
            camera_.update_zoom(active_room, finder_, player);
            update_active_assets(camera_.get_screen_center());
            rebuild_active_assets_if_needed();
            update_filtered_active_assets();
        }
    }
    if (!dev_mode) {
        std::vector<Asset*> non_player_assets;
        non_player_assets.reserve(active_assets.size());
        for (Asset* asset : active_assets) {
            if (asset && asset != player) {
                non_player_assets.push_back(asset);
            }
        }

        const std::size_t task_count = non_player_assets.size();
        if (task_count == 1) {
            non_player_assets.front()->update();
        } else if (task_count > 1) {
            const unsigned hardware_threads = std::max(1u, std::thread::hardware_concurrency());
            const std::size_t thread_count = std::min<std::size_t>(hardware_threads, task_count);

            if (thread_count <= 1) {
                for (Asset* asset : non_player_assets) {
                    asset->update();
                }
            } else {
                std::atomic<std::size_t> next_index{0};
                std::vector<std::thread> workers;
                workers.reserve(thread_count);

                for (std::size_t i = 0; i < thread_count; ++i) {
                    workers.emplace_back([&non_player_assets, &next_index, task_count]() {
                        for (;;) {
                            const std::size_t idx = next_index.fetch_add(1, std::memory_order_relaxed);
                            if (idx >= task_count) {
                                break;
                            }
                            if (Asset* asset = non_player_assets[idx]) {
                                asset->update();
                            }
                        }
                    });
                }

                for (std::thread& worker : workers) {
                    if (worker.joinable()) {
                        worker.join();
                    }
                }
            }
        }
    }

    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->set_player(player);
        dev_controls_->set_active_assets(filtered_active_assets);
        dev_controls_->set_current_room(current_room_);
        dev_controls_->set_screen_dimensions(screen_width, screen_height);
        dev_controls_->set_rooms(&rooms_);
        dev_controls_->update(input);
        dev_controls_->update_ui(input);
    }

    if (scene && !suppress_render_) scene->render();

    process_removals();
}

void Assets::set_dev_mode(bool mode) {
    const bool changed = (dev_mode != mode);
    dev_mode = mode;

    // In dev mode, prefer low-quality rendering for responsiveness.
    // This avoids creating large intermediate render targets that can stall/freeze some GPUs.
    // When not in dev mode, use the normal (high quality) pipeline.
    force_high_quality_rendering_ = false;
    update_scene_render_quality();

    if (dev_mode) {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        if (changed) {
        dev_mode_trace("[Assets] Dev Mode enabled: using low-quality rendering for responsiveness.");
        std::cout << "[Assets] Dev Mode enabled: using low-quality rendering for responsiveness.\n";
    }
        dev_mode_trace("[Assets] Enabling Dev Controls");
        std::cout << "[Assets] Enabling Dev Controls\n";
        ensure_dev_controls();
        if (dev_controls_) {
            dev_mode_trace("[Assets] Dev Controls acquired, toggling on");
            std::cout << "[Assets] Dev Controls acquired, toggling on\n";
            dev_controls_->set_enabled(true);
            dev_controls_->set_player(player);
            dev_controls_->set_active_assets(filtered_active_assets);
            dev_controls_->set_current_room(current_room_);
            dev_controls_->set_screen_dimensions(screen_width, screen_height);
            dev_controls_->set_rooms(&rooms_);
            dev_controls_->set_input(input);
            dev_controls_->set_map_info(&map_info_json_, [this]() { return on_map_light_changed(); });
            dev_controls_->set_map_context(&map_info_json_, map_path_);
            dev_controls_->resolve_current_room(current_room_);
        }
        refresh_filtered_active_assets();
        dev_mode_trace("[Assets] Dev Controls enabled");
        std::cout << "[Assets] Dev Controls enabled\n";
    } else {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");
        if (changed) {
            std::cout << "[Assets] Dev Mode disabled: restoring high render quality.\n";
        }
        if (dev_controls_) {
            dev_mode_trace("[Assets] Disabling Dev Controls");
            std::cout << "[Assets] Disabling Dev Controls\n";
            dev_controls_->set_enabled(false);
            dev_controls_->clear_selection();
        }
        update_filtered_active_assets();
    }
}

void Assets::set_force_high_quality_rendering(bool enable) {
    if (force_high_quality_rendering_ == enable) {
        return;
    }
    force_high_quality_rendering_ = enable;
    update_scene_render_quality();
}

void Assets::update_scene_render_quality() {
    if (!scene) {
        return;
    }
    const bool low_quality = dev_mode && !force_high_quality_rendering_;
    scene->set_low_quality_rendering(low_quality);
}

void Assets::set_render_suppressed(bool suppressed) {
    suppress_render_ = suppressed;
}

const std::vector<Asset*>& Assets::getFilteredActiveAssets() const {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        return filtered_active_assets;
    }
    return active_assets;
}

const std::vector<Asset*>& Assets::get_selected_assets() const {
    static std::vector<Asset*> empty;
    return (dev_controls_ && dev_controls_->is_enabled())
               ? dev_controls_->get_selected_assets() : empty;
}

const std::vector<Asset*>& Assets::get_highlighted_assets() const {
    static std::vector<Asset*> empty;
    return (dev_controls_ && dev_controls_->is_enabled())
               ? dev_controls_->get_highlighted_assets() : empty;
}

Asset* Assets::get_hovered_asset() const {
    return (dev_controls_ && dev_controls_->is_enabled())
               ? dev_controls_->get_hovered_asset() : nullptr;
}

nlohmann::json Assets::save_current_room(std::string ) {

    return nlohmann::json::object();
}

void Assets::addAsset(const std::string& name, SDL_Point g) {
    std::cout << "\n[Assets::addAsset] Request to create asset '" << name
              << "' at grid (" << g.x << ", " << g.y << ")\n";

    auto info = library_.get(name);
    if (!info) {
        std::cerr << "[Assets::addAsset][Error] No asset info found for '" << name << "'\n";
        return;
    }
    std::cout << "[Assets::addAsset] Retrieved AssetInfo '" << info->name
              << "' at " << info << "\n";

    Area spawn_area(name, SDL_Point{g.x, g.y}, 1, 1, "Point", 1, 1, 1);
    std::cout << "[Assets::addAsset] Created Area '" << spawn_area.get_name() << "' at (" << g.x << ", " << g.y << ")\n";

    size_t prev_size = owned_assets.size();

    owned_assets.emplace_back(
        std::make_unique<Asset>(info, spawn_area, SDL_Point{g.x, g.y}, 0, nullptr));

    if (owned_assets.size() <= prev_size) {
        std::cerr << "[Assets::addAsset][Error] owned_assets did not grow!\n";
        return;
    }

    Asset* newAsset = owned_assets.back().get();
    if (!newAsset) {
        std::cerr << "[Assets::addAsset][Error] Asset allocation failed for '" << name << "'\n";
        return;
    }
    std::cout << "[Assets::addAsset][Debug] New Asset allocated at " << newAsset
              << " (info=" << (newAsset->info ? newAsset->info->name : "<null>") << ")\n";

    all.push_back(newAsset);
    std::cout << "[Assets::addAsset] all.size() now = " << all.size() << "\n";

    try {
        set_camera_recursive(newAsset, &camera_);
        set_assets_owner_recursive(newAsset, this);
        std::cout << "[Assets::addAsset] View set successfully\n";
        newAsset->finalize_setup();
        std::cout << "[Assets::addAsset] Finalize setup successful\n";
    } catch (const std::exception& e) {
        std::cerr << "[Assets::addAsset][Exception] " << e.what() << "\n";
    }

    initialize_active_assets(camera_.get_screen_center());
    rebuild_active_assets_if_needed();
    update_filtered_active_assets();

    std::cout << "[Assets::addAsset] Active assets=" << active_assets.size() << "\n";

    std::cout << "[Assets::addAsset] Successfully added asset '" << name
              << "' at (" << g.x << ", " << g.y << ")\n";
}

Asset* Assets::spawn_asset(const std::string& name, SDL_Point world_pos) {
    std::cout << "\n[Assets::spawn_asset] Request to spawn asset '" << name
              << "' at world (" << world_pos.x << ", " << world_pos.y << ")\n";

    auto info = library_.get(name);
    if (!info) {
        std::cerr << "[Assets::spawn_asset][Error] No asset info found for '" << name << "'\n";
        return nullptr;
    }
    std::cout << "[Assets::spawn_asset] Retrieved AssetInfo '" << info->name
              << "' at " << info << "\n";

    Area spawn_area(name, SDL_Point{world_pos.x, world_pos.y}, 1, 1, "Point", 1, 1, 1);
    std::cout << "[Assets::spawn_asset] Created Area '" << spawn_area.get_name() << "' at (" << world_pos.x << ", " << world_pos.y << ")\n";

    size_t prev_size = owned_assets.size();
    owned_assets.emplace_back( std::make_unique<Asset>(info, spawn_area, world_pos, 0, nullptr));

    if (owned_assets.size() <= prev_size) {
        std::cerr << "[Assets::spawn_asset][Error] owned_assets did not grow!\n";
        return nullptr;
    }

    Asset* newAsset = owned_assets.back().get();
    if (!newAsset) {
        std::cerr << "[Assets::spawn_asset][Error] Asset allocation failed for '" << name << "'\n";
        return nullptr;
    }

    std::cout << "[Assets::spawn_asset][Debug] New Asset allocated at " << newAsset
              << " (info=" << (newAsset->info ? newAsset->info->name : "<null>") << ")\n";

    all.push_back(newAsset);
    std::cout << "[Assets::spawn_asset] all.size() now = " << all.size() << "\n";

    try {
        set_camera_recursive(newAsset, &camera_);
        set_assets_owner_recursive(newAsset, this);
        std::cout << "[Assets::spawn_asset] View set successfully\n";
        newAsset->finalize_setup();
        std::cout << "[Assets::spawn_asset] Finalize setup successful\n";
    } catch (const std::exception& e) {
        std::cerr << "[Assets::spawn_asset][Exception] " << e.what() << "\n";
    }

    initialize_active_assets(camera_.get_screen_center());
    rebuild_active_assets_if_needed();
    update_filtered_active_assets();

    std::cout << "[Assets::spawn_asset] Active assets=" << active_assets.size() << "\n";

    std::cout << "[Assets::spawn_asset] Successfully spawned asset '" << name
              << "' at (" << world_pos.x << ", " << world_pos.y << ")\n";

    return newAsset;
}

void Assets::mark_active_assets_dirty() {
    active_assets_dirty_.store(true, std::memory_order_release);
}

void Assets::initialize_active_assets(SDL_Point center) {
    const int radius = active_search_radius();
    active_asset_list_ = std::make_unique<AssetList>(
        all,
        center,
        radius,
        std::vector<std::string>{},
        std::vector<std::string>{},
        std::vector<std::string>{},
        SortMode::ZIndexAsc);
    active_assets_dirty_.store(true, std::memory_order_release);
}

void Assets::update_active_assets(SDL_Point center) {
    if (!active_asset_list_) {
        initialize_active_assets(center);
        return;
    }

    active_asset_list_->set_center(center);
    active_asset_list_->set_search_radius(active_search_radius());
    active_asset_list_->update();
    active_assets_dirty_.store(true, std::memory_order_release);
}

void Assets::rebuild_active_assets_if_needed() {
    if (!active_asset_list_) {
        initialize_active_assets(camera_.get_screen_center());
    }

    if (!active_asset_list_ || !active_assets_dirty_.load(std::memory_order_acquire)) {
        return;
    }

    active_assets.clear();
    active_light_assets_.clear();
    active_asset_list_->full_list(active_assets);
    active_light_assets_.reserve(active_assets.size());
    for (Asset* asset : active_assets) {
        if (!asset || !asset->info) {
            continue;
        }
        const auto& info = asset->info;
        if (!info->light_sources.empty()) {
            active_light_assets_.push_back(asset);
        }
    }
    active_assets_dirty_.store(false, std::memory_order_release);
}

int Assets::active_search_radius() const {
    return std::max(1, camera_.get_render_distance_world_margin());
}

void Assets::schedule_removal(Asset* a) {
    if (!a) {
        return;
    }
    std::lock_guard<std::mutex> lock(removal_queue_mutex_);
    removal_queue.push_back(a);
}

void Assets::process_removals() {
    std::vector<Asset*> pending_removals;
    {
        std::lock_guard<std::mutex> lock(removal_queue_mutex_);
        if (removal_queue.empty()) {
            return;
        }
        pending_removals.swap(removal_queue);
    }

    if (pending_removals.empty()) {
        return;
    }

    std::unordered_set<Asset*> removal_lookup(pending_removals.begin(), pending_removals.end());

    for (auto it = owned_assets.begin(); it != owned_assets.end();) {
        if (removal_lookup.count(it->get()) > 0) {
            it = owned_assets.erase(it);
        } else {
            ++it;
        }
    }

    auto erase_ptrs = [&removal_lookup](auto& vec) {
        vec.erase(
            std::remove_if(vec.begin(), vec.end(),
                           [&removal_lookup](auto* candidate) {
                               return removal_lookup.count(candidate) > 0;
                           }),
            vec.end());
};

    erase_ptrs(all);
    erase_ptrs(active_assets);
    erase_ptrs(active_light_assets_);
    erase_ptrs(filtered_active_assets);

    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->clear_selection();
        dev_controls_->set_active_assets(filtered_active_assets);
    }

    initialize_active_assets(camera_.get_screen_center());
    rebuild_active_assets_if_needed();
    update_filtered_active_assets();
}

void Assets::render_overlays(SDL_Renderer* renderer) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->render_overlays(renderer);
    }

    if (!renderer) {
        return;
    }

    if (scaling_notice_) {
        const Uint32 now = SDL_GetTicks();
        if (now >= scaling_notice_->expiry_ms) {
            scaling_notice_.reset();
        }
    }

    if (!scaling_notice_) {
        return;
    }

    TTF_Font* font = scaling_notice_font();
    if (!font) {
        return;
    }

    SDL_Color color{255, 255, 255, 255};
    std::unique_ptr<SDL_Surface, SDLSurfaceDeleter> surface(
        TTF_RenderUTF8_Blended(font, scaling_notice_->message.c_str(), color));
    if (!surface) {
        return;
    }

    std::unique_ptr<SDL_Texture, SDLTextureDeleter> texture(
        SDL_CreateTextureFromSurface(renderer, surface.get()));
    if (!texture) {
        return;
    }

    const int padding_x = 16;
    const int padding_y = 10;
    SDL_Rect dest{0, 0, surface->w, surface->h};
    dest.x = (screen_width - dest.w) / 2;
    dest.x = std::clamp(dest.x, 0, std::max(0, screen_width - dest.w));
    dest.y = std::max(10, screen_height / 10);

    SDL_Rect background{
        dest.x - padding_x,
        dest.y - padding_y,
        dest.w + padding_x * 2,
        dest.h + padding_y * 2
    };

    background.x = std::clamp(background.x, 0, std::max(0, screen_width - background.w));
    background.y = std::clamp(background.y, 0, std::max(0, screen_height - background.h));
    dest.x = background.x + (background.w - dest.w) / 2;
    dest.y = background.y + (background.h - dest.h) / 2;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
    SDL_RenderFillRect(renderer, &background);

    SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(renderer, texture.get(), nullptr, &dest);
}

SDL_Renderer* Assets::renderer() const {
    return scene ? scene->get_renderer() : nullptr;
}

Asset* Assets::find_asset_by_name(const std::string& name) const {
    if (name.empty()) {
        return nullptr;
    }
    for (Asset* asset : active_assets) {
        if (asset && asset->info && asset->info->name == name) {
            return asset;
        }
    }
    for (Asset* asset : all) {
        if (asset && asset->info && asset->info->name == name) {
            return asset;
        }
    }
    return nullptr;
}

bool Assets::contains_asset(const Asset* asset) const {
    if (!asset) {
        return false;
    }

    if (std::find(all.begin(), all.end(), asset) != all.end()) {
        return true;
    }

    return std::any_of(owned_assets.begin(), owned_assets.end(), [asset](const auto& candidate) {
        return candidate.get() == asset;
    });
}

Global_Light_Source* Assets::map_light_source() {
    if (!scene) {
        return nullptr;
    }
    return &scene->map_light_source();
}

const Global_Light_Source* Assets::map_light_source() const {
    return const_cast<Assets*>(this)->map_light_source();
}

render_pipeline::shading::ReactiveShadowSettings* Assets::reactive_shadow_settings() {
    if (!scene) {
        return nullptr;
    }
    return &scene->reactive_shadow_settings();
}

const render_pipeline::shading::ReactiveShadowSettings* Assets::reactive_shadow_settings() const {
    return const_cast<Assets*>(this)->reactive_shadow_settings();
}

void Assets::set_map_light_panel_visible(bool visible) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->set_map_light_panel_visible(visible);
    }
}

bool Assets::is_map_light_panel_visible() const {
    return dev_controls_ && dev_controls_->is_enabled() && dev_controls_->is_map_light_panel_visible();
}

void Assets::toggle_asset_library() {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->toggle_asset_library();
    }
}

void Assets::open_asset_library() {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->open_asset_library();
    }
}

void Assets::close_asset_library() {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->close_asset_library();
    }
}

bool Assets::is_asset_library_open() const {
    return dev_controls_ && dev_controls_->is_enabled() && dev_controls_->is_asset_library_open();
}

void Assets::toggle_room_config() {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->toggle_room_config();
    }
}

void Assets::close_room_config() {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->close_room_config();
    }
}

bool Assets::is_room_config_open() const {
    return dev_controls_ && dev_controls_->is_enabled() && dev_controls_->is_room_config_open();
}

std::shared_ptr<AssetInfo> Assets::consume_selected_asset_from_library() {
    if (!dev_controls_ || !dev_controls_->is_enabled()) return nullptr;
    return dev_controls_->consume_selected_asset_from_library();
}

void Assets::open_asset_info_editor(const std::shared_ptr<AssetInfo>& info) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->open_asset_info_editor(info);
    }
}

void Assets::open_asset_info_editor_for_asset(Asset* a) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->open_asset_info_editor_for_asset(a);
    }
}

void Assets::finalize_asset_drag(Asset* a, const std::shared_ptr<AssetInfo>& info) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->finalize_asset_drag(a, info);
    }
}

void Assets::close_asset_info_editor() {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->close_asset_info_editor();
    }
}

bool Assets::is_asset_info_editor_open() const {
    return dev_controls_ && dev_controls_->is_enabled() && dev_controls_->is_asset_info_editor_open();
}

void Assets::clear_editor_selection() {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->clear_selection();
    }
}

void Assets::handle_sdl_event(const SDL_Event& e) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->handle_sdl_event(e);
    }
}

void Assets::focus_camera_on_asset(Asset* a, double zoom_factor, int duration_steps) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->focus_camera_on_asset(a, zoom_factor, duration_steps);
    }
}

void Assets::begin_area_edit_for_selected_asset(const std::string& area_name) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->begin_area_edit_for_selected_asset(area_name);
    }
}

devmode::core::ManifestStore* Assets::manifest_store() {
    if (!dev_controls_) {
        return nullptr;
    }
    return &dev_controls_->manifest_store();
}

const devmode::core::ManifestStore* Assets::manifest_store() const {
    if (!dev_controls_) {
        return nullptr;
    }
    return &dev_controls_->manifest_store();
}

void Assets::notify_spawn_group_config_changed(const nlohmann::json& entry) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->notify_spawn_group_config_changed(entry);
    }
}

void Assets::notify_spawn_group_removed(const std::string& spawn_id) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->notify_spawn_group_removed(spawn_id);
    }
}

void Assets::set_editor_current_room(Room* room) {
    current_room_ = room;
    if (dev_controls_) {
        dev_controls_->set_current_room(room);
    }
}
