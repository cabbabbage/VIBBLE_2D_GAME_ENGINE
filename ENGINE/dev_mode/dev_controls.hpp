#pragma once

#include <SDL.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "MapLightPanel.hpp"


class Asset;
class Input;
class Assets;
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
        MapEditor
    };

    DevControls(Assets* owner, int screen_w, int screen_h);
    ~DevControls();

    void set_input(Input* input);
    void set_player(Asset* player);
    void set_active_assets(std::vector<Asset*>& actives);
    void set_screen_dimensions(int width, int height);
    void set_current_room(Room* room);
    void set_rooms(std::vector<Room*>* rooms);

    void set_map_info(nlohmann::json* map_info, MapLightPanel::SaveCallback on_save);
    void set_map_context(nlohmann::json* map_info, const std::string& map_path);


    Room* resolve_current_room(Room* detected_room);

    void set_enabled(bool enabled);
    bool is_enabled() const { return enabled_; }
    Mode mode() const { return mode_; }

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

    void open_spawn_group_for_asset(Asset* asset);
    void finalize_asset_drag(Asset* asset, const std::shared_ptr<AssetInfo>& info);

    void toggle_room_config();
    void close_room_config();
    bool is_room_config_open() const;

    void begin_area_edit_for_selected_asset(const std::string& area_name);
    void focus_camera_on_asset(Asset* asset, double zoom_factor = 0.8, int duration_steps = 25);

    void reset_click_state();
    void clear_selection();
    void purge_asset(Asset* asset);

    const std::vector<Asset*>& get_selected_assets() const;
    const std::vector<Asset*>& get_highlighted_assets() const;
    Asset* get_hovered_asset() const;

    void set_zoom_scale_factor(double factor);
    double get_zoom_scale_factor() const;

private:
    bool can_use_room_editor_ui() const;
    void enter_map_editor_mode();
    void exit_map_editor_mode(bool focus_player, bool restore_previous_state);
    void handle_map_selection();
    void toggle_map_light_panel();
    void toggle_camera_panel();
    void close_camera_panel();
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

private:
    Assets* assets_ = nullptr;
    Input* input_ = nullptr;
    std::vector<Asset*>* active_assets_ = nullptr;
    Asset* player_ = nullptr;
    Room* current_room_ = nullptr;
    Room* detected_room_ = nullptr;
    Room* dev_selected_room_ = nullptr;
    std::vector<Room*>* rooms_ = nullptr;

    int screen_w_ = 0;
    int screen_h_ = 0;
    bool enabled_ = false;
    Mode mode_ = Mode::RoomEditor;

    std::unique_ptr<RoomEditor> room_editor_;
    std::unique_ptr<MapEditor> map_editor_;
    nlohmann::json* map_info_json_ = nullptr;
    MapLightPanel::SaveCallback map_light_save_cb_;
    std::unique_ptr<MapModeUI> map_mode_ui_;
    std::unique_ptr<CameraUIPanel> camera_panel_;
    std::unique_ptr<RegenerateRoomPopup> regenerate_popup_;
    std::string map_path_;
    bool pointer_over_camera_panel_ = false;
};

