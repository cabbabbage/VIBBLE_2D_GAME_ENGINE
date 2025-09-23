#include "dev_controls.hpp"

#include "dev_mode/map_editor.hpp"
#include "dev_mode/room_editor.hpp"
#include "dev_mode/map_mode_ui.hpp"
#include "dev_mode/room_configurator.hpp"
#include "dev_mode/spawn_groups_config.hpp"
#include "dev_mode/full_screen_collapsible.hpp"
#include "dev_mode/camera_ui.hpp"
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
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <random>
#include <vector>
#include <nlohmann/json.hpp>

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

std::string generate_spawn_id() {
    static std::mt19937 rng(std::random_device{}());
    static const char* hex = "0123456789abcdef";
    std::uniform_int_distribution<int> dist(0, 15);
    std::string s = "spn-";
    for (int i = 0; i < 12; ++i) s.push_back(hex[dist(rng)]);
    return s;
}

nlohmann::json& ensure_spawn_groups_array(nlohmann::json& root) {
    if (root.contains("spawn_groups") && root["spawn_groups"].is_array()) {
        return root["spawn_groups"];
    }
    if (root.contains("assets") && root["assets"].is_array()) {
        root["spawn_groups"] = root["assets"];
        root.erase("assets");
        return root["spawn_groups"];
    }
    root["spawn_groups"] = nlohmann::json::array();
    return root["spawn_groups"];
}

bool sanitize_perimeter_spawn_groups(nlohmann::json& groups) {
    if (!groups.is_array()) return false;
    bool changed = false;
    for (auto& entry : groups) {
        if (!entry.is_object()) continue;
        std::string method = entry.value("position", std::string{});
        if (method == "Exact Position") {
            method = "Exact";
        }
        if (method != "Perimeter") continue;
        int min_number = entry.value("min_number", entry.value("max_number", 2));
        int max_number = entry.value("max_number", min_number);
        if (min_number < 2) {
            min_number = 2;
            changed = true;
        }
        if (max_number < 2) {
            max_number = 2;
            changed = true;
        }
        if (max_number < min_number) {
            max_number = min_number;
            changed = true;
        }
        if (!entry.contains("min_number") || !entry["min_number"].is_number_integer() ||
            entry["min_number"].get<int>() != min_number) {
            entry["min_number"] = min_number;
        }
        if (!entry.contains("max_number") || !entry["max_number"].is_number_integer() ||
            entry["max_number"].get<int>() != max_number) {
            entry["max_number"] = max_number;
        }
    }
    return changed;
}
} // namespace

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
    initialize_asset_filters();
    layout_filter_header();
    update_trail_ui_bounds();
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
    rebuild_map_asset_spawn_ids();
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
    update_trail_ui_bounds();
    layout_filter_header();
}

void DevControls::set_current_room(Room* room) {
    current_room_ = room;
    if (regenerate_popup_) regenerate_popup_->close();
    if (room_editor_) {
        room_editor_->set_current_room(room);
    }
    rebuild_current_room_spawn_ids();
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
    rebuild_map_asset_spawn_ids();
    configure_header_button_sets();
}

