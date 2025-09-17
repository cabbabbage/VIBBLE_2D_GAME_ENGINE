#include "dev_controls.hpp"

#include "dev_mode/map_editor.hpp"
#include "dev_mode/room_editor.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "room/room.hpp"
#include "utils/input.hpp"

#include <algorithm>
#include <cmath>

DevControls::DevControls(Assets* owner, int screen_w, int screen_h)
    : assets_(owner),
      screen_w_(screen_w),
      screen_h_(screen_h) {
    room_editor_ = std::make_unique<RoomEditor>(assets_, screen_w_, screen_h_);
    map_editor_ = std::make_unique<MapEditor>(assets_);
}

DevControls::~DevControls() = default;

void DevControls::set_input(Input* input) {
    input_ = input;
    if (room_editor_) room_editor_->set_input(input);
    if (map_editor_) map_editor_->set_input(input);
}

void DevControls::set_player(Asset* player) {
    player_ = player;
    if (room_editor_) room_editor_->set_player(player);
}

void DevControls::set_active_assets(std::vector<Asset*>& actives) {
    active_assets_ = &actives;
    if (room_editor_) room_editor_->set_active_assets(actives);
}

void DevControls::set_screen_dimensions(int width, int height) {
    screen_w_ = width;
    screen_h_ = height;
    if (room_editor_) room_editor_->set_screen_dimensions(width, height);
    if (map_editor_) map_editor_->set_screen_dimensions(width, height);
}

void DevControls::set_current_room(Room* room) {
    current_room_ = room;
    if (room_editor_) room_editor_->set_current_room(room);
}

void DevControls::set_rooms(std::vector<Room*>* rooms) {
    rooms_ = rooms;
    if (map_editor_) map_editor_->set_rooms(rooms);
}

Room* DevControls::resolve_current_room(Room* detected_room) {
    detected_room_ = detected_room;
    if (!enabled_) {
        dev_selected_room_ = nullptr;
        set_current_room(detected_room_);
        return current_room_;
    }

    if (!dev_selected_room_) {
        dev_selected_room_ = detected_room_;
    }

    set_current_room(dev_selected_room_ ? dev_selected_room_ : detected_room_);
    return current_room_;
}

void DevControls::set_enabled(bool enabled) {
    if (enabled == enabled_) return;
    enabled_ = enabled;

    if (enabled_) {
        mode_ = Mode::RoomEditor;
        dev_selected_room_ = current_room_ ? current_room_ : detected_room_;
        if (room_editor_) room_editor_->set_enabled(true);
        if (map_editor_) map_editor_->set_enabled(false);
    } else {
        if (map_editor_ && map_editor_->is_enabled()) {
            map_editor_->exit(true, false);
        }
        mode_ = Mode::RoomEditor;
        dev_selected_room_ = nullptr;
        if (room_editor_) {
            room_editor_->set_enabled(false);
        }
    }
}

void DevControls::update(const Input& input) {
    if (!enabled_) return;

    if (assets_) {
        camera& cam = assets_->getView();
        const double scale = std::max(0.0001, static_cast<double>(cam.get_scale()));
        // Enter map mode once the camera sees roughly eight times the base area.
        // Use an explicit constant instead of std::sqrt to avoid non-constexpr
        // evaluation requirements on older standard libraries.
        constexpr double kEnterScale = 2.8284271247461903;  // sqrt(8)
        constexpr double kExitScale = kEnterScale * 0.85;

        if (mode_ != Mode::MapEditor && scale >= kEnterScale) {
            enter_map_editor_mode();
        } else if (mode_ == Mode::MapEditor && scale < kExitScale) {
            exit_map_editor_mode(false, true);
        }
    }

    if (mode_ == Mode::MapEditor) {
        if (map_editor_) {
            map_editor_->update(input);
            handle_map_selection();
        }
    } else if (room_editor_ && room_editor_->is_enabled()) {
        room_editor_->update(input);
    }
}

void DevControls::update_ui(const Input& input) {
    if (!enabled_) return;
    if (mode_ != Mode::RoomEditor) return;
    if (!room_editor_ || !room_editor_->is_enabled()) return;

    room_editor_->update_ui(input);
}

void DevControls::handle_sdl_event(const SDL_Event& event) {
    if (!enabled_) return;
    if (!can_use_room_editor_ui()) return;
    if (room_editor_) room_editor_->handle_sdl_event(event);
}

void DevControls::render_overlays(SDL_Renderer* renderer) {
    if (!enabled_) return;

    if (mode_ == Mode::MapEditor) {
        if (map_editor_) map_editor_->render(renderer);
    } else if (room_editor_) {
        room_editor_->render_overlays(renderer);
    }
}

void DevControls::toggle_asset_library() {
    if (!can_use_room_editor_ui()) return;
    room_editor_->toggle_asset_library();
}

void DevControls::open_asset_library() {
    if (!can_use_room_editor_ui()) return;
    room_editor_->open_asset_library();
}

