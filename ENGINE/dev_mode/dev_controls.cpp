#include "dev_controls.hpp"

#include "dev_mode/map_editor.hpp"
#include "dev_mode/room_editor.hpp"
#include "dev_mode/map_assets_panel.hpp"
#include "dev_mode/map_mode_ui.hpp"
#include "dev_mode/full_screen_collapsible.hpp"
#include "dev_mode/camera_ui.hpp"
#include "dm_styles.hpp"

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

namespace {
bool is_pointer_event(const SDL_Event& e) {
    return e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION;
}

SDL_Point event_point(const SDL_Event& e) {
    if (e.type == SDL_MOUSEMOTION) {
        return SDL_Point{e.motion.x, e.motion.y};
    }
    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        return SDL_Point{e.button.x, e.button.y};
    }
    int mx = 0;
    int my = 0;
    SDL_GetMouseState(&mx, &my);
    return SDL_Point{mx, my};
}

constexpr double kMapModeEnterScale = 2.4;
} // namespace

DevControls::DevControls(Assets* owner, int screen_w, int screen_h)
    : assets_(owner),
      screen_w_(screen_w),
      screen_h_(screen_h) {
    room_editor_ = std::make_unique<RoomEditor>(assets_, screen_w_, screen_h_);
    map_editor_ = std::make_unique<MapEditor>(assets_);
    map_mode_ui_ = std::make_unique<MapModeUI>(assets_);
    camera_panel_ = std::make_unique<CameraUIPanel>(assets_, 72, 72);
    if (camera_panel_) {
        camera_panel_->close();
    }
    if (map_editor_) {
        map_editor_->set_ui_blocker([this](int x, int y) { return is_pointer_over_dev_ui(x, y); });
    }
    ensure_map_assets_panel();
    if (map_mode_ui_) {
        map_mode_ui_->set_shared_assets_panel(map_assets_panel_);
        map_mode_ui_->set_footer_always_visible(true);
        map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Room);
    }
    if (room_editor_) {
        room_editor_->set_map_assets_panel(map_assets_panel_.get());
        if (map_mode_ui_) {
            room_editor_->set_shared_fullscreen_panel(map_mode_ui_->get_footer_panel());
        }
    }
    configure_header_button_sets();
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
    if (map_mode_ui_) {
        map_mode_ui_->set_light_save_callback(map_light_save_cb_);
        map_mode_ui_->set_map_context(map_info_json_, map_path_);
    }
    ensure_map_assets_panel();
    if (map_assets_panel_) {
        map_assets_panel_->set_map_info(map_info_json_, map_path_);
    }
    configure_header_button_sets();
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
    SDL_Rect bounds{0, 0, screen_w_, screen_h_};
    if (camera_panel_) camera_panel_->set_work_area(bounds);
    if (map_assets_panel_) map_assets_panel_->set_work_area(bounds);
}

void DevControls::set_current_room(Room* room) {
    current_room_ = room;
    if (room_editor_) {
        room_editor_->set_current_room(room);
    }
}

void DevControls::set_rooms(std::vector<Room*>* rooms) {
    rooms_ = rooms;
    if (map_editor_) map_editor_->set_rooms(rooms);
}

void DevControls::set_map_context(nlohmann::json* map_info, const std::string& map_path) {
    map_path_ = map_path;
    if (map_mode_ui_) map_mode_ui_->set_map_context(map_info, map_path);
    if (map_mode_ui_) map_mode_ui_->set_light_save_callback(map_light_save_cb_);
    ensure_map_assets_panel();
    configure_header_button_sets();
}

void DevControls::ensure_map_assets_panel() {
    if (!map_assets_panel_) {
        map_assets_panel_ = std::make_shared<MapAssetsPanel>(72, 72);
        map_assets_panel_->close();
    }
    if (!map_assets_panel_) {
        return;
    }
    SDL_Rect bounds{0, 0, screen_w_, screen_h_};
    map_assets_panel_->set_work_area(bounds);
    if (map_info_json_) {
        map_assets_panel_->set_map_info(map_info_json_, map_path_);
    }
    if (map_mode_ui_) {
        map_mode_ui_->set_shared_assets_panel(map_assets_panel_);
    }
    if (room_editor_) {
        room_editor_->set_map_assets_panel(map_assets_panel_.get());
    }
}

