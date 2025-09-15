#include "AssetsManager.hpp"
#include "asset/initialize_assets.hpp"

#include "find_current_room.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_utils.hpp"
#include "dev_mode/dev_mouse_controls.hpp"
#include "utils/input.hpp"
#include "render/scene_renderer.hpp"
#include "utils/area.hpp"
#include <vector>
#include "dev_mode/asset_library_ui.hpp"
#include "dev_mode/asset_info_ui.hpp"
#include "dev_mode/room_configurator.hpp"
#include "dev_mode/assets_config.hpp"
#include "dev_mode/area_overlay_editor.hpp"
#include "dev_mode/widgets.hpp"
#include "room/room.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <limits>
#include <random>
#include <tuple>
#include <cmath>
#include <nlohmann/json.hpp>
#include "utils/range_util.hpp"

static std::string generate_spawn_id() {
    static std::mt19937 rng(std::random_device{}());
    static const char* hex = "0123456789abcdef";
    std::uniform_int_distribution<int> dist(0, 15);
    std::string s = "spn-";
    for (int i = 0; i < 12; ++i) s.push_back(hex[dist(rng)]);
    return s;
}


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
      library_(library)
{
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

    for (Asset* a : all) {
        if (a) a->set_assets(this);
    }

}


Assets::~Assets() {
    delete scene;
    delete finder_;
    delete dev_mouse;
    delete library_ui_;
    delete info_ui_;
    delete assets_cfg_ui_;
    delete area_editor_;
    delete room_cfg_ui_;
}

void Assets::set_input(Input* m) {
    input = m;

    delete dev_mouse;
    if (input) {
        dev_mouse = new DevMouseControls(input,
                                         this,
                                         active_assets,
                                         player,
                                         screen_width,
                                         screen_height);
    } else {
        dev_mouse = nullptr;
    }
    if (!area_editor_) area_editor_ = new AreaOverlayEditor();
    if (area_editor_) area_editor_->attach_assets(this);
}

void Assets::update(const Input& input,
                    int screen_center_x,
                    int screen_center_y)
{

    activeManager.updateAssetVectors(player, screen_center_x, screen_center_y);

    current_room_ = finder_ ? finder_->getCurrentRoom() : nullptr;
    camera.update_zoom(current_room_, finder_, player);

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

    if (dev_mode && dev_mouse) {
        bool ui_blocking = false;
        int mx = input.getX();
        int my = input.getY();
        if (info_ui_ && info_ui_->is_visible() && info_ui_->is_point_inside(mx, my)) {
            ui_blocking = true;
        } else if (room_cfg_ui_ && room_cfg_ui_->visible() && room_cfg_ui_->is_point_inside(mx, my)) {
            ui_blocking = true;
        } else if (library_ui_ && library_ui_->is_visible() && library_ui_->is_input_blocking_at(mx, my)) {
            ui_blocking = true;
        } else if (area_editor_ && area_editor_->is_active()) {
            ui_blocking = true;
        }
        if (!ui_blocking) {
            dev_mouse->handle_mouse_input(input);
        }
    }

    bool ctrl = input.isScancodeDown(SDL_SCANCODE_LCTRL) || input.isScancodeDown(SDL_SCANCODE_RCTRL);
    if (ctrl) {
        if (input.wasScancodePressed(SDL_SCANCODE_A)) {
            toggle_asset_library();
        }
        if (input.wasScancodePressed(SDL_SCANCODE_R)) {
            toggle_room_config();
        }
    }

    update_ui(input);

    if (scene && !suppress_render_) scene->render();

    process_removals();
}

void Assets::set_dev_mode(bool mode) {
    dev_mode = mode;
    if (dev_mode) {
        // Disable parallax effects while in dev mode to simplify editing
        camera.set_parallax_enabled(false);
        camera.set_manual_zoom_override(false);
        close_asset_info_editor();
    } else {
        // Restore parallax when returning to player mode
        camera.set_parallax_enabled(true);
        camera.set_manual_zoom_override(false);
        // Leaving dev mode: close floating UIs
        if (library_ui_) library_ui_->close();
        if (room_cfg_ui_) room_cfg_ui_->close();
        if (info_ui_) info_ui_->close();
        if (assets_cfg_ui_) assets_cfg_ui_->close_all_asset_configs();
    }

    if (input) input->clearClickBuffer();
    if (dev_mouse) dev_mouse->reset_click_state();
}

