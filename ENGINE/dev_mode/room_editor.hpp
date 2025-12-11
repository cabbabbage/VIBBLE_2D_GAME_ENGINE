#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <SDL.h>
#include <SDL_ttf.h>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "dev_mode/pan_and_zoom.hpp"

class Asset;
class Input;
class Assets;
class AssetLibraryUI;
class AssetInfoUI;
class Area;
class RoomConfigurator;
class SpawnGroupConfig;
class AssetInfo;
class Room;
class WarpedScreenGrid;
namespace vibble::grid {
class Occupancy;
class Grid;
}
class DMButton;
class DevFooterBar;
class DevControls;

namespace devmode::core {
class ManifestStore;
}

class RoomEditor {
public:
    RoomEditor(Assets* owner, int screen_w, int screen_h);
    ~RoomEditor();

    void set_input(Input* input);
    void set_player(Asset* player);
    void set_active_assets(std::vector<Asset*>& actives, std::uint64_t generation);
    void set_screen_dimensions(int width, int height);
    void set_current_room(Room* room);
    void set_room_config_visible(bool visible);
    void set_shared_footer_bar(DevFooterBar* footer);
    void set_header_visibility_callback(std::function<void(bool)> cb);
    void set_map_assets_panel_callback(std::function<void()> cb);
    void set_boundary_assets_panel_callback(std::function<void()> cb);
    void set_manifest_store(devmode::core::ManifestStore* store);

    void set_enabled(bool enabled, bool preserve_camera_state = false);
    bool is_enabled() const { return enabled_; }

    void update(const Input& input);
    void update_ui(const Input& input);
    bool handle_sdl_event(const SDL_Event& event);
    bool is_room_panel_blocking_point(int x, int y) const;
    bool is_room_ui_blocking_point(int x, int y) const;
    bool is_shift_key_down() const;
    void render_overlays(SDL_Renderer* renderer);

    void toggle_asset_library();
    void open_asset_library();
    void close_asset_library();
    bool is_asset_library_open() const;
    bool is_library_drag_active() const;

    std::shared_ptr<AssetInfo> consume_selected_asset_from_library();

    void open_asset_info_editor(const std::shared_ptr<AssetInfo>& info);
    void open_animation_editor_for_asset(const std::shared_ptr<AssetInfo>& info);
    void open_asset_info_editor_for_asset(Asset* asset);
    void close_asset_info_editor();
    bool is_asset_info_editor_open() const;
    bool is_asset_info_lighting_section_expanded() const;
    bool has_active_modal() const;
    void pulse_active_modal_header();

    void finalize_asset_drag(Asset* asset, const std::shared_ptr<AssetInfo>& info);

    void toggle_room_config();
    void open_room_config();
    void close_room_config();
    bool is_room_config_open() const;
    void regenerate_room();
    void regenerate_room_from_template(Room* source_room);

    using RoomAssetsSavedCallback = std::function<void()>;
    void set_room_assets_saved_callback(RoomAssetsSavedCallback cb);

    void focus_camera_on_asset(Asset* asset, double zoom_factor = 0.8, int duration_steps = 0);
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

    bool is_spawn_group_panel_visible() const;

protected:
    void handle_spawn_config_change(const nlohmann::json& entry);

private:

    void begin_area_drag_session(const std::string& area_name, const SDL_Point& world_mouse);
    void update_area_drag_session(const SDL_Point& world_mouse);
    void finalize_area_drag_session();
    nlohmann::json* find_area_entry_json(Room* room, const std::string& area_name) const;
    void ensure_area_anchor_spawn_entry(Room* room, const std::string& area_name);
    enum class BlockingPanel {
        Camera,
        Lighting,
        MapLayers,
        AssetLibrary,
        Count,
};

    void set_blocking_panel_visible(BlockingPanel panel, bool visible);
    bool any_blocking_panel_visible() const;
    void open_room_config_for(Asset* asset);
    enum class DragMode {
        None,
        Free,
        Exact,
        Percent,
        Perimeter,
        PerimeterCenter,
        Edge,
};

    enum class ActiveModal {
        None,
        AssetInfo,
};

    struct PerimeterOverlay {
        SDL_Point center{0, 0};
        double radius = 0.0;
};