bool DevControls::is_pointer_over_dev_ui(int x, int y) const {
    if (camera_panel_ && camera_panel_->is_visible() && camera_panel_->is_point_inside(x, y)) {
        return true;
    }
    if (map_assets_panel_ && map_assets_panel_->is_visible() && map_assets_panel_->is_point_inside(x, y)) {
        return true;
    }
    if (room_editor_ && room_editor_->is_room_panel_blocking_point(x, y)) {
        return true;
    }
    if (map_mode_ui_ && map_mode_ui_->is_point_inside(x, y)) {
        return true;
    }
    return false;
}

bool DevControls::handle_shared_assets_event(const SDL_Event& event) {
    if (mode_ == Mode::MapEditor) {
        return false;
    }
    if (!map_assets_panel_ || !map_assets_panel_->is_visible()) {
        return false;
    }
    const bool pointer_event = is_pointer_event(event);
    const bool wheel_event = (event.type == SDL_MOUSEWHEEL);
    SDL_Point pointer{0, 0};
    if (pointer_event || wheel_event) {
        pointer = event_point(event);
        if (room_editor_ && room_editor_->is_room_panel_blocking_point(pointer.x, pointer.y)) {
            return false;
        }
    }
    if (map_assets_panel_->handle_event(event)) {
        if (input_) input_->consumeEvent(event);
        return true;
    }
    if (pointer_event || wheel_event) {
        if (room_editor_ && room_editor_->is_room_panel_blocking_point(pointer.x, pointer.y)) {
            return false;
        }
        if (map_assets_panel_->is_point_inside(pointer.x, pointer.y)) {
            if (input_) input_->consumeEvent(event);
            return true;
        }
    }
    return false;
}

Room* DevControls::resolve_current_room(Room* detected_room) {
    detected_room_ = detected_room;
    Room* target = choose_room(detected_room_);
    if (!enabled_) {
        dev_selected_room_ = nullptr;
        set_current_room(target);
        return current_room_;
    }

    if (!dev_selected_room_) {
        dev_selected_room_ = choose_room(detected_room_);
    }

    target = choose_room(dev_selected_room_);
    dev_selected_room_ = target;
    set_current_room(target);
    return current_room_;
}

void DevControls::set_enabled(bool enabled) {
    if (enabled == enabled_) return;
    enabled_ = enabled;

    if (enabled_) {
        close_all_floating_panels();
        mode_ = Mode::RoomEditor;
        Room* target = choose_room(current_room_ ? current_room_ : detected_room_);
        dev_selected_room_ = target;
        if (room_editor_) room_editor_->set_enabled(true);
        if (map_editor_) map_editor_->set_enabled(false);
        if (camera_panel_) camera_panel_->set_assets(assets_);
        set_current_room(target);
        if (map_mode_ui_) {
            map_mode_ui_->set_map_mode_active(false);
            map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Room);
            if (auto* panel = map_mode_ui_->get_footer_panel()) {
                panel->set_expanded(false);
            }
        }
    } else {
        close_all_floating_panels();
        if (map_editor_ && map_editor_->is_enabled()) {
            map_editor_->exit(true, false);
        }
        if (map_mode_ui_) {
            map_mode_ui_->set_map_mode_active(false);
            map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Room);
            if (auto* panel = map_mode_ui_->get_footer_panel()) {
                panel->set_expanded(false);
            }
        }
        mode_ = Mode::RoomEditor;
        dev_selected_room_ = nullptr;
        if (room_editor_) {
            room_editor_->set_enabled(false);
        }
        close_camera_panel();
    }

    sync_header_button_states();
}

