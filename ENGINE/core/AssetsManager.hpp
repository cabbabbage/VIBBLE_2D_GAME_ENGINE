#pragma once

#include "render/camera.hpp"
#include "active_assets_manager.hpp"
#include "asset/asset_library.hpp"
#include <SDL.h>
#include <string>
#include <vector>
#include <memory>
#include <deque>
#include <nlohmann/json.hpp>
#include "room/room.hpp"

class Asset;
class SceneRenderer;
struct SDL_Renderer;
class CurrentRoomFinder;
class Room;
class Input;
class DevControls;
class AssetInfo;

class Assets {
public:
    Assets(std::vector<Asset>&& loaded,
           AssetLibrary& library,
           Asset*,
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
    void update(const Input& input, int screen_center_x, int screen_center_y);
    void set_dev_mode(bool mode);
    void set_render_suppressed(bool suppressed);
    void set_input(Input* m);
    Input* get_input() const { return input; }

    const std::vector<Asset*>& get_selected_assets() const;
    const std::vector<Asset*>& get_highlighted_assets() const;
    Asset* get_hovered_asset() const;

    const std::vector<Asset*>& getActive() const { return active_assets; }
    const std::vector<Asset*>& getFilteredActiveAssets() const { return filtered_active_assets; }
    std::vector<Asset*>& mutable_filtered_active_assets() { return filtered_active_assets; }
    const std::vector<Asset*>& getClosest() const { return closest_assets; }
    camera& getView() { return camera_; }
    const camera& getView() const { return camera_; }

    void render_overlays(SDL_Renderer* renderer);
    void toggle_asset_library();
    void open_asset_library();
    void close_asset_library();
    bool is_asset_library_open() const;
    void toggle_room_config();
    void close_room_config();
    bool is_room_config_open() const;

    std::shared_ptr<AssetInfo> consume_selected_asset_from_library();
    void open_asset_info_editor(const std::shared_ptr<AssetInfo>& info);
    void open_asset_info_editor_for_asset(Asset* a);
    void close_asset_info_editor();
    bool is_asset_info_editor_open() const;
    void clear_editor_selection();
    void handle_sdl_event(const SDL_Event& e);
    void open_spawn_group_for_asset(Asset* a);
    void finalize_asset_drag(Asset* a, const std::shared_ptr<AssetInfo>& info);
    void on_camera_settings_changed();
    void reload_camera_settings();

    void focus_camera_on_asset(Asset* a, double zoom_factor = 0.8, int duration_steps = 25);
    void begin_area_edit_for_selected_asset(const std::string& area_name);

    nlohmann::json& map_info_json() { return map_info_json_; }
    const nlohmann::json& map_info_json() const { return map_info_json_; }
    const std::string& map_path() const { return map_path_; }
    const std::string& map_info_path() const { return map_info_path_; }

    ActiveAssetsManager& active_manager() { return activeManager; }
    const ActiveAssetsManager& active_manager() const { return activeManager; }

    AssetLibrary& library();
    const AssetLibrary& library() const;

    void set_rooms(std::vector<Room*> rooms);
    std::vector<Room*>& rooms();
    const std::vector<Room*>& rooms() const;

    void refresh_active_asset_lists();
    void refresh_filtered_active_assets();
    void update_closest_assets(Asset* player, int max_count);

    int shading_group_count() const { return num_groups_; }

    std::deque<std::unique_ptr<Asset>> owned_assets;
    std::vector<Asset*> all;
    Asset* player = nullptr;

    Asset* spawn_asset(const std::string& name, SDL_Point world_pos);

private:
    void load_map_info_json();
    void save_map_info_json();
    void apply_map_light_config();
    void on_map_light_changed();
    void hydrate_map_info_sections();
    void load_camera_settings_from_json();
    void write_camera_settings_to_json();
    void schedule_removal(Asset* a);
    void process_removals();
    void addAsset(const std::string& name, SDL_Point g);
    void update_filtered_active_assets();

    friend class SceneRenderer;
    friend class Asset;

    CurrentRoomFinder* finder_ = nullptr;
    Input* input = nullptr;
    DevControls* dev_controls_ = nullptr;
    camera camera_;
    SceneRenderer* scene = nullptr;
    ActiveAssetsManager activeManager;
    int screen_width;
    int screen_height;
    int dx = 0;
    int dy = 0;
    std::vector<Asset*> active_assets;
    std::vector<Asset*> filtered_active_assets;
    std::vector<Asset*> closest_assets;
    std::vector<Room*> rooms_;
    Room* current_room_ = nullptr;
    int num_groups_ = 4;
    bool dev_mode = false;
    bool suppress_render_ = false;
    std::vector<Asset*> removal_queue;

    AssetLibrary& library_;
    std::string map_path_;
    std::string map_info_path_;
    nlohmann::json map_info_json_;
};
