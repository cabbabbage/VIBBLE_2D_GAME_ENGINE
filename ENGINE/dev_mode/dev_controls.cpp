#include "dev_controls.hpp"

#include <SDL.h>
#include <fstream>
#include <sstream>
#include <cmath>

#include "dev_mode/map_editor.hpp"
#include "dev_mode/room_editor.hpp"
#include "dev_mode/map_mode_ui.hpp"
#include "dev_mode/full_screen_collapsible.hpp"
#include "dev_mode/camera_ui.hpp"
#include "dev_mode/LightRaysUIPanel.hpp"
#include "dev_mode/sdl_pointer_utils.hpp"
#include "dev_mode/area_mode/create_room_area_panel.hpp"
#include "dev_mode/area_mode/edit_room_area_panel.hpp"
#include "dev_mode/area_mode/area_types.hpp"
#include "dev_mode/area_overlay_editor.hpp"
#include "asset/asset_info.hpp"
#include "dm_styles.hpp"
#include "draw_utils.hpp"
#include "widgets.hpp"
#include "dev_controls_persistence.hpp"
#include "render/global_light_source.hpp"
#include "map_generation/map_layers_geometry.hpp"

#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "asset/asset_utils.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "map_generation/room.hpp"
#include "spawn/asset_spawn_planner.hpp"
#include "spawn/asset_spawner.hpp"
#include "spawn/check.hpp"
#include "spawn/methods/center_spawner.hpp"
#include "spawn/methods/exact_spawner.hpp"
#include "spawn/methods/perimeter_spawner.hpp"
#include "spawn/methods/percent_spawner.hpp"
#include "spawn/methods/random_spawner.hpp"
#include "spawn/spawn_logger.hpp"
#include "spawn/spawn_context.hpp"
#include "utils/area.hpp"
#include "utils/map_grid.hpp"
#include "utils/input.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <cctype>
#include <string>
#include <vector>
#include <optional>
#include <iostream>
#include <random>
#include <nlohmann/json.hpp>

using devmode::sdl::event_point;
using devmode::sdl::is_pointer_event;

namespace {

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

void dev_mode_trace(const std::string& message) {
    try {
        std::ofstream log("dev_mode_trace.log", std::ios::app);
        log << message << '\n';
    } catch (...) {
        // Never propagate logging failures.
    }
}

constexpr const char* kModeIdRoom = "room";
constexpr const char* kModeIdMap = "map";
constexpr const char* kModeIdArea = "area";
constexpr int kPopupOutlineThickness = 1;

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

}

void DevControls::RoomAreaCache::set_listener(Listener listener) {
    listener_ = std::move(listener);
}

void DevControls::RoomAreaCache::invalidate() {
    dirty_ = true;
}