void DevControls::update(const Input& input) {
    if (!enabled_) return;

    maybe_update_mode_from_zoom();

    const bool ctrl = input.isScancodeDown(SDL_SCANCODE_LCTRL) || input.isScancodeDown(SDL_SCANCODE_RCTRL);
    if (ctrl && input.wasScancodePressed(SDL_SCANCODE_M)) {
        toggle_map_light_panel();
    }
    if (ctrl && input.wasScancodePressed(SDL_SCANCODE_C)) {
        toggle_camera_panel();
    }
    pointer_over_camera_panel_ =
        camera_panel_ && camera_panel_->is_visible() &&
        camera_panel_->is_point_inside(input.getX(), input.getY());

    if (mode_ == Mode::MapEditor) {
        if (map_click_cooldown_ > 0) {
            --map_click_cooldown_;
        }
        if (map_mode_ui_) {
            if (input.wasScancodePressed(SDL_SCANCODE_F8)) {
                map_mode_ui_->toggle_layers_panel();
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
    } else if (room_editor_ && room_editor_->is_enabled()) {
        if (!pointer_over_camera_panel_) {
            room_editor_->update(input);
        }
        if (map_assets_panel_ && map_assets_panel_->is_visible()) {
            map_assets_panel_->update(input, screen_w_, screen_h_);
        }
    }

    if (camera_panel_) {
        camera_panel_->update(input, screen_w_, screen_h_);
    }
    if (map_mode_ui_) {
        map_mode_ui_->update(input);
    }

    sync_header_button_states();
}

void DevControls::update_ui(const Input& input) {
    if (!enabled_) return;
    if (mode_ != Mode::RoomEditor) return;
    if (!room_editor_ || !room_editor_->is_enabled()) return;

    room_editor_->update_ui(input);
}

void DevControls::handle_sdl_event(const SDL_Event& event) {
    if (!enabled_) return;

    const bool pointer_event = is_pointer_event(event);
    const bool wheel_event = (event.type == SDL_MOUSEWHEEL);
    SDL_Point pointer{0, 0};
    if (pointer_event || wheel_event) {
        pointer = event_point(event);
    }

    const bool can_route_room_editor = (mode_ != Mode::MapEditor) && can_use_room_editor_ui() && room_editor_;
    const bool pointer_over_room_panel = can_route_room_editor && (pointer_event || wheel_event) &&
        room_editor_->is_room_panel_blocking_point(pointer.x, pointer.y);

    auto route_room_editor = [&](bool consume_event) {
        if (!can_route_room_editor) {
            return false;
        }
        if (room_editor_->handle_sdl_event(event)) {
            if (consume_event && input_) {
                input_->consumeEvent(event);
            }
            return true;
        }
        return false;
    };

    bool attempted_room_editor = false;

    if (pointer_over_room_panel) {
        attempted_room_editor = true;
        if (route_room_editor(true)) {
            return;
        }
    }

    bool pointer_event_inside_camera = false;
    if (!pointer_over_room_panel && camera_panel_ && camera_panel_->is_visible()) {
        switch (event.type) {
        case SDL_MOUSEMOTION:
            pointer_event_inside_camera = camera_panel_->is_point_inside(event.motion.x, event.motion.y);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            pointer_event_inside_camera = camera_panel_->is_point_inside(event.button.x, event.button.y);
            break;
        case SDL_MOUSEWHEEL: {
            int mx = 0;
            int my = 0;
            SDL_GetMouseState(&mx, &my);
            pointer_event_inside_camera = camera_panel_->is_point_inside(mx, my);
            break;
        }
        default:
            break;
        }
    }

    if (!pointer_over_room_panel && camera_panel_ && camera_panel_->is_visible()) {
        if (camera_panel_->handle_event(event)) {
            if (input_) input_->consumeEvent(event);
            return;
        }
    }

    bool block_for_camera = pointer_event_inside_camera;
    if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP || event.type == SDL_TEXTINPUT) && pointer_over_camera_panel_) {
        block_for_camera = true;
    }
    if (block_for_camera) {
        if (input_) input_->consumeEvent(event);
        return;
    }

    if (!pointer_over_room_panel) {
        if (handle_shared_assets_event(event)) {
            return;
        }
    }

    if (!pointer_over_room_panel && map_mode_ui_) {
        if (map_mode_ui_->handle_event(event)) {
            if (input_) input_->consumeEvent(event);
            return;
        }
        if (pointer_event || wheel_event) {
            if (map_mode_ui_->is_point_inside(pointer.x, pointer.y)) {
                if (input_) input_->consumeEvent(event);
                return;
            }
        }
    }

    if (mode_ == Mode::MapEditor) {
        return;
    }

    if (!attempted_room_editor) {
        attempted_room_editor = true;
        if (route_room_editor(false)) {
            return;
        }
    }
}

void DevControls::render_overlays(SDL_Renderer* renderer) {
    if (!enabled_) return;

    if (mode_ == Mode::MapEditor) {
        if (map_editor_) map_editor_->render(renderer);
    } else if (room_editor_) {
        room_editor_->render_overlays(renderer);
        if (map_assets_panel_ && map_assets_panel_->is_visible()) {
            map_assets_panel_->render(renderer);
        }
    }
    if (map_mode_ui_) map_mode_ui_->render(renderer);
    if (room_editor_) {
        room_editor_->render_room_config_fullscreen(renderer);
    }
    if (camera_panel_ && camera_panel_->is_visible()) {
        camera_panel_->render(renderer);
    }
}