void Assets::set_render_suppressed(bool suppressed) {
    suppress_render_ = suppressed;
}

const std::vector<Asset*>& Assets::get_selected_assets() const {
    static std::vector<Asset*> empty;
    return dev_mouse ? dev_mouse->get_selected_assets() : empty;
}

const std::vector<Asset*>& Assets::get_highlighted_assets() const {
    static std::vector<Asset*> empty;
    return dev_mouse ? dev_mouse->get_highlighted_assets() : empty;
}

Asset* Assets::get_hovered_asset() const {
    return dev_mouse ? dev_mouse->get_hovered_asset() : nullptr;
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
    if (library_ui_ && library_ui_->is_visible()) {
        library_ui_->render(renderer, screen_width, screen_height);
    }
    if (area_editor_ && area_editor_->is_active()) {
        area_editor_->render(renderer);
    }
    if (info_ui_ && info_ui_->is_visible()) {
        info_ui_->render_world_overlay(renderer, camera);
        info_ui_->render(renderer, screen_width, screen_height);
    }
    if (assets_cfg_ui_) {
        assets_cfg_ui_->render(renderer);
    }
    if (room_cfg_ui_ && room_cfg_ui_->any_panel_visible()) {
        room_cfg_ui_->render(renderer);
    }
    DMDropdown::render_active_options(renderer);
}

void Assets::toggle_asset_library() {
    if (!library_ui_) library_ui_ = new AssetLibraryUI();
    library_ui_->toggle();
}

void Assets::open_asset_library() {
    if (!library_ui_) library_ui_ = new AssetLibraryUI();
    library_ui_->open();
}

void Assets::close_asset_library() {
    if (library_ui_) library_ui_->close();
}

bool Assets::is_asset_library_open() const {
    return library_ui_ && library_ui_->is_visible();
}

void Assets::toggle_room_config() {
    if (!room_cfg_ui_) room_cfg_ui_ = new RoomConfigurator();
    if (room_cfg_ui_->visible()) {
        room_cfg_ui_->close();
    } else {
        room_cfg_ui_->open(current_room_);
        room_cfg_ui_->set_position(10, 10);
    }
}

void Assets::close_room_config() {
    if (room_cfg_ui_) room_cfg_ui_->close();
}

bool Assets::is_room_config_open() const {
    return room_cfg_ui_ && room_cfg_ui_->visible();
}

void Assets::update_ui(const Input& input) {
    // Keep UI panels updated
    if (library_ui_ && library_ui_->is_visible()) {
        library_ui_->update(input, screen_width, screen_height, library_, *this);
    }
    if (room_cfg_ui_ && room_cfg_ui_->visible()) {
        room_cfg_ui_->update(input);
    }
    if (area_editor_) {
        const bool was = last_area_editor_active_;
        const bool now = area_editor_->is_active();
        if (now) {
            area_editor_->update(input, screen_width, screen_height);
        }
        // Detect save-and-close completion
        if (was && !now) {
            if (area_editor_->consume_saved_flag() && reopen_info_after_area_edit_ && info_for_reopen_) {
                // Reopen Asset Info for the same asset/info
                open_asset_info_editor(info_for_reopen_);
                reopen_info_after_area_edit_ = false;
                info_for_reopen_.reset();
            } else {
                // If not saved or no pending reopen, just clear flags
                reopen_info_after_area_edit_ = false;
                info_for_reopen_.reset();
            }
        }
        last_area_editor_active_ = now;
    }
    if (info_ui_ && info_ui_->is_visible()) {
        info_ui_->update(input, screen_width, screen_height);
    }
    if (assets_cfg_ui_) {
        assets_cfg_ui_->update(input);
    }

    // When editing an area overlay, lock the camera to the selected or hovered asset.
    const bool editing_overlay_active = (area_editor_ && area_editor_->is_active());

    if (editing_overlay_active) {
        Asset* focus = nullptr;
        const auto& sel = get_selected_assets();
        if (!sel.empty()) focus = sel.front();
        if (!focus) focus = get_hovered_asset();
        if (!focus) focus = player; // fallback to player
        if (focus) {
            camera.set_manual_zoom_override(true);
            camera.set_focus_override(SDL_Point{ focus->pos.x, focus->pos.y });
        }
    } else {
        // Return camera control to normal gameplay when editors are closed
        camera.clear_focus_override();
    }
}