    struct DraggedAssetState {
        Asset*     asset     = nullptr;
        SDL_Point  start_pos {0, 0};
        SDL_Point  last_synced_pos {0, 0};
        SDL_FPoint direction {0.0f, 0.0f};
        bool       active    = false;
        double     edge_length = 0.0;
};
    void handle_mouse_input(const Input& input);
    Asset* hit_test_asset(SDL_Point screen_point, SDL_Renderer* renderer) const;
    void update_hover_state(Asset* hit);
    void handle_click(const Input& input);
    std::optional<std::string> find_room_area_at_point(SDL_Point world_point);
    void update_highlighted_assets();
    bool is_ui_blocking_input(int mx, int my) const;
    bool should_enable_mouse_controls() const;
    void handle_shortcuts(const Input& input);
    void handle_delete_shortcut(const Input& input);
    void ensure_room_configurator();
    void ensure_spawn_group_config_ui();
    void update_room_config_bounds();
    void begin_drag_session(const SDL_Point& world_mouse, bool ctrl_modifier);
    void update_drag_session(const SDL_Point& world_mouse);
    void apply_perimeter_drag(const SDL_Point& world_mouse);
    void apply_edge_drag(const SDL_Point& world_mouse);
    bool snap_dragged_assets_to_grid();
    void sync_dragged_assets_immediately();
    void update_spawn_json_during_drag();
    void finalize_drag_session();
    void reset_drag_state();
    nlohmann::json* find_spawn_entry(const std::string& spawn_id);
    struct SpawnEntryResolution {
        enum class Source {
            None,
            Room,
            Map,
};
        nlohmann::json* entry = nullptr;
        nlohmann::json* owner_array = nullptr;
        Source source = Source::None;
        bool valid() const { return entry != nullptr; }
};
    SpawnEntryResolution locate_spawn_entry(const std::string& spawn_id);
    SDL_Point get_room_center() const;
    std::pair<int, int> get_room_dimensions() const;
    int current_grid_resolution() const;
    void refresh_spawn_group_config_ui();
    void update_spawn_group_config_anchor();
    SDL_Point spawn_groups_anchor_point() const;
    void clear_active_spawn_group_target();
    void sync_spawn_group_panel_with_selection();
    void update_exact_json(nlohmann::json& entry, const Asset& asset, SDL_Point center, int width, int height);
    void update_percent_json(nlohmann::json& entry, const Asset& asset, SDL_Point center, int width, int height);
    void save_perimeter_json(nlohmann::json& entry, int dx, int dy, int orig_w, int orig_h, int radius);
    void save_edge_json(nlohmann::json& entry, int inset_percent);
    const Area* find_edge_area_for_entry(const nlohmann::json& entry) const;
    double edge_length_along_direction(const Area& area, SDL_Point center, SDL_FPoint direction) const;
    void respawn_spawn_group(const nlohmann::json& entry);
    std::unique_ptr<vibble::grid::Occupancy> build_room_grid(const std::string& ignore_spawn_id) const;
    void render_room_labels(SDL_Renderer* renderer);
    void render_room_label(SDL_Renderer* renderer, Room* room, SDL_FPoint desired_center);
    SDL_Rect label_background_rect(int text_w, int text_h, SDL_FPoint desired_center) const;
    SDL_Rect resolve_edge_overlap(SDL_Rect rect, SDL_FPoint desired_center);
    SDL_Rect resolve_horizontal_edge_overlap(SDL_Rect rect, float desired_center_x, bool top_edge);
    SDL_Rect resolve_vertical_edge_overlap(SDL_Rect rect, float desired_center_y, bool left_edge);
    static bool rects_overlap(const SDL_Rect& a, const SDL_Rect& b);
    void ensure_label_font();
    void release_label_font();
    void invalidate_label_cache(Room* room);
    void invalidate_all_room_labels();
    void prune_label_cache(const std::vector<Room*>& rooms);
    void integrate_spawned_assets(std::vector<std::unique_ptr<Asset>>& spawned);
    void regenerate_current_room();
    void configure_shared_panel();
    void refresh_room_config_visibility();
    void sanitize_perimeter_spawn_groups();
    bool sanitize_perimeter_spawn_groups(nlohmann::json& groups);
    std::optional<PerimeterOverlay> compute_perimeter_overlay_for_drag();
    std::optional<PerimeterOverlay> compute_perimeter_overlay_for_spawn(const std::string& spawn_id);

