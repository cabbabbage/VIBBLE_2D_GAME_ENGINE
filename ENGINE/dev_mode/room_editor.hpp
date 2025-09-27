#pragma once

#include <string>
#include <SDL.h>
#include <functional>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "dev_mode/spawn_group_config_ui.hpp"
#include "dev_mode/pan_and_zoom.hpp"

class Asset;
class Input;
class Assets;
class AssetLibraryUI;
class AssetInfoUI;
class AreaOverlayEditor;
class RoomConfigurator;
class SpawnGroupsConfig;
class AssetInfo;
class Room;
class MapGrid;
class DMButton;
class FullScreenCollapsible;

class RoomEditor {
public:
    RoomEditor(Assets* owner, int screen_w, int screen_h);
    ~RoomEditor();

    void set_input(Input* input);
    void set_player(Asset* player);
    void set_active_assets(std::vector<Asset*>& actives);
    void set_screen_dimensions(int width, int height);
    void set_current_room(Room* room);
    void set_room_config_visible(bool visible);
    void set_shared_fullscreen_panel(FullScreenCollapsible* panel);

    void set_enabled(bool enabled);
    bool is_enabled() const { return enabled_; }

    void update(const Input& input);
    void update_ui(const Input& input);
    bool handle_sdl_event(const SDL_Event& event);
    bool is_room_panel_blocking_point(int x, int y) const;
    bool is_room_ui_blocking_point(int x, int y) const;
    void render_overlays(SDL_Renderer* renderer);

    void toggle_asset_library();
    void open_asset_library();
    void close_asset_library();
    bool is_asset_library_open() const;
    bool is_library_drag_active() const;

    std::shared_ptr<AssetInfo> consume_selected_asset_from_library();

    void open_asset_info_editor(const std::shared_ptr<AssetInfo>& info);
    void open_asset_info_editor_for_asset(Asset* asset);
    void close_asset_info_editor();
    bool is_asset_info_editor_open() const;
    bool has_active_modal() const;
    void pulse_active_modal_header();

    void open_spawn_group_for_asset(Asset* asset);
    void finalize_asset_drag(Asset* asset, const std::shared_ptr<AssetInfo>& info);

    void toggle_room_config();
    void open_room_config();
    void close_room_config();
    bool is_room_config_open() const;
    void regenerate_room();
    void regenerate_room_from_template(Room* source_room);

    void begin_area_edit_for_selected_asset(const std::string& area_name);
    void focus_camera_on_asset(Asset* asset, double zoom_factor = 0.8, int duration_steps = 25);
    void focus_camera_on_room_center(bool reframe_zoom = true);

    void reset_click_state();
    void clear_selection();
    void clear_highlighted_assets();
    void purge_asset(Asset* asset);

    const std::vector<Asset*>& get_selected_assets() const { return selected_assets_; }
    const std::vector<Asset*>& get_highlighted_assets() const { return highlighted_assets_; }
    Asset* get_hovered_asset() const { return hovered_asset_; }

    void set_zoom_scale_factor(double factor);
    double get_zoom_scale_factor() const { return zoom_scale_factor_; }
private:
    enum class DragMode {
        None,
        Free,
        Exact,
        Percent,
        Perimeter,
        PerimeterCenter,
    };

    enum class ActiveModal {
        None,
        AssetInfo,
    };

    struct PerimeterOverlay {
        SDL_Point center{0, 0};
        double radius = 0.0;
    };

    struct PendingSpawnGroupOpen {
        std::string id;
        SDL_Point position{0, 0};
        int retry_frames = 0;
        int remaining_attempts = 3;
        bool awaiting_confirmation = false;
    };