std::shared_ptr<AssetInfo> Assets::consume_selected_asset_from_library() {
    if (!library_ui_) return nullptr;
    return library_ui_->consume_selection();
}

void Assets::open_asset_info_editor(const std::shared_ptr<AssetInfo>& info) {
    if (!info) return;
    if (!info_ui_) info_ui_ = new AssetInfoUI();
    if (info_ui_) info_ui_->set_assets(this);
    // Always clear previous data so the UI only shows the requested asset
    info_ui_->clear_info();
    info_ui_->set_info(info);
    info_ui_->set_target_asset(nullptr);
    info_ui_->open();
}

void Assets::open_asset_info_editor_for_asset(Asset* a) {
    if (!a || !a->info) return;
    std::cout << "Opening AssetInfoUI for asset: " << a->info->name << std::endl;
    if (dev_mouse) dev_mouse->clear_selection();
    focus_camera_on_asset(a, 0.8, 20);
    open_asset_info_editor(a->info);
    if (info_ui_) info_ui_->set_target_asset(a);
}

void Assets::open_asset_config_for_asset(Asset* a) {
    if (!a) return;
    if (!assets_cfg_ui_) {
        assets_cfg_ui_ = new AssetsConfig();
        if (current_room_) {
            auto& assets_json = current_room_->assets_data()["assets"];
            assets_cfg_ui_->load(assets_json, [this]() {
                if (current_room_) current_room_->save_assets_json();
            });
        }
    }
    SDL_Point scr = camera.map_to_screen({a->pos.x, a->pos.y});
    std::string id = a->spawn_id.empty() ? (a->info ? a->info->name : std::string{}) : a->spawn_id;
    assets_cfg_ui_->open_asset_config(id, scr.x, scr.y);
}

void Assets::finalize_asset_drag(Asset* a, const std::shared_ptr<AssetInfo>& info) {
    if (!a || !info || !current_room_) return;
    auto& root = current_room_->assets_data();
    auto& arr = root["assets"];
    if (!arr.is_array()) arr = nlohmann::json::array();
    int width = 0, height = 0;
    SDL_Point center{0,0};
    if (current_room_->room_area) {
        auto b = current_room_->room_area->get_bounds();
        width = std::max(1, std::get<2>(b) - std::get<0>(b));
        height = std::max(1, std::get<3>(b) - std::get<1>(b));
        auto c = current_room_->room_area->get_center();
        center.x = c.x; center.y = c.y;
    }
    auto clamp_int = [](int v){ return std::max(0, std::min(100, v)); };
    int ep_x = 50, ep_y = 50;
    if (width != 0 && height != 0) {
        ep_x = clamp_int(static_cast<int>(std::lround(((double)(a->pos.x - center.x) / width) * 100.0 + 50.0)));
        ep_y = clamp_int(static_cast<int>(std::lround(((double)(a->pos.y - center.y) / height) * 100.0 + 50.0)));
    }
    std::string spawn_id = generate_spawn_id();
    nlohmann::json entry;
    entry["name"] = info->name;
    entry["spawn_id"] = spawn_id;
    entry["min_number"] = 1;
    entry["max_number"] = 1;
    entry["position"] = "Exact Position";
    entry["exact_position"] = nullptr;
    entry["inherited"] = false;
    entry["check_overlap"] = false;
    entry["check_min_spacing"] = false;
    entry["tag"] = false;
    entry["ep_x_min"] = ep_x;
    entry["ep_x_max"] = ep_x;
    entry["ep_y_min"] = ep_y;
    entry["ep_y_max"] = ep_y;
    arr.push_back(entry);
    current_room_->save_assets_json();
    a->spawn_id = spawn_id;
    a->spawn_method = "Exact Position";
    if (assets_cfg_ui_) {
        assets_cfg_ui_->load(arr, [this]() {
            if (current_room_) current_room_->save_assets_json();
        });
    }
}

