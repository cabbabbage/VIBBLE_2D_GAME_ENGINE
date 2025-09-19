#include "AssetsManager.hpp"
#include "asset/initialize_assets.hpp"

#include "find_current_room.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_utils.hpp"
#include "dev_mode/dev_controls.hpp"
#include "render/scene_renderer.hpp"
#include "room/room.hpp"
#include "utils/area.hpp"
#include "utils/input.hpp"
#include "utils/range_util.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <system_error>
#include <vector>


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
               const std::string& map_path)
    : camera_(
          screen_width_,
          screen_height_,
          Area(
              "starting_camera",
              std::vector<SDL_Point>{
                  // Reduce starting view extents to one third
                  SDL_Point{-map_radius / 3, -map_radius / 3},
                  SDL_Point{ map_radius / 3, -map_radius / 3},
                  SDL_Point{ map_radius / 3,  map_radius / 3},
                  SDL_Point{-map_radius / 3,  map_radius / 3}
              })
      ),
      activeManager(screen_width_, screen_height_, camera_),
      screen_width(screen_width_),
      screen_height(screen_height_),
      library_(library),
      map_path_(map_path),
      map_info_path_(map_path_.empty() ? std::string{} : (map_path_ + "/map_info.json"))
{
    load_map_info_json();

    InitializeAssets::initialize(*this,
                                 std::move(loaded),
                                 std::move(rooms),
                                 screen_width_,
                                 screen_height_,
                                 screen_center_x,
                                 screen_center_y,
                                 map_radius);

    finder_ = new CurrentRoomFinder(rooms_, player);
    if (finder_) {
        camera_.set_up_rooms(finder_);
    }

    scene = new SceneRenderer(renderer, this, screen_width_, screen_height_, map_path);
    apply_map_light_config();

    for (Asset* a : all) {
        if (a) a->set_assets(this);
    }

    dev_controls_ = new DevControls(this, screen_width_, screen_height_);
    if (dev_controls_) {
        dev_controls_->set_player(player);
        dev_controls_->set_active_assets(active_assets);
        dev_controls_->set_current_room(current_room_);
        dev_controls_->set_screen_dimensions(screen_width_, screen_height_);
        dev_controls_->set_rooms(&rooms_);
        dev_controls_->set_input(input);
        dev_controls_->set_map_info(&map_info_json_, [this]() { on_map_light_changed(); });
        dev_controls_->set_map_context(&map_info_json_, map_path_);
    }

}


