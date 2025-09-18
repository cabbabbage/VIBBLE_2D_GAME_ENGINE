#include "dev_controls.hpp"

#include "dev_mode/map_editor.hpp"
#include "dev_mode/room_editor.hpp"
#include "dev_mode/map_mode_ui.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "room/room.hpp"
#include "utils/input.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <cctype>
#include <limits>
#include <string>

DevControls::DevControls(Assets* owner, int screen_w, int screen_h)
    : assets_(owner),
      screen_w_(screen_w),
      screen_h_(screen_h) {
    room_editor_ = std::make_unique<RoomEditor>(assets_, screen_w_, screen_h_);
    map_editor_ = std::make_unique<MapEditor>(assets_);
    map_mode_ui_ = std::make_unique<MapModeUI>(assets_);
}

DevControls::~DevControls() = default;

void DevControls::set_input(Input* input) {
    input_ = input;
    if (room_editor_) room_editor_->set_input(input);
    if (map_editor_) map_editor_->set_input(input);
}

void DevControls::set_map_info(nlohmann::json* map_info, MapLightPanel::SaveCallback on_save) {
    map_info_json_ = map_info;
    map_light_save_cb_ = std::move(on_save);
    if (!map_light_panel_) {
        map_light_panel_ = std::make_unique<MapLightPanel>(40, 40);
    }
    if (map_light_panel_) {
        map_light_panel_->set_map_info(map_info_json_, map_light_save_cb_);
    }
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
    if (map_mode_ui_) map_mode_ui_->set_screen_dimensions(width, height);
}

void DevControls::set_current_room(Room* room) {
    current_room_ = room;
    if (room_editor_) room_editor_->set_current_room(room);
}

void DevControls::set_rooms(std::vector<Room*>* rooms) {
    rooms_ = rooms;
    if (map_editor_) map_editor_->set_rooms(rooms);
}

void DevControls::set_map_context(nlohmann::json* map_info, const std::string& map_path) {
    if (map_mode_ui_) map_mode_ui_->set_map_context(map_info, map_path);
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
        if (map_light_panel_) map_light_panel_->close();
    } else {
        if (map_editor_ && map_editor_->is_enabled()) {
            map_editor_->exit(true, false);
        }
        mode_ = Mode::RoomEditor;
        dev_selected_room_ = nullptr;
        if (room_editor_) {
            room_editor_->set_enabled(false);
        }
        if (map_light_panel_) {
            map_light_panel_->close();
        }
    }
}

void DevControls::update(const Input& input) {
    if (!enabled_) return;

    const bool ctrl = input.isScancodeDown(SDL_SCANCODE_LCTRL) || input.isScancodeDown(SDL_SCANCODE_RCTRL);
    if (ctrl && input.wasScancodePressed(SDL_SCANCODE_L)) {
        toggle_map_light_panel();
    }

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
        if (map_click_cooldown_ > 0) {
            --map_click_cooldown_;
        }
        if (map_mode_ui_) {
            if (input.wasScancodePressed(SDL_SCANCODE_LCTRL) ||
                input.wasScancodePressed(SDL_SCANCODE_RCTRL)) {
                map_mode_ui_->toggle_light_panel();
            }
        }
        bool consumed = false;
        if (map_editor_) {
            map_editor_->update(input);
            if (map_mode_ui_) {
                consumed = handle_map_mode_asset_click(input);
            }
            if (!consumed) {
                handle_map_selection();
            } else {
                (void)map_editor_->consume_selected_room();
            }
        }
        if (map_mode_ui_) {
            map_mode_ui_->update(input);
        }
    } else if (room_editor_ && room_editor_->is_enabled()) {
        room_editor_->update(input);
    }
}

void DevControls::update_ui(const Input& input) {
    if (!enabled_) return;
    if (map_light_panel_) {
        map_light_panel_->update(input);
    }
    if (mode_ != Mode::RoomEditor) return;
    if (!room_editor_ || !room_editor_->is_enabled()) return;

    room_editor_->update_ui(input);
}

