#pragma once

#include <SDL.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <optional>

#include <nlohmann/json_fwd.hpp>

#include "MapLightPanel.hpp"
#include "map_grid_panel.hpp"
#include "asset_filter_bar.hpp"
#include "trail_editor_suite.hpp"
#include "dev_mode/pan_and_zoom.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "map_assets_modals.hpp"

class Asset;
class Input;
class Assets;
class camera;
class AssetInfo;
class Room;
class RoomEditor;
class MapEditor;
class MapModeUI;
class CameraUIPanel;
class RegenerateRoomPopup;

class DevControls {
public:
    enum class Mode {
        RoomEditor,
        MapEditor,
        AreaMode
};

    DevControls(Assets* owner, int screen_w, int screen_h);
    ~DevControls();

    void set_input(Input* input);
    void set_player(Asset* player);
    void set_active_assets(std::vector<Asset*>& actives);
    void set_screen_dimensions(int width, int height);
    void set_current_room(Room* room, bool force_refresh = false);
    void set_rooms(std::vector<Room*>* rooms, std::size_t generation = 0);

    void set_map_info(nlohmann::json* map_info, MapLightPanel::SaveCallback on_save);
    void set_map_context(nlohmann::json* map_info, const std::string& map_path);

    struct RoomAreaCache {
        struct Polygon {
            std::string name;
            std::string type;
            std::vector<SDL_Point> points;
            SDL_Point anchor{0, 0};
};
        using PolygonList  = std::vector<Polygon>;
        using Listener     = std::function<void(const PolygonList&, std::size_t)>;

        void set_listener(Listener listener);
        void invalidate();
        const PolygonList& ensure_from_json(const nlohmann::json* root, std::optional<SDL_Point> default_anchor = std::nullopt);
        std::size_t generation() const { return generation_; }

    private:
        PolygonList cached_;
        const nlohmann::json* last_source_ = nullptr;
        bool dirty_ = true;
        std::size_t generation_ = 0;
        Listener listener_;
};

    void set_room_area_cache_listener(RoomAreaCache::Listener listener);
    std::size_t room_area_cache_generation() const;
    void notify_room_area_data_changed();

    Room* resolve_current_room(Room* detected_room);

    void set_enabled(bool enabled);
    bool is_enabled() const { return enabled_; }
    Mode mode() const { return mode_; }

    void set_camera_override_for_testing(camera* camera_override);

    void update(const Input& input);
    void update_ui(const Input& input);
    void handle_sdl_event(const SDL_Event& event);
    void render_overlays(SDL_Renderer* renderer);

    void toggle_asset_library();
    void open_asset_library();
    void close_asset_library();
    bool is_asset_library_open() const;

    std::shared_ptr<AssetInfo> consume_selected_asset_from_library();

    void open_asset_info_editor(const std::shared_ptr<AssetInfo>& info);
    void open_asset_info_editor_for_asset(Asset* asset);
    void close_asset_info_editor();
    bool is_asset_info_editor_open() const;

    void finalize_asset_drag(Asset* asset, const std::shared_ptr<AssetInfo>& info);

    [[nodiscard]] devmode::core::ManifestStore& manifest_store();
    [[nodiscard]] const devmode::core::ManifestStore& manifest_store() const;

    void toggle_room_config();
    void close_room_config();
    bool is_room_config_open() const;

    void set_map_light_panel_visible(bool visible);
    bool is_map_light_panel_visible() const;

    void begin_area_edit_for_selected_asset(const std::string& area_name);
    void focus_camera_on_asset(Asset* asset, double zoom_factor = 0.8, int duration_steps = 25);

    void reset_click_state();
    void clear_selection();
    void purge_asset(Asset* asset);

    void notify_spawn_group_config_changed(const nlohmann::json& entry);
    void notify_spawn_group_removed(const std::string& spawn_id);

    void refresh_reactive_shadow_settings();
    void clear_reactive_shadow_settings();

    const std::vector<Asset*>& get_selected_assets() const;
    const std::vector<Asset*>& get_highlighted_assets() const;
    Asset* get_hovered_asset() const;

    void set_zoom_scale_factor(double factor);
    double get_zoom_scale_factor() const;

