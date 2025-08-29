
#include "Assets.hpp"
#include "asset/initialize_assets.hpp"

#include "controls_manager.hpp"
#include "find_current_room.hpp"   
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "dev_mode/dev_mouse_controls.hpp"
#include "utils/input.hpp"
#include "render/scene_renderer.hpp"
#include "ui/asset_library_ui.hpp"
#include "ui/asset_info_ui.hpp"

#include <algorithm>
#include <iostream>

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
    : window(screen_width_, screen_height_, view::Bounds{
          -map_radius, map_radius,
          -map_radius, map_radius
      }),
      activeManager(screen_width_, screen_height_, window),
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
        window.set_up_rooms(finder_);
    }

    // Create scene renderer owned by Assets
    scene = new SceneRenderer(renderer, this, screen_width_, screen_height_, map_path);
}


Assets::~Assets() {
    delete controls;
    delete scene;
    delete finder_;
    delete dev_mouse;
    delete library_ui_;
    delete info_ui_;
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
}

void Assets::update(const Input& input,
                    int screen_center_x,
                    int screen_center_y)
{
    activeManager.updateAssetVectors(player, screen_center_x, screen_center_y);

    current_room_ = finder_ ? finder_->getCurrentRoom() : nullptr;
    window.update_zoom(current_room_, finder_, player);

    dx = dy = 0;

    if (controls) {
        controls->update(input); 
        dx = controls->get_dx();
        dy = controls->get_dy();
    }

    active_assets  = activeManager.getActive();
    closest_assets = activeManager.getClosest();

    if (player) player->update();
    for (Asset* a : active_assets) {
        if (a && a != player)
            a->update();
    }

    if (dx != 0 || dy != 0)
        activeManager.sortByZIndex();

    
    if (dev_mode && dev_mouse) {
        bool ui_blocking = (library_ui_ && library_ui_->is_visible()) || (info_ui_ && info_ui_->is_visible());
        if (!ui_blocking) {
            dev_mouse->handle_mouse_input(input);
        }
    }

    // UI toggles
    if (input.wasKeyPressed(SDLK_TAB)) {
        toggle_asset_library();
    }

    // Update any visible UI
    update_ui(input);

    // Render the scene each tick from within Assets
    if (scene) scene->render();
}

void Assets::remove(Asset* asset) {
    if (!asset) return;

    std::cout << "[Assets] Removing asset: "
              << (asset->info ? asset->info->name : "<null>")
              << " at (" << asset->pos_X << ", " << asset->pos_Y << ")\n";

    active_assets.erase(std::remove(active_assets.begin(), active_assets.end(), asset),
                        active_assets.end());
    closest_assets.erase(std::remove(closest_assets.begin(), closest_assets.end(), asset),
                         closest_assets.end());

    all.erase(std::remove_if(all.begin(), all.end(),
                             [asset](Asset& a) { return &a == asset; }),
              all.end());

    asset->~Asset();
}

void Assets::set_dev_mode(bool mode) {
    dev_mode = mode;
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

void Assets::addAsset(const std::string& name, int gx, int gy) {
    auto info = library_.get(name);
    if (!info) {
        std::cerr << "[Assets] addAsset failed: no info for '" << name << "'\n";
        return;
    }

    
    Area spawn_area(name,
                    gx, gy,
                    1, 1,
                    "Point",
                    1,
                    1,
                    1);

    
    all.emplace_back(info, spawn_area, gx, gy, 0, nullptr);
    Asset* newAsset = &all.back();

    
    

    std::cout << "[Assets] Added asset '" << name
              << "' at (" << gx << ", " << gy << ")\n";
}

Asset* Assets::spawn_asset(const std::string& name, int world_x, int world_y) {
    auto info = library_.get(name);
    if (!info) {
        std::cerr << "[Assets] spawn_asset failed: no info for '" << name << "'\n";
        return nullptr;
    }

    Area spawn_area(name,
                    world_x, world_y,
                    1, 1,
                    "Point",
                    1,
                    1,
                    1);

    all.emplace_back(info, spawn_area, world_x, world_y, 0, nullptr);
    Asset* newAsset = &all.back();
    newAsset->set_view(&window);
    newAsset->finalize_setup();

    // Optionally place into active list this frame by recomputing vectors
    activeManager.updateAssetVectors(player, player ? player->pos_X : world_x, player ? player->pos_Y : world_y);
    active_assets  = activeManager.getActive();
    closest_assets = activeManager.getClosest();

    std::cout << "[Assets] Spawned asset '" << name
              << "' at (" << world_x << ", " << world_y << ")\n";
    return newAsset;
}

void Assets::render_overlays(SDL_Renderer* renderer) {
    if (library_ui_ && library_ui_->is_visible()) {
        library_ui_->render(renderer, library_, screen_width, screen_height);
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
    if (library_ui_ && library_ui_->is_visible()) {
        library_ui_->update(input, screen_width, screen_height, library_);
    }
    if (info_ui_ && info_ui_->is_visible()) {
        info_ui_->update(input, screen_width, screen_height);
    }
}

std::shared_ptr<AssetInfo> Assets::consume_selected_asset_from_library() {
    if (!library_ui_) return nullptr;
    return library_ui_->consume_selection();
}

void Assets::open_asset_info_editor(const std::shared_ptr<AssetInfo>& info) {
    if (!info) return;
    if (!info_ui_) info_ui_ = new AssetInfoUI();
    info_ui_->set_info(info);
    info_ui_->open();
}

void Assets::close_asset_info_editor() {
    if (info_ui_) info_ui_->close();
}

bool Assets::is_asset_info_editor_open() const {
    return info_ui_ && info_ui_->is_visible();
}

void Assets::handle_sdl_event(const SDL_Event& e) {
    if (info_ui_ && info_ui_->is_visible()) {
        info_ui_->handle_event(e);
    }
}
