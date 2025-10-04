#include "dev_controls.hpp"

#include "dev_mode/map_editor.hpp"
#include "dev_mode/room_editor.hpp"
#include "dev_mode/map_mode_ui.hpp"
#include "dev_mode/full_screen_collapsible.hpp"
#include "dev_mode/camera_ui.hpp"
#include "dev_mode/sdl_pointer_utils.hpp"
#include "dev_mode/area_mode/create_room_area_panel.hpp"
#include "dev_mode/area_mode/edit_room_area_panel.hpp"
#include "dev_mode/area_mode/area_types.hpp"
#include "dev_mode/area_overlay_editor.hpp"
#include "asset/asset_info.hpp"
#include "dm_styles.hpp"
#include "widgets.hpp"

#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "map_generation/room.hpp"
#include "utils/input.hpp"

#include <algorithm>
#include <utility>
#include <cctype>
#include <string>
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>

using devmode::sdl::event_point;
using devmode::sdl::is_pointer_event;

namespace {

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool is_trail_room(const Room* room) {
    if (!room || room->type.empty()) {
        return false;
    }
    return to_lower_copy(room->type) == "trail";
}

template <class Modal>
bool consume_modal_event(Modal* modal,
                         const SDL_Event& event,
                         const SDL_Point& pointer,
                         bool pointer_relevant,
                         Input* input) {
    if (!modal || !modal->visible()) {
        return false;
    }
    if (modal->handle_event(event)) {
        if (input) {
            input->consumeEvent(event);
        }
        return true;
    }
    if (pointer_relevant && modal->is_point_inside(pointer.x, pointer.y)) {
        if (input) {
            input->consumeEvent(event);
        }
        return true;
    }
    return false;
}

}  // namespace

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
        const int spacing = DMSpacing::small_gap();
        const int button_height = DMButton::height();
        const int button_width = std::max(220, screen_w / 6);
        rect_.w = button_width + margin * 2;
        const int total_buttons = static_cast<int>(rooms_.size());
        const int content_height = total_buttons * button_height + std::max(0, total_buttons - 1) * spacing;
        rect_.h = margin * 2 + content_height;
        const int max_height = std::max(240, screen_h - DMSpacing::panel_padding() * 2);
        rect_.h = std::min(rect_.h, max_height);
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
        const int margin = DMSpacing::item_gap();
        const int spacing = DMSpacing::small_gap();
        const int button_height = DMButton::height();
        SDL_Rect btn_rect{ rect_.x + margin, rect_.y + margin, rect_.w - margin * 2, button_height };
        const int bottom = rect_.y + rect_.h - margin;
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
            btn_rect.y += button_height + spacing;
            if (btn_rect.y + button_height > bottom) {
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
        const int margin = DMSpacing::item_gap();
        const int spacing = DMSpacing::small_gap();
        const int button_height = DMButton::height();
        SDL_Rect btn_rect{ rect_.x + margin, rect_.y + margin, rect_.w - margin * 2, button_height };
        const int bottom = rect_.y + rect_.h - margin;
        for (const auto& btn : buttons_) {
            if (!btn) continue;
            btn->set_rect(btn_rect);
            btn->render(renderer);
            btn_rect.y += button_height + spacing;
            if (btn_rect.y > bottom) {
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
        apply_camera_area_render_flag();
        map_mode_ui_->set_on_mode_changed([this](MapModeUI::HeaderMode mode){
            if (mode == MapModeUI::HeaderMode::Map) {
                if (this->mode_ != Mode::MapEditor) {
                    enter_map_editor_mode();
                }
            } else if (mode == MapModeUI::HeaderMode::Room) {
                if (this->mode_ == Mode::MapEditor) {
                    exit_map_editor_mode(false, true);
                }
                this->mode_ = Mode::RoomEditor;
                apply_camera_area_render_flag();
                if (map_mode_ui_) map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Room);
            } else if (mode == MapModeUI::HeaderMode::Area) {
                if (this->mode_ == Mode::MapEditor) {
                    exit_map_editor_mode(false, true);
                }
                this->mode_ = Mode::AreaMode;
                apply_camera_area_render_flag();
                if (map_mode_ui_) map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Area);
                // Default to 'all' view type when entering Area mode
                active_area_type_filters_.clear();
                active_area_type_filters_.insert("all");
            }
            sync_header_button_states();
        });
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
    if (create_area_panel_ && create_area_panel_->visible() && create_area_panel_->is_point_inside(x, y)) {
        return true;
    }
    if (edit_area_panel_ && edit_area_panel_->visible() && edit_area_panel_->is_point_inside(x, y)) {
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
        camera_panel_ && camera_panel_->is_visible() && camera_panel_->is_point_inside(input.getX(), input.getY());

    if (mode_ == Mode::MapEditor) {
        if (map_mode_ui_ && input.wasScancodePressed(SDL_SCANCODE_F8)) {
            map_mode_ui_->toggle_layers_panel();
        }
        if (map_editor_) {
            map_editor_->update(input);
            handle_map_selection();
        }
    } else if (mode_ == Mode::RoomEditor && room_editor_ && room_editor_->is_enabled()) {
        if (!pointer_over_camera_panel_) {
            room_editor_->update(input);
        }
    } else if (mode_ == Mode::AreaMode) {
        // Pan and zoom same as room mode, but no room editor UI
        if (assets_) {
            area_pan_zoom_.handle_input(assets_->getView(), input, false);
        }
        if (create_area_panel_) {
            create_area_panel_->update(input, screen_w_, screen_h_);
        }
        if (edit_area_panel_) {
            edit_area_panel_->update(input, screen_w_, screen_h_);
        }
        if (asset_area_editor_ && asset_area_editor_->is_active()) {
            asset_area_editor_->update(input, screen_w_, screen_h_);
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
    const bool pointer_relevant = pointer_event || wheel_event;
    SDL_Point pointer{0, 0};
    if (pointer_relevant) {
        pointer = event_point(event);
    }

    auto consume = [&](bool used) {
        if (used && input_) {
            input_->consumeEvent(event);
        }
        return used;
    };

    if (pointer_event && consume(asset_filter_.handle_event(event))) {
        return;
    }
    if (pointer_relevant && enabled_ && asset_filter_.contains_point(pointer.x, pointer.y)) {
        consume(true);
        return;
    }

    if (trail_suite_ && trail_suite_->is_open()) {
        if (consume(trail_suite_->handle_event(event))) {
            return;
        }
        if (pointer_relevant && trail_suite_->contains_point(pointer.x, pointer.y)) {
            consume(true);
            return;
        }
    }

    if (consume_modal_event(map_assets_modal_.get(), event, pointer, pointer_relevant, input_)) {
        return;
    }
    if (consume_modal_event(boundary_assets_modal_.get(), event, pointer, pointer_relevant, input_)) {
        return;
    }
    if (consume_modal_event(regenerate_popup_.get(), event, pointer, pointer_relevant, input_)) {
        return;
    }

    const bool can_route_room_editor = (mode_ != Mode::MapEditor) && can_use_room_editor_ui() && room_editor_;
    const bool pointer_over_room_ui = can_route_room_editor && pointer_relevant &&
                                      room_editor_->is_room_ui_blocking_point(pointer.x, pointer.y);

    if (pointer_over_room_ui) {
        room_editor_->handle_sdl_event(event);
        consume(true);
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
        if (consume(camera_panel_->handle_event(event))) {
            return;
        }
    }

    bool block_for_camera = pointer_event_inside_camera;
    if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP || event.type == SDL_TEXTINPUT) && pointer_over_camera_panel_) {
        block_for_camera = true;
    }
    if (block_for_camera) {
        consume(true);
        return;
    }

    if (!pointer_over_room_ui && map_mode_ui_) {
        if (consume(map_mode_ui_->handle_event(event))) {
            return;
        }
        if (pointer_relevant && map_mode_ui_->is_point_inside(pointer.x, pointer.y)) {
            consume(true);
            return;
        }
    }

    if (mode_ == Mode::MapEditor) {
        return;
    }

    if (mode_ == Mode::AreaMode) {
        // Route events to asset area overlay editor first
        if (asset_area_editor_ && asset_area_editor_->is_active()) {
            if (asset_area_editor_->handle_event(event)) {
                consume(true);
                return;
            }
        }
        // Parse room areas for hover/select and creation checks
        auto parse_room_areas = [this]() -> std::vector<std::pair<std::string, std::vector<SDL_Point>>> {
            std::vector<std::pair<std::string, std::vector<SDL_Point>>> out;
            if (!current_room_) return out;
            const auto& root = current_room_->assets_data();
            if (!root.contains("areas") || !root["areas"].is_array()) return out;
            for (const auto& item : root["areas"]) {
                if (!item.is_object()) continue;
                const std::string type = item.contains("type") && item["type"].is_string() ? item["type"].get<std::string>() : std::string{};
                const auto& pts = item.contains("points") ? item["points"] : nlohmann::json();
                if (!pts.is_array() || pts.size() < 3) continue;
                // Anchor defaults to (0,0) meaning points are absolute world coords
                int ax = 0, ay = 0;
                if (item.contains("anchor") && item["anchor"].is_object()) {
                    ax = item["anchor"].value("x", 0);
                    ay = item["anchor"].value("y", 0);
                }
                std::vector<SDL_Point> poly;
                poly.reserve(pts.size());
                for (const auto& p : pts) {
                    if (!p.is_object()) continue;
                    int x = p.value("x", 0);
                    int y = p.value("y", 0);
                    poly.push_back(SDL_Point{ax + x, ay + y});
                }
                if (poly.size() >= 3) out.emplace_back(type, std::move(poly));
            }
            return out;
        };

        auto area_list = parse_room_areas();

        if (create_area_panel_ && create_area_panel_->handle_event(event)) {
            consume(true);
            return;
        }
        if (edit_area_panel_ && edit_area_panel_->handle_event(event)) {
            consume(true);
            return;
        }

        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
            // Auto-create a room area only if the currently viewed type is trigger or spawning
            auto selected_view_type = [this]() -> std::string {
                for (const auto& t : devmode::area_mode::area_types()) {
                    if (active_area_type_filters_.count(t)) return t;
                }
                return std::string{};
            }();
            if (!selected_view_type.empty() && (selected_view_type == "trigger" || selected_view_type == "spawning")) {
                SDL_Point p{event.button.x, event.button.y};
                SDL_Point world = assets_ ? assets_->getView().screen_to_map(p) : p;
                last_area_click_world_ = world;
                bool inside_room = current_room_ && (!current_room_->room_area || current_room_->room_area->contains_point(world));
                if (inside_room && current_room_) {
                    auto& data = current_room_->assets_data();
                    if (!data.contains("areas") || !data["areas"].is_array()) {
                        data["areas"] = nlohmann::json::array();
                    }
                    int w = 0, h = 0;
                    if (current_room_->room_area) {
                        auto b = current_room_->room_area->get_bounds();
                        int minx, miny, maxx, maxy; std::tie(minx, miny, maxx, maxy) = b;
                        w = std::max(0, maxx - minx);
                        h = std::max(0, maxy - miny);
                    }
                    if (w <= 0 || h <= 0) { w = 32; h = 32; }
                    nlohmann::json entry;
                    entry["type"] = selected_view_type;
                    // default name if not changed later
                    int next_index = 1;
                    try { const auto& arr = data["areas"]; next_index = static_cast<int>(arr.size()) + 1; } catch (...) {}
                    entry["name"] = selected_view_type + std::string("_") + std::to_string(next_index);
                    // Anchor at click, center the rectangle
                    int ax = std::max(0, last_area_click_world_.x - w / 2);
                    int ay = std::max(0, last_area_click_world_.y - h / 2);
                    entry["anchor"] = nlohmann::json::object({{"x", ax}, {"y", ay}});
                    entry["original_dimensions"] = { {"width", w}, {"height", h} };
                    entry["points"] = nlohmann::json::array({
                        nlohmann::json::object({{"x", 0}, {"y", 0}}),
                        nlohmann::json::object({{"x", w}, {"y", 0}}),
                        nlohmann::json::object({{"x", w}, {"y", h}}),
                        nlohmann::json::object({{"x", 0}, {"y", h}})
                    });
                    data["areas"].push_back(std::move(entry));
                    current_room_->save_assets_json();
                    consume(true);
                    return;
                }
            }
        }

        // Hover/select over visible filtered areas
        auto type_visible = [this](const std::string& type) -> bool {
            if (active_area_type_filters_.count("all") > 0) return true; // 'all' shows all
            if (active_area_type_filters_.empty()) return true; // show all if none selected
            return active_area_type_filters_.count(type) > 0;
        };

        auto point_in_poly = [](const std::vector<SDL_Point>& poly, SDL_Point pt) -> bool {
            bool inside = false;
            const size_t n = poly.size();
            for (size_t i = 0, j = n - 1; i < n; j = i++) {
                const int xi = poly[i].x;
                const int yi = poly[i].y;
                const int xj = poly[j].x;
                const int yj = poly[j].y;
                const bool intersect = ((yi > pt.y) != (yj > pt.y)) && (pt.x < (xj - xi) * (pt.y - yi) / (double)(yj - yi + 1e-12) + xi);
                if (intersect) inside = !inside;
            }
            return inside;
        };

        if (event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
            auto selected_view_type_inline = [this]() -> std::string {
                for (const auto& t : devmode::area_mode::area_types()) {
                    if (active_area_type_filters_.count(t)) return t;
                }
                return std::string{};
            }();
            SDL_Point sp = (event.type == SDL_MOUSEMOTION) ? SDL_Point{event.motion.x, event.motion.y}
                                                           : SDL_Point{event.button.x, event.button.y};
            SDL_Point world = assets_ ? assets_->getView().screen_to_map(sp) : sp;
            int new_hover = -1;
            const bool allow_room_area_hover = (selected_view_type_inline == "trigger" || selected_view_type_inline == "spawning");
            if (allow_room_area_hover) {
                for (int i = static_cast<int>(area_list.size()) - 1; i >= 0; --i) {
                    if (!type_visible(area_list[i].first)) continue;
                    if (point_in_poly(area_list[i].second, world)) { new_hover = i; break; }
                }
            }
            hovered_area_index_ = new_hover;

            // Asset hover logic: highlight assets that do NOT have the currently viewed area type
            auto first_selected_type = [this]() -> std::string {
                for (const auto& t : devmode::area_mode::area_types()) {
                    if (active_area_type_filters_.count(t)) return t;
                }
                return std::string{};
            }();
            auto has_area_of_type = [](const AssetInfo* info, const std::string& type_name) -> bool {
                if (!info) return false;
                for (const auto& na : info->areas) {
                    // Prefer explicit type if present, fallback to matching legacy name
                    if (!na.type.empty()) { if (na.type == type_name) return true; }
                    else if (na.name == type_name) return true;
                }
                return false;
            };
            // Clear previous highlights when moving
            if (event.type == SDL_MOUSEMOTION && assets_) {
                for (Asset* a : assets_->getFilteredActiveAssets()) {
                    if (a) a->set_highlighted(false);
                }
            }
            area_hovered_asset_ = nullptr;
            if (!first_selected_type.empty() && assets_) {
                // Build simple hit test similar to scene rect
                const camera& cam = assets_->getView();
                float scale = cam.get_scale();
                float inv_scale = (scale != 0.0f) ? (1.0f / scale) : 1.0f;
                // Player screen height reference
                float player_screen_height = 1.0f;
                Asset* playerAsset = assets_->player;
                if (playerAsset) {
                    int ph = playerAsset->cached_h;
                    if (ph <= 0) {
                        if (SDL_Texture* pf = playerAsset->get_final_texture()) SDL_QueryTexture(pf, nullptr, nullptr, nullptr, &ph);
                    }
                    if (ph > 0) player_screen_height = static_cast<float>(ph) * inv_scale;
                }
                if (player_screen_height <= 0.0f) player_screen_height = 1.0f;

                auto screen_rect_for = [&](Asset* a) -> SDL_Rect {
                    SDL_Rect zero{0,0,0,0};
                    if (!a) return zero;
                    int fw = a->cached_w, fh = a->cached_h;
                    if ((fw == 0 || fh == 0)) {
                        if (SDL_Texture* ft = a->get_final_texture()) {
                            SDL_QueryTexture(ft, nullptr, nullptr, &fw, &fh);
                            a->cached_w = fw; a->cached_h = fh;
                        }
                    }
                    if (fw <= 0 || fh <= 0) return zero;
                    float base_sw = static_cast<float>(fw) * inv_scale;
                    float base_sh = static_cast<float>(fh) * inv_scale;
                    camera::RenderEffects eff = cam.compute_render_effects(SDL_Point{a->pos.x, a->pos.y}, base_sh, player_screen_height);
                    float scaled_sw = base_sw * eff.distance_scale;
                    float scaled_sh = base_sh * eff.distance_scale;
                    float final_h   = scaled_sh * eff.vertical_scale;
                    int sw_px = std::max(1, static_cast<int>(std::round(scaled_sw)));
                    int sh_px = std::max(1, static_cast<int>(std::round(final_h)));
                    return SDL_Rect{ eff.screen_position.x - sw_px / 2, eff.screen_position.y - sh_px, sw_px, sh_px };
                };

                // Iterate in reverse to prioritize top-most similar to renderer loop-end
                const auto& list = assets_->getFilteredActiveAssets();
                for (int i = static_cast<int>(list.size()) - 1; i >= 0; --i) {
                    Asset* a = list[i]; if (!a || !a->info) continue;
                    if (has_area_of_type(a->info.get(), first_selected_type)) continue; // skip assets that already have this area type
                    SDL_Rect fb = screen_rect_for(a);
                    if (fb.w <= 0 || fb.h <= 0) continue;
                    if (SDL_PointInRect(&sp, &fb)) {
                        area_hovered_asset_ = a;
                        a->set_highlighted(true);
                        break;
                    }
                }
            }
            if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                selected_area_index_ = hovered_area_index_;
                if (selected_area_index_ >= 0) {
                    // Open or update edit panel near pointer
                    if (!edit_area_panel_) edit_area_panel_ = std::make_unique<EditRoomAreaPanel>();
                    edit_area_panel_->set_area_types(devmode::area_mode::area_types());
                    // Set current type
                    if (selected_area_index_ >= 0 && selected_area_index_ < (int)area_list.size()) {
                        edit_area_panel_->set_selected_type(area_list[selected_area_index_].first);
                    }
                    // Set current name
                    if (current_room_) {
                        try {
                            auto& data = current_room_->assets_data();
                            if (data.contains("areas") && data["areas"].is_array()) {
                                const auto& item = data["areas"][selected_area_index_];
                                std::string nm = item.value("name", std::string{});
                                if (nm.empty()) nm = area_list[selected_area_index_].first;
                                edit_area_panel_->set_selected_name(nm);
                            }
                        } catch (...) {}
                    }
                    // Wire callbacks
                    edit_area_panel_->set_on_change_type([this](const std::string& new_type){
                        if (!current_room_) return;
                        try {
                            auto& data = current_room_->assets_data();
                            if (!data.contains("areas") || !data["areas"].is_array()) return;
                            if (selected_area_index_ < 0 || selected_area_index_ >= (int)data["areas"].size()) return;
                            auto& item = data["areas"][selected_area_index_];
                            if (item.is_object()) {
                                item["type"] = new_type;
                                current_room_->save_assets_json();
                            }
                        } catch (...) {}
                    });
                    edit_area_panel_->set_on_change_name([this](const std::string& new_name){
                        if (!current_room_) return;
                        try {
                            auto& data = current_room_->assets_data();
                            if (!data.contains("areas") || !data["areas"].is_array()) return;
                            if (selected_area_index_ < 0 || selected_area_index_ >= (int)data["areas"].size()) return;
                            auto& item = data["areas"][selected_area_index_];
                            if (item.is_object()) {
                                item["name"] = new_name;
                                current_room_->save_assets_json();
                            }
                        } catch (...) {}
                    });
                    edit_area_panel_->set_on_delete([this]() {
                        if (!current_room_) return;
                        try {
                            auto& data = current_room_->assets_data();
                            if (!data.contains("areas") || !data["areas"].is_array()) return;
                            if (selected_area_index_ < 0 || selected_area_index_ >= (int)data["areas"].size()) return;
                            auto& arr = data["areas"];
                            // erase by rebuilding array without the index
                            nlohmann::json new_arr = nlohmann::json::array();
                            for (int i = 0; i < (int)arr.size(); ++i) if (i != selected_area_index_) new_arr.push_back(arr[i]);
                            arr = std::move(new_arr);
                            current_room_->save_assets_json();
                            selected_area_index_ = -1;
                        } catch (...) {}
                    });
                    edit_area_panel_->open(event.button.x + 12, event.button.y + 12);
                } else {
                    if (edit_area_panel_) edit_area_panel_->close();
                }
            }
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
                // If right-click on a hovered asset lacking the type, open AreaOverlayEditor for that asset
                if (area_hovered_asset_ && !active_area_type_filters_.empty()) {
                    std::string area_type;
                    for (const auto& t : devmode::area_mode::area_types()) { if (active_area_type_filters_.count(t)) { area_type = t; break; } }
                    if (!area_type.empty()) {
                        if (!asset_area_editor_) asset_area_editor_ = std::make_unique<AreaOverlayEditor>();
                        if (asset_area_editor_) asset_area_editor_->attach_assets(assets_);
                        if (asset_area_editor_ && area_hovered_asset_->info) {
                            if (asset_area_editor_->begin(area_hovered_asset_->info.get(), area_hovered_asset_, area_type)) {
                                consume(true);
                                return;
                            }
                        }
                    }
                }
            }
        }

        return;
    }

    if (can_route_room_editor && room_editor_->handle_sdl_event(event)) {
        consume(true);
        return;
    }
}

