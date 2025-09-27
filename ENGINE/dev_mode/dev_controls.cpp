#include "dev_controls.hpp"

#include "dev_mode/map_editor.hpp"
#include "dev_mode/room_editor.hpp"
#include "dev_mode/map_mode_ui.hpp"
#include "dev_mode/full_screen_collapsible.hpp"
#include "dev_mode/camera_ui.hpp"
#include "dev_mode/sdl_pointer_utils.hpp"
#include "dm_styles.hpp"
#include "widgets.hpp"

#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "room/room.hpp"
#include "utils/input.hpp"

#include <algorithm>
#include <utility>
#include <cctype>
#include <string>
#include <limits>
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>

using devmode::sdl::event_point;
using devmode::sdl::is_pointer_event;

class RegenerateRoomPopup {
public:
    using Callback = std::function<void(Room*)>;

    void open(std::vector<std::pair<std::string, Room*>> rooms,
              Callback cb,
              int screen_w,
              int screen_h) {
        rooms_ = std::move(rooms);
        callback_ = std::move(cb);
        buttons_.clear();
        if (rooms_.empty()) {
            visible_ = false;
            return;
        }
        const int margin = DMSpacing::item_gap();
        const int button_height = DMButton::height();
        const int spacing = DMSpacing::small_gap();
        const int button_width = std::max(220, screen_w / 6);
        rect_.w = button_width + margin * 2;
        rect_.h = margin + static_cast<int>(rooms_.size()) * (button_height + spacing);
        rect_.h += margin - spacing;
        const int max_height = std::max(240, screen_h - DMSpacing::panel_padding() * 2);
        if (rect_.h > max_height) {
            rect_.h = max_height;
        }
        rect_.x = std::max(16, screen_w - rect_.w - DMSpacing::panel_padding());
        rect_.y = DMSpacing::panel_padding();

        buttons_.reserve(rooms_.size());
        for (const auto& entry : rooms_) {
            auto btn = std::make_unique<DMButton>(entry.first, &DMStyles::ListButton(), button_width, button_height);
            buttons_.push_back(std::move(btn));
        }
        visible_ = true;
    }

    void close() {
        visible_ = false;
        callback_ = nullptr;
    }

    bool visible() const { return visible_; }

    void update(const Input&) {}

    bool handle_event(const SDL_Event& e) {
        if (!visible_) return false;
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
            close();
            return true;
        }
        if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION) {
            SDL_Point p{ e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x,
                         e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y };
            if (!SDL_PointInRect(&p, &rect_)) {
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    close();
                }
                return false;
            }
        }

        bool used = false;
        SDL_Rect btn_rect{ rect_.x + DMSpacing::item_gap(),
                           rect_.y + DMSpacing::item_gap(),
                           rect_.w - DMSpacing::item_gap() * 2,
                           DMButton::height() };
        for (size_t i = 0; i < buttons_.size(); ++i) {
            auto& btn = buttons_[i];
            if (!btn) continue;
            btn->set_rect(btn_rect);
            if (btn->handle_event(e)) {
                used = true;
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    if (callback_) callback_(rooms_[i].second);
                    close();
                }
            }
            btn_rect.y += DMButton::height() + DMSpacing::small_gap();
            if (btn_rect.y + DMButton::height() > rect_.y + rect_.h - DMSpacing::item_gap()) {
                break;
            }
        }
        return used;
    }

    void render(SDL_Renderer* renderer) const {
        if (!visible_ || !renderer) return;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_Color bg = DMStyles::PanelBG();
        SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
        SDL_RenderFillRect(renderer, &rect_);
        SDL_Color border = DMStyles::Border();
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &rect_);
        SDL_Rect btn_rect{ rect_.x + DMSpacing::item_gap(),
                           rect_.y + DMSpacing::item_gap(),
                           rect_.w - DMSpacing::item_gap() * 2,
                           DMButton::height() };
        for (const auto& btn : buttons_) {
            if (!btn) continue;
            btn->set_rect(btn_rect);
            btn->render(renderer);
            btn_rect.y += DMButton::height() + DMSpacing::small_gap();
            if (btn_rect.y > rect_.y + rect_.h - DMSpacing::item_gap()) {
                break;
            }
        }
    }

    bool is_point_inside(int x, int y) const {
        if (!visible_) return false;
        SDL_Point p{x, y};
        return SDL_PointInRect(&p, &rect_);
    }