const DevControls::RoomAreaCache::PolygonList&
DevControls::RoomAreaCache::ensure_from_json(const nlohmann::json* root,
                                             std::optional<SDL_Point> default_anchor) {
    if (root != last_source_) {
        dirty_ = true;
    }
    if (!dirty_) {
        return cached_;
    }
    cached_.clear();
    last_source_ = root;
    if (root) {
        try {
            if (root->contains("areas") && (*root)["areas"].is_array()) {
                for (const auto& item : (*root)["areas"]) {
                    if (!item.is_object()) continue;
                    const std::string name = item.contains("name") && item["name"].is_string()
                                                ? item["name"].get<std::string>()
                                                : std::string{};
                    if (name.empty()) continue;
                    const std::string type = item.contains("type") && item["type"].is_string()
                                                 ? item["type"].get<std::string>()
                                                 : std::string{};
                    RoomAreaSerialization::Kind kind =
                            RoomAreaSerialization::infer_kind_from_entry(item, type, name);
                    if (!RoomAreaSerialization::is_supported_kind(kind)) {
                        continue;
                    }
                    const auto& pts = item.contains("points") ? item["points"] : nlohmann::json();
                    if (!pts.is_array() || pts.size() < 3) continue;

                    if (default_anchor.has_value()) {
                        SDL_Point fallback = *default_anchor;
                        auto anchor = RoomAreaSerialization::resolve_anchor(item, fallback, kind);
                        std::vector<SDL_Point> poly = RoomAreaSerialization::decode_points(item, anchor.world);
                        if (poly.size() >= 3) {
                            Polygon entry;
                            entry.name = name;
                            entry.type = !type.empty() ? type : RoomAreaSerialization::to_string(kind);
                            entry.points = std::move(poly);
                            entry.anchor = anchor.world;
                            cached_.push_back(std::move(entry));
                        }
                    } else {
                        int ax = 0;
                        int ay = 0;
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
                        if (poly.size() >= 3) {
                            Polygon entry;
                            entry.name = name;
                            entry.type = !type.empty() ? type : RoomAreaSerialization::to_string(kind);
                            entry.points = std::move(poly);
                            entry.anchor = SDL_Point{ ax, ay };
                            cached_.push_back(std::move(entry));
                        }
                    }
                }
            }
        } catch (...) {
            cached_.clear();
        }
    }
    dirty_ = false;
    ++generation_;
    if (listener_) {
        listener_(cached_, generation_);
    }
    return cached_;
}

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
        const SDL_Color bg = DMStyles::PanelBG();
        const SDL_Color highlight = DMStyles::HighlightColor();
        const SDL_Color shadow = DMStyles::ShadowColor();
        dm_draw::DrawBeveledRect(
            renderer,
            rect_,
            DMStyles::CornerRadius(),
            DMStyles::BevelDepth(),
            bg,
            highlight,
            shadow,
            false,
            DMStyles::HighlightIntensity(),
            DMStyles::ShadowIntensity());
        const SDL_Color border = DMStyles::Border();
        dm_draw::DrawRoundedOutline(
            renderer,
            rect_,
            DMStyles::CornerRadius(),
            kPopupOutlineThickness,
            border);
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
    const char* ctor_start = "[DevControls] ctor start";
    dev_mode_trace(ctor_start);
    std::cout << ctor_start << "\n";
    room_editor_ = std::make_unique<RoomEditor>(assets_, screen_w_, screen_h_);
    if (room_editor_) {
        room_editor_->set_room_assets_saved_callback([this]() { notify_room_area_data_changed(); });
        room_editor_->set_header_visibility_callback([this](bool visible) {
            sliding_headers_hidden_ = visible;
            apply_header_suppression();
        });
    }
    map_editor_ = std::make_unique<MapEditor>(assets_);
    map_mode_ui_ = std::make_unique<MapModeUI>(assets_);
    apply_header_suppression();
    camera_panel_ = std::make_unique<CameraUIPanel>(assets_, 72, 72);
    if (camera_panel_) {
        camera_panel_->close();
    }
    light_rays_panel_ = std::make_unique<LightRaysUIPanel>(360, 96);
    if (light_rays_panel_) {
        light_rays_panel_->close();
        light_rays_panel_->set_on_close([this]() { sync_header_button_states(); });
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
                asset_filter_.set_active_mode(kModeIdMap);
            } else if (mode == MapModeUI::HeaderMode::Room) {
                if (this->mode_ == Mode::MapEditor) {
                    exit_map_editor_mode(false, true);
                }
                this->set_mode(Mode::RoomEditor);
                if (map_mode_ui_) {
                    map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Room);
                    if (auto* footer = map_mode_ui_->get_footer_panel()) {
                        std::string label = std::string("Room: ") + (current_room_ ? current_room_->room_name : std::string{});
                        footer->set_title(label);
                    }
                }
                asset_filter_.set_active_mode(kModeIdRoom);
            } else if (mode == MapModeUI::HeaderMode::Area) {
                if (this->mode_ == Mode::MapEditor) {
                    exit_map_editor_mode(false, true);
                }
                this->set_mode(Mode::AreaMode);
                if (map_mode_ui_) {
                    map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Area);
                    if (auto* footer = map_mode_ui_->get_footer_panel()) {
                        std::string label = std::string("Area Mode — Room: ") + (current_room_ ? current_room_->room_name : std::string{});
                        footer->set_title(label);
                    }
                }

                active_area_type_filters_.clear();
                active_area_type_filters_.insert("all");
                asset_filter_.set_active_mode(kModeIdArea);
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
    const bool layers_panel_open = map_mode_ui_ && map_mode_ui_->is_layers_footer_visible();
    asset_filter_.set_enabled(enabled_ && !layers_panel_open);
    asset_filter_.set_screen_dimensions(screen_w_, screen_h_);
    asset_filter_.set_map_info(map_info_json_);
    asset_filter_.set_current_room(current_room_);
    asset_filter_.set_mode_buttons({
        {kModeIdRoom, "Room", mode_ == Mode::RoomEditor},
        {kModeIdMap, "Map", mode_ == Mode::MapEditor},
        {kModeIdArea, "Area", mode_ == Mode::AreaMode}
    });
    asset_filter_.set_mode_changed_callback([this](const std::string& id) {
        if (id == kModeIdMap) {
            if (this->mode_ != Mode::MapEditor) {
                enter_map_editor_mode();
            }
        } else if (id == kModeIdRoom) {
            if (this->mode_ == Mode::MapEditor) {
                exit_map_editor_mode(false, true);
            }
            this->set_mode(Mode::RoomEditor);
            if (map_mode_ui_) {
                map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Room);
                if (auto* footer = map_mode_ui_->get_footer_panel()) {
                    std::string label = std::string("Room: ") +
                                        (current_room_ ? current_room_->room_name : std::string{});
                    footer->set_title(label);
                }
            }
        } else if (id == kModeIdArea) {
            if (this->mode_ == Mode::MapEditor) {
                exit_map_editor_mode(false, true);
            }
            this->set_mode(Mode::AreaMode);
            if (map_mode_ui_) {
                map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Area);
                if (auto* footer = map_mode_ui_->get_footer_panel()) {
                    std::string label = std::string("Area Mode — Room: ") +
                                        (current_room_ ? current_room_->room_name : std::string{});
                    footer->set_title(label);
                }
            }
            active_area_type_filters_.clear();
            active_area_type_filters_.insert("all");
        }
        sync_header_button_states();
    });
    asset_filter_.set_filters_expanded(false);
    const char* ctor_end = "[DevControls] ctor complete";
    dev_mode_trace(ctor_end);
    std::cout << ctor_end << "\n";
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
    if (light_rays_panel_) {
        light_rays_panel_->set_map_info(map_info_json_, map_light_save_cb_);
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
    if (light_rays_panel_) light_rays_panel_->set_work_area(bounds);
    if (trail_suite_) trail_suite_->set_screen_dimensions(width, height);
    asset_filter_.set_screen_dimensions(width, height);
    if (map_assets_modal_) map_assets_modal_->set_screen_dimensions(width, height);
    if (boundary_assets_modal_) boundary_assets_modal_->set_screen_dimensions(width, height);
    asset_filter_.ensure_layout();
}

void DevControls::set_current_room(Room* room) {
    {
        std::ostringstream oss;
        oss << "[DevControls] set_current_room begin -> "
            << (room ? room->room_name : std::string("<null>"));
        dev_mode_trace(oss.str());
    }
    current_room_ = room;

    dev_selected_room_ = room;
    if (regenerate_popup_) regenerate_popup_->close();
    if (room_editor_) {
        dev_mode_trace("[DevControls] set_current_room -> room_editor set_current_room");
        room_editor_->set_current_room(room);
    }
    asset_filter_.set_current_room(room);
    if (map_mode_ui_) {
        if (auto* footer = map_mode_ui_->get_footer_panel()) {
            std::string label;
            if (mode_ == Mode::AreaMode) label = std::string("Area Mode — Room: ") + (current_room_ ? current_room_->room_name : std::string{});
            else if (mode_ == Mode::RoomEditor) label = std::string("Room: ") + (current_room_ ? current_room_->room_name : std::string{});
            else label = std::string("Map");
            footer->set_title(label);
        }
    }

    notify_room_area_data_changed();
    dev_mode_trace("[DevControls] set_current_room complete");
}

void DevControls::set_rooms(std::vector<Room*>* rooms) {
    rooms_ = rooms;
    if (map_editor_) map_editor_->set_rooms(rooms);
}

void DevControls::set_camera_override_for_testing(camera* camera_override) {
    camera_override_for_testing_ = camera_override;
    if (map_editor_) {
        map_editor_->set_camera_override_for_testing(camera_override);
    }
    apply_camera_area_render_flag();
}

void DevControls::set_map_context(nlohmann::json* map_info, const std::string& map_path) {
    map_info_json_ = map_info;
    map_path_ = map_path;
    if (map_mode_ui_) map_mode_ui_->set_map_context(map_info, map_path);
    if (map_mode_ui_) map_mode_ui_->set_light_save_callback(map_light_save_cb_);
    if (light_rays_panel_) light_rays_panel_->set_map_info(map_info_json_, map_light_save_cb_);
    asset_filter_.set_map_info(map_info_json_);
    configure_header_button_sets();
    notify_room_area_data_changed();
}