void Assets::load_map_info_json() {
    map_info_json_ = nlohmann::json::object();
    if (map_info_path_.empty()) {
        return;
    }

    std::ifstream in(map_info_path_);
    if (!in.is_open()) {
        std::cerr << "[Assets] Failed to open map_info.json at " << map_info_path_ << "\n";
        return;
    }

    try {
        in >> map_info_json_;
    } catch (const std::exception& e) {
        std::cerr << "[Assets] Failed to parse map_info.json: " << e.what() << "\n";
        map_info_json_ = nlohmann::json::object();
    }

    if (!map_info_json_.is_object()) {
        map_info_json_ = nlohmann::json::object();
    }

    hydrate_map_info_sections();
    load_camera_settings_from_json();
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
    if (map_path_.empty()) {
        return;
    }

    const auto hydrate_from_file = [&](const char* legacy_key, const char* merged_key) {
        if (map_info_json_.contains(merged_key)) {
            return;
        }
        auto it = map_info_json_.find(legacy_key);
        if (it == map_info_json_.end() || !it->is_string()) {
            return;
        }
        const std::string file_path = map_path_ + "/" + it->get<std::string>();
        std::ifstream section(file_path);
        if (!section.is_open()) {
            std::cerr << "[Assets] Legacy map section missing: " << file_path << "\n";
            return;
        }
        try {
            nlohmann::json data;
            section >> data;
            map_info_json_[merged_key] = std::move(data);
        } catch (const std::exception& ex) {
            std::cerr << "[Assets] Failed to hydrate " << merged_key << " from "
                      << file_path << ": " << ex.what() << "\n";
        }
    };

    hydrate_from_file("map_assets", "map_assets_data");
    hydrate_from_file("map_boundary", "map_boundary_data");
    hydrate_from_file("map_light", "map_light_data");

    const auto hydrate_directory = [&](const char* merged_key, const char* directory_name) {
        if (map_info_json_.contains(merged_key) && map_info_json_[merged_key].is_object()) {
            return;
        }

        const std::filesystem::path dir = std::filesystem::path(map_path_) / directory_name;
        if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
            return;
        }

        std::error_code ec;
        std::filesystem::directory_iterator it(dir, ec);
        if (ec) {
            std::cerr << "[Assets] Failed to scan legacy directory " << dir << ": "
                      << ec.message() << "\n";
            return;
        }

        nlohmann::json merged = nlohmann::json::object();
        for (const auto& entry : it) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const auto& path = entry.path();
            if (path.extension() != ".json") {
                continue;
            }
            std::ifstream in(path);
            if (!in.is_open()) {
                std::cerr << "[Assets] Failed to open legacy section " << path << "\n";
                continue;
            }
            try {
                nlohmann::json section;
                in >> section;
                merged[path.stem().string()] = std::move(section);
            } catch (const std::exception& ex) {
                std::cerr << "[Assets] Failed to hydrate " << merged_key << " entry from "
                          << path << ": " << ex.what() << "\n";
            }
        }

        if (!merged.is_object()) {
            merged = nlohmann::json::object();
        }
        map_info_json_[merged_key] = std::move(merged);
    };

    hydrate_directory("rooms_data", "rooms");
    hydrate_directory("trails_data", "trails");

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
    // Populate defaults for map_light_data when missing, so editors and renderer have sane values.
    {
        nlohmann::json& L = map_info_json_["map_light_data"];
        if (!L.is_object()) {
            map_info_json_["map_light_data"] = nlohmann::json::object();
        }
        nlohmann::json& D = map_info_json_["map_light_data"];
        if (!D.contains("radius"))          D["radius"] = 0;
        if (!D.contains("intensity"))       D["intensity"] = 255;
        if (!D.contains("orbit_radius"))    D["orbit_radius"] = 0;
        if (!D.contains("update_interval")) D["update_interval"] = 10;
        if (!D.contains("mult"))            D["mult"] = 0.0;
        if (!D.contains("fall_off"))        D["fall_off"] = 100;
        if (!D.contains("min_opacity"))     D["min_opacity"] = 0;
        if (!D.contains("max_opacity"))     D["max_opacity"] = 255;
        if (!D.contains("base_color") || !D["base_color"].is_array() || D["base_color"].size() < 4) {
            D["base_color"] = nlohmann::json::array({255, 255, 255, 255});
        }
        if (!D.contains("keys") || !D["keys"].is_array() || D["keys"].empty()) {
            // Default one key at 0 degrees using the base color
            D["keys"] = nlohmann::json::array();
            D["keys"].push_back(nlohmann::json::array({ 0.0, D["base_color"] }));
        }
    }
    ensure_object("rooms_data");
    ensure_object("trails_data");
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
    if (it == map_info_json_.end() || !it->is_object()) {
        return;
    }
    scene->apply_map_light_config(*it);
}

void Assets::on_map_light_changed() {
    apply_map_light_config();
    save_map_info_json();
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
    active_assets  = activeManager.getActive();
    closest_assets = activeManager.getClosest();
}

void Assets::update_closest_assets(Asset* player, int max_count) {
    activeManager.updateClosestAssets(player, max_count);
    closest_assets = activeManager.getClosest();
}

void Assets::set_input(Input* m) {
    input = m;

    if (dev_controls_) {
        dev_controls_->set_input(m);
        dev_controls_->set_player(player);
        dev_controls_->set_active_assets(active_assets);
        dev_controls_->set_current_room(current_room_);
        dev_controls_->set_screen_dimensions(screen_width, screen_height);
        dev_controls_->set_rooms(&rooms_);
        dev_controls_->set_map_context(&map_info_json_, map_path_);
    }
}

