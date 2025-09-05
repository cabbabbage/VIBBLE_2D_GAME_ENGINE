
#pragma once

#include "utils/view.hpp"
#include "active_assets_manager.hpp"
#include "asset/asset_library.hpp"
#include <SDL.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <memory>
#include <deque>
#include "room/room.hpp"

class Asset;
class ControlsManager;
class SceneRenderer;
struct SDL_Renderer;
class CurrentRoomFinder;
class Room;
class Input;            
class DevMouseControls; 
class AssetLibraryUI;
class AssetInfoUI;
class AssetInfo;

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

    void update(const Input& input,
                int screen_center_x,
                int screen_center_y);

    void remove(Asset* asset);
    void set_dev_mode(bool mode);

    
    void set_input(Input* m);
    Input* get_input() const { return input; }

    
    const std::vector<Asset*>& get_selected_assets() const;
    const std::vector<Asset*>& get_highlighted_assets() const;
    Asset* get_hovered_asset() const;

    
    const std::vector<Asset*>& getActive()  const { return active_assets; }
    const std::vector<Asset*>& getClosest() const { return closest_assets; }

    view& getView() { return window; }
    const view& getView() const { return window; }

    
    // Owns all asset instances to keep their memory stable
    std::deque<std::unique_ptr<Asset>> owned_assets;
    // Non-owning flat list for easy iteration/access
    std::vector<Asset*> all;
    Asset* player = nullptr;
    ControlsManager* controls = nullptr;
    
    CurrentRoomFinder* finder_ = nullptr;

    Input* input = nullptr;     
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

    // Spawn API
    Asset* spawn_asset(const std::string& name, int world_x, int world_y);

    // Overlay UIs
public:
    // Called by renderer to draw overlays (e.g., asset library)
    void render_overlays(SDL_Renderer* renderer);
    // Toggle asset library (bound to TAB)
    void toggle_asset_library();
    void open_asset_library();
    void close_asset_library();
    bool is_asset_library_open() const;
    // Pump UI update
    void update_ui(const Input& input);
    // Retrieve selection from asset library, if any
    std::shared_ptr<AssetInfo> consume_selected_asset_from_library();

    // Asset info editor panel
    void open_asset_info_editor(const std::shared_ptr<AssetInfo>& info);
    void close_asset_info_editor();
    bool is_asset_info_editor_open() const;
    void handle_sdl_event(const SDL_Event& e);

private:
    AssetLibraryUI* library_ui_ = nullptr;
    AssetInfoUI*    info_ui_    = nullptr;
private:
    void addAsset(const std::string& name, int gx, int gy);

    friend class SceneRenderer;
    friend class Asset; // allow assets to access internals if needed
};

