#pragma once

#include <SDL.h>

#include <functional>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <optional>

#include <nlohmann/json_fwd.hpp>

#include "MapLightPanel.hpp"
#include "asset_filter_bar.hpp"
#include "trail_editor_suite.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "map_assets_modals.hpp"

class Asset;
class Input;
class Assets;
class WarpedScreenGrid;
class AssetInfo;
class Room;
class RoomEditor;
class MapEditor;
class MapModeUI;
class CameraUIPanel;
class ForegroundBackgroundEffectPanel;
class RegenerateRoomPopup;

namespace animation_editor {
class AnimationDocument;
class PreviewProvider;
class AnimationEditorWindow;
}

class DevControls {
public:
    enum class Mode {
        RoomEditor,
        MapEditor
};

    DevControls(Assets* owner, int screen_w, int screen_h);
    ~DevControls();

    void set_input(Input* input);
    void set_player(Asset* player);
    void set_active_assets(std::vector<Asset*>& actives, std::uint64_t version);
    void set_screen_dimensions(int width, int height);
    void set_current_room(Room* room, bool force_refresh = false);
    void set_rooms(std::vector<Room*>* rooms, std::size_t generation = 0);

    void set_map_info(nlohmann::json* map_info, MapLightPanel::SaveCallback on_save);
    void set_map_context(nlohmann::json* map_info, const std::string& map_path);

    Room* resolve_current_room(Room* detected_room);

    void set_enabled(bool enabled);
    bool is_enabled() const { return enabled_; }
    Mode mode() const { return mode_; }

    void set_camera_override_for_testing(WarpedScreenGrid* camera_override);

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
    void open_animation_editor_for_asset(const std::shared_ptr<AssetInfo>& info);
    void close_asset_info_editor();
    bool is_asset_info_editor_open() const;
    bool is_asset_info_lighting_section_expanded() const;

    void finalize_asset_drag(Asset* asset, const std::shared_ptr<AssetInfo>& info);

    [[nodiscard]] devmode::core::ManifestStore& manifest_store();
    [[nodiscard]] const devmode::core::ManifestStore& manifest_store() const;

    void toggle_room_config();
    void close_room_config();
    bool is_room_config_open() const;

    void set_map_light_panel_visible(bool visible);
    bool is_map_light_panel_visible() const;

    void focus_camera_on_asset(Asset* asset, double zoom_factor = 0.8, int duration_steps = 0);

    void reset_click_state();
    void clear_selection();
    void purge_asset(Asset* asset);

    void notify_spawn_group_config_changed(const nlohmann::json& entry);
    void notify_spawn_group_removed(const std::string& spawn_id);

    const std::vector<Asset*>& get_selected_assets() const;
    const std::vector<Asset*>& get_highlighted_assets() const;
    Asset* get_hovered_asset() const;

    void set_zoom_scale_factor(double factor);
    double get_zoom_scale_factor() const;

    void filter_active_assets(std::vector<Asset*>& assets) const;

    bool is_grid_overlay_enabled() const { return grid_overlay_enabled_; }
    bool is_snap_to_grid_enabled() const { return snap_to_grid_enabled_; }
    int  grid_cell_size_px() const { return grid_cell_size_px_; }

    void begin_frame_editor_session(Asset* asset, std::shared_ptr<animation_editor::AnimationDocument> document, std::shared_ptr<animation_editor::PreviewProvider> preview, const std::string& animation_id, animation_editor::AnimationEditorWindow* host_to_toggle);
    void end_frame_editor_session();
    bool is_frame_editor_session_active() const;

private:
    bool can_use_room_editor_ui() const;
    void enter_map_editor_mode();
    void exit_map_editor_mode(bool focus_player, bool restore_previous_state);
    void handle_map_selection();
    void toggle_map_light_panel();
    void toggle_camera_panel();
    void close_camera_panel();
    void toggle_image_effect_panel();
    void close_image_effect_panel();
    void toggle_map_assets_modal();
    void toggle_boundary_assets_modal();
    void open_map_assets_modal();
    void open_boundary_assets_modal();
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
    void create_trail_template();