private:
    bool visible_ = false;
    SDL_Rect rect_{0, 0, 280, 320};
    std::vector<std::pair<std::string, Room*>> rooms_;
    std::vector<std::unique_ptr<DMButton>> buttons_;
    Callback callback_;
};

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
    if (map_mode_ui_) {
        map_mode_ui_->set_footer_always_visible(true);
        map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Room);
    }
    if (room_editor_ && map_mode_ui_) {
        room_editor_->set_shared_fullscreen_panel(map_mode_ui_->get_footer_panel());
    }
    configure_header_button_sets();
    trail_suite_ = std::make_unique<TrailEditorSuite>();
    if (trail_suite_) {
        trail_suite_->set_screen_dimensions(screen_w_, screen_h_);
    }
    asset_filter_.initialize();
    asset_filter_.set_state_changed_callback([this]() { refresh_active_asset_filters(); });
    asset_filter_.set_enabled(enabled_);
    asset_filter_.set_screen_dimensions(screen_w_, screen_h_);
    asset_filter_.set_footer_panel(map_mode_ui_ ? map_mode_ui_->get_footer_panel() : nullptr);
    asset_filter_.set_map_info(map_info_json_);
    asset_filter_.set_current_room(current_room_);
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
    asset_filter_.set_map_info(map_info_json_);
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
    if (trail_suite_) trail_suite_->set_screen_dimensions(width, height);
    asset_filter_.set_screen_dimensions(width, height);
    if (map_assets_modal_) map_assets_modal_->set_screen_dimensions(width, height);
    if (boundary_assets_modal_) boundary_assets_modal_->set_screen_dimensions(width, height);
    asset_filter_.ensure_layout();
}

void DevControls::set_current_room(Room* room) {
    current_room_ = room;
    // Keep the developer-selected room in sync so subsequent
    // resolve_current_room() preserves this choice.
    dev_selected_room_ = room;
    if (regenerate_popup_) regenerate_popup_->close();
    if (room_editor_) {
        room_editor_->set_current_room(room);
    }
    asset_filter_.set_current_room(room);
}

void DevControls::set_rooms(std::vector<Room*>* rooms) {
    rooms_ = rooms;
    if (map_editor_) map_editor_->set_rooms(rooms);
}

void DevControls::set_map_context(nlohmann::json* map_info, const std::string& map_path) {
    map_info_json_ = map_info;
    map_path_ = map_path;
    if (map_mode_ui_) map_mode_ui_->set_map_context(map_info, map_path);
    if (map_mode_ui_) map_mode_ui_->set_light_save_callback(map_light_save_cb_);
    asset_filter_.set_map_info(map_info_json_);
    configure_header_button_sets();
}