void DevControls::render_overlays(SDL_Renderer* renderer) {
    if (!enabled_) return;

    if (mode_ == Mode::MapEditor) {
        if (map_editor_) map_editor_->render(renderer);
    } else if (mode_ == Mode::RoomEditor && room_editor_) {
        room_editor_->render_overlays(renderer);
    } else if (mode_ == Mode::AreaMode) {
        // Render room areas in UI overlay
        if (renderer && assets_) {
            auto parse_room_areas = [this]() -> std::vector<std::pair<std::string, std::vector<SDL_Point>>> {
                std::vector<std::pair<std::string, std::vector<SDL_Point>>> out;
                if (!current_room_) return out;
                const auto& root = current_room_->assets_data();
                if (!root.contains("areas") || !root["areas"].is_array()) return out;
                for (const auto& item : root["areas"]) {
                    if (!item.is_object()) continue;
                    const std::string type = item.contains("type") && item["type"].is_string() ? item["type"].get<std::string>() : std::string{};
                    const auto& pts = item.contains("points") ? item["points"] : nlohmann::json();
                    if (!pts.is_array() || pts.size() < 3) continue;
                    int ax = 0, ay = 0;
                    if (item.contains("anchor") && item["anchor"].is_object()) {
                        ax = item["anchor"].value("x", 0);
                        ay = item["anchor"].value("y", 0);
                    }
                    std::vector<SDL_Point> poly;
                    poly.reserve(pts.size());
                    for (const auto& p : pts) {
                        if (!p.is_object()) continue;
                        int x = p.value("x", 0);
                        int y = p.value("y", 0);
                        poly.push_back(SDL_Point{ax + x, ay + y});
                    }
                    if (poly.size() >= 3) out.emplace_back(type, std::move(poly));
                }
                return out;
            };

            auto type_visible = [this](const std::string& type) -> bool {
                if (active_area_type_filters_.count("all") > 0) return true; // 'all' shows all
                if (active_area_type_filters_.empty()) return true; // show all if none selected
                return active_area_type_filters_.count(type) > 0;
            };

            auto color_for_type = [](const std::string& type) -> SDL_Color {
                auto tl = [](std::string s){ std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){return (char)std::tolower(c);}); return s; };
                std::string lower = tl(type);
                if (lower.find("impas") != std::string::npos) return SDL_Color{255, 0, 0, 96};
                if (lower.find("spacing") != std::string::npos) return SDL_Color{0, 200, 0, 96};
                if (lower.find("trigger") != std::string::npos) return SDL_Color{0, 120, 255, 96};
                if (lower.find("child") != std::string::npos) return SDL_Color{255, 220, 0, 96};
                if (lower.find("spawn") != std::string::npos) return SDL_Color{180, 0, 220, 96};
                return SDL_Color{255, 140, 0, 96};
            };

            SDL_BlendMode prev_mode = SDL_BLENDMODE_NONE;
            SDL_GetRenderDrawBlendMode(renderer, &prev_mode);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            Uint8 pr=0,pg=0,pb=0,pa=0; SDL_GetRenderDrawColor(renderer, &pr, &pg, &pb, &pa);

            const camera& cam = assets_->getView();
            // In asset modes, render asset areas for the selected type
            {
                bool viewing_all = active_area_type_filters_.count("all") > 0;
                bool viewing_room_types = active_area_type_filters_.count("trigger") > 0 || active_area_type_filters_.count("spawning") > 0;
                // asset mode means not all and not room types
                if (!viewing_all && !viewing_room_types) {
                    // Determine selected asset area type
                    std::string selected_type;
                    for (const auto& t : devmode::area_mode::area_types()) {
                        if (t == "all" || t == "trigger" || t == "spawning") continue;
                        if (active_area_type_filters_.count(t)) { selected_type = t; break; }
                    }
                    if (!selected_type.empty() && assets_) {
                        const auto& list = assets_->getFilteredActiveAssets();
                        for (Asset* a : list) {
                            if (!a || !a->info) continue;
                            for (const auto& na : a->info->areas) {
                                const std::string& at = !na.type.empty() ? na.type : na.name;
                                if (at != selected_type) continue;
                                if (!na.area) continue;
                                // Build world polygon via asset transform
                                Area world_area = a->get_area(na.name);
                                const auto& wpts = world_area.get_points();
                                if (wpts.size() < 3) continue;
                                std::vector<SDL_Point> spts; spts.reserve(wpts.size());
                                for (const auto& wp : wpts) spts.push_back(cam.map_to_screen(wp));
#if SDL_VERSION_ATLEAST(2,0,18)
                                if (spts.size() >= 3) {
                                    SDL_Color fill = SDL_Color{230, 200, 80, 50};
                                    std::vector<SDL_Vertex> verts; verts.reserve(spts.size());
                                    for (auto p : spts) { SDL_Vertex v{}; v.position=SDL_FPoint{(float)p.x,(float)p.y}; v.color=fill; verts.push_back(v);} 
                                    std::vector<int> idxs; idxs.reserve((spts.size()-2)*3);
                                    for (size_t i=1;i+1<spts.size();++i){ idxs.push_back(0); idxs.push_back((int)i); idxs.push_back((int)(i+1)); }
                                    if (!idxs.empty()) SDL_RenderGeometry(renderer, nullptr, verts.data(), (int)verts.size(), idxs.data(), (int)idxs.size());
                                }
#endif
                                if (!spts.empty()) {
                                    SDL_Color outline{230, 200, 80, 120};
                                    std::vector<SDL_Point> pts = spts; pts.push_back(spts.front());
                                    SDL_SetRenderDrawColor(renderer, outline.r, outline.g, outline.b, outline.a);
                                    SDL_RenderDrawLines(renderer, pts.data(), (int)pts.size());
                                }
                            }
                        }
                    }
                }
            }

            SDL_SetRenderDrawColor(renderer, pr, pg, pb, pa);
            SDL_SetRenderDrawBlendMode(renderer, prev_mode);
        }
        if (create_area_panel_ && create_area_panel_->visible()) {
            create_area_panel_->render(renderer);
        }
        if (edit_area_panel_ && edit_area_panel_->visible()) {
            edit_area_panel_->render(renderer);
        }
        if (asset_area_editor_ && asset_area_editor_->is_active()) {
            asset_area_editor_->render(renderer);
        }
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
    std::vector<MapModeUI::HeaderButtonConfig> area_buttons;

    map_buttons.push_back(make_camera_button());

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

    // Area mode buttons will be built below

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

    // Area mode: multi-select checkboxes for available types ('all' is exclusive)
    for (const auto& type : devmode::area_mode::area_types()) {
        MapModeUI::HeaderButtonConfig cfg;
        cfg.id = std::string("area_") + type;
        cfg.label = type;
        cfg.active = (active_area_type_filters_.count(type) > 0);
        cfg.on_toggle = [this, type](bool active) {
            if (active) {
                if (type == "all") {
                    active_area_type_filters_.clear();
                    active_area_type_filters_.insert("all");
                } else {
                    active_area_type_filters_.erase("all");
                    active_area_type_filters_.insert(type);
                }
            } else {
                active_area_type_filters_.erase(type);
            }
            sync_header_button_states();
        };
        area_buttons.push_back(std::move(cfg));
    }

    map_mode_ui_->set_mode_button_sets(std::move(map_buttons), std::move(room_buttons), std::move(area_buttons));
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

    const bool map_assets_open = map_assets_modal_ && map_assets_modal_->visible();
    const bool boundary_open = boundary_assets_modal_ && boundary_assets_modal_->visible();
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "map_assets", map_assets_open);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "map_boundary", boundary_open);

    // Area mode multi-select buttons
    for (const auto& type : devmode::area_mode::area_types()) {
        const std::string id = std::string("area_") + type;
        map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Area, id, active_area_type_filters_.count(type) > 0);
    }
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
    auto save = [this]() { persist_map_info_to_disk(); };
    auto& map_json = assets_->map_info_json();
    SDL_Color color{200, 200, 255, 255};
    map_assets_modal_->open(map_json, "map_assets_data", "batch_map_assets", "Map-wide", color, save);
}

void DevControls::apply_camera_area_render_flag() {
    if (!assets_) return;
    camera& cam = assets_->getView();
    // Always render debug areas via the UI overlay, not the scene renderer
    cam.set_render_areas_enabled(false);
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
    auto save = [this]() { persist_map_info_to_disk(); };
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
        if (is_trail_room(room)) {
            continue;
        }
        std::string name = room->room_name.empty() ? std::string("<unnamed>") : room->room_name;
        entries.emplace_back(std::move(name), room);
    }

    if (entries.empty()) {
        if (regenerate_popup_) regenerate_popup_->close();
        return;
    }

    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return to_lower_copy(a.first) < to_lower_copy(b.first);
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
    if (is_trail_room(selected)) {
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

void DevControls::persist_map_info_to_disk() const {
    if (!assets_) {
        return;
    }
    try {
        const std::string& path = assets_->map_info_path();
        if (path.empty()) {
            return;
        }
        std::ofstream out(path);
        if (!out.is_open()) {
            return;
        }
        out << assets_->map_info_json().dump(2);
    } catch (...) {
    }
}