void DevControls::toggle_asset_library() {
    if (!can_use_room_editor_ui()) return;
    room_editor_->toggle_asset_library();
    sync_header_button_states();
}

void DevControls::open_asset_library() {
    if (!can_use_room_editor_ui()) return;
    room_editor_->open_asset_library();
    sync_header_button_states();
}

void DevControls::close_asset_library() {
    if (room_editor_) room_editor_->close_asset_library();
    sync_header_button_states();
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
    sync_header_button_states();
}

void DevControls::close_room_config() {
    if (room_editor_) room_editor_->close_room_config();
    sync_header_button_states();
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

void DevControls::configure_header_button_sets() {
    if (!map_mode_ui_) return;

    auto make_camera_button = [this]() {
        MapModeUI::HeaderButtonConfig camera_btn;
        camera_btn.id = "camera";
        camera_btn.label = "Camera";
        camera_btn.active = camera_panel_ && camera_panel_->is_visible();
        camera_btn.on_toggle = [this](bool active) {
            if (!camera_panel_) {
                sync_header_button_states();
                return;
            }
            camera_panel_->set_assets(assets_);
            if (camera_panel_->is_visible() != active) {
                toggle_camera_panel();
            } else {
                sync_header_button_states();
            }
        };
        return camera_btn;
    };

    std::vector<MapModeUI::HeaderButtonConfig> map_buttons;
    std::vector<MapModeUI::HeaderButtonConfig> room_buttons;

    map_buttons.push_back(make_camera_button());

    MapModeUI::HeaderButtonConfig assets_btn;
    assets_btn.id = "assets";
    assets_btn.label = "Map Assets";
    assets_btn.active = map_mode_ui_ && map_mode_ui_->is_assets_panel_visible();
    assets_btn.on_toggle = [this](bool active) {
        if (!map_mode_ui_) {
            sync_header_button_states();
            return;
        }
        const bool currently_open = map_mode_ui_->is_assets_panel_visible();
        if (active && !currently_open) {
            map_mode_ui_->open_assets_panel();
        } else if (!active && currently_open) {
            map_mode_ui_->close_all_panels();
        }
        sync_header_button_states();
    };
    room_buttons.push_back(std::move(assets_btn));

    MapModeUI::HeaderButtonConfig lights_btn;
    lights_btn.id = "lights";
    lights_btn.label = "Lighting";
    lights_btn.active = map_mode_ui_ && map_mode_ui_->is_light_panel_visible();
    lights_btn.on_toggle = [this](bool active) {
        if (!map_mode_ui_) {
            sync_header_button_states();
            return;
        }
        const bool currently_open = map_mode_ui_->is_light_panel_visible();
        if (active != currently_open) {
            map_mode_ui_->toggle_light_panel();
        }
        sync_header_button_states();
    };
    room_buttons.push_back(std::move(lights_btn));

    room_buttons.push_back(make_camera_button());

    MapModeUI::HeaderButtonConfig room_config_btn;
    room_config_btn.id = "room_config";
    room_config_btn.label = "Room Config";
    room_config_btn.active = room_editor_ && room_editor_->is_room_config_open();
    room_config_btn.on_toggle = [this](bool active) {
        if (!room_editor_) return;
        room_editor_->set_room_config_visible(active);
        sync_header_button_states();
    };
    room_buttons.push_back(std::move(room_config_btn));

    MapModeUI::HeaderButtonConfig library_btn;
    library_btn.id = "asset_library";
    library_btn.label = "Asset Library";
    library_btn.active = room_editor_ && room_editor_->is_asset_library_open();
    library_btn.on_toggle = [this](bool active) {
        if (!room_editor_) return;
        if (active) {
            room_editor_->open_asset_library();
        } else {
            room_editor_->close_asset_library();
        }
        sync_header_button_states();
    };
    room_buttons.push_back(std::move(library_btn));

    MapModeUI::HeaderButtonConfig regenerate_btn;
    regenerate_btn.id = "regenerate";
    regenerate_btn.label = "Regenerate Room";
    regenerate_btn.momentary = true;
    regenerate_btn.style_override = &DMStyles::DeleteButton();
    regenerate_btn.on_toggle = [this](bool) {
        if (room_editor_) {
            room_editor_->regenerate_room();
        }
        sync_header_button_states();
    };
    room_buttons.push_back(std::move(regenerate_btn));

    map_mode_ui_->set_mode_button_sets(std::move(map_buttons), std::move(room_buttons));
    sync_header_button_states();
}

void DevControls::sync_header_button_states() {
    if (!map_mode_ui_) return;
    const bool room_config_open = room_editor_ && room_editor_->is_room_config_open();
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "room_config", room_config_open);
    const bool library_open = room_editor_ && room_editor_->is_asset_library_open();
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "asset_library", library_open);
    const bool camera_open = camera_panel_ && camera_panel_->is_visible();
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "camera", camera_open);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "camera", camera_open);
    const bool assets_open = map_mode_ui_->is_assets_panel_visible();
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "assets", assets_open);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "assets", assets_open);
    const bool lights_open = map_mode_ui_->is_light_panel_visible();
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "lights", lights_open);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "lights", lights_open);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "regenerate", false);
}