    std::optional<std::vector<SDL_Point>> compute_edge_path_for_drag();
    std::optional<std::vector<SDL_Point>> compute_edge_path_for_spawn(const std::string& spawn_id);
    void add_spawn_group_internal();
    bool delete_spawn_group_internal(const std::string& spawn_id);
    bool remove_spawn_group_by_id(const std::string& spawn_id);
    void move_spawn_group_internal(const std::string& spawn_id, int dir);
    void reorder_spawn_group_internal(const std::string& spawn_id, size_t target_index);
    void open_spawn_group_editor_by_id(const std::string& spawn_id);
    void reopen_room_configurator();
    void notify_room_assets_saved();
    void save_current_room_assets_json();
    void copy_selected_spawn_group();
    void paste_spawn_group_from_clipboard();
    std::optional<std::string> selected_spawn_group_id() const;
    bool spawn_group_is_boundary(const std::string& spawn_id) const;
    Room* resolve_room_for_clipboard_action() const;
    void select_spawn_group_assets(const std::string& spawn_id);
    void remap_clipboard_entry_to_room(nlohmann::json& entry, Room* room);
    void ensure_clipboard_position_is_valid(nlohmann::json& entry, Room* room);
    static std::string strip_copy_suffix(const std::string& name);
    std::string next_clipboard_display_name();
    void show_notice(const std::string& message) const;
    class Asset* find_asset_spawn_owner(const std::string& spawn_id) const;
    void respawn_asset_child_spawn_group(class Asset* owner, const nlohmann::json& entry);
    static bool asset_info_contains_spawn_group(const class AssetInfo* info, const std::string& spawn_id);
    void mark_highlight_dirty();
    bool spawn_group_locked(const std::string& spawn_id) const;

    struct AssetSpatialEntry {
        SDL_Rect bounds{0, 0, 0, 0};
        int screen_y = std::numeric_limits<int>::min();
        int z_index = std::numeric_limits<int>::min();
        std::vector<int64_t> cells;
};

    void mark_spatial_index_dirty() const;
    bool ensure_spatial_index(const WarpedScreenGrid& cam) const;
    bool camera_state_changed(const WarpedScreenGrid& cam) const;
    float compute_reference_screen_height(const WarpedScreenGrid& cam, float inv_scale) const;
    bool compute_asset_screen_bounds(const WarpedScreenGrid& cam, float reference_height, float inv_scale, Asset* asset, SDL_Rect& out_rect, int& out_screen_y) const;
    void rebuild_spatial_index(const WarpedScreenGrid& cam) const;
    void insert_asset_entry(Asset* asset, const SDL_Rect& rect, int screen_y) const;
    void add_asset_to_cell(Asset* asset, int cell_x, int cell_y, std::vector<int64_t>& cell_keys) const;
    void remove_asset_from_spatial_index(Asset* asset) const;
    void refresh_asset_spatial_entry(const WarpedScreenGrid& cam, Asset* asset) const;
    void refresh_spatial_entries_for_dragged_assets();
    std::vector<Asset*> gather_candidate_assets_for_point(SDL_Point screen_point) const;
    Asset* hit_test_asset_fallback(const WarpedScreenGrid& cam, SDL_Point screen_point) const;

private:
    Assets* assets_ = nullptr;
    Input* input_ = nullptr;
    std::vector<Asset*>* active_assets_ = nullptr;
    std::uint64_t active_assets_version_ = 0;
    Asset* player_ = nullptr;
    Room* current_room_ = nullptr;

    int screen_w_ = 0;
    int screen_h_ = 0;
    bool enabled_ = false;
    bool mouse_controls_enabled_last_frame_ = false;

    std::unique_ptr<AssetLibraryUI> library_ui_;
    std::unique_ptr<AssetInfoUI> info_ui_;