void DevControls::close_asset_library() {
    if (room_editor_) room_editor_->close_asset_library();
}

bool DevControls::is_asset_library_open() const {
    if (!room_editor_) return false;
    return room_editor_->is_asset_library_open();
}

std::shared_ptr<AssetInfo> DevControls::consume_selected_asset_from_library() {
    if (!can_use_room_editor_ui()) return nullptr;
    return room_editor_->consume_selected_asset_from_library();
}

void DevControls::open_asset_info_editor(const std::shared_ptr<AssetInfo>& info) {
    if (!can_use_room_editor_ui()) return;
    room_editor_->open_asset_info_editor(info);
}

void DevControls::open_asset_info_editor_for_asset(Asset* asset) {
    if (!can_use_room_editor_ui()) return;
    room_editor_->open_asset_info_editor_for_asset(asset);
}

void DevControls::close_asset_info_editor() {
    if (room_editor_) room_editor_->close_asset_info_editor();
}

bool DevControls::is_asset_info_editor_open() const {
    if (!room_editor_) return false;
    return room_editor_->is_asset_info_editor_open();
}

void DevControls::open_asset_config_for_asset(Asset* asset) {
    if (!can_use_room_editor_ui()) return;
    room_editor_->open_asset_config_for_asset(asset);
}

void DevControls::finalize_asset_drag(Asset* asset, const std::shared_ptr<AssetInfo>& info) {
    if (!can_use_room_editor_ui()) return;
    room_editor_->finalize_asset_drag(asset, info);
}

void DevControls::toggle_room_config() {
    if (!can_use_room_editor_ui()) return;
    room_editor_->toggle_room_config();
}

void DevControls::close_room_config() {
    if (room_editor_) room_editor_->close_room_config();
}

bool DevControls::is_room_config_open() const {
    if (!room_editor_) return false;
    return room_editor_->is_room_config_open();
}

void DevControls::begin_area_edit_for_selected_asset(const std::string& area_name) {
    if (!can_use_room_editor_ui()) return;
    room_editor_->begin_area_edit_for_selected_asset(area_name);
}

void DevControls::focus_camera_on_asset(Asset* asset, double zoom_factor, int duration_steps) {
    if (!room_editor_) return;
    room_editor_->focus_camera_on_asset(asset, zoom_factor, duration_steps);
}

void DevControls::reset_click_state() {
    if (room_editor_) room_editor_->reset_click_state();
}

void DevControls::clear_selection() {
    if (room_editor_) room_editor_->clear_selection();
}

void DevControls::purge_asset(Asset* asset) {
    if (!room_editor_) return;
    room_editor_->purge_asset(asset);
}

const std::vector<Asset*>& DevControls::get_selected_assets() const {
    static std::vector<Asset*> empty;
    if (!can_use_room_editor_ui()) return empty;
    return room_editor_->get_selected_assets();
}

const std::vector<Asset*>& DevControls::get_highlighted_assets() const {
    static std::vector<Asset*> empty;
    if (!can_use_room_editor_ui()) return empty;
    return room_editor_->get_highlighted_assets();
}

Asset* DevControls::get_hovered_asset() const {
    if (!can_use_room_editor_ui()) return nullptr;
    return room_editor_->get_hovered_asset();
}

void DevControls::set_zoom_scale_factor(double factor) {
    if (room_editor_) room_editor_->set_zoom_scale_factor(factor);
}

double DevControls::get_zoom_scale_factor() const {
    if (!room_editor_) return 1.0;
    return room_editor_->get_zoom_scale_factor();
}

bool DevControls::can_use_room_editor_ui() const {
    return enabled_ && mode_ == Mode::RoomEditor && room_editor_ && room_editor_->is_enabled();
}

void DevControls::enter_map_editor_mode() {
    if (!map_editor_) return;
    if (mode_ == Mode::MapEditor) return;

    mode_ = Mode::MapEditor;
    map_editor_->set_input(input_);
    map_editor_->set_rooms(rooms_);
    map_editor_->set_screen_dimensions(screen_w_, screen_h_);
    map_editor_->set_enabled(true);
    if (room_editor_) room_editor_->set_enabled(false);
}

void DevControls::exit_map_editor_mode(bool focus_player, bool restore_previous_state) {
    if (!map_editor_) return;
    if (mode_ != Mode::MapEditor) return;

    map_editor_->exit(focus_player, restore_previous_state);
    mode_ = Mode::RoomEditor;
    if (room_editor_ && enabled_) {
        room_editor_->set_enabled(true);
        room_editor_->set_current_room(current_room_);
    }
}

void DevControls::handle_map_selection() {
    if (!map_editor_) return;
    Room* selected = map_editor_->consume_selected_room();
    if (!selected) return;

    dev_selected_room_ = selected;
    set_current_room(selected);
    map_editor_->focus_on_room(selected);
    exit_map_editor_mode(false, false);
}