    void filter_active_assets(std::vector<Asset*>& assets) const;

private:
    bool can_use_room_editor_ui() const;
    void enter_map_editor_mode();
    void exit_map_editor_mode(bool focus_player, bool restore_previous_state);
    void handle_map_selection();
    void toggle_map_light_panel();
    void toggle_camera_panel();
    void close_camera_panel();
    void toggle_map_assets_modal();
    void toggle_boundary_assets_modal();
    void configure_header_button_sets();
    void sync_header_button_states();
    Room* find_spawn_room() const;
    Room* choose_room(Room* preferred) const;
    bool is_pointer_over_dev_ui(int x, int y) const;
    void close_all_floating_panels();
    void maybe_update_mode_from_zoom();
    void open_regenerate_room_popup();
    bool is_modal_blocking_panels() const;
    void pulse_modal_header();
    void apply_header_suppression();

    void refresh_active_asset_filters();
    void reset_asset_filters();
    bool passes_asset_filters(Asset* asset) const;
    void apply_camera_area_render_flag();
    void set_mode_from_header(int header_mode);
    void set_mode(Mode new_mode);
    std::string generate_unique_room_area_name(const std::string& base) const;
    void restore_filter_hidden_assets() const;

private:
    int map_radius_or_default() const;
    void remove_spawn_group_assets(const std::string& spawn_id);
    void integrate_spawned_assets(std::vector<std::unique_ptr<Asset>>& spawned);
    void regenerate_map_spawn_group(const nlohmann::json& entry);
    void regenerate_boundary_spawn_group(const nlohmann::json& entry);
    void regenerate_map_grid_assets();

    bool persist_map_info_to_disk();

    const RoomAreaCache::PolygonList& room_area_polygons();

    Assets* assets_ = nullptr;
    Input* input_ = nullptr;
    std::vector<Asset*>* active_assets_ = nullptr;
    Asset* player_ = nullptr;
    Room* current_room_ = nullptr;
    Room* detected_room_ = nullptr;
    Room* dev_selected_room_ = nullptr;
    std::vector<Room*>* rooms_ = nullptr;
    std::size_t rooms_generation_ = 0;

    int screen_w_ = 0;
    int screen_h_ = 0;
    bool enabled_ = false;
    Mode mode_ = Mode::RoomEditor;

    std::unique_ptr<RoomEditor> room_editor_;
    std::unique_ptr<MapEditor> map_editor_;
    nlohmann::json* map_info_json_ = nullptr;
    MapLightPanel::SaveCallback map_light_save_cb_;
    MapGridPanel::SaveCallback map_grid_save_cb_;
    std::function<void()> map_grid_regen_cb_;
    std::unique_ptr<MapModeUI> map_mode_ui_;
    std::unique_ptr<CameraUIPanel> camera_panel_;
    std::unique_ptr<RegenerateRoomPopup> regenerate_popup_;
    std::string map_path_;
    bool pointer_over_camera_panel_ = false;
    bool modal_headers_hidden_ = false;
    bool sliding_headers_hidden_ = false;
    mutable std::unordered_map<Asset*, bool> filter_hidden_assets_;
    std::unique_ptr<TrailEditorSuite> trail_suite_;
    devmode::core::ManifestStore manifest_store_;
    AssetFilterBar asset_filter_;

    camera* camera_override_for_testing_ = nullptr;

    std::unique_ptr<SingleSpawnGroupModal> map_assets_modal_;
    std::unique_ptr<SingleSpawnGroupModal> boundary_assets_modal_;

    class PanAndZoom area_pan_zoom_;
    std::unique_ptr<class CreateRoomAreaPanel> create_area_panel_;
    std::unique_ptr<class EditRoomAreaPanel>   edit_area_panel_;
    std::unique_ptr<class AreaOverlayEditor>   asset_area_editor_;
    class Asset* area_hovered_asset_ = nullptr;

    class Asset* area_hovered_asset_with_area_ = nullptr;
    std::string area_hovered_area_name_;

    std::unordered_set<std::string> active_area_type_filters_;

    int hovered_area_index_ = -1;
    int selected_area_index_ = -1;
    SDL_Point last_area_click_world_{0,0};

    RoomAreaCache room_area_cache_;
};