bool DevControls::is_pointer_over_dev_ui(int x, int y) const {
    if (camera_panel_ && camera_panel_->is_visible() && camera_panel_->is_point_inside(x, y)) {
        return true;
    }
    if (room_editor_ && room_editor_->is_room_ui_blocking_point(x, y)) {
        return true;
    }
    if (trail_suite_ && trail_suite_->contains_point(x, y)) {
        return true;
    }
    if (map_mode_ui_ && map_mode_ui_->is_point_inside(x, y)) {
        return true;
    }
    if (regenerate_popup_ && regenerate_popup_->visible() && regenerate_popup_->is_point_inside(x, y)) {
        return true;
    }
    if (enabled_ && asset_filter_.contains_point(x, y)) {
        return true;
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
    asset_filter_.set_enabled(enabled_);

    if (enabled_) {
        const bool camera_was_visible = camera_panel_ && camera_panel_->is_visible();
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
        if (camera_was_visible && camera_panel_) {
            camera_panel_->open();
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
    if (!enabled_) {
        reset_asset_filters();
    } else {
        asset_filter_.ensure_layout();
    }
}

void DevControls::update(const Input& input) {
    if (!enabled_) return;

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
        if (map_mode_ui_ && input.wasScancodePressed(SDL_SCANCODE_F8)) {
            map_mode_ui_->toggle_layers_panel();
        }
        if (map_editor_) {
            map_editor_->update(input);
            handle_map_selection();
        }
    } else if (room_editor_ && room_editor_->is_enabled()) {
        if (!pointer_over_camera_panel_) {
            room_editor_->update(input);
        }
    }

    if (camera_panel_) {
        camera_panel_->update(input, screen_w_, screen_h_);
    }
    if (regenerate_popup_ && regenerate_popup_->visible()) {
        regenerate_popup_->update(input);
    }
    if (map_mode_ui_) {
        map_mode_ui_->update(input);
    }
    if (map_assets_modal_ && map_assets_modal_->visible()) {
        map_assets_modal_->update(input);
    }
    if (boundary_assets_modal_ && boundary_assets_modal_->visible()) {
        boundary_assets_modal_->update(input);
    }
    if (trail_suite_) {
        trail_suite_->update(input);
    }

    asset_filter_.ensure_layout();

    if (room_editor_ && room_editor_->is_enabled()) {
        FullScreenCollapsible* footer = map_mode_ui_ ? map_mode_ui_->get_footer_panel() : nullptr;
        if (footer && footer->visible()) {
            const SDL_Rect& header = footer->header_rect();
            SDL_Point pointer{input.getX(), input.getY()};
            if (header.w > 0 && header.h > 0 && SDL_PointInRect(&pointer, &header)) {
                room_editor_->clear_highlighted_assets();
            }
        }
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

    asset_filter_.ensure_layout();

    const bool pointer_event = is_pointer_event(event);
    const bool wheel_event = (event.type == SDL_MOUSEWHEEL);
    SDL_Point pointer{0, 0};
    if (pointer_event || wheel_event) {
        pointer = event_point(event);
    }

    if (pointer_event) {
        if (asset_filter_.handle_event(event)) {
            if (input_) input_->consumeEvent(event);
            return;
        }
    }
    if ((pointer_event || wheel_event) && enabled_ && asset_filter_.contains_point(pointer.x, pointer.y)) {
        if (input_) input_->consumeEvent(event);
        return;
    }

    if (trail_suite_ && trail_suite_->handle_event(event)) {
        if (input_) input_->consumeEvent(event);
        return;
    }
    if ((pointer_event || wheel_event) && trail_suite_ && trail_suite_->contains_point(pointer.x, pointer.y)) {
        if (input_) input_->consumeEvent(event);
        return;
    }

    // Route to new modals (map mode)
    if (map_assets_modal_ && map_assets_modal_->visible()) {
        if (map_assets_modal_->handle_event(event)) {
            if (input_) input_->consumeEvent(event);
            return;
        }
        if ((pointer_event || wheel_event) && map_assets_modal_->is_point_inside(pointer.x, pointer.y)) {
            if (input_) input_->consumeEvent(event);
            return;
        }
    }
    if (boundary_assets_modal_ && boundary_assets_modal_->visible()) {
        if (boundary_assets_modal_->handle_event(event)) {
            if (input_) input_->consumeEvent(event);
            return;
        }
        if ((pointer_event || wheel_event) && boundary_assets_modal_->is_point_inside(pointer.x, pointer.y)) {
            if (input_) input_->consumeEvent(event);
            return;
        }
    }

    if (regenerate_popup_ && regenerate_popup_->visible()) {
        if (regenerate_popup_->handle_event(event)) {
            if (input_) input_->consumeEvent(event);
            return;
        }
        if ((pointer_event || wheel_event) && regenerate_popup_->is_point_inside(pointer.x, pointer.y)) {
            if (input_) input_->consumeEvent(event);
            return;
        }
    }

    const bool can_route_room_editor = (mode_ != Mode::MapEditor) && can_use_room_editor_ui() && room_editor_;
    const bool pointer_over_room_ui = can_route_room_editor && (pointer_event || wheel_event) &&
        room_editor_->is_room_ui_blocking_point(pointer.x, pointer.y);

    if (pointer_over_room_ui) {
        room_editor_->handle_sdl_event(event);
        if (input_) {
            input_->consumeEvent(event);
        }
        return;
    }

    bool pointer_event_inside_camera = false;
    if (camera_panel_ && camera_panel_->is_visible()) {
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

    if (camera_panel_ && camera_panel_->is_visible()) {
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

    if (!pointer_over_room_ui && map_mode_ui_) {
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

    if (can_route_room_editor && room_editor_->handle_sdl_event(event)) {
        if (input_) {
            input_->consumeEvent(event);
        }
        return;
    }
}

void DevControls::render_overlays(SDL_Renderer* renderer) {
    if (!enabled_) return;

    if (mode_ == Mode::MapEditor) {
        if (map_editor_) map_editor_->render(renderer);
    } else if (room_editor_) {
        room_editor_->render_overlays(renderer);
    }
    if (map_mode_ui_) map_mode_ui_->render(renderer);
    if (map_assets_modal_ && map_assets_modal_->visible()) {
        map_assets_modal_->render(renderer);
    }
    if (boundary_assets_modal_ && boundary_assets_modal_->visible()) {
        boundary_assets_modal_->render(renderer);
    }
    if (trail_suite_) trail_suite_->render(renderer);
    if (camera_panel_ && camera_panel_->is_visible()) {
        camera_panel_->render(renderer);
    }
    if (regenerate_popup_ && regenerate_popup_->visible()) {
        regenerate_popup_->render(renderer);
    }
    asset_filter_.render(renderer);
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

void DevControls::open_spawn_group_for_asset(Asset* asset) {
    if (!can_use_room_editor_ui()) return;
    room_editor_->open_spawn_group_for_asset(asset);
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
            if (room_editor_) {
                room_editor_->close_room_config();
            }
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

    MapModeUI::HeaderButtonConfig to_room_btn;
    to_room_btn.id = "switch_mode";
    to_room_btn.label = "Room Mode";
    to_room_btn.momentary = true;
    to_room_btn.style_override = &DMStyles::AccentButton();
    to_room_btn.on_toggle = [this](bool) {
        if (room_editor_) {
            room_editor_->close_room_config();
        }
        if (mode_ == Mode::MapEditor) {
            exit_map_editor_mode(false, true);
        }
        sync_header_button_states();
    };
    map_buttons.push_back(std::move(to_room_btn));

    map_buttons.push_back(make_camera_button());

    // Map Mode: Map Assets modal button
    {
        MapModeUI::HeaderButtonConfig map_assets_btn;
        map_assets_btn.id = "map_assets";
        map_assets_btn.label = "Map Assets";
        map_assets_btn.active = (map_assets_modal_ && map_assets_modal_->visible());
        map_assets_btn.on_toggle = [this](bool active) {
            if (active) {
                toggle_map_assets_modal();
            } else {
                if (map_assets_modal_) map_assets_modal_->close();
            }
            sync_header_button_states();
        };
        map_buttons.push_back(std::move(map_assets_btn));
    }

    // Map Mode: Boundary Assets modal button
    {
        MapModeUI::HeaderButtonConfig boundary_btn;
        boundary_btn.id = "map_boundary";
        boundary_btn.label = "Boundary Assets";
        boundary_btn.active = (boundary_assets_modal_ && boundary_assets_modal_->visible());
        boundary_btn.on_toggle = [this](bool active) {
            if (active) {
                toggle_boundary_assets_modal();
            } else {
                if (boundary_assets_modal_) boundary_assets_modal_->close();
            }
            sync_header_button_states();
        };
        map_buttons.push_back(std::move(boundary_btn));
    }

    MapModeUI::HeaderButtonConfig to_map_btn;
    to_map_btn.id = "switch_mode";
    to_map_btn.label = "Map Mode";
    to_map_btn.momentary = true;
    to_map_btn.style_override = &DMStyles::AccentButton();
    to_map_btn.on_toggle = [this](bool) {
        if (room_editor_) {
            room_editor_->close_room_config();
        }
        if (mode_ != Mode::MapEditor) {
            enter_map_editor_mode();
        }
        sync_header_button_states();
    };
    room_buttons.push_back(std::move(to_map_btn));

    MapModeUI::HeaderButtonConfig lights_btn;
    lights_btn.id = "lights";
    lights_btn.label = "Lighting";
    lights_btn.active = map_mode_ui_ && map_mode_ui_->is_light_panel_visible();
    lights_btn.on_toggle = [this](bool active) {
        if (room_editor_) {
            room_editor_->close_room_config();
        }
        if (!map_mode_ui_) {
            sync_header_button_states();
            return;
        }
        const bool currently_open = map_mode_ui_->is_light_panel_visible();
        if (active != currently_open) {
            if (active && !currently_open && is_modal_blocking_panels()) {
                pulse_modal_header();
                sync_header_button_states();
                return;
            }
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
        room_editor_->close_room_config();
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
    regenerate_btn.label = "Regen Room";
    regenerate_btn.momentary = true;
    regenerate_btn.style_override = &DMStyles::DeleteButton();
    regenerate_btn.on_toggle = [this](bool) {
        if (room_editor_) {
            room_editor_->close_room_config();
            room_editor_->regenerate_room();
        }
        sync_header_button_states();
    };
    room_buttons.push_back(std::move(regenerate_btn));

    MapModeUI::HeaderButtonConfig regenerate_other_btn;
    regenerate_other_btn.id = "regenerate_other";
    regenerate_other_btn.label = "Regen Other";
    regenerate_other_btn.momentary = true;
    regenerate_other_btn.style_override = &DMStyles::DeleteButton();
    regenerate_other_btn.on_toggle = [this](bool) {
        if (!room_editor_) {
            sync_header_button_states();
            return;
        }
        room_editor_->close_room_config();
        if (is_modal_blocking_panels()) {
            pulse_modal_header();
            sync_header_button_states();
            return;
        }
        open_regenerate_room_popup();
        sync_header_button_states();
    };
    room_buttons.push_back(std::move(regenerate_other_btn));

    map_mode_ui_->set_mode_button_sets(std::move(map_buttons), std::move(room_buttons));
    asset_filter_.set_footer_panel(map_mode_ui_->get_footer_panel());
    asset_filter_.ensure_layout();
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
    const bool lights_open = map_mode_ui_->is_light_panel_visible();
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "lights", lights_open);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "lights", lights_open);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "regenerate", false);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "regenerate_other", false);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "switch_mode", false);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "switch_mode", false);
    // New modals in Map Mode header
    const bool map_assets_open = map_assets_modal_ && map_assets_modal_->visible();
    const bool boundary_open = boundary_assets_modal_ && boundary_assets_modal_->visible();
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "map_assets", map_assets_open);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "map_boundary", boundary_open);
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
    if (map_assets_modal_) map_assets_modal_->close();
    if (boundary_assets_modal_) boundary_assets_modal_->close();
    if (trail_suite_) {
        trail_suite_->close();
    }
    if (regenerate_popup_) {
        regenerate_popup_->close();
    }
    sync_header_button_states();
}

void DevControls::maybe_update_mode_from_zoom() {}

bool DevControls::is_modal_blocking_panels() const {
    return room_editor_ && room_editor_->has_active_modal();
}

void DevControls::pulse_modal_header() {
    if (room_editor_) {
        room_editor_->pulse_active_modal_header();
    }
}

void DevControls::toggle_map_assets_modal() {
    if (!assets_) return;
    if (!map_assets_modal_) {
        map_assets_modal_ = std::make_unique<SingleSpawnGroupModal>();
        map_assets_modal_->set_screen_dimensions(screen_w_, screen_h_);
        map_assets_modal_->set_floating_stack_key("map_assets_modal");
    } else {
        map_assets_modal_->set_screen_dimensions(screen_w_, screen_h_);
    }
    auto save = [this]() {
        try {
            const std::string& path = assets_->map_info_path();
            if (!path.empty()) {
                std::ofstream out(path);
                if (out.is_open()) {
                    out << assets_->map_info_json().dump(2);
                    out.close();
                }
            }
        } catch (...) {}
    };
    auto& map_json = assets_->map_info_json();
    SDL_Color color{200, 200, 255, 255};
    map_assets_modal_->open(map_json, "map_assets_data", "batch_map_assets", "Map-wide", color, save);
}

void DevControls::toggle_boundary_assets_modal() {
    if (!assets_) return;
    if (!boundary_assets_modal_) {
        boundary_assets_modal_ = std::make_unique<SingleSpawnGroupModal>();
        boundary_assets_modal_->set_screen_dimensions(screen_w_, screen_h_);
        boundary_assets_modal_->set_floating_stack_key("boundary_assets_modal");
    } else {
        boundary_assets_modal_->set_screen_dimensions(screen_w_, screen_h_);
    }
    auto save = [this]() {
        try {
            const std::string& path = assets_->map_info_path();
            if (!path.empty()) {
                std::ofstream out(path);
                if (out.is_open()) {
                    out << assets_->map_info_json().dump(2);
                    out.close();
                }
            }
        } catch (...) {}
    };
    auto& map_json = assets_->map_info_json();
    SDL_Color color{255, 200, 120, 255};
    boundary_assets_modal_->open(map_json, "map_boundary_data", "batch_map_boundary", "Boundary", color, save);
}


void DevControls::open_regenerate_room_popup() {
    if (!can_use_room_editor_ui()) return;
    if (!rooms_ || rooms_->empty()) {
        if (regenerate_popup_) regenerate_popup_->close();
        return;
    }

    std::vector<std::pair<std::string, Room*>> entries;
    entries.reserve(rooms_->size());
    for (Room* room : *rooms_) {
        if (!room || room == current_room_) continue;
        if (!room->room_area) continue;
        if (!room->type.empty()) {
            std::string lowered = room->type;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lowered == "trail") {
                continue;
            }
        }
        std::string name = room->room_name.empty() ? std::string("<unnamed>") : room->room_name;
        entries.emplace_back(std::move(name), room);
    }

    if (entries.empty()) {
        if (regenerate_popup_) regenerate_popup_->close();
        return;
    }

    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        std::string la = a.first;
        std::string lb = b.first;
        std::transform(la.begin(), la.end(), la.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(lb.begin(), lb.end(), lb.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return la < lb;
    });

    if (!regenerate_popup_) {
        regenerate_popup_ = std::make_unique<RegenerateRoomPopup>();
    }

    regenerate_popup_->open(entries,
                            [this](Room* selected) {
                                if (!selected || !room_editor_) return;
                                room_editor_->regenerate_room_from_template(selected);
                                if (regenerate_popup_) regenerate_popup_->close();
                                sync_header_button_states();
                            },
                            screen_w_,
                            screen_h_);
}