void Assets::update(const Input& input,
                    int screen_center_x,
                    int screen_center_y)
{

    activeManager.updateAssetVectors(player, screen_center_x, screen_center_y);

    Room* detected_room = finder_ ? finder_->getCurrentRoom() : nullptr;
    Room* active_room = detected_room;
    if (dev_controls_) {
        active_room = dev_controls_->resolve_current_room(detected_room);
    }
    current_room_ = active_room;

    camera_.update_zoom(active_room, finder_, player);

    dx = dy = 0;

    int start_px = player ? player->pos.x : 0;
    int start_py = player ? player->pos.y : 0;

    active_assets  = activeManager.getActive();
    closest_assets = activeManager.getClosest();

    // Suspend movement updates in Dev Mode
    if (!dev_mode) {
        if (player) player->update();
    }

    if (player) {
        dx = player->pos.x - start_px;
        dy = player->pos.y - start_py;
    }
    if (player) {
        player->distance_to_player_sq = 0.0f;
        for (Asset* a : active_assets) {
            if (!a || a == player) continue;
            const double d = Range::get_distance(a, player);
            a->distance_to_player_sq = static_cast<float>(d * d);
        }
    } else {
        for (Asset* a : active_assets) {
            if (!a) continue;
            a->distance_to_player_sq = std::numeric_limits<float>::infinity();
        }
    }
    if (!dev_mode) {
        for (Asset* a : active_assets) {
            if (a && a != player)
                a->update();
        }
    }

    //activeManager.sortByZIndex();

    if (dev_controls_) {
        dev_controls_->set_player(player);
        dev_controls_->set_active_assets(active_assets);
        dev_controls_->set_current_room(current_room_);
        dev_controls_->set_screen_dimensions(screen_width, screen_height);
        dev_controls_->set_rooms(&rooms_);
        if (dev_mode) {
            dev_controls_->update(input);
            dev_controls_->update_ui(input);
        }
    }

    if (scene && !suppress_render_) scene->render();

    process_removals();
}

void Assets::set_dev_mode(bool mode) {
    dev_mode = mode;
    if (dev_controls_) {
        dev_controls_->set_enabled(mode);
        if (mode) {
            dev_controls_->resolve_current_room(current_room_);
        } else {
            dev_controls_->clear_selection();
        }
    }
}

void Assets::set_render_suppressed(bool suppressed) {
    suppress_render_ = suppressed;
}

const std::vector<Asset*>& Assets::get_selected_assets() const {
    static std::vector<Asset*> empty;
    return dev_controls_ ? dev_controls_->get_selected_assets() : empty;
}

const std::vector<Asset*>& Assets::get_highlighted_assets() const {
    static std::vector<Asset*> empty;
    return dev_controls_ ? dev_controls_->get_highlighted_assets() : empty;
}

Asset* Assets::get_hovered_asset() const {
    return dev_controls_ ? dev_controls_->get_hovered_asset() : nullptr;
}

nlohmann::json Assets::save_current_room(std::string /*room_name*/) {
    // Placeholder stub until proper room-saving logic is reintroduced.
    // For now, return an empty JSON object.
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
    std::cout << "[Assets::addAsset] Created Area '" << spawn_area.get_name()
              << "' at (" << g.x << ", " << g.y << ")\n";

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
              << " (info=" << (newAsset->info ? newAsset->info->name : "<null>")
              << ")\n";

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

    activeManager.activate(newAsset);
    activeManager.updateClosestAssets(player, 3);
    active_assets  = activeManager.getActive();
    closest_assets = activeManager.getClosest();

    std::cout << "[Assets::addAsset] Active assets=" << active_assets.size()
              << ", Closest=" << closest_assets.size() << "\n";

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
    std::cout << "[Assets::spawn_asset] Created Area '" << spawn_area.get_name()
              << "' at (" << world_pos.x << ", " << world_pos.y << ")\n";

    size_t prev_size = owned_assets.size();
    owned_assets.emplace_back(
        std::make_unique<Asset>(info, spawn_area, world_pos, 0, nullptr));

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
              << " (info=" << (newAsset->info ? newAsset->info->name : "<null>")
              << ")\n";

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

    activeManager.activate(newAsset);
    activeManager.updateClosestAssets(player, 3);
    active_assets  = activeManager.getActive();
    closest_assets = activeManager.getClosest();

    std::cout << "[Assets::spawn_asset] Active assets=" << active_assets.size()
              << ", Closest=" << closest_assets.size() << "\n";

    std::cout << "[Assets::spawn_asset] Successfully spawned asset '" << name
              << "' at (" << world_pos.x << ", " << world_pos.y << ")\n";

    return newAsset;
}