bool DevControls::is_pointer_over_dev_ui(int x, int y) const {
    if (camera_panel_ && camera_panel_->is_visible() && camera_panel_->is_point_inside(x, y)) {
        return true;
    }
    if (light_rays_panel_ && light_rays_panel_->is_visible() && light_rays_panel_->is_point_inside(x, y)) {
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
    if (!is_modal_blocking_panels() && enabled_ && asset_filter_.contains_point(x, y)) {
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
    {
        std::ostringstream oss;
        oss << "[DevControls] set_enabled(" << (enabled ? "true" : "false") << ") begin";
        const std::string msg = oss.str();
        dev_mode_trace(msg);
        std::cout << msg << "\n";
    }
    if (enabled == enabled_) {
        const char* msg = "[DevControls] set_enabled unchanged, exiting";
        dev_mode_trace(msg);
        std::cout << msg << "\n";
        return;
    }
    enabled_ = enabled;
    const bool layers_panel_open = map_mode_ui_ && map_mode_ui_->is_layers_footer_visible();
    asset_filter_.set_enabled(enabled_ && !layers_panel_open);

    if (enabled_) {
        const char* msg = "[DevControls] preparing enable flow";
        dev_mode_trace(msg);
        std::cout << msg << "\n";
        const bool camera_was_visible = camera_panel_ && camera_panel_->is_visible();
        const bool light_rays_was_visible = light_rays_panel_ && light_rays_panel_->is_visible();
        close_all_floating_panels();
        set_mode(Mode::RoomEditor);
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
        if (light_rays_was_visible && light_rays_panel_) {
            light_rays_panel_->open();
        }
        const char* msg_enable_done = "[DevControls] enable flow complete";
        dev_mode_trace(msg_enable_done);
        std::cout << msg_enable_done << "\n";
    } else {
        const char* msg_disable = "[DevControls] preparing disable flow";
        dev_mode_trace(msg_disable);
        std::cout << msg_disable << "\n";
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
        set_mode(Mode::RoomEditor);
        dev_selected_room_ = nullptr;
        if (room_editor_) {
            room_editor_->set_enabled(false);
        }
        close_camera_panel();
        close_light_rays_panel();
        const char* msg_disable_done = "[DevControls] disable flow complete";
        dev_mode_trace(msg_disable_done);
        std::cout << msg_disable_done << "\n";
    }

    sync_header_button_states();
    if (enabled_) {
        asset_filter_.ensure_layout();
    }
    {
        std::ostringstream oss;
        oss << "[DevControls] set_enabled(" << (enabled ? "true" : "false") << ") done";
        const std::string msg = oss.str();
        dev_mode_trace(msg);
        std::cout << msg << "\n";
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
    pointer_over_light_rays_panel_ =
        light_rays_panel_ && light_rays_panel_->is_visible() && light_rays_panel_->is_point_inside(input.getX(), input.getY());

    if (mode_ == Mode::MapEditor) {
        if (map_mode_ui_ && input.wasScancodePressed(SDL_SCANCODE_F8)) {
            map_mode_ui_->toggle_layers_panel();
        }
        if (map_editor_) {
            map_editor_->update(input);
            handle_map_selection();
        }
    } else if (mode_ == Mode::RoomEditor && room_editor_ && room_editor_->is_enabled()) {
        if (!pointer_over_camera_panel_ && !pointer_over_light_rays_panel_) {
            room_editor_->update(input);
        }
    } else if (mode_ == Mode::AreaMode) {

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
    if (light_rays_panel_) {
        light_rays_panel_->update(input, screen_w_, screen_h_);
    }
    if (regenerate_popup_ && regenerate_popup_->visible()) {
        regenerate_popup_->update(input);
    }
    const bool modal_hide = is_modal_blocking_panels();
    modal_headers_hidden_ = modal_hide;
    const bool hide_headers = modal_hide || sliding_headers_hidden_;
    const bool layers_panel_open = map_mode_ui_ && map_mode_ui_->is_layers_footer_visible();
    asset_filter_.set_enabled(enabled_ && !layers_panel_open);
    apply_header_suppression();
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
        SDL_Point pointer{input.getX(), input.getY()};
        if (!hide_headers && asset_filter_.contains_point(pointer.x, pointer.y)) {
            room_editor_->clear_highlighted_assets();
        } else if (!hide_headers) {
            FullScreenCollapsible* footer = map_mode_ui_ ? map_mode_ui_->get_footer_panel() : nullptr;
            if (footer && footer->visible()) {
                const SDL_Rect& header = footer->header_rect();
                if (header.w > 0 && header.h > 0 && SDL_PointInRect(&pointer, &header)) {
                    room_editor_->clear_highlighted_assets();
                }
            }
        }
    }

    sync_header_button_states();
}

void DevControls::update_ui(const Input& input) {
    if (!enabled_) return;
    if (!room_editor_) return;

    const bool room_editor_active = (mode_ == Mode::RoomEditor) && room_editor_->is_enabled();
    const bool spawn_panel_visible = room_editor_->is_spawn_group_panel_visible();

    if (!room_editor_active && !spawn_panel_visible) {
        return;
    }

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

    const bool modal_hide = is_modal_blocking_panels();
    modal_headers_hidden_ = modal_hide;
    const bool hide_headers = modal_hide || sliding_headers_hidden_;
    const bool layers_panel_open = map_mode_ui_ && map_mode_ui_->is_layers_footer_visible();
    asset_filter_.set_enabled(enabled_ && !layers_panel_open);
    apply_header_suppression();

    auto consume = [&](bool used) {
        if (used && input_) {
            input_->consumeEvent(event);
        }
        return used;
};

    if (!hide_headers && pointer_event && consume(asset_filter_.handle_event(event))) {
        return;
    }
    if (!hide_headers && pointer_relevant && enabled_ && asset_filter_.contains_point(pointer.x, pointer.y)) {
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

    const bool room_editor_active = can_use_room_editor_ui();
    const bool spawn_panel_visible = room_editor_ && room_editor_->is_spawn_group_panel_visible();
    const bool can_route_room_editor = room_editor_ && (room_editor_active || spawn_panel_visible);
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

    bool pointer_event_inside_light_rays = false;
    if (light_rays_panel_ && light_rays_panel_->is_visible()) {
        switch (event.type) {
        case SDL_MOUSEMOTION:
            pointer_event_inside_light_rays = light_rays_panel_->is_point_inside(event.motion.x, event.motion.y);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            pointer_event_inside_light_rays = light_rays_panel_->is_point_inside(event.button.x, event.button.y);
            break;
        case SDL_MOUSEWHEEL: {
            int mx = 0;
            int my = 0;
            SDL_GetMouseState(&mx, &my);
            pointer_event_inside_light_rays = light_rays_panel_->is_point_inside(mx, my);
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
    if (light_rays_panel_ && light_rays_panel_->is_visible()) {
        if (consume(light_rays_panel_->handle_event(event))) {
            return;
        }
    }

    bool block_for_camera = pointer_event_inside_camera;
    if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP || event.type == SDL_TEXTINPUT) && pointer_over_camera_panel_) {
        block_for_camera = true;
    }
    bool block_for_light_rays = pointer_event_inside_light_rays;
    if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP || event.type == SDL_TEXTINPUT) && pointer_over_light_rays_panel_) {
        block_for_light_rays = true;
    }
    if (block_for_camera || block_for_light_rays) {
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

        if (asset_area_editor_ && asset_area_editor_->is_active()) {
            if (asset_area_editor_->handle_event(event)) {
                consume(true);
                return;
            }
        }
        const auto& area_list = room_area_polygons();

        if (create_area_panel_ && create_area_panel_->handle_event(event)) {
            consume(true);
            return;
        }
        if (edit_area_panel_ && edit_area_panel_->handle_event(event)) {
            consume(true);
            return;
        }

        auto type_visible = [this](const std::string& type) -> bool {
            if (active_area_type_filters_.count("all") > 0) return true;
            if (active_area_type_filters_.empty()) return true;
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

        std::vector<std::string> selected_asset_types;
        for (const auto& t : devmode::area_mode::area_types()) {
            if (t == "all" || t == "trigger" || t == "spawning") continue;
            if (active_area_type_filters_.count(t)) selected_asset_types.push_back(t);
        }
        const bool viewing_all_assets = active_area_type_filters_.empty() || active_area_type_filters_.count("all") > 0;
        auto asset_type_visible = [&](const std::string& type) -> bool {
            if (viewing_all_assets) return true;
            return std::find(selected_asset_types.begin(), selected_asset_types.end(), type) != selected_asset_types.end();
};

        auto first_selected_type = [this]() -> std::string {
            for (const auto& t : devmode::area_mode::area_types()) {
                if (active_area_type_filters_.count(t)) return t;
            }
            return std::string{};
        }();

        if (event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
            SDL_Point sp = (event.type == SDL_MOUSEMOTION) ? SDL_Point{event.motion.x, event.motion.y}
                                                           : SDL_Point{event.button.x, event.button.y};
            SDL_Point world = assets_ ? assets_->getView().screen_to_map(sp) : sp;
            int new_hover = -1;
            const bool allow_room_area_hover = (first_selected_type == "trigger" || first_selected_type == "spawning");
            if (allow_room_area_hover) {
                for (int i = static_cast<int>(area_list.size()) - 1; i >= 0; --i) {
                    if (!type_visible(area_list[i].type)) continue;
                    if (point_in_poly(area_list[i].points, world)) { new_hover = i; break; }
                }
            }
            hovered_area_index_ = new_hover;

            Asset* new_hover_asset = nullptr;
            if (assets_) {
                const camera& cam = assets_->getView();
                float scale = cam.get_scale();
                float inv_scale = (scale != 0.0f) ? (1.0f / scale) : 1.0f;
                float player_screen_height = 1.0f;
                if (Asset* playerAsset = assets_->player) {
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

                if (event.type == SDL_MOUSEMOTION) {
                    for (Asset* a : assets_->getFilteredActiveAssets()) {
                        if (a) a->set_highlighted(false);
                    }
                }

                const auto& list = assets_->getFilteredActiveAssets();
                for (int i = static_cast<int>(list.size()) - 1; i >= 0; --i) {
                    Asset* a = list[i];
                    if (!a) continue;
                    SDL_Rect fb = screen_rect_for(a);
                    if (fb.w <= 0 || fb.h <= 0) continue;
                    if (SDL_PointInRect(&sp, &fb)) {
                        new_hover_asset = a;
                        break;
                    }
                }
                if (new_hover_asset) {
                    new_hover_asset->set_highlighted(true);
                }
            }
            area_hovered_asset_ = new_hover_asset;

            area_hovered_asset_with_area_ = nullptr;
            area_hovered_area_name_.clear();
            if (assets_) {
                const camera& cam = assets_->getView();
                float scale = cam.get_scale();
                float inv_scale = (scale != 0.0f) ? (1.0f / scale) : 1.0f;
                float player_screen_height = 1.0f;
                if (Asset* playerAsset = assets_->player) {
                    int ph = playerAsset->cached_h;
                    if (ph <= 0) {
                        if (SDL_Texture* pf = playerAsset->get_final_texture()) SDL_QueryTexture(pf, nullptr, nullptr, nullptr, &ph);
                    }
                    if (ph > 0) player_screen_height = static_cast<float>(ph) * inv_scale;
                }
                if (player_screen_height <= 0.0f) player_screen_height = 1.0f;

                const auto& list2 = assets_->getFilteredActiveAssets();
                bool found_area = false;
                for (int i = static_cast<int>(list2.size()) - 1; i >= 0 && !found_area; --i) {
                    Asset* a = list2[i];
                    if (!a || !a->info) continue;
                    const AssetInfo* inf = a->info.get();
                    for (const auto& na : inf->areas) {
                        const std::string& at = !na.type.empty() ? na.type : na.name;
                        if (!asset_type_visible(at)) continue;
                        if (!na.area) continue;
                        Area world_area = a->get_area(na.name);
                        const auto& wpts = world_area.get_points();
                        if (wpts.size() < 3) continue;
                        camera::RenderEffects eff = cam.compute_render_effects(SDL_Point{a->pos.x, a->pos.y}, 0.0f, player_screen_height);
                        SDL_Point pivot_linear = cam.map_to_screen(SDL_Point{a->pos.x, a->pos.y});
                        std::vector<SDL_Point> spts; spts.reserve(wpts.size());
                        for (const auto& wp : wpts) {
                            SDL_Point p_lin = cam.map_to_screen(wp);
                            const float dx = static_cast<float>(p_lin.x - pivot_linear.x);
                            const float dy = static_cast<float>(p_lin.y - pivot_linear.y);
                            const float sx = eff.screen_position.x + dx * eff.distance_scale;
                            const float sy = eff.screen_position.y + dy * (eff.distance_scale * eff.vertical_scale);
                            spts.push_back(SDL_Point{ static_cast<int>(std::lround(sx)), static_cast<int>(std::lround(sy)) });
                        }
                        if (!spts.empty() && point_in_poly(spts, sp)) {
                            area_hovered_asset_with_area_ = a;
                            area_hovered_area_name_ = na.name;
                            if (!area_hovered_asset_) {
                                area_hovered_asset_ = a;
                                if (event.type == SDL_MOUSEMOTION) {
                                    a->set_highlighted(true);
                                }
                            }
                            found_area = true;
                            break;
                        }
                    }
                }
            }
        }

        if (event.type == SDL_MOUSEBUTTONDOWN &&
            (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) {
            if (!first_selected_type.empty() && (first_selected_type == "trigger" || first_selected_type == "spawning")) {
                if (assets_ && current_room_) {
                    SDL_Point sp{event.button.x, event.button.y};
                    SDL_Point world = assets_->getView().screen_to_map(sp);
                    std::string area_type = first_selected_type;
                    std::string area_name;
                    if (hovered_area_index_ >= 0 && hovered_area_index_ < static_cast<int>(area_list.size())) {
                        const auto& hovered = area_list[hovered_area_index_];
                        area_name = hovered.name;
                        if (!hovered.type.empty()) {
                            area_type = hovered.type;
                        }
                    } else {
                        area_name = generate_unique_room_area_name(area_type);
                    }
                    if (!asset_area_editor_) asset_area_editor_ = std::make_unique<AreaOverlayEditor>();
                    if (asset_area_editor_) {
                        asset_area_editor_->attach_assets(assets_);
                        asset_area_editor_->set_on_saved([this]() {
                            notify_room_area_data_changed();
                        });
                        if (asset_area_editor_->begin_for_room(current_room_, area_name, area_type, world)) {
                            if (map_mode_ui_) {
                                if (auto* footer = map_mode_ui_->get_footer_panel()) {
                                    footer->set_title(std::string("Editing Room Area — ") + area_name);
                                }
                            }
                            consume(true);
                            return;
                        }
                    }
                }
            } else if (!first_selected_type.empty() && first_selected_type != "all") {
                if (asset_area_editor_) asset_area_editor_->set_on_saved(nullptr);
                Asset* target_asset = area_hovered_asset_with_area_ ? area_hovered_asset_with_area_ : area_hovered_asset_;
                if (target_asset && target_asset->info) {
                    std::string area_name = first_selected_type;
                    bool removed_existing = false;
                    for (const auto& na : target_asset->info->areas) {
                        const std::string normalized = !na.type.empty() ? na.type : na.name;
                        if (normalized == first_selected_type) {
                            area_name = na.name;
                            removed_existing = target_asset->info->remove_area(na.name);
                            if (removed_existing) {
                                (void)target_asset->info->update_info_json();
                            }
                            break;
                        }
                    }
                    if (!asset_area_editor_) asset_area_editor_ = std::make_unique<AreaOverlayEditor>();
                    if (asset_area_editor_) asset_area_editor_->attach_assets(assets_);
                    if (asset_area_editor_ && asset_area_editor_->begin(target_asset->info.get(), target_asset, area_name)) {
                        if (map_mode_ui_) {
                            if (auto* footer = map_mode_ui_->get_footer_panel()) {
                                std::string label = std::string("Editing ") + target_asset->info->name + std::string(" — Area: ") + area_name;
                                footer->set_title(label);
                            }
                        }
                        consume(true);
                        return;
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

        if (renderer && assets_) {

            auto type_visible = [this](const std::string& type) -> bool {
                if (active_area_type_filters_.count("all") > 0) return true;
                if (active_area_type_filters_.empty()) return true;
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

            auto draw_anchor = [&](SDL_Point world, SDL_Color color) {
                SDL_Point screen = cam.map_to_screen(world);
                const int arm = 5;
                SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
                SDL_RenderDrawLine(renderer, screen.x - arm, screen.y, screen.x + arm, screen.y);
                SDL_RenderDrawLine(renderer, screen.x, screen.y - arm, screen.x, screen.y + arm);
};

            {
                const auto& room_areas = room_area_polygons();
                if (!room_areas.empty()) {
                    for (int i = 0; i < static_cast<int>(room_areas.size()); ++i) {
                        const std::string& t = room_areas[i].type;
                        if (!type_visible(t)) continue;
                        const auto& poly_world = room_areas[i].points;
                        std::vector<SDL_Point> spts; spts.reserve(poly_world.size());
                        for (const auto& wp : poly_world) spts.push_back(cam.map_to_screen(wp));
                        if (spts.size() >= 3) {
                            SDL_Color base = color_for_type(t);
                            if (i == hovered_area_index_) base = SDL_Color{ std::min<Uint8>(255, Uint8(base.r + 30)), std::min<Uint8>(255, Uint8(base.g + 30)), std::min<Uint8>(255, Uint8(base.b + 30)), 120 };
                            if (i == selected_area_index_) base = SDL_Color{ std::min<Uint8>(255, Uint8(base.r + 60)), std::min<Uint8>(255, Uint8(base.g + 60)), std::min<Uint8>(255, Uint8(base.b + 60)), 150 };
#if SDL_VERSION_ATLEAST(2,0,18)
                            std::vector<SDL_Vertex> verts; verts.reserve(spts.size());
                            for (auto p : spts) { SDL_Vertex v{}; v.position=SDL_FPoint{(float)p.x,(float)p.y}; v.color=base; verts.push_back(v);}
                            std::vector<int> idxs; idxs.reserve((spts.size()-2)*3);
                            for (size_t k=1;k+1<spts.size();++k){ idxs.push_back(0); idxs.push_back((int)k); idxs.push_back((int)(k+1)); }
                            if (!idxs.empty()) SDL_RenderGeometry(renderer, nullptr, verts.data(), (int)verts.size(), idxs.data(), (int)idxs.size());
#endif
                        }
                        if (!spts.empty()) {
                            SDL_Color outline = (i == selected_area_index_) ? SDL_Color{255,255,255,200} : (i == hovered_area_index_ ? SDL_Color{230,230,230,180} : SDL_Color{220,220,220,120});
                            std::vector<SDL_Point> pts = spts; pts.push_back(spts.front());
                            SDL_SetRenderDrawColor(renderer, outline.r, outline.g, outline.b, outline.a);
                            SDL_RenderDrawLines(renderer, pts.data(), (int)pts.size());
                            if (!room_areas[i].points.empty()) {
                                draw_anchor(room_areas[i].anchor, SDL_Color{outline.r, outline.g, outline.b, 220});
                            }
                        }
                    }
                }
            }

            {
                bool viewing_all = active_area_type_filters_.count("all") > 0;
                bool viewing_room_types = active_area_type_filters_.count("trigger") > 0 || active_area_type_filters_.count("spawning") > 0;
                std::vector<std::string> selected_asset_types;
                for (const auto& t : devmode::area_mode::area_types()) {
                    if (t == "all" || t == "trigger" || t == "spawning") continue;
                    if (active_area_type_filters_.count(t)) selected_asset_types.push_back(t);
                }
                const bool show_all_assets = viewing_all || active_area_type_filters_.empty();
                if (assets_ && !viewing_room_types && (show_all_assets || !selected_asset_types.empty())) {
                    auto is_visible_type = [&](const std::string& type) {
                        if (show_all_assets) return true;
                        return std::find(selected_asset_types.begin(), selected_asset_types.end(), type) != selected_asset_types.end();
};
                    auto lighten = [](Uint8 channel, int delta) -> Uint8 {
                        int v = static_cast<int>(channel) + delta;
                        if (v < 0) v = 0;
                        if (v > 255) v = 255;
                        return static_cast<Uint8>(v);
};

                    const auto& list = assets_->getFilteredActiveAssets();
                    for (Asset* a : list) {
                        if (!a || !a->info) continue;
                        float scale = cam.get_scale();
                        float inv_scale = (scale != 0.0f) ? (1.0f / scale) : 1.0f;
                        float player_screen_height = 1.0f;
                        if (Asset* playerAsset = assets_->player) {
                            int ph = playerAsset->cached_h;
                            if (ph <= 0) {
                                if (SDL_Texture* pf = playerAsset->get_final_texture()) SDL_QueryTexture(pf, nullptr, nullptr, nullptr, &ph);
                            }
                            if (ph > 0) player_screen_height = static_cast<float>(ph) * inv_scale;
                        }
                        if (player_screen_height <= 0.0f) player_screen_height = 1.0f;

                        for (const auto& na : a->info->areas) {
                            const std::string& normalized = !na.type.empty() ? na.type : na.name;
                            if (!is_visible_type(normalized)) continue;
                            if (!na.area) continue;

                            Area world_area = a->get_area(na.name);
                            const auto& wpts = world_area.get_points();
                            if (wpts.size() < 3) continue;

                            camera::RenderEffects eff = cam.compute_render_effects(SDL_Point{a->pos.x, a->pos.y}, 0.0f, player_screen_height);
                            SDL_Point pivot_linear = cam.map_to_screen(SDL_Point{a->pos.x, a->pos.y});

                            std::vector<SDL_Point> spts; spts.reserve(wpts.size());
                            for (const auto& wp : wpts) {
                                SDL_Point p_lin = cam.map_to_screen(wp);
                                const float dx = static_cast<float>(p_lin.x - pivot_linear.x);
                                const float dy = static_cast<float>(p_lin.y - pivot_linear.y);
                                const float sx = eff.screen_position.x + dx * eff.distance_scale;
                                const float sy = eff.screen_position.y + dy * (eff.distance_scale * eff.vertical_scale);
                                spts.push_back(SDL_Point{ static_cast<int>(std::lround(sx)), static_cast<int>(std::lround(sy)) });
                            }

                            const bool hovered_this = (a == area_hovered_asset_with_area_) && (na.name == area_hovered_area_name_);
                            SDL_Color base = color_for_type(normalized);
                            SDL_Color fill = base;
                            fill.a = static_cast<Uint8>(hovered_this ? std::min<int>(255, base.a + 64) : std::max<int>(30, base.a / 2));

#if SDL_VERSION_ATLEAST(2,0,18)
                            if (spts.size() >= 3) {
                                std::vector<SDL_Vertex> verts; verts.reserve(spts.size());
                                for (auto p : spts) {
                                    SDL_Vertex v{};
                                    v.position = SDL_FPoint{static_cast<float>(p.x), static_cast<float>(p.y)};
                                    v.color = fill;
                                    verts.push_back(v);
                                }
                                std::vector<int> idxs; idxs.reserve((spts.size() - 2) * 3);
                                for (size_t i = 1; i + 1 < spts.size(); ++i) {
                                    idxs.push_back(0);
                                    idxs.push_back(static_cast<int>(i));
                                    idxs.push_back(static_cast<int>(i + 1));
                                }
                                if (!idxs.empty()) {
                                    SDL_RenderGeometry(renderer, nullptr, verts.data(), static_cast<int>(verts.size()), idxs.data(), static_cast<int>(idxs.size()));
                                }
                            }
#endif
                            if (!spts.empty()) {
                                SDL_Color outline = hovered_this ? SDL_Color{255, 255, 255, 200}
                                                                : SDL_Color{lighten(base.r, 20), lighten(base.g, 20), lighten(base.b, 20), std::max<Uint8>(base.a, 120)};
                                std::vector<SDL_Point> pts = spts; pts.push_back(spts.front());
                                SDL_SetRenderDrawColor(renderer, outline.r, outline.g, outline.b, outline.a);
                                SDL_RenderDrawLines(renderer, pts.data(), static_cast<int>(pts.size()));
                                draw_anchor(SDL_Point{a->pos.x, a->pos.y}, SDL_Color{outline.r, outline.g, outline.b, 220});
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
    if (renderer && map_mode_ui_ && map_mode_ui_->is_light_panel_visible() && assets_) {
        const camera& cam = assets_->getView();
        SDL_Point screen_center_map = cam.get_screen_center();
        SDL_Point screen_center = cam.map_to_screen(screen_center_map);
        SDL_BlendMode prev_mode = SDL_BLENDMODE_NONE;
        SDL_GetRenderDrawBlendMode(renderer, &prev_mode);
        Uint8 pr = 0, pg = 0, pb = 0, pa = 0;
        SDL_GetRenderDrawColor(renderer, &pr, &pg, &pb, &pa);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        bool drew_indicator = false;
        if (const Global_Light_Source* light = assets_->map_light_source()) {
            SDL_Texture* texture = light->get_texture();
            if (texture) {
                SDL_Point light_pos = light->get_position();
                SDL_Point start = screen_center;
                SDL_Point end = cam.map_to_screen(light_pos);
                SDL_SetRenderDrawColor(renderer, 220, 32, 32, 230);
                SDL_RenderDrawLine(renderer, start.x, start.y, end.x, end.y);
                SDL_Rect tip{ end.x - 4, end.y - 4, 8, 8 };
                SDL_RenderFillRect(renderer, &tip);
                drew_indicator = true;
            }
        }

        if (!drew_indicator) {
            SDL_SetRenderDrawColor(renderer, 220, 32, 32, 230);
            SDL_RenderDrawLine(renderer, screen_center.x - 6, screen_center.y - 6, screen_center.x + 6, screen_center.y + 6);
            SDL_RenderDrawLine(renderer, screen_center.x - 6, screen_center.y + 6, screen_center.x + 6, screen_center.y - 6);
        }

        SDL_SetRenderDrawColor(renderer, pr, pg, pb, pa);
        SDL_SetRenderDrawBlendMode(renderer, prev_mode);
    }
    if (map_mode_ui_) map_mode_ui_->render(renderer);
    if (map_assets_modal_ && map_assets_modal_->visible()) {
        map_assets_modal_->render(renderer);
    }
    if (boundary_assets_modal_ && boundary_assets_modal_->visible()) {
        boundary_assets_modal_->render(renderer);
    }
    if (trail_suite_) trail_suite_->render(renderer);
    if (light_rays_panel_ && light_rays_panel_->is_visible()) {
        light_rays_panel_->render(renderer);
    }
    if (camera_panel_ && camera_panel_->is_visible()) {
        camera_panel_->render(renderer);
    }
    if (regenerate_popup_ && regenerate_popup_->visible()) {
        regenerate_popup_->render(renderer);
    }
    const bool layers_panel_open = map_mode_ui_ && map_mode_ui_->is_layers_footer_visible();
    const bool hide_headers = modal_headers_hidden_ || sliding_headers_hidden_;
    if (!hide_headers && !is_modal_blocking_panels() && !layers_panel_open) {
        asset_filter_.render(renderer);
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
        map_buttons.push_back(std::move(lights_btn));
    }

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

    MapModeUI::HeaderButtonConfig light_rays_btn;
    light_rays_btn.id = "light_rays";
    light_rays_btn.label = "Light Rays";
    light_rays_btn.active = light_rays_panel_ && light_rays_panel_->is_visible();
    light_rays_btn.on_toggle = [this](bool active) {
        if (!light_rays_panel_) {
            sync_header_button_states();
            return;
        }
        if (room_editor_) {
            room_editor_->close_room_config();
        }
        const bool currently_open = light_rays_panel_->is_visible();
        if (active != currently_open) {
            toggle_light_rays_panel();
        } else {
            sync_header_button_states();
        }
};
    room_buttons.push_back(std::move(light_rays_btn));

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

    for (const auto& type : devmode::area_mode::area_types()) {
        MapModeUI::HeaderButtonConfig cfg;
        cfg.id = std::string("area_") + type;
        cfg.label = type;
        cfg.active = (active_area_type_filters_.count(type) > 0);
        cfg.active_style_override = &DMStyles::AccentButton();
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
    const bool light_rays_open = light_rays_panel_ && light_rays_panel_->is_visible();
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "light_rays", light_rays_open);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "lights", lights_open);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "regenerate", false);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "regenerate_other", false);

    const bool map_assets_open = map_assets_modal_ && map_assets_modal_->visible();
    const bool boundary_open = boundary_assets_modal_ && boundary_assets_modal_->visible();
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "map_assets", map_assets_open);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "map_boundary", boundary_open);

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
    if (light_rays_panel_) {
        light_rays_panel_->close();
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

void DevControls::apply_header_suppression() {
    if (map_mode_ui_) {
        const bool hide_headers = modal_headers_hidden_ || sliding_headers_hidden_;
        map_mode_ui_->set_headers_suppressed(hide_headers);
        map_mode_ui_->set_dev_sliding_headers_hidden(sliding_headers_hidden_);
    }
}

int DevControls::map_radius_or_default() const {
    if (!assets_) {
        return 1000;
    }
    int radius = 0;
    try {
        const nlohmann::json& map_json = assets_->map_info_json();
        if (map_json.is_object()) {
            const double computed = map_layers::map_radius_from_map_info(map_json);
            if (computed > 0.0) {
                radius = static_cast<int>(std::lround(computed));
            }
        }
    } catch (...) {
        radius = 0;
    }
    if (radius <= 0) {
        const auto& rooms = assets_->rooms();
        for (Room* room : rooms) {
            if (!room || !room->room_area) {
                continue;
            }
            auto [minx, miny, maxx, maxy] = room->room_area->get_bounds();
            int extent = 0;
            extent = std::max(extent, std::abs(minx));
            extent = std::max(extent, std::abs(miny));
            extent = std::max(extent, std::abs(maxx));
            extent = std::max(extent, std::abs(maxy));
            radius = std::max(radius, extent);
        }
    }
    if (radius <= 0) {
        radius = 1000;
    }
    return radius;
}

void DevControls::remove_spawn_group_assets(const std::string& spawn_id) {
    if (!assets_ || spawn_id.empty()) {
        return;
    }
    std::vector<Asset*> to_remove;
    to_remove.reserve(assets_->all.size());
    for (Asset* asset : assets_->all) {
        if (!asset || asset->dead) {
            continue;
        }
        if (asset == assets_->player) {
            continue;
        }
        if (asset->spawn_id == spawn_id) {
            to_remove.push_back(asset);
        }
    }
    for (Asset* asset : to_remove) {
        purge_asset(asset);
        auto& all = assets_->all;
        all.erase(std::remove(all.begin(), all.end(), asset), all.end());
        asset->Delete();
    }
}

void DevControls::integrate_spawned_assets(std::vector<std::unique_ptr<Asset>>& spawned) {
    if (!assets_ || spawned.empty()) {
        return;
    }
    for (auto& uptr : spawned) {
        if (!uptr) {
            continue;
        }
        Asset* raw = uptr.get();
        set_camera_recursive(raw, &assets_->getView());
        set_assets_owner_recursive(raw, assets_);
        raw->finalize_setup();
        assets_->owned_assets.emplace_back(std::move(uptr));
        assets_->all.push_back(raw);
    }
    spawned.clear();
    assets_->initialize_active_assets(assets_->getView().get_screen_center());
    assets_->refresh_active_asset_lists();
    assets_->update_closest_assets(assets_->player, 3);
}

void DevControls::regenerate_map_spawn_group(const nlohmann::json& entry) {
    if (!assets_ || !entry.is_object()) {
        return;
    }
    const std::string spawn_id = entry.value("spawn_id", std::string{});
    if (spawn_id.empty()) {
        return;
    }

    remove_spawn_group_assets(spawn_id);

    std::unordered_map<std::string, std::shared_ptr<AssetInfo>> asset_info_library = assets_->library().all();
    std::vector<std::unique_ptr<Asset>> spawned;
    Check checker(false);
    std::mt19937 rng(std::random_device{}());

    const auto& rooms = assets_->rooms();
    ExactSpawner exact;
    CenterSpawner center;
    RandomSpawner random;
    PerimeterSpawner perimeter;
    PercentSpawner percent;

    for (Room* room : rooms) {
        if (!room || !room->room_area) {
            continue;
        }
        nlohmann::json& room_json = room->assets_data();
        if (!room_json.is_object()) {
            continue;
        }
        if (!room_json.value("inherits_map_assets", false)) {
            continue;
        }

        nlohmann::json root = nlohmann::json::object();
        root["spawn_groups"] = nlohmann::json::array();
        root["spawn_groups"].push_back(entry);
        std::vector<nlohmann::json> sources{root};
        std::vector<std::string> paths;
        AssetSpawnPlanner planner(sources, *room->room_area, assets_->library(), paths);

        MapGrid grid = MapGrid::from_area_bounds(*room->room_area, 100);
        std::vector<Area> exclusion;
        SpawnLogger logger("", "");
        SpawnContext ctx(rng, checker, logger, exclusion, asset_info_library, spawned, &assets_->library(), &grid);

        const auto& queue = planner.get_spawn_queue();
        const Area* area_ptr = room->room_area.get();
        for (const auto& info : queue) {
            const std::string& pos = info.position;
            if (pos == "Exact" || pos == "Exact Position") {
                exact.spawn(info, area_ptr, ctx);
            } else if (pos == "Center") {
                center.spawn(info, area_ptr, ctx);
            } else if (pos == "Perimeter") {
                perimeter.spawn(info, area_ptr, ctx);
            } else if (pos == "Percent") {
                percent.spawn(info, area_ptr, ctx);
            } else {
                random.spawn(info, area_ptr, ctx);
            }
        }
    }

    integrate_spawned_assets(spawned);
}

void DevControls::regenerate_boundary_spawn_group(const nlohmann::json& entry) {
    if (!assets_ || !entry.is_object()) {
        return;
    }
    const std::string spawn_id = entry.value("spawn_id", std::string{});
    if (spawn_id.empty()) {
        return;
    }

    remove_spawn_group_assets(spawn_id);

    const int radius = map_radius_or_default();
    const int diameter = radius * 2;
    SDL_Point center{radius, radius};
    Area area("map_boundary_regen", center, diameter, diameter, "Circle", 1, diameter, diameter);

    std::vector<Area> exclusion;
    const auto& rooms = assets_->rooms();
    exclusion.reserve(rooms.size());
    for (Room* room : rooms) {
        if (room && room->room_area) {
            exclusion.push_back(*room->room_area);
        }
    }

    AssetSpawner spawner(&assets_->library(), exclusion);
    nlohmann::json root = nlohmann::json::object();
    root["spawn_groups"] = nlohmann::json::array();
    root["spawn_groups"].push_back(entry);
    std::string source = assets_->map_info_path();
    if (!source.empty()) {
        source += "::map_boundary_data";
    }
    auto spawned = spawner.spawn_boundary_from_json(root, area, source);
    integrate_spawned_assets(spawned);
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
    auto save = [this]() { return persist_map_info_to_disk(); };
    auto regen = [this](const nlohmann::json& entry) { this->regenerate_map_spawn_group(entry); };
    auto& map_json = assets_->map_info_json();
    SDL_Color color{200, 200, 255, 255};
    map_assets_modal_->open(map_json, "map_assets_data", "batch_map_assets", "Map-wide", color, save, regen);
}

void DevControls::apply_camera_area_render_flag() {
    camera* cam_ptr = nullptr;
    if (camera_override_for_testing_) {
        cam_ptr = camera_override_for_testing_;
    } else if (assets_) {
        cam_ptr = &assets_->getView();
    }

    if (!cam_ptr) {
        return;
    }

    cam_ptr->set_render_areas_enabled(false);

    const bool area_mode = (mode_ == Mode::AreaMode);
    cam_ptr->set_realism_enabled(!area_mode);
}

void DevControls::set_mode(Mode new_mode) {
    if (mode_ == new_mode) {
        return;
    }
    mode_ = new_mode;
    switch (mode_) {
    case Mode::RoomEditor:
        asset_filter_.set_active_mode(kModeIdRoom);
        break;
    case Mode::MapEditor:
        asset_filter_.set_active_mode(kModeIdMap);
        break;
    case Mode::AreaMode:
        asset_filter_.set_active_mode(kModeIdArea);
        break;
    }
    apply_camera_area_render_flag();
}

std::string DevControls::generate_unique_room_area_name(const std::string& base) const {
    std::unordered_set<std::string> used_names;
    if (current_room_) {
        for (const auto& entry : current_room_->areas) {
            used_names.insert(entry.name);
        }
    }

    std::string prefix = base.empty() ? std::string("area") : base;
    const std::string suffix = "_area";
    if (prefix.size() < suffix.size() || prefix.substr(prefix.size() - suffix.size()) != suffix) {
        prefix += suffix;
    }

    std::string candidate = prefix;
    int counter = 1;
    while (used_names.count(candidate) > 0) {
        candidate = prefix + "_" + std::to_string(counter++);
    }
    return candidate;
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
    auto save = [this]() { return persist_map_info_to_disk(); };
    auto regen = [this](const nlohmann::json& entry) { this->regenerate_boundary_spawn_group(entry); };
    auto& map_json = assets_->map_info_json();
    SDL_Color color{255, 200, 120, 255};
    boundary_assets_modal_->open(map_json,
                                 "map_boundary_data",
                                 "batch_map_boundary",
                                 "Boundary",
                                 color,
                                 save,
                                 regen);
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

void DevControls::set_room_area_cache_listener(RoomAreaCache::Listener listener) {
    room_area_cache_.set_listener(std::move(listener));
}

std::size_t DevControls::room_area_cache_generation() const {
    return room_area_cache_.generation();
}

void DevControls::notify_room_area_data_changed() {
    room_area_cache_.invalidate();
}

const DevControls::RoomAreaCache::PolygonList& DevControls::room_area_polygons() {
    const nlohmann::json* root = nullptr;
    std::optional<SDL_Point> default_anchor;
    if (current_room_) {
        root = &current_room_->assets_data();
        SDL_Point anchor{ current_room_->map_origin.first, current_room_->map_origin.second };
        if (current_room_->room_area) {
            anchor = current_room_->room_area->get_center();
        }
        default_anchor = anchor;
    }
    return room_area_cache_.ensure_from_json(root, default_anchor);
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

void DevControls::set_map_light_panel_visible(bool visible) {
    if (!map_mode_ui_) {
        return;
    }
    const bool currently_open = map_mode_ui_->is_light_panel_visible();
    if (visible == currently_open) {
        return;
    }
    if (visible) {
        if (is_modal_blocking_panels()) {
            pulse_modal_header();
            sync_header_button_states();
            return;
        }
        map_mode_ui_->open_light_panel();
    } else {
        map_mode_ui_->close_light_panel();
    }
    sync_header_button_states();
}

bool DevControls::is_map_light_panel_visible() const {
    return map_mode_ui_ && map_mode_ui_->is_light_panel_visible();
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

void DevControls::toggle_light_rays_panel() {
    if (!light_rays_panel_) {
        return;
    }
    if (light_rays_panel_->is_visible()) {
        light_rays_panel_->close();
    } else {
        if (is_modal_blocking_panels()) {
            pulse_modal_header();
            sync_header_button_states();
            return;
        }
        light_rays_panel_->open();
    }
    sync_header_button_states();
}

void DevControls::close_light_rays_panel() {
    if (light_rays_panel_) {
        light_rays_panel_->close();
    }
}

bool DevControls::can_use_room_editor_ui() const {
    return enabled_ && mode_ == Mode::RoomEditor && room_editor_ && room_editor_->is_enabled();
}

void DevControls::enter_map_editor_mode() {
    if (!map_editor_) return;
    if (mode_ == Mode::MapEditor) return;

    close_all_floating_panels();
    set_mode(Mode::MapEditor);
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
    set_mode(Mode::RoomEditor);
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
    if (!assets_ || !enabled_) {
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

bool DevControls::persist_map_info_to_disk() const {
    if (!assets_) {
        std::cerr << "[DevControls] Cannot persist map info: assets manager not set\n";
        return false;
    }
    return devmode::write_map_info_json(assets_->map_info_path(), assets_->map_info_json(), std::cerr);
}