void DevControls::close_all_floating_panels() {
    if (room_editor_) {
        room_editor_->close_room_config();
        room_editor_->close_asset_library();
        room_editor_->close_asset_info_editor();
    }
    if (camera_panel_) {
        camera_panel_->close();
    }
    if (map_mode_ui_) {
        map_mode_ui_->close_all_panels();
    }
    if (map_assets_panel_) {
        map_assets_panel_->close();
    }
    sync_header_button_states();
}

void DevControls::maybe_update_mode_from_zoom() {
    if (!enabled_ || !assets_) {
        return;
    }

    const camera& cam = assets_->getView();
    const double scale = std::max(0.0001, static_cast<double>(cam.get_scale()));
    if (mode_ != Mode::MapEditor && scale >= kMapModeEnterScale) {
        enter_map_editor_mode();
    }
}

void DevControls::toggle_map_light_panel() {
    if (!map_mode_ui_) {
        return;
    }
    map_mode_ui_->toggle_light_panel();
    sync_header_button_states();
}

void DevControls::toggle_camera_panel() {
    if (!camera_panel_) {
        return;
    }
    camera_panel_->set_assets(assets_);
    if (camera_panel_->is_visible()) {
        camera_panel_->close();
    } else {
        camera_panel_->open();
    }
    sync_header_button_states();
}

void DevControls::close_camera_panel() {
    if (camera_panel_) {
        camera_panel_->close();
    }
}

bool DevControls::can_use_room_editor_ui() const {
    return enabled_ && mode_ == Mode::RoomEditor && room_editor_ && room_editor_->is_enabled();
}

void DevControls::enter_map_editor_mode() {
    if (!map_editor_) return;
    if (mode_ == Mode::MapEditor) return;

    close_all_floating_panels();
    mode_ = Mode::MapEditor;
    map_editor_->set_input(input_);
    map_editor_->set_rooms(rooms_);
    map_editor_->set_screen_dimensions(screen_w_, screen_h_);
    map_editor_->set_enabled(true);
    if (room_editor_) room_editor_->set_enabled(false);
    if (map_mode_ui_) {
        map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Map);
        map_mode_ui_->set_map_mode_active(true);
    }
    sync_header_button_states();
}

void DevControls::exit_map_editor_mode(bool focus_player, bool restore_previous_state) {
    if (!map_editor_) return;
    if (mode_ != Mode::MapEditor) return;

    close_all_floating_panels();
    map_editor_->exit(focus_player, restore_previous_state);
    if (map_mode_ui_) map_mode_ui_->close_all_panels();
    if (map_mode_ui_) {
        map_mode_ui_->set_map_mode_active(false);
        map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Room);
    }
    mode_ = Mode::RoomEditor;
    if (room_editor_ && enabled_) {
        room_editor_->set_enabled(true);
        room_editor_->set_current_room(current_room_);
    }
    sync_header_button_states();
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
    if (camera_panel_ && camera_panel_->is_visible() &&
        camera_panel_->is_point_inside(mx, my)) {
        return false;
    }
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

Room* DevControls::find_spawn_room() const {
    if (!rooms_) return nullptr;
    for (Room* room : *rooms_) {
        if (room && room->is_spawn_room()) {
            return room;
        }
    }
    return nullptr;
}

Room* DevControls::choose_room(Room* preferred) const {
    if (preferred) {
        return preferred;
    }
    if (Room* spawn = find_spawn_room()) {
        return spawn;
    }
    if (!rooms_) {
        return nullptr;
    }
    for (Room* room : *rooms_) {
        if (room && room->room_area) {
            return room;
        }
    }
    return nullptr;
}




