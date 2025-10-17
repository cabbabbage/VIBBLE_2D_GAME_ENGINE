#pragma once

#include "render/camera.hpp"
#include "asset_list.hpp"
#include "asset/asset_library.hpp"
#include <SDL.h>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "map_generation/room.hpp"

class Asset;
class SceneRenderer;
struct VirtualLightMap;
struct SDL_Renderer;
class CurrentRoomFinder;
class Room;
class Input;
class DevControls;
class AssetInfo;
class Global_Light_Source;
namespace devmode::core {
class ManifestStore;
}
namespace render_pipeline::shading {
struct ReactiveShadowSettings;
}

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
           const std::string& map_id,
           const nlohmann::json& map_manifest,
           std::string content_root = {});
    ~Assets();

    nlohmann::json save_current_room(std::string room_name);
    void update(const Input& input);
    void set_dev_mode(bool mode);
    void set_force_high_quality_rendering(bool enable);
    bool force_high_quality_rendering() const { return force_high_quality_rendering_; }
    void set_render_suppressed(bool suppressed);
    void set_input(Input* m);
    Input* get_input() const { return input; }
    Asset* find_asset_by_name(const std::string& name) const;
    bool contains_asset(const Asset* asset) const;

    const std::vector<Asset*>& get_selected_assets() const;
    const std::vector<Asset*>& get_highlighted_assets() const;
    Asset* get_hovered_asset() const;

    const std::vector<Asset*>& getActive() const { return active_assets; }
    const std::vector<Asset*>& getFilteredActiveAssets() const;
    const std::vector<Asset*>& getActiveLightAssets() const { return active_light_assets_; }
    const std::vector<Asset*>& getActiveLitAssets() const { return active_light_assets_; }
    std::vector<Asset*>& mutable_filtered_active_assets() { return filtered_active_assets; }
    camera& getView() { return camera_; }
    const camera& getView() const { return camera_; }

    void render_overlays(SDL_Renderer* renderer);
    SDL_Renderer* renderer() const;
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
    void finalize_asset_drag(Asset* a, const std::shared_ptr<AssetInfo>& info);
    void on_camera_settings_changed();
    void reload_camera_settings();
    void apply_camera_runtime_settings();

    void focus_camera_on_asset(Asset* a, double zoom_factor = 0.8, int duration_steps = 25);
    void begin_area_edit_for_selected_asset(const std::string& area_name);

    devmode::core::ManifestStore* manifest_store();
    const devmode::core::ManifestStore* manifest_store() const;
    void notify_spawn_group_config_changed(const nlohmann::json& entry);
    void notify_spawn_group_removed(const std::string& spawn_id);

    void set_editor_current_room(Room* room);

    Room* current_room() { return current_room_; }
    const Room* current_room() const { return current_room_; }
    std::vector<const Room::NamedArea*> current_room_trigger_areas() const;

    nlohmann::json& map_info_json() { return map_info_json_; }
    const nlohmann::json& map_info_json() const { return map_info_json_; }
    const std::string& map_path() const { return map_path_; }
    const std::string& map_info_path() const { return map_info_path_; }
    const std::string& map_id() const { return map_id_; }

    AssetLibrary& library();
    const AssetLibrary& library() const;

    void set_rooms(std::vector<Room*> rooms);
    std::vector<Room*>& rooms();
    const std::vector<Room*>& rooms() const;

    void refresh_active_asset_lists();
    void refresh_filtered_active_assets();
    void mark_active_assets_dirty();
    void initialize_active_assets(SDL_Point center);

    Global_Light_Source* map_light_source();
    const Global_Light_Source* map_light_source() const;
    render_pipeline::shading::ReactiveShadowSettings* reactive_shadow_settings();
    const render_pipeline::shading::ReactiveShadowSettings* reactive_shadow_settings() const;
    const VirtualLightMap* virtual_light_map() const;
    void set_virtual_light_map_quadrants(int quadrants);
    int  virtual_light_map_quadrants() const;
    void force_virtual_light_map_refresh();
    void force_shaded_assets_rerender();
    void set_map_light_panel_visible(bool visible);
    bool is_map_light_panel_visible() const;

    bool is_dev_mode() const { return dev_mode; }

    int shading_group_count() const { return num_groups_; }

    std::deque<std::unique_ptr<Asset>> owned_assets;
    std::vector<Asset*> all;
    Asset* player = nullptr;

    Asset* spawn_asset(const std::string& name, SDL_Point world_pos);

private:
    void save_map_info_json();
    void apply_map_light_config();
    bool on_map_light_changed();
    void hydrate_map_info_sections();
    void load_camera_settings_from_json();
    void write_camera_settings_to_json();
    void schedule_removal(Asset* a);
    void process_removals();
    void addAsset(const std::string& name, SDL_Point g);
    void update_filtered_active_assets();
    void ensure_dev_controls();
    void update_scene_render_quality();
    int  saved_render_quality_percent() const;
    int  effective_render_quality_percent() const;

    friend class SceneRenderer;
    friend class Asset;

    CurrentRoomFinder* finder_ = nullptr;
    Input* input = nullptr;
    DevControls* dev_controls_ = nullptr;
    camera camera_;
    SceneRenderer* scene = nullptr;
    int screen_width;
    int screen_height;
    int dx = 0;
    int dy = 0;
    std::vector<Asset*> active_assets;
    std::vector<Asset*> filtered_active_assets;
    std::vector<Asset*> active_light_assets_;
    std::vector<Room*> rooms_;
    Room* current_room_ = nullptr;
    int num_groups_ = 40;
    bool dev_mode = false;
    bool suppress_render_ = false;
    bool force_high_quality_rendering_ = false;
    std::vector<Asset*> removal_queue;
    std::mutex removal_queue_mutex_;

    AssetLibrary& library_;
    std::string map_id_;
    std::string map_path_;
    std::string map_info_path_;
    nlohmann::json map_info_json_;
    std::unique_ptr<AssetList> active_asset_list_;
    std::atomic<bool> active_assets_dirty_{true};
    std::unique_ptr<devmode::core::ManifestStore> manifest_store_fallback_;

    struct ScalingNotice {
        std::string message;
        Uint32 expiry_ms = 0;
    };

    std::optional<ScalingNotice> scaling_notice_;

    void rebuild_active_assets_if_needed();
    void update_active_assets(SDL_Point center);
    int active_search_radius() const;
};