    void refresh_active_asset_filters();
    void reset_asset_filters();
    bool passes_asset_filters(Asset* asset) const;
    bool should_hide_assets_for_map_mode() const;
    void apply_camera_area_render_flag();
    void set_mode_from_header(int header_mode);
    void set_mode(Mode new_mode);
    void apply_overlay_grid_resolution(int resolution, bool user_override, bool update_stepper, bool update_footer);
    void restore_filter_hidden_assets() const;
    void apply_dark_mask_visibility();
    bool lighting_section_forces_dark_mask() const;

private:
    int map_radius_or_default() const;
    void remove_spawn_group_assets(const std::string& spawn_id);
    void integrate_spawned_assets(std::vector<std::unique_ptr<Asset>>& spawned);
    void regenerate_map_spawn_group(const nlohmann::json& entry);
    void regenerate_boundary_spawn_group(const nlohmann::json& entry);
    void regenerate_map_grid_assets();
    void ensure_map_assets_modal_open();
    void ensure_boundary_assets_modal_open();

    bool persist_map_info_to_disk();

    Assets* assets_ = nullptr;
    Input* input_ = nullptr;
    std::vector<Asset*>* active_assets_ = nullptr;
    std::uint64_t active_assets_version_ = 0;
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
    MapLightPanel::SaveCallback map_grid_save_cb_;
    std::function<void()> map_grid_regen_cb_;
    std::unique_ptr<MapModeUI> map_mode_ui_;
    std::unique_ptr<CameraUIPanel> camera_panel_;
    std::unique_ptr<ForegroundBackgroundEffectPanel> image_effect_panel_;
    std::unique_ptr<RegenerateRoomPopup> regenerate_popup_;
    std::string map_path_;
    bool pointer_over_camera_panel_ = false;
    bool pointer_over_image_effect_panel_ = false;
    bool modal_headers_hidden_ = false;
    bool sliding_headers_hidden_ = false;
    mutable std::unordered_map<Asset*, bool> filter_hidden_assets_;
    std::unique_ptr<TrailEditorSuite> trail_suite_;
    std::unique_ptr<Room> pending_trail_template_;
    devmode::core::ManifestStore manifest_store_;
    AssetFilterBar asset_filter_;

    WarpedScreenGrid* camera_override_for_testing_ = nullptr;

    std::unique_ptr<SingleSpawnGroupModal> map_assets_modal_;
    std::unique_ptr<SingleSpawnGroupModal> boundary_assets_modal_;

    bool grid_overlay_enabled_ = false;
    bool snap_to_grid_enabled_ = false;
    int  grid_overlay_resolution_r_ = 0;
    bool grid_overlay_resolution_user_override_ = false;
    int  grid_cell_size_px_ = 1;

    bool depth_effects_forced_realism_disabled_ = false;
    bool depth_effects_prev_realism_enabled_ = true;

    std::unique_ptr<class DMNumericStepper> grid_resolution_stepper_;

    std::unique_ptr<class DMCheckbox> grid_overlay_checkbox_;

    SDL_Rect grid_stepper_rect_{0,0,0,0};
    SDL_Rect grid_checkbox_rect_{0,0,0,0};

    std::unique_ptr<class FrameEditorSession> frame_editor_session_;
    bool frame_editor_prev_grid_overlay_ = false;

    bool frame_editor_prev_asset_info_open_ = false;
    Asset* frame_editor_asset_for_reopen_ = nullptr;

    bool render_suppression_in_progress_ = false;

    enum class DepthCueDragState { None, Foreground, Background };
    DepthCueDragState depthcue_drag_state_ = DepthCueDragState::None;
    float depthcue_drag_start_y_ = 0.0f;
    int depthcue_drag_mouse_start_ = 0;
    bool hover_depthcue_foreground_ = false;
    bool hover_depthcue_background_ = false;
};
