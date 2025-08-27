
#include "assets.hpp"
#include "asset/initialize_assets.hpp"

#include "controls_manager.hpp"
#include "zoom_control.hpp"
#include "find_current_room.hpp"   
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "dev_mode/dev_mouse_controls.hpp"
#include "utils/mouse_input.hpp"

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
               int map_radius)
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
    
}


Assets::~Assets() {
    delete controls;
    delete zoom_control;
    delete finder_;
    delete dev_mouse;
}

void Assets::set_mouse_input(MouseInput* m) {
    mouse_input = m;

    delete dev_mouse; 
    if (mouse_input) {
        dev_mouse = new DevMouseControls(mouse_input,
                                         active_assets,
                                         player,        
                                         screen_width,  
                                         screen_height);       
    } else {
        dev_mouse = nullptr;
    }
}

void Assets::update(const std::unordered_set<SDL_Keycode>& keys,
                    int screen_center_x,
                    int screen_center_y)
{
    activeManager.updateAssetVectors(player, screen_center_x, screen_center_y);

    current_room_ = finder_ ? finder_->getCurrentRoom() : nullptr;
    if (zoom_control) zoom_control->update(current_room_);

    dx = dy = 0;

    if (controls) {
        controls->update(keys); 
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
        dev_mouse->handle_mouse_input(keys);
    }
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