void DevControls::handle_sdl_event(const SDL_Event& event) {
    if (!enabled_) return;
    if (map_light_panel_ && map_light_panel_->is_visible()) {
        if (map_light_panel_->handle_event(event)) {
            return;
        }
    }
    if (mode_ == Mode::MapEditor) {
        if (map_mode_ui_ && map_mode_ui_->handle_event(event)) {
            return;
        }
        return;
    }
    if (!can_use_room_editor_ui()) return;
    if (room_editor_) room_editor_->handle_sdl_event(event);
}

void DevControls::render_overlays(SDL_Renderer* renderer) {
    if (!enabled_) return;

    if (mode_ == Mode::MapEditor) {
        if (map_editor_) map_editor_->render(renderer);
        if (map_mode_ui_) map_mode_ui_->render(renderer);
    } else if (room_editor_) {
        room_editor_->render_overlays(renderer);
    }
    if (map_light_panel_ && map_light_panel_->is_visible()) {
        map_light_panel_->render(renderer);
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

void DevControls::toggle_map_light_panel() {
    if (!map_light_panel_) {
        if (!map_info_json_) return;
        map_light_panel_ = std::make_unique<MapLightPanel>(40, 40);
    }

    if (!map_light_panel_) return;

    if (map_info_json_) {
        map_light_panel_->set_map_info(map_info_json_, map_light_save_cb_);
    }

    if (map_light_panel_->is_visible()) {
        map_light_panel_->close();
    } else {
        map_light_panel_->open();
    }
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
    if (map_mode_ui_) map_mode_ui_->close_all_panels();
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

bool DevControls::handle_map_mode_asset_click(const Input& input) {
    if (!map_mode_ui_ || !assets_) return false;
    if (map_click_cooldown_ > 0) return false;
    if (!input.wasClicked(Input::LEFT)) return false;

    const int mx = input.getX();
    const int my = input.getY();
    if (map_mode_ui_->is_point_inside(mx, my)) return false;

    Asset* hit = hit_test_boundary_asset(SDL_Point{mx, my});
    if (!hit) return false;

    map_mode_ui_->open_assets_panel();
    map_click_cooldown_ = 2;
    return true;
}

Asset* DevControls::hit_test_boundary_asset(SDL_Point screen_point) const {
    if (!assets_) return nullptr;

    const camera& cam = assets_->getView();
    const float scale = std::max(0.0001f, cam.get_scale());
    const float inv_scale = 1.0f / scale;

    Asset* best = nullptr;
    int best_screen_y = std::numeric_limits<int>::min();
    int best_z_index = std::numeric_limits<int>::min();

    for (Asset* asset : assets_->all) {
        if (!asset || !asset->info) continue;
        if (asset->is_hidden()) continue;

        bool is_boundary = false;
        if (!asset->spawn_method.empty()) {
            std::string method = asset->spawn_method;
            std::transform(method.begin(), method.end(), method.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (method == "boundary") {
                is_boundary = true;
            }
        }
        if (!is_boundary) {
            std::string type = asset->info->type;
            std::transform(type.begin(), type.end(), type.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (type == "boundary") {
                is_boundary = true;
            }
        }
        if (!is_boundary) continue;

        SDL_Texture* tex = asset->get_final_texture();
        int fw = asset->cached_w;
        int fh = asset->cached_h;
        if ((fw == 0 || fh == 0) && tex) {
            SDL_QueryTexture(tex, nullptr, nullptr, &fw, &fh);
        }
        if (fw <= 0 || fh <= 0) continue;

        SDL_Point center = cam.map_to_screen(SDL_Point{asset->pos.x, asset->pos.y});
        int sw = static_cast<int>(std::lround(static_cast<double>(fw) * inv_scale));
        int sh = static_cast<int>(std::lround(static_cast<double>(fh) * inv_scale));
        if (sw <= 0 || sh <= 0) continue;

        SDL_Rect rect{center.x - sw / 2, center.y - sh, sw, sh};
        if (!SDL_PointInRect(&screen_point, &rect)) continue;

        if (!best || center.y > best_screen_y ||
            (center.y == best_screen_y && asset->z_index > best_z_index)) {
            best = asset;
            best_screen_y = center.y;
            best_z_index = asset->z_index;
        }
    }

    return best;
}

