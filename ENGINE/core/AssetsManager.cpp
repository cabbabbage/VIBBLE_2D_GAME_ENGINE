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
#include "dev_mode/area_overlay_editor.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <limits>
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
    delete area_editor_;
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
        bool lib_block = false;
        if (library_ui_ && library_ui_->is_visible()) {
            lib_block = library_ui_->is_input_blocking_at(input.getX(), input.getY());
        }
        bool ui_blocking = lib_block || (info_ui_ && info_ui_->is_visible()) || (area_editor_ && area_editor_->is_active());
        if (!ui_blocking) {
            dev_mouse->handle_mouse_input(input);
        }
    }

    if (input.wasScancodePressed(SDL_SCANCODE_TAB)) {
        toggle_asset_library();
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
        // Open library immediately (expanded) and pin to top-left
        if (!library_ui_) library_ui_ = new AssetLibraryUI();
        library_ui_->open();
        library_ui_->set_position(10, 10);
        library_ui_->set_expanded(true);
        std::cout << "[Assets] Dev Mode ON. Asset Library opened at (10,10), expanded=1\n";
        close_asset_info_editor();
    } else {
        // Restore parallax when returning to player mode
        camera.set_parallax_enabled(true);
        // Leaving dev mode: close floating UIs
        if (library_ui_) {
            library_ui_->close();
            std::cout << "[Assets] Dev Mode OFF. Asset Library closed.\n";
        }
        if (info_ui_) info_ui_->close();
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

nlohmann::json Assets::save_current_room(std::string room_name) {
    if (!current_room_) {
        throw std::runtime_error("[Assets] No current room to save!");
    }

    nlohmann::json j = current_room_->create_static_room_json(room_name);
    j["room_name"] = room_name;

    return j;
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
        info_ui_->render(renderer, screen_width, screen_height);
    }
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

void Assets::update_ui(const Input& input) {
    // Keep UI panels updated
    if (library_ui_ && library_ui_->is_visible()) {
        library_ui_->update(input, screen_width, screen_height, library_, *this);
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

    // When editing (asset info open or area overlay active), lock the camera
    // to the selected or hovered asset to avoid auto-zoom/pan drifting away.
    const bool editing_overlay_active =
        (area_editor_ && area_editor_->is_active()) ||
        (info_ui_ && info_ui_->is_visible());

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
        camera.set_manual_zoom_override(false);
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
    info_ui_->set_info(info);
    // If the asset library is open now, close it and remember to restore
    reopen_library_on_info_close_ = is_asset_library_open();
    if (reopen_library_on_info_close_) {
        close_asset_library();
    }
    info_ui_->open();
}

void Assets::open_asset_info_editor_for_asset(Asset* a) {
    if (!a || !a->info) return;
    // Pan and zoom to the asset before opening the editor
    focus_camera_on_asset(a, 0.8, 25);
    open_asset_info_editor(a->info);
}

void Assets::close_asset_info_editor() {
    if (info_ui_) info_ui_->close();
    // Reopen the asset library if we closed it when opening this editor
    if (reopen_library_on_info_close_) {
        reopen_library_on_info_close_ = false;
        open_asset_library();
    }
}

bool Assets::is_asset_info_editor_open() const {
    return info_ui_ && info_ui_->is_visible();
}

void Assets::handle_sdl_event(const SDL_Event& e) {
    if (area_editor_ && area_editor_->is_active()) {
        if (area_editor_->handle_event(e)) return;
    }
    if (library_ui_ && library_ui_->is_visible()) {
        library_ui_->handle_event(e);
    }
    if (info_ui_ && info_ui_->is_visible()) {
        info_ui_->handle_event(e);
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
        // Do not reopen library when closing for area editing
        reopen_library_on_info_close_ = false;
        info_ui_->close();
    } else {
        reopen_info_after_area_edit_ = false;
        info_for_reopen_.reset();
    }
    focus_camera_on_asset(target, 0.8, 20);
    area_editor_->begin(target->info.get(), target, area_name);
}