void Assets::schedule_removal(Asset* a) {
    if (a) removal_queue.push_back(a);
}

void Assets::process_removals() {
    if (removal_queue.empty()) return;
    for (Asset* a : removal_queue) {
        auto it = std::find_if(owned_assets.begin(), owned_assets.end(),
                               [a](const std::unique_ptr<Asset>& p){ return p.get() == a; });
        if (it != owned_assets.end()) {
            owned_assets.erase(it);
        }
    }
    removal_queue.clear();
}

void Assets::render_overlays(SDL_Renderer* renderer) {
    if (dev_controls_) dev_controls_->render_overlays(renderer);
}

void Assets::toggle_asset_library() {
    if (dev_controls_ && dev_mode) {
        dev_controls_->toggle_asset_library();
    }
}

void Assets::open_asset_library() {
    if (dev_controls_ && dev_mode) {
        dev_controls_->open_asset_library();
    }
}

void Assets::close_asset_library() {
    if (dev_controls_) dev_controls_->close_asset_library();
}

bool Assets::is_asset_library_open() const {
    return dev_controls_ && dev_controls_->is_asset_library_open();
}

void Assets::toggle_room_config() {
    if (dev_controls_ && dev_mode) {
        dev_controls_->toggle_room_config();
    }
}

void Assets::close_room_config() {
    if (dev_controls_) dev_controls_->close_room_config();
}

bool Assets::is_room_config_open() const {
    return dev_controls_ && dev_controls_->is_room_config_open();
}

std::shared_ptr<AssetInfo> Assets::consume_selected_asset_from_library() {
    if (!dev_controls_ || !dev_mode) return nullptr;
    return dev_controls_->consume_selected_asset_from_library();
}

void Assets::open_asset_info_editor(const std::shared_ptr<AssetInfo>& info) {
    if (dev_controls_ && dev_mode) {
        dev_controls_->open_asset_info_editor(info);
    }
}

void Assets::open_asset_info_editor_for_asset(Asset* a) {
    if (dev_controls_ && dev_mode) {
        dev_controls_->open_asset_info_editor_for_asset(a);
    }
}

void Assets::open_asset_config_for_asset(Asset* a) {
    if (dev_controls_ && dev_mode) {
        dev_controls_->open_asset_config_for_asset(a);
    }
}

void Assets::finalize_asset_drag(Asset* a, const std::shared_ptr<AssetInfo>& info) {
    if (dev_controls_ && dev_mode) {
        dev_controls_->finalize_asset_drag(a, info);
    }
}

void Assets::close_asset_info_editor() {
    if (dev_controls_) dev_controls_->close_asset_info_editor();
}

bool Assets::is_asset_info_editor_open() const {
    return dev_controls_ && dev_controls_->is_asset_info_editor_open();
}

void Assets::clear_editor_selection() {
    if (dev_controls_ && dev_mode) {
        dev_controls_->clear_selection();
    }
}

void Assets::handle_sdl_event(const SDL_Event& e) {
    if (dev_controls_ && dev_mode) {
        dev_controls_->handle_sdl_event(e);
    }
}

void Assets::focus_camera_on_asset(Asset* a, double zoom_factor, int duration_steps) {
    if (dev_controls_) dev_controls_->focus_camera_on_asset(a, zoom_factor, duration_steps);
}

void Assets::begin_area_edit_for_selected_asset(const std::string& area_name) {
    if (dev_controls_ && dev_mode) {
        dev_controls_->begin_area_edit_for_selected_asset(area_name);
    }
}
