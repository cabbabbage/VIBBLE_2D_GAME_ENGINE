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
    Assets(std::vector<Asset>&& loaded, AssetLibrary& library, Asset* , std::vector<Room*> rooms, int screen_width, int screen_height, int screen_center_x, int screen_center_y, int map_radius, SDL_Renderer* renderer, const std::string& map_path);
    ~Assets();
    nlohmann::json save_current_room(std::string room_name);
    void update(const Input& input, int screen_center_x, int screen_center_y);
    void set_dev_mode(bool mode);
    void set_render_suppressed(bool suppressed);
    void set_input(Input* m);
    Input* get_input() const { return input; }
    const std::vector<Asset*>& get_selected_assets() const;
    const std::vector<Asset*>& get_highlighted_assets() const;
    Asset* get_hovered_asset() const;
    const std::vector<Asset*>& getActive()  const { return active_assets; }
    const std::vector<Asset*>& getClosest() const { return closest_assets; }
    view& getView() { return window; }
    const view& getView() const { return window; }
    std::deque<std::unique_ptr<Asset>> owned_assets;
    std::vector<Asset*> all;
    Asset* player = nullptr;
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
    bool suppress_render_ = false;
    Asset* spawn_asset(const std::string& name, int world_x, int world_y);

	public:
    void render_overlays(SDL_Renderer* renderer);
    void toggle_asset_library();
    void open_asset_library();
    void close_asset_library();
    bool is_asset_library_open() const;
    void update_ui(const Input& input);
    std::shared_ptr<AssetInfo> consume_selected_asset_from_library();
    void open_asset_info_editor(const std::shared_ptr<AssetInfo>& info);
    void close_asset_info_editor();
    bool is_asset_info_editor_open() const;
    void handle_sdl_event(const SDL_Event& e);

	private:
    AssetLibraryUI* library_ui_ = nullptr;
    AssetInfoUI*    info_ui_    = nullptr;
    std::vector<Asset*> removal_queue;
    void schedule_removal(Asset* a);
    void process_removals();

	private:
    void addAsset(const std::string& name, int gx, int gy);
    friend class SceneRenderer;
    friend class Asset;
};