    struct DraggedAssetState {
        Asset*     asset     = nullptr;   // pointer to the dragged asset (nullable)
        SDL_Point  start_pos {0, 0};      // screen/world position at drag start
        SDL_FPoint direction {0.0f, 0.0f}; // normalized drag direction or delta
        bool       active    = false;     // is a drag in progress?
    };
    void handle_mouse_input(const Input& input);
    Asset* hit_test_asset(SDL_Point screen_point) const;
    void update_hover_state(Asset* hit);
    void handle_click(const Input& input);
    void update_highlighted_assets();
    bool is_ui_blocking_input(int mx, int my) const;
    void handle_shortcuts(const Input& input);
    void handle_delete_shortcut(const Input& input);
    void update_area_editor_focus();
    void ensure_area_editor();
    void apply_area_editor_camera_override(bool enable);
    void ensure_room_configurator();
    void ensure_spawn_groups_config_ui();
    void update_room_config_bounds();
    void begin_drag_session(const SDL_Point& world_mouse, bool ctrl_modifier);
    void update_drag_session(const SDL_Point& world_mouse);
    void apply_perimeter_drag(const SDL_Point& world_mouse);
    void finalize_drag_session();
    void reset_drag_state();
    nlohmann::json* find_spawn_entry(const std::string& spawn_id);
    SDL_Point get_room_center() const;
    std::pair<int, int> get_room_dimensions() const;
    void refresh_spawn_groups_config_ui();
    void update_spawn_groups_config_anchor();
    SDL_Point spawn_groups_anchor_point() const;
    void handle_spawn_group_panel_closed(const std::string& spawn_id);
    void clear_active_spawn_group_target();
    void update_exact_json(nlohmann::json& entry, const Asset& asset, SDL_Point center, int width, int height);
    void update_percent_json(nlohmann::json& entry, const Asset& asset, SDL_Point center, int width, int height);
    void save_perimeter_json(nlohmann::json& entry, int dx, int dy, int orig_w, int orig_h, int radius);
    void handle_spawn_config_change(const nlohmann::json& entry, const SpawnGroupsConfigPanel::ChangeSummary& summary);
    void respawn_spawn_group(const nlohmann::json& entry);
    std::unique_ptr<MapGrid> build_room_grid(const std::string& ignore_spawn_id) const;
    void integrate_spawned_assets(std::vector<std::unique_ptr<Asset>>& spawned);
    void regenerate_current_room();
    void configure_shared_panel();
    void refresh_room_config_visibility();
    void sanitize_perimeter_spawn_groups();
    bool sanitize_perimeter_spawn_groups(nlohmann::json& groups);
    std::optional<PerimeterOverlay> compute_perimeter_overlay_for_drag();
    std::optional<PerimeterOverlay> compute_perimeter_overlay_for_spawn(const std::string& spawn_id);
    void add_spawn_group_internal();
    void duplicate_spawn_group_internal(const std::string& spawn_id);
    void delete_spawn_group_internal(const std::string& spawn_id);
    bool remove_spawn_group_by_id(const std::string& spawn_id);
    void open_spawn_group_editor_by_id(const std::string& spawn_id);
    void reopen_room_configurator();
    void process_pending_spawn_group_open();

private:
    Assets* assets_ = nullptr;
    Input* input_ = nullptr;
    std::vector<Asset*>* active_assets_ = nullptr;
    Asset* player_ = nullptr;
    Room* current_room_ = nullptr;

    int screen_w_ = 0;
    int screen_h_ = 0;
    bool enabled_ = false;

    std::unique_ptr<AssetLibraryUI> library_ui_;
    std::unique_ptr<AssetInfoUI> info_ui_;
    std::unique_ptr<SpawnGroupsConfig> spawn_groups_cfg_ui_;
    std::unique_ptr<AreaOverlayEditor> area_editor_;
    std::unique_ptr<RoomConfigurator> room_cfg_ui_;
    SDL_Rect room_config_bounds_{0, 0, 0, 0};
    FullScreenCollapsible* shared_fullscreen_panel_ = nullptr;
    bool room_config_dock_open_ = false;
    ActiveModal active_modal_ = ActiveModal::None;

    bool last_area_editor_active_ = false;
    bool area_editor_override_active_ = false;
    bool reopen_info_after_area_edit_ = false;
    std::shared_ptr<AssetInfo> info_for_reopen_;
    Asset* info_target_for_reopen_ = nullptr;

    Asset* hovered_asset_ = nullptr;
    std::vector<Asset*> selected_assets_;
    std::vector<Asset*> highlighted_assets_;

    bool dragging_ = false;
    Asset* drag_anchor_asset_ = nullptr;
    DragMode drag_mode_ = DragMode::None;
    std::vector<DraggedAssetState> drag_states_;
    SDL_Point drag_last_world_{0, 0};
    SDL_Point drag_room_center_{0, 0};
    SDL_Point drag_perimeter_circle_center_{0, 0};
    double drag_perimeter_base_radius_ = 0.0;
    SDL_Point drag_perimeter_center_offset_world_{0, 0};
    int drag_perimeter_orig_w_ = 0;
    int drag_perimeter_orig_h_ = 0;
    int drag_perimeter_curr_w_ = 0;
    int drag_perimeter_curr_h_ = 0;
    bool drag_moved_ = false;
    std::string drag_spawn_id_;
    Uint32 last_click_time_ms_ = 0;
    Asset* last_click_asset_ = nullptr;

    int click_buffer_frames_ = 0;
    int rclick_buffer_frames_ = 0;
    int hover_miss_frames_ = 0;
    std::optional<SDL_Point> pending_spawn_world_pos_{};
    std::optional<PendingSpawnGroupOpen> pending_spawn_group_open_{};
    std::optional<std::string> active_spawn_group_id_{};
    bool suppress_spawn_group_close_clear_ = false;

    double zoom_scale_factor_ = 1.1;
    PanAndZoom pan_zoom_;
    std::unordered_set<std::string> room_spawn_ids_;
    void rebuild_room_spawn_id_cache();
    bool is_room_spawn_id(const std::string& spawn_id) const;
    bool asset_belongs_to_room(const Asset* asset) const;
};




