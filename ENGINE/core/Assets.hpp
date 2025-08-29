
#pragma once

#include "utils/view.hpp"
#include "active_assets_manager.hpp"
#include "asset/asset_library.hpp"
#include <SDL.h>
#include <string>
#include <vector>
#include <unordered_set>
#include "room/room.hpp"

class Asset;
class ControlsManager;
class SceneRenderer;
struct SDL_Renderer;
class CurrentRoomFinder;
class Room;
class MouseInput;       
class DevMouseControls; 

class Assets {
public:
Assets(std::vector<Asset>&& loaded,
       AssetLibrary& library,
       Asset* ,
       std::vector<Room*> rooms,
       int screen_width,
       int screen_height,
       int screen_center_x,
       int screen_center_y,
       int map_radius,
       SDL_Renderer* renderer,
       const std::string& map_path);




    ~Assets();

    nlohmann::json save_current_room(std::string room_name);

    void update(const std::unordered_set<SDL_Keycode>& keys,
                int screen_center_x,
                int screen_center_y);

    void remove(Asset* asset);
    void set_dev_mode(bool mode);

    
    void set_mouse_input(MouseInput* m);
    MouseInput* get_mouse_input() const { return mouse_input; }

    
    const std::vector<Asset*>& get_selected_assets() const;
    const std::vector<Asset*>& get_highlighted_assets() const;
    Asset* get_hovered_asset() const;

    
    const std::vector<Asset*>& getActive()  const { return active_assets; }
    const std::vector<Asset*>& getClosest() const { return closest_assets; }

    view& getView() { return window; }
    const view& getView() const { return window; }

    
    std::vector<Asset> all;
    Asset* player = nullptr;
    ControlsManager* controls = nullptr;
    
    CurrentRoomFinder* finder_ = nullptr;

    MouseInput* mouse_input = nullptr;     
    DevMouseControls* dev_mouse = nullptr; 

    view window;
    SceneRenderer* scene = nullptr;
    ActiveAssetsManager activeManager;

    int screen_width;
    int screen_height;
    int dx = 0;
    int dy = 0;

    std::vector<Asset*> active_assets;
    std::vector<Asset*> closest_assets;

    std::vector<Room*> rooms_;
    Room* current_room_ = nullptr;

    int num_groups_ = 4;
    bool dev_mode = false;
    AssetLibrary& library_;
private:
    void addAsset(const std::string& name, int gx, int gy);

    friend class SceneRenderer;
};
