#pragma once

#include <SDL.h>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <utility>
#include <vector>

#include "dev_mode/asset_config_ui.hpp"
#include "dev_mode/pan_and_zoom.hpp"

class Asset;
class Input;
class Assets;
class AssetLibraryUI;
class AssetInfoUI;
class AreaOverlayEditor;
class RoomConfigurator;
class AssetsConfig;
class AssetInfo;
class Room;
class MapGrid;
class DMButton;

class RoomEditor {
public:
    RoomEditor(Assets* owner, int screen_w, int screen_h);
    ~RoomEditor();

    void set_input(Input* input);
    void set_player(Asset* player);
    void set_active_assets(std::vector<Asset*>& actives);
    void set_screen_dimensions(int width, int height);
    void set_current_room(Room* room);

    void set_enabled(bool enabled);
    bool is_enabled() const { return enabled_; }

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

    void open_asset_config_for_asset(Asset* asset);
    void finalize_asset_drag(Asset* asset, const std::shared_ptr<AssetInfo>& info);

    void toggle_room_config();
    void close_room_config();
    bool is_room_config_open() const;

    void begin_area_edit_for_selected_asset(const std::string& area_name);
    void focus_camera_on_asset(Asset* asset, double zoom_factor = 0.8, int duration_steps = 25);
    void focus_camera_on_room_center(bool reframe_zoom = true);

    void reset_click_state();
    void clear_selection();
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

    struct DraggedAssetState {
        Asset* asset = nullptr;
        SDL_Point start_pos{0, 0};
        SDL_FPoint direction{0.0f, 0.0f};
        double start_distance = 0.0;
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
    void begin_drag_session(const SDL_Point& world_mouse, bool ctrl_modifier);
    void update_drag_session(const SDL_Point& world_mouse);
    void apply_perimeter_drag(const SDL_Point& world_mouse);
    void finalize_drag_session();
    void reset_drag_state();
    nlohmann::json* find_spawn_entry(const std::string& spawn_id);
    SDL_Point get_room_center() const;
    std::pair<int, int> get_room_dimensions() const;
    void refresh_assets_config_ui();
    void update_exact_json(nlohmann::json& entry, const Asset& asset, SDL_Point center, int width, int height);
    void update_percent_json(nlohmann::json& entry, const Asset& asset, SDL_Point center, int width, int height);
    void update_perimeter_border_json(nlohmann::json& entry, double border_shift);
    void update_perimeter_center_json(nlohmann::json& entry, SDL_Point offset);
    void handle_spawn_config_change(const nlohmann::json& entry, const AssetConfigUI::ChangeSummary& summary);
    void respawn_spawn_group(const nlohmann::json& entry);
    std::unique_ptr<MapGrid> build_room_grid(const std::string& ignore_spawn_id) const;
    void integrate_spawned_assets(std::vector<std::unique_ptr<Asset>>& spawned);
    void regenerate_current_room();
    void position_regenerate_button();

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
    std::unique_ptr<AssetsConfig> assets_cfg_ui_;
    std::unique_ptr<AreaOverlayEditor> area_editor_;
    std::unique_ptr<RoomConfigurator> room_cfg_ui_;

    bool last_area_editor_active_ = false;
    bool reopen_info_after_area_edit_ = false;
    std::shared_ptr<AssetInfo> info_for_reopen_;

    Asset* hovered_asset_ = nullptr;
    std::vector<Asset*> selected_assets_;
    std::vector<Asset*> highlighted_assets_;

    bool dragging_ = false;
    Asset* drag_anchor_asset_ = nullptr;
    DragMode drag_mode_ = DragMode::None;
    std::vector<DraggedAssetState> drag_states_;
    SDL_Point drag_last_world_{0, 0};
    SDL_Point drag_room_center_{0, 0};
    double drag_perimeter_base_radius_ = 0.0;
    SDL_Point drag_perimeter_start_offset_{0, 0};
    bool drag_moved_ = false;
    std::string drag_spawn_id_;
    Uint32 last_click_time_ms_ = 0;
    Asset* last_click_asset_ = nullptr;

    int click_buffer_frames_ = 0;
    int rclick_buffer_frames_ = 0;
    int hover_miss_frames_ = 0;

    double zoom_scale_factor_ = 1.1;
    std::unique_ptr<DMButton> regenerate_button_;
    SDL_Rect regenerate_button_rect_{0, 0, 0, 0};
    PanAndZoom pan_zoom_;
};
