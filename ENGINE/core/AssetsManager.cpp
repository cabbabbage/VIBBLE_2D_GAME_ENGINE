#include "AssetsManager.hpp"
#include "asset/initialize_assets.hpp"

#include "find_current_room.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_utils.hpp"
#include "dev_mode/dev_controls.hpp"
#include "utils/input.hpp"
#include "render/scene_renderer.hpp"
#include "utils/area.hpp"
#include <vector>
#include "room/room.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <limits>
#include <fstream>
#include <nlohmann/json.hpp>
#include "utils/range_util.hpp"


Assets::Assets(std::vector<Asset>&& loaded,
               AssetLibrary& library,
               Asset* ,
               std::vector<Room*> rooms,
               int screen_width_,
               int screen_height_,
               int screen_center_x,
               int screen_center_y,
               int map_radius,
               SDL_Renderer* renderer,
               const std::string& map_path)
    : camera(
          screen_width_,
          screen_height_,
          Area(
              "starting_camera",
              std::vector<SDL_Point>{
                  // Reduce starting view extents to one third
                  SDL_Point{-map_radius/3, -map_radius/3},
                  SDL_Point{ map_radius/3, -map_radius/3},
                  SDL_Point{ map_radius/3,  map_radius/3},
                  SDL_Point{-map_radius/3,  map_radius/3}
              }
          )
      ),
      activeManager(screen_width_, screen_height_, camera),
      screen_width(screen_width_),
      screen_height(screen_height_),
<<<<<<< ours
      map_path_(map_path),
      map_info_path_(map_path + "/map_info.json"),
      library_(library)
{
    load_map_info_json();
=======
      library_(library),
      map_path_(map_path)
{
    if (!map_path_.empty()) {
        map_info_path_ = map_path_ + "/map_info.json";
        std::ifstream map_info_in(map_info_path_);
        if (map_info_in) {
            try {
                map_info_in >> map_info_json_;
                if (!map_info_json_.is_object()) {
                    map_info_json_ = nlohmann::json::object();
                }
            } catch (const std::exception& ex) {
                std::cerr << "[Assets] Failed to parse map_info.json: " << ex.what() << "\n";
                map_info_json_ = nlohmann::json::object();
            }
        } else {
            map_info_json_ = nlohmann::json::object();
        }
    } else {
        map_info_json_ = nlohmann::json::object();
    }

>>>>>>> theirs
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
        camera.set_up_rooms(finder_);
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
<<<<<<< ours
        dev_controls_->set_map_info(&map_info_json_, [this]() { on_map_light_changed(); });
=======
        dev_controls_->set_map_context(&map_info_json_, map_path_);
>>>>>>> theirs
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
}

void Assets::save_map_info_json() const {
    if (map_info_path_.empty()) {
        return;
    }
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

    camera.update_zoom(active_room, finder_, player);

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
        set_camera_recursive(newAsset, &camera);
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
        set_camera_recursive(newAsset, &camera);
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