void Assets::close_asset_info_editor() {
    if (info_ui_) info_ui_->close();
}

bool Assets::is_asset_info_editor_open() const {
    return info_ui_ && info_ui_->is_visible();
}

void Assets::handle_sdl_event(const SDL_Event& e) {
    if (auto* dd = DMDropdown::active_dropdown()) {
        dd->handle_event(e);
        return;
    }
    if (area_editor_ && area_editor_->is_active()) {
        if (area_editor_->handle_event(e)) return;
    }
    int mx = 0, my = 0;
    if (e.type == SDL_MOUSEMOTION) {
        mx = e.motion.x; my = e.motion.y;
    } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        mx = e.button.x; my = e.button.y;
    } else if (e.type == SDL_MOUSEWHEEL) {
        SDL_GetMouseState(&mx, &my);
    }
    bool handled = false;
    if (!handled && info_ui_ && info_ui_->is_visible() && info_ui_->is_point_inside(mx, my)) {
        info_ui_->handle_event(e);
        handled = true;
    }
    if (!handled && assets_cfg_ui_ && assets_cfg_ui_->any_visible() && assets_cfg_ui_->is_point_inside(mx, my)) {
        assets_cfg_ui_->handle_event(e);
        handled = true;
    }
    if (!handled && room_cfg_ui_ && room_cfg_ui_->visible() && room_cfg_ui_->is_point_inside(mx, my)) {
        room_cfg_ui_->handle_event(e);
        handled = true;
    }
    if (!handled && library_ui_ && library_ui_->is_visible() && library_ui_->is_input_blocking_at(mx, my)) {
        library_ui_->handle_event(e);
        handled = true;
    }
    if (!handled) {
        if (info_ui_ && info_ui_->is_visible()) {
            info_ui_->handle_event(e);
        } else if (assets_cfg_ui_ && assets_cfg_ui_->any_visible()) {
            assets_cfg_ui_->handle_event(e);
        } else if (room_cfg_ui_ && room_cfg_ui_->any_panel_visible()) {
            room_cfg_ui_->handle_event(e);
        } else if (library_ui_ && library_ui_->is_visible()) {
            library_ui_->handle_event(e);
        }
    }
    if (handled && input && (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP)) {
        input->clearClickBuffer();
    }
}

void Assets::focus_camera_on_asset(Asset* a, double zoom_factor, int duration_steps) {
    if (!a) return;
    camera.set_manual_zoom_override(true);
    camera.pan_and_zoom_to_asset(a, zoom_factor, duration_steps);
}

void Assets::begin_area_edit_for_selected_asset(const std::string& area_name) {
    if (!area_editor_) {
        area_editor_ = new AreaOverlayEditor();
        area_editor_->attach_assets(this);
    }
    const auto& sel = get_selected_assets();
    Asset* target = nullptr;
    if (!sel.empty()) target = sel.front();
    if (!target) target = get_hovered_asset();
    if (!target || !target->info) return;
    // Prepare to reopen Asset Info after successful save
    if (info_ui_ && info_ui_->is_visible()) {
        reopen_info_after_area_edit_ = true;
        info_for_reopen_ = target->info;
        info_ui_->close();
    } else {
        reopen_info_after_area_edit_ = false;
        info_for_reopen_.reset();
    }
    focus_camera_on_asset(target, 0.8, 20);
    area_editor_->begin(target->info.get(), target, area_name);
}