bool DevControls::is_pointer_over_dev_ui(int x, int y) const {
    if (camera_panel_ && camera_panel_->is_visible() && camera_panel_->is_point_inside(x, y)) {
        return true;
    }
    if (room_editor_ && room_editor_->is_room_ui_blocking_point(x, y)) {
        return true;
    }
    if (is_point_inside_trail_ui(x, y)) {
        return true;
    }
    if (map_mode_ui_ && map_mode_ui_->is_point_inside(x, y)) {
        return true;
    }
    if (regenerate_popup_ && regenerate_popup_->visible() && regenerate_popup_->is_point_inside(x, y)) {
        return true;
    }
    if (enabled_ && is_point_inside_filter_header(x, y)) {
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
    update_trail_ui(input);

    layout_filter_header();

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

    layout_filter_header();

    const bool pointer_event = is_pointer_event(event);
    const bool wheel_event = (event.type == SDL_MOUSEWHEEL);
    SDL_Point pointer{0, 0};
    if (pointer_event || wheel_event) {
        pointer = event_point(event);
    }

    if (pointer_event) {
        if (handle_filter_header_event(event)) {
            if (input_) input_->consumeEvent(event);
            return;
        }
    }
    if ((pointer_event || wheel_event) && enabled_ && is_point_inside_filter_header(pointer.x, pointer.y)) {
        if (input_) input_->consumeEvent(event);
        return;
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

    if (handle_trail_ui_event(event)) {
        if (input_) input_->consumeEvent(event);
        return;
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
    render_trail_ui(renderer);
    if (camera_panel_ && camera_panel_->is_visible()) {
        camera_panel_->render(renderer);
    }
    if (regenerate_popup_ && regenerate_popup_->visible()) {
        regenerate_popup_->render(renderer);
    }
    render_filter_header(renderer);
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
    close_trail_config();
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

void DevControls::ensure_trail_ui() {
    if (!trail_config_ui_) {
        trail_config_ui_ = std::make_unique<RoomConfigurator>();
        if (trail_config_ui_) {
            trail_config_ui_->set_on_close([this]() { close_trail_config(); });
            trail_config_ui_->set_spawn_group_callbacks(
                [this](const std::string& id) { open_trail_spawn_group_editor(id); },
                [this](const std::string& id) { duplicate_trail_spawn_group(id); },
                [this](const std::string& id) { delete_trail_spawn_group(id); },
                [this]() { add_trail_spawn_group(); });
        }
    }
    if (!trail_spawn_groups_ui_) {
        trail_spawn_groups_ui_ = std::make_unique<SpawnGroupsConfig>();
    }
    if (trail_config_ui_) {
        trail_config_ui_->set_bounds(trail_config_bounds_);
        trail_config_ui_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
    }
    if (trail_spawn_groups_ui_) {
        SDL_Point anchor{trail_config_bounds_.x + trail_config_bounds_.w + 16, trail_config_bounds_.y};
        trail_spawn_groups_ui_->set_anchor(anchor.x, anchor.y);
    }
}

void DevControls::update_trail_ui_bounds() {
    const int margin = 48;
    const int max_width = std::max(320, screen_w_ - 2 * margin);
    const int desired_width = std::max(360, screen_w_ / 3);
    const int width = std::min(max_width, desired_width);
    const int height = std::max(240, screen_h_ - 2 * margin);
    const int x = std::max(margin, screen_w_ - width - margin);
    const int y = margin;
    trail_config_bounds_ = SDL_Rect{x, y, width, height};
    if (trail_config_ui_) {
        trail_config_ui_->set_bounds(trail_config_bounds_);
        trail_config_ui_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
    }
    if (trail_spawn_groups_ui_) {
        SDL_Point anchor{x + width + 16, y};
        trail_spawn_groups_ui_->set_anchor(anchor.x, anchor.y);
    }
}

void DevControls::open_trail_config(Room* trail) {
    if (!trail) return;
    ensure_trail_ui();
    active_trail_ = trail;
    update_trail_ui_bounds();
    if (trail_config_ui_) {
        trail_config_ui_->open(trail);
        trail_config_ui_->set_bounds(trail_config_bounds_);
    }
    refresh_trail_spawn_groups_ui();
}

void DevControls::close_trail_config() {
    active_trail_ = nullptr;
    if (trail_spawn_groups_ui_) {
        trail_spawn_groups_ui_->close_all();
        trail_spawn_groups_ui_->close();
    }
    if (trail_config_ui_) {
        trail_config_ui_->close();
    }
}

bool DevControls::is_trail_config_open() const {
    return trail_config_ui_ && trail_config_ui_->visible();
}

void DevControls::update_trail_ui(const Input& input) {
    if (trail_config_ui_ && trail_config_ui_->visible()) {
        trail_config_ui_->update(input, screen_w_, screen_h_);
    }
    if (trail_spawn_groups_ui_) {
        trail_spawn_groups_ui_->update(input, screen_w_, screen_h_);
    }
}

bool DevControls::handle_trail_ui_event(const SDL_Event& event) {
    bool used = false;
    if (trail_spawn_groups_ui_ && trail_spawn_groups_ui_->handle_event(event)) {
        used = true;
    }
    if (trail_config_ui_ && trail_config_ui_->handle_event(event)) {
        used = true;
    }
    if (used) {
        return true;
    }
    if (is_pointer_event(event) || event.type == SDL_MOUSEWHEEL) {
        SDL_Point p = event_point(event);
        if (is_point_inside_trail_ui(p.x, p.y)) {
            return true;
        }
    }
    return false;
}

void DevControls::render_trail_ui(SDL_Renderer* renderer) {
    if (trail_config_ui_) trail_config_ui_->render(renderer);
    if (trail_spawn_groups_ui_) trail_spawn_groups_ui_->render(renderer);
}

bool DevControls::is_point_inside_trail_ui(int x, int y) const {
    if (trail_config_ui_ && trail_config_ui_->is_point_inside(x, y)) {
        return true;
    }
    if (trail_spawn_groups_ui_ && trail_spawn_groups_ui_->is_point_inside(x, y)) {
        return true;
    }
    return false;
}

void DevControls::refresh_trail_spawn_groups_ui() {
    if (!active_trail_) return;
    ensure_trail_ui();
    if (!trail_spawn_groups_ui_) return;
    auto& root = active_trail_->assets_data();
    auto& groups = ensure_spawn_groups_array(root);
    auto reopen = trail_spawn_groups_ui_->capture_open_spawn_group();
    trail_spawn_groups_ui_->close_all();
    const bool sanitized = sanitize_perimeter_spawn_groups(groups);
    if (sanitized) {
        active_trail_->save_assets_json();
    }
    auto on_change = [this]() {
        if (!active_trail_) return;
        auto& root = active_trail_->assets_data();
        auto& arr = ensure_spawn_groups_array(root);
        const bool changed = sanitize_perimeter_spawn_groups(arr);
        active_trail_->save_assets_json();
        if (trail_config_ui_) trail_config_ui_->refresh_spawn_groups(active_trail_);
        if (changed) {
            refresh_trail_spawn_groups_ui();
        }
    };
    auto on_entry_change = [this](const nlohmann::json&, const SpawnGroupConfigUI::ChangeSummary&) {
        if (!active_trail_) return;
        active_trail_->save_assets_json();
        if (trail_config_ui_) trail_config_ui_->refresh_spawn_groups(active_trail_);
    };
    trail_spawn_groups_ui_->load(groups, on_change, on_entry_change, {});
    if (trail_config_ui_) {
        trail_config_ui_->refresh_spawn_groups(active_trail_);
    }
    if (reopen) {
        trail_spawn_groups_ui_->restore_open_spawn_group(*reopen);
    }
}

void DevControls::open_trail_spawn_group_editor(const std::string& spawn_id) {
    if (spawn_id.empty()) return;
    ensure_trail_ui();
    if (!trail_spawn_groups_ui_) return;
    SDL_Point anchor{trail_config_bounds_.x + trail_config_bounds_.w + 16, trail_config_bounds_.y};
    trail_spawn_groups_ui_->set_anchor(anchor.x, anchor.y);
    trail_spawn_groups_ui_->open_spawn_group(spawn_id, anchor.x, anchor.y);
}

void DevControls::duplicate_trail_spawn_group(const std::string& spawn_id) {
    if (spawn_id.empty() || !active_trail_) return;
    auto& root = active_trail_->assets_data();
    auto& groups = ensure_spawn_groups_array(root);
    nlohmann::json* original = find_trail_spawn_entry(spawn_id);
    if (!original) return;
    nlohmann::json duplicate = *original;
    std::string new_id = generate_spawn_id();
    duplicate["spawn_id"] = new_id;
    if (duplicate.contains("display_name") && duplicate["display_name"].is_string()) {
        duplicate["display_name"] = duplicate["display_name"].get<std::string>() + " Copy";
    }
    groups.push_back(duplicate);
    sanitize_perimeter_spawn_groups(groups);
    active_trail_->save_assets_json();
    refresh_trail_spawn_groups_ui();
    open_trail_spawn_group_editor(new_id);
}

void DevControls::delete_trail_spawn_group(const std::string& spawn_id) {
    if (spawn_id.empty() || !active_trail_) return;
    auto& root = active_trail_->assets_data();
    auto& groups = ensure_spawn_groups_array(root);
    auto it = std::remove_if(groups.begin(), groups.end(), [&](nlohmann::json& entry) {
        if (!entry.is_object()) return false;
        if (!entry.contains("spawn_id") || !entry["spawn_id"].is_string()) return false;
        return entry["spawn_id"].get<std::string>() == spawn_id;
    });
    if (it == groups.end()) return;
    groups.erase(it, groups.end());
    sanitize_perimeter_spawn_groups(groups);
    active_trail_->save_assets_json();
    if (trail_spawn_groups_ui_) {
        trail_spawn_groups_ui_->close_all();
    }
    refresh_trail_spawn_groups_ui();
}

void DevControls::add_trail_spawn_group() {
    if (!active_trail_) return;
    auto& root = active_trail_->assets_data();
    auto& groups = ensure_spawn_groups_array(root);
    nlohmann::json entry;
    entry["spawn_id"] = generate_spawn_id();
    entry["display_name"] = "New Spawn";
    entry["position"] = "Exact";
    entry["min_number"] = 1;
    entry["max_number"] = 1;
    entry["check_overlap"] = false;
    entry["enforce_spacing"] = false;
    entry["chance_denominator"] = 100;
    entry["candidates"] = nlohmann::json::array();
    entry["candidates"].push_back({{"name", "null"}, {"chance", 0}});
    groups.push_back(entry);
    sanitize_perimeter_spawn_groups(groups);
    active_trail_->save_assets_json();
    refresh_trail_spawn_groups_ui();
    if (entry.contains("spawn_id") && entry["spawn_id"].is_string()) {
        open_trail_spawn_group_editor(entry["spawn_id"].get<std::string>());
    }
}

nlohmann::json* DevControls::find_trail_spawn_entry(const std::string& spawn_id) {
    if (!active_trail_ || spawn_id.empty()) return nullptr;
    auto& root = active_trail_->assets_data();
    auto& groups = ensure_spawn_groups_array(root);
    for (auto& entry : groups) {
        if (!entry.is_object()) continue;
        if (!entry.contains("spawn_id") || !entry["spawn_id"].is_string()) continue;
        if (entry["spawn_id"].get<std::string>() == spawn_id) {
            return &entry;
        }
    }
    return nullptr;
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
        open_trail_config(selected);
        return;
    }

    close_trail_config();

    dev_selected_room_ = selected;
    set_current_room(selected);
    exit_map_editor_mode(false, false);
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
    assets.erase(
        std::remove_if(assets.begin(), assets.end(),
                       [this](Asset* asset) { return !passes_asset_filters(asset); }),
        assets.end());
}

void DevControls::initialize_asset_filters() {
    filter_entries_.clear();
    filter_state_.type_filters.clear();

    FilterEntry map_entry;
    map_entry.id = "map_assets";
    map_entry.kind = FilterKind::MapAssets;
    map_entry.checkbox = std::make_unique<DMCheckbox>("Map Assets", false);
    filter_entries_.push_back(std::move(map_entry));

    FilterEntry room_entry;
    room_entry.id = "current_room";
    room_entry.kind = FilterKind::CurrentRoom;
    room_entry.checkbox = std::make_unique<DMCheckbox>("Current Room", true);
    filter_entries_.push_back(std::move(room_entry));

    for (const std::string& type : asset_types::all_as_strings()) {
        FilterEntry entry;
        entry.id = type;
        entry.kind = FilterKind::Type;
        const bool default_enabled =
            (type == asset_types::npc) || (type == asset_types::object);
        entry.checkbox = std::make_unique<DMCheckbox>(format_type_label(type), default_enabled);
        filter_state_.type_filters[type] = default_enabled;
        filter_entries_.push_back(std::move(entry));
    }

    filter_state_.map_assets = false;
    filter_state_.current_room = true;
    sync_filter_state_from_ui();
}

void DevControls::layout_filter_header() {
    filter_header_rect_ = SDL_Rect{0, 0, 0, 0};

    auto reset_checkbox_rects = [this]() {
        for (auto& entry : filter_entries_) {
            if (entry.checkbox) {
                entry.checkbox->set_rect(SDL_Rect{0, 0, 0, 0});
            }
        }
    };

    if (screen_w_ <= 0 || screen_h_ <= 0) {
        reset_checkbox_rects();
        return;
    }

    FullScreenCollapsible* footer = map_mode_ui_ ? map_mode_ui_->get_footer_panel() : nullptr;
    if (!footer) {
        reset_checkbox_rects();
        return;
    }

    const int margin_x = DMSpacing::item_gap();
    const int margin_y = DMSpacing::item_gap();
    const int row_gap = DMSpacing::small_gap();
    const int checkbox_width = 180;
    const int checkbox_height = DMCheckbox::height();

    const int available_width = std::max(0, screen_w_ - margin_x * 2);
    if (available_width <= 0) {
        reset_checkbox_rects();
        return;
    }

    std::vector<std::vector<FilterEntry*>> rows;
    rows.emplace_back();
    for (auto& entry : filter_entries_) {
        if (!entry.checkbox) {
            continue;
        }
        auto& current_row = rows.back();
        int current_row_width = 0;
        if (!current_row.empty()) {
            current_row_width = static_cast<int>(current_row.size()) * checkbox_width +
                                static_cast<int>(current_row.size() - 1) * margin_x;
        }
        int width_with_new = current_row_width + checkbox_width;
        if (!current_row.empty()) {
            width_with_new += margin_x;
        }
        if (!current_row.empty() && width_with_new > available_width) {
            rows.emplace_back();
        }
        rows.back().push_back(&entry);
    }

    if (!rows.empty() && rows.back().empty()) {
        rows.pop_back();
    }

    int row_count = 0;
    for (const auto& row : rows) {
        if (!row.empty()) {
            ++row_count;
        }
    }

    if (row_count == 0) {
        reset_checkbox_rects();
        return;
    }

    const int checkbox_rows_height = row_count * checkbox_height + (row_count - 1) * row_gap;
    const int desired_header_height = margin_y + DMButton::height() + row_gap + checkbox_rows_height + margin_y;
    footer->set_header_height(desired_header_height);

    SDL_Rect header = footer->header_rect();
    if (header.w <= 0 || header.h <= 0) {
        reset_checkbox_rects();
        return;
    }

    int y = header.y + margin_y + DMButton::height() + row_gap;
    int min_x = std::numeric_limits<int>::max();
    int min_y = std::numeric_limits<int>::max();
    int max_x = std::numeric_limits<int>::min();
    int max_y = std::numeric_limits<int>::min();

    const int left_limit = header.x + margin_x;
    const int right_limit = header.x + header.w - margin_x;

    for (const auto& row : rows) {
        if (row.empty()) {
            continue;
        }

        const int row_width = static_cast<int>(row.size()) * checkbox_width +
                              static_cast<int>(row.size() - 1) * margin_x;
        int x = header.x + (header.w - row_width) / 2;
        if (row_width > (right_limit - left_limit)) {
            x = left_limit;
        } else {
            x = std::max(x, left_limit);
            if (x + row_width > right_limit) {
                x = right_limit - row_width;
            }
        }

        for (FilterEntry* entry : row) {
            if (!entry || !entry->checkbox) {
                continue;
            }

            SDL_Rect rect{x, y, checkbox_width, checkbox_height};
            entry->checkbox->set_rect(rect);

            min_x = std::min(min_x, rect.x);
            min_y = std::min(min_y, rect.y);
            max_x = std::max(max_x, rect.x + rect.w);
            max_y = std::max(max_y, rect.y + rect.h);

            x += checkbox_width + margin_x;
        }

        y += checkbox_height + row_gap;
    }

    if (max_x > min_x && max_y > min_y) {
        filter_header_rect_ = SDL_Rect{min_x, min_y, max_x - min_x, max_y - min_y};
    } else {
        filter_header_rect_ = SDL_Rect{0, 0, 0, 0};
    }
}

void DevControls::render_filter_header(SDL_Renderer* renderer) const {
    if (!enabled_ || !renderer) return;
    if (filter_header_rect_.w <= 0 || filter_header_rect_.h <= 0) return;

    for (const auto& entry : filter_entries_) {
        if (entry.checkbox) {
            entry.checkbox->render(renderer);
        }
    }
}

bool DevControls::handle_filter_header_event(const SDL_Event& event) {
    if (!enabled_) return false;
    if (filter_header_rect_.w <= 0 || filter_header_rect_.h <= 0) return false;
    bool used = false;
    for (auto& entry : filter_entries_) {
        if (!entry.checkbox) continue;
        if (entry.checkbox->handle_event(event)) {
            used = true;
        }
    }
    if (used) {
        sync_filter_state_from_ui();
    }
    return used;
}

bool DevControls::is_point_inside_filter_header(int x, int y) const {
    if (!enabled_) return false;
    SDL_Point p{x, y};
    for (const auto& entry : filter_entries_) {
        if (!entry.checkbox) continue;
        const SDL_Rect& rect = entry.checkbox->rect();
        if (rect.w <= 0 || rect.h <= 0) continue;
        if (SDL_PointInRect(&p, &rect)) {
            return true;
        }
    }
    return false;
}

void DevControls::sync_filter_state_from_ui() {
    for (auto& entry : filter_entries_) {
        if (!entry.checkbox) continue;
        const bool value = entry.checkbox->value();
        switch (entry.kind) {
        case FilterKind::MapAssets:
            filter_state_.map_assets = value;
            break;
        case FilterKind::CurrentRoom:
            filter_state_.current_room = value;
            break;
        case FilterKind::Type:
            filter_state_.type_filters[entry.id] = value;
            break;
        }
    }
    refresh_active_asset_filters();
}

void DevControls::reset_asset_filters() {
    for (auto& entry : filter_entries_) {
        if (entry.checkbox) {
            entry.checkbox->set_value(true);
        }
    }
    filter_state_.map_assets = true;
    filter_state_.current_room = true;
    for (auto& kv : filter_state_.type_filters) {
        kv.second = true;
    }
    sync_filter_state_from_ui();
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
    if (assets_) {
        const auto& active = assets_->getActive();
        for (Asset* asset : active) {
            if (!asset) continue;
            if (!passes_asset_filters(asset)) {
                asset->set_highlighted(false);
                asset->set_selected(false);
            }
        }
    }
}

void DevControls::rebuild_map_asset_spawn_ids() {
    map_asset_spawn_ids_.clear();
    if (!map_info_json_) return;
    try {
        auto it = map_info_json_->find("map_assets_data");
        if (it != map_info_json_->end()) {
            collect_spawn_ids(*it, map_asset_spawn_ids_);
        }
    } catch (...) {
    }
    refresh_active_asset_filters();
}

void DevControls::rebuild_current_room_spawn_ids() {
    current_room_spawn_ids_.clear();
    if (!current_room_) return;
    try {
        nlohmann::json& data = current_room_->assets_data();
        collect_spawn_ids(data, current_room_spawn_ids_);
    } catch (...) {
    }
    refresh_active_asset_filters();
}

void DevControls::collect_spawn_ids(const nlohmann::json& node, std::unordered_set<std::string>& out) {
    if (node.is_object()) {
        auto sg = node.find("spawn_groups");
        if (sg != node.end() && sg->is_array()) {
            for (const auto& entry : *sg) {
                if (!entry.is_object()) continue;
                auto id_it = entry.find("spawn_id");
                if (id_it != entry.end() && id_it->is_string()) {
                    out.insert(id_it->get<std::string>());
                }
            }
        }
        for (const auto& item : node.items()) {
            if (item.key() == "spawn_groups") continue;
            collect_spawn_ids(item.value(), out);
        }
    } else if (node.is_array()) {
        for (const auto& element : node) {
            collect_spawn_ids(element, out);
        }
    }
}

bool DevControls::type_filter_enabled(const std::string& type) const {
    auto it = filter_state_.type_filters.find(type);
    if (it == filter_state_.type_filters.end()) {
        return true;
    }
    return it->second;
}

bool DevControls::is_map_asset(const Asset* asset) const {
    if (!asset) return false;
    if (asset->spawn_id.empty()) return false;
    return map_asset_spawn_ids_.find(asset->spawn_id) != map_asset_spawn_ids_.end();
}

bool DevControls::is_current_room_asset(const Asset* asset, bool already_map_asset) const {
    if (!asset || already_map_asset) return false;
    if (!asset->info) return false;
    std::string type = asset_types::canonicalize(asset->info->type);
    if (type == asset_types::boundary) return false;
    if (asset->spawn_id.empty()) return false;
    return current_room_spawn_ids_.find(asset->spawn_id) != current_room_spawn_ids_.end();
}

bool DevControls::passes_asset_filters(Asset* asset) const {
    if (!asset) return false;
    if (!asset->info) return true;
    std::string type = asset_types::canonicalize(asset->info->type);
    if (!type_filter_enabled(type)) {
        return false;
    }
    const bool map_asset = is_map_asset(asset);
    if (map_asset && !filter_state_.map_assets) {
        return false;
    }
    if (is_current_room_asset(asset, map_asset) && !filter_state_.current_room) {
        return false;
    }
    return true;
}

std::string DevControls::format_type_label(const std::string& type) const {
    if (type.empty()) return std::string{};
    std::string label = type;
    for (char& ch : label) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
    return label;
}