    std::unique_ptr<RoomConfigurator> room_cfg_ui_;
    SDL_Rect room_config_bounds_{0, 0, 0, 0};
    DevFooterBar* shared_footer_bar_ = nullptr;
    bool room_config_dock_open_ = false;
    bool room_config_was_visible_ = false;
    bool suppress_room_config_selection_clear_ = false;
    ActiveModal active_modal_ = ActiveModal::None;
    std::function<void(bool)> header_visibility_callback_{};
    std::function<void()> open_map_assets_panel_callback_{};
    std::function<void()> open_boundary_assets_panel_callback_{};
    bool room_config_panel_visible_ = false;
    bool asset_info_panel_visible_ = false;

    std::array<bool, static_cast<size_t>(BlockingPanel::Count)> blocking_panel_visible_{};

    Asset* hovered_asset_ = nullptr;
    std::vector<Asset*> selected_assets_;
    std::vector<Asset*> highlighted_assets_;
    bool highlight_dirty_ = true;

    SDL_Point snapped_cursor_world_{0, 0};
    int cursor_snap_resolution_ = 0;

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
    int drag_resolution_ = 0;

    const Area* drag_edge_area_ = nullptr;
    SDL_Point drag_edge_center_{0, 0};
    double drag_edge_inset_percent_ = 100.0;

    devmode::core::ManifestStore* manifest_store_ = nullptr;
    int drag_perimeter_curr_h_ = 0;
    bool drag_moved_ = false;
    std::string drag_spawn_id_;
    bool suppress_next_left_click_ = false;

    std::optional<int> overlay_resolution_before_drag_{};

    int click_buffer_frames_ = 0;
    int rclick_buffer_frames_ = 0;
    int hover_miss_frames_ = 0;
    Asset* last_click_asset_ = nullptr;
    Uint32 last_click_time_ms_ = 0;
    std::optional<SDL_Point> pending_spawn_world_pos_{};
    std::optional<std::string> active_spawn_group_id_{};
    bool suppress_spawn_group_close_clear_ = false;
    std::unique_ptr<SpawnGroupConfig> spawn_group_panel_{};

    bool area_dragging_ = false;
    bool area_drag_moved_ = false;
    std::string area_drag_name_;
    int area_drag_resolution_ = 0;
    SDL_Point area_drag_start_world_{0, 0};
    SDL_Point area_drag_last_world_{0, 0};

    struct SpawnGroupClipboard {
        nlohmann::json entry;
        std::string base_display_name;
        int paste_count = 0;
};
    std::optional<SpawnGroupClipboard> spawn_group_clipboard_{};

    TTF_Font* label_font_ = nullptr;
    std::vector<SDL_Rect> label_rects_;
    struct LabelCacheEntry {
        SDL_Texture* texture = nullptr;
        SDL_Point text_size{0, 0};
        std::string last_name;
        SDL_Color last_color{0, 0, 0, 0};
        bool dirty = true;
};
    std::unordered_map<Room*, LabelCacheEntry> label_cache_;

    double zoom_scale_factor_ = 1.1;
    PanAndZoom pan_zoom_;
    std::unordered_set<std::string> room_spawn_ids_;
    void rebuild_room_spawn_id_cache();
    bool is_room_spawn_id(const std::string& spawn_id) const;
    bool asset_belongs_to_room(const Asset* asset) const;

    RoomAssetsSavedCallback room_assets_saved_callback_;
    std::string rename_active_room(const std::string& old_name, const std::string& desired_name);
    std::shared_ptr<AssetInfo> last_selected_from_library_;

    friend class DevControls;

    static constexpr int kSpatialCellSize = 256;

    mutable bool spatial_index_dirty_ = true;
    mutable bool cached_camera_state_valid_ = false;
    mutable float cached_camera_scale_ = 0.0f;
    mutable SDL_Point cached_camera_center_{0, 0};
    mutable bool cached_camera_parallax_enabled_ = false;
    mutable bool cached_camera_realism_enabled_ = false;
    mutable float cached_reference_screen_height_ = 1.0f;
    mutable bool cached_reference_height_valid_ = false;
    mutable std::unordered_map<Asset*, AssetSpatialEntry> asset_bounds_cache_;
    mutable std::unordered_map<int64_t, std::vector<Asset*>> spatial_grid_;
};