void DevControls::toggle_map_light_panel() {
    if (!map_mode_ui_) {
        return;
    }
    const bool currently_open = map_mode_ui_->is_light_panel_visible();
    if (!currently_open && is_modal_blocking_panels()) {
        pulse_modal_header();
        sync_header_button_states();
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
        if (is_modal_blocking_panels()) {
            pulse_modal_header();
            sync_header_button_states();
            return;
        }
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

    const bool camera_was_visible = camera_panel_ && camera_panel_->is_visible();
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
    if (camera_was_visible && camera_panel_) {
        camera_panel_->open();
    }
    sync_header_button_states();
}

void DevControls::handle_map_selection() {
    if (!map_editor_) return;
    Room* selected = map_editor_->consume_selected_room();
    if (!selected) return;

    map_editor_->focus_on_room(selected);
    std::string type = selected->type;
    std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    const bool is_trail = (type == "trail");
    if (is_trail) {
        if (trail_suite_) {
            trail_suite_->open(selected);
        }
        return;
    }

    if (trail_suite_) {
        trail_suite_->close();
    }

    dev_selected_room_ = selected;
    set_current_room(selected);
    exit_map_editor_mode(false, false);
    if (room_editor_) {
        room_editor_->open_room_config();
    }
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

void DevControls::filter_active_assets(std::vector<Asset*>& assets) const {
    if (!enabled_) return;
    if (mode_ != Mode::RoomEditor) return;
    if (!room_editor_ || !room_editor_->is_enabled()) return;
    assets.erase(std::remove_if(assets.begin(), assets.end(),
                                [this](Asset* asset) { return !passes_asset_filters(asset); }),
                 assets.end());
}

void DevControls::refresh_active_asset_filters() {
    if (!assets_) {
        return;
    }
    assets_->refresh_filtered_active_assets();
    auto& filtered = assets_->mutable_filtered_active_assets();
    set_active_assets(filtered);
    if (room_editor_) {
        room_editor_->clear_highlighted_assets();
    }
    const auto& active = assets_->getActive();
    for (Asset* asset : active) {
        if (!asset) {
            continue;
        }
        if (!passes_asset_filters(asset)) {
            asset->set_highlighted(false);
            asset->set_selected(false);
        }
    }
}

void DevControls::reset_asset_filters() {
    asset_filter_.reset();
    refresh_active_asset_filters();
}

bool DevControls::passes_asset_filters(Asset* asset) const {
    if (!asset) {
        return false;
    }
    return asset_filter_.passes(*asset);
}




