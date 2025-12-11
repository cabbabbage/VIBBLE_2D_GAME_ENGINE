#include "map_mode_ui.hpp"

#include "MapLightPanel.hpp"
#include "map_layers_preview_panel.hpp"
#include "DockableCollapsible.hpp"
#include "FloatingPanelLayoutManager.hpp"
#include "FloatingDockableManager.hpp"
#include "dev_footer_bar.hpp"
#include "map_layers_controller.hpp"
#include "map_layer_controls_display.hpp"
#include "map_layers_common.hpp"
#include "map_layers_panel.hpp"
#include "map_rooms_display.hpp"
#include "room_config/room_configurator.hpp"
#include "spawn_group_config/spawn_group_utils.hpp"
#include "SlidingWindowContainer.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/widgets.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/dev_controls_persistence.hpp"
#include "dm_styles.hpp"
#include "utils/input.hpp"

#include <SDL.h>
#include <SDL_log.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <iostream>
#include <vector>
#include <utility>
#include <nlohmann/json.hpp>

namespace {
constexpr int kDefaultPanelX = 48;
constexpr int kDefaultPanelY = 48;
constexpr const char* kButtonIdLights = "lights";

std::string trim_copy(const std::string& input) {
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    std::string result = input;
    result.erase(result.begin(), std::find_if(result.begin(), result.end(), [&](unsigned char ch) {
        return !is_space(ch);
    }));
    result.erase(std::find_if(result.rbegin(), result.rend(), [&](unsigned char ch) {
        return !is_space(ch);
    }).base(), result.end());
    return result;
}

using devmode::spawn::ensure_spawn_group_entry_defaults;
using devmode::spawn::ensure_spawn_groups_array;
using devmode::spawn::generate_spawn_id;
using devmode::spawn::sanitize_perimeter_spawn_groups;

std::string sanitize_room_key(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    bool last_underscore = false;
    for (char ch : input) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch)) {
            out.push_back(static_cast<char>(std::tolower(uch)));
            last_underscore = false;
        } else if (ch == '_' || ch == '-') {
            if (!last_underscore && !out.empty()) {
                out.push_back('_');
                last_underscore = true;
            }
        } else if (std::isspace(uch)) {
            if (!last_underscore && !out.empty()) {
                out.push_back('_');
                last_underscore = true;
            }
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    if (out.empty()) {
        out = "room";
    }
    return out;
}

}

MapModeUI::MapModeUI(Assets* assets)
    : assets_(assets) {}

MapModeUI::~MapModeUI() {
    cancel_map_color_sampling(true);
    if (map_color_sampling_cursor_handle_) {
        SDL_FreeCursor(map_color_sampling_cursor_handle_);
        map_color_sampling_cursor_handle_ = nullptr;
    }
}

void MapModeUI::set_manifest_store(devmode::core::ManifestStore* store) {
    SDL_assert(store != nullptr);
    manifest_store_ = store;
    if (layers_controller_) {
        layers_controller_->set_manifest_store(manifest_store_, map_id_);
    }
}

void MapModeUI::set_map_context(nlohmann::json* map_info, const std::string& map_path) {
    map_info_ = map_info;
    map_path_ = map_path;
    map_id_ = assets_ ? assets_->map_id() : std::string{};
    if (layers_controller_) {
        layers_controller_->set_manifest_store(manifest_store_, map_id_);
    }
    sync_panel_map_info();
}

void MapModeUI::set_screen_dimensions(int w, int h) {
    screen_w_ = w;
    screen_h_ = h;
    light_panel_centered_ = false;
    ensure_panels();
    sliding_area_bounds_ = sanitize_sliding_area(sliding_area_bounds_);
    apply_sliding_area_bounds();
    update_footer_visibility();
    ensure_light_and_shading_positions();
}

void MapModeUI::set_sliding_area_bounds(const SDL_Rect& bounds) {
    SDL_Rect sanitized = sanitize_sliding_area(bounds);
    if (sanitized.x == sliding_area_bounds_.x &&
        sanitized.y == sliding_area_bounds_.y &&
        sanitized.w == sliding_area_bounds_.w &&
        sanitized.h == sliding_area_bounds_.h) {
        return;
    }
    sliding_area_bounds_ = sanitized;
    ensure_panels();
    apply_sliding_area_bounds();
}

void MapModeUI::set_map_mode_active(bool active) {
    map_mode_active_ = active;
    if (active) {
        footer_buttons_configured_ = false;
    }
    ensure_panels();
    update_footer_visibility();
    sync_footer_button_states();
    set_active_panel(PanelType::None);
    if (!active) {
        close_room_configuration(false);
    }
}

DevFooterBar* MapModeUI::get_footer_bar() const {
    return footer_bar_.get();
}

void MapModeUI::collect_sliding_container_rects(std::vector<SDL_Rect>& out) const {
    auto append_container = [&out](const SlidingWindowContainer* container) {
        if (!container || !container->is_visible()) {
            return;
        }
        const SDL_Rect& rect = container->panel_rect();
        if (rect.w > 0 && rect.h > 0) {
            out.push_back(rect);
        }
};

    append_container(room_config_container_.get());
    append_container(rooms_list_container_.get());
    append_container(layer_controls_container_.get());

    if (room_configurator_ && room_configurator_->visible()) {
        const SDL_Rect& rect = room_configurator_->panel_rect();
        if (rect.w > 0 && rect.h > 0) {
            out.push_back(rect);
        }
    }
}

SDL_Rect MapModeUI::sanitize_sliding_area(const SDL_Rect& bounds) const {
    if (screen_w_ <= 0 || screen_h_ <= 0) {
        return SDL_Rect{0, 0, 0, 0};
    }
    SDL_Rect result = bounds;
    if (result.w <= 0 || result.h <= 0) {
        result = SDL_Rect{0, 0, screen_w_, screen_h_};
    }
    if (result.w > screen_w_) {
        result.w = screen_w_;
    }
    if (result.h > screen_h_) {
        result.h = screen_h_;
    }
    int max_x = std::max(0, screen_w_ - result.w);
    int max_y = std::max(0, screen_h_ - result.h);
    result.x = std::clamp(result.x, 0, max_x);
    result.y = std::clamp(result.y, 0, max_y);
    return result;
}

SDL_Rect MapModeUI::effective_work_area() const {
    if (screen_w_ <= 0 || screen_h_ <= 0) {
        return SDL_Rect{0, 0, 0, 0};
    }
    SDL_Rect area = sliding_area_bounds_;
    if (area.w <= 0 || area.h <= 0) {
        return SDL_Rect{0, 0, screen_w_, screen_h_};
    }
    int height = std::min(area.h, screen_h_);
    int y = std::clamp(area.y, 0, std::max(0, screen_h_ - height));
    return SDL_Rect{0, y, screen_w_, height};
}

void MapModeUI::apply_sliding_area_bounds() {
    sliding_area_bounds_ = sanitize_sliding_area(sliding_area_bounds_);
    SDL_Rect work_area = effective_work_area();
    SDL_Rect right_bounds = room_config_bounds();

    if (light_panel_) light_panel_->set_work_area(work_area);
    if (layers_preview_panel_) layers_preview_panel_->set_work_area(work_area);

    if (layers_panel_) {
        layers_panel_->set_work_area(work_area);
        int left_width = work_area.w;
        if (right_bounds.w > 0) {
            int available = right_bounds.x - work_area.x;
            left_width = std::clamp(available, 0, work_area.w);
        }
        SDL_Rect left_bounds{work_area.x, work_area.y, left_width, work_area.h};
        layers_panel_->set_embedded_bounds(left_bounds);
    }

    if (room_configurator_) {
        room_configurator_->set_work_area(work_area);
        room_configurator_->set_bounds(right_bounds);
    }

    if (room_config_container_) {
        room_config_container_->set_panel_bounds_override(right_bounds);
    }
    if (rooms_list_container_) {
        rooms_list_container_->set_panel_bounds_override(right_bounds);
    }
    if (layer_controls_container_) {
        layer_controls_container_->set_panel_bounds_override(right_bounds);
    }
}

void MapModeUI::set_footer_always_visible(bool on) {
    footer_always_visible_ = on;
    ensure_panels();
    update_footer_visibility();
}

void MapModeUI::set_headers_suppressed(bool suppressed) {
    base_headers_suppressed_ = suppressed;
    refresh_header_suppression_state();
}

void MapModeUI::set_sliding_headers_hidden(bool hidden) {
    int previous = sliding_header_request_count_;
    if (hidden) {
        ++sliding_header_request_count_;
    } else if (sliding_header_request_count_ > 0) {
        --sliding_header_request_count_;
    }
    if (previous == sliding_header_request_count_) {
        return;
    }
    refresh_header_suppression_state();
}

void MapModeUI::set_dev_sliding_headers_hidden(bool hidden) {
    if (dev_sliding_headers_hidden_ == hidden) {
        return;
    }
    dev_sliding_headers_hidden_ = hidden;
    refresh_header_suppression_state();
}

void MapModeUI::refresh_header_suppression_state() {
    const bool sliding_requested = (sliding_header_request_count_ > 0) || dev_sliding_headers_hidden_;
    const bool final_state = base_headers_suppressed_ || sliding_requested;
    const bool sliding_only = sliding_requested && !base_headers_suppressed_;
    const bool state_changed = (headers_suppressed_ != final_state) || (sliding_only_header_suppression_ != sliding_only);
    headers_suppressed_ = final_state;
    sliding_only_header_suppression_ = sliding_only;

    if (state_changed) {
        ensure_panels();
        if (headers_suppressed_ && !sliding_only_header_suppression_) {
            if (layers_panel_) {
                layers_panel_->close();
            }
            close_room_configuration(false);
        }
    }

    update_footer_visibility();
}

void MapModeUI::set_mode_button_sets(std::vector<HeaderButtonConfig> map_buttons,
                                     std::vector<HeaderButtonConfig> room_buttons) {
    map_mode_buttons_ = std::move(map_buttons);
    room_mode_buttons_ = std::move(room_buttons);
    footer_buttons_configured_ = false;
    ensure_panels();
}

void MapModeUI::set_header_mode(HeaderMode mode) {
    if (header_mode_ == mode) {
        return;
    }
    header_mode_ = mode;
    footer_buttons_configured_ = false;
    ensure_panels();
    sync_footer_button_states();
}

MapModeUI::HeaderButtonConfig* MapModeUI::find_button(HeaderMode mode, const std::string& id) {
    auto& list = (mode == HeaderMode::Map) ? map_mode_buttons_ : room_mode_buttons_;
    auto it = std::find_if(list.begin(), list.end(),
                           [&](const HeaderButtonConfig& cfg) { return cfg.id == id; });
    if (it == list.end()) {
        return nullptr;
    }
    return &(*it);
}

bool MapModeUI::ensure_panel_unlocked(DockableCollapsible* panel, const char* panel_name) const {
    if (panel && panel->isLocked()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[MapModeUI] %s panel is locked; action ignored.", panel_name);
        return false;
    }
    return true;
}

void MapModeUI::set_button_state(const std::string& id, bool active) {
    set_button_state(header_mode_, id, active);
}

void MapModeUI::set_button_state(HeaderMode mode, const std::string& id, bool active) {
    if (HeaderButtonConfig* cfg = find_button(mode, id)) {
        cfg->active = active;
    }
    if (footer_bar_ && mode == header_mode_) {
        footer_bar_->set_button_active_state(id, active);
    }
}

void MapModeUI::register_floating_panel(DockableCollapsible* panel) {
    track_floating_panel(panel);
}

void MapModeUI::track_floating_panel(DockableCollapsible* panel) {
    if (!panel) return;
    auto it = std::find(floating_panels_.begin(), floating_panels_.end(), panel);
    if (it == floating_panels_.end()) {
        floating_panels_.push_back(panel);
    }
}

void MapModeUI::rebuild_floating_stack() {
    floating_panels_.erase( std::remove(floating_panels_.begin(), floating_panels_.end(), nullptr), floating_panels_.end());
}

void MapModeUI::bring_panel_to_front(DockableCollapsible* panel) {
    if (!panel) return;
    auto it = std::find(floating_panels_.begin(), floating_panels_.end(), panel);
    if (it == floating_panels_.end()) return;
    if (std::next(it) == floating_panels_.end()) return;
    DockableCollapsible* ptr = *it;
    floating_panels_.erase(it);
    floating_panels_.push_back(ptr);
}

bool MapModeUI::is_pointer_event(const SDL_Event& e) const {
    return e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION;
}

SDL_Point MapModeUI::event_point(const SDL_Event& e) const {
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

bool MapModeUI::pointer_inside_floating_panel(int x, int y) const {
    SDL_Point p{x, y};
    for (DockableCollapsible* panel : floating_panels_) {
        if (!panel) continue;
        if (auto* lights = dynamic_cast<MapLightPanel*>(panel)) {
            if (lights->is_visible() && lights->is_point_inside(p.x, p.y)) {
                return true;
            }
            continue;
        }
        if (auto* layers_preview = dynamic_cast<MapLayersPreviewPanel*>(panel)) {
            if (layers_preview->is_visible() && layers_preview->is_point_inside(p.x, p.y)) {
                return true;
            }
            continue;
        }
        if (panel->is_visible() && panel->is_point_inside(p.x, p.y)) {
            return true;
        }
    }
    for (DockableCollapsible* panel : FloatingDockableManager::instance().open_panels()) {
        if (!panel || !panel->is_visible()) {
            continue;
        }
        if (panel->is_point_inside(p.x, p.y)) {
            return true;
        }
    }
    return false;
}

bool MapModeUI::handle_floating_panel_event(const SDL_Event& e, bool& used) {
    if (floating_panels_.empty()) return false;

    const bool pointer_event = is_pointer_event(e);
    const bool wheel_event = (e.type == SDL_MOUSEWHEEL);
    SDL_Point p = event_point(e);
    bool consumed = false;

    for (auto it = floating_panels_.rbegin(); it != floating_panels_.rend(); ++it) {
        DockableCollapsible* panel = *it;
        if (!panel) continue;

        MapLightPanel* lights = dynamic_cast<MapLightPanel*>(panel);
        MapLayersPreviewPanel* layers_preview = dynamic_cast<MapLayersPreviewPanel*>(panel);

        auto handle_and_check = [&](auto* concrete) -> bool {
            if (!concrete || !concrete->is_visible()) return false;
            if (concrete->handle_event(e)) {
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    bring_panel_to_front(panel);
                }
                used = true;
                return true;
            }
            return false;
};

        bool handled_special = false;
        if (lights) {
            handled_special = handle_and_check(lights);
        } else if (layers_preview) {
            handled_special = handle_and_check(layers_preview);
        } else {
            if (!panel->is_visible()) continue;
            if (panel->handle_event(e)) {
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    bring_panel_to_front(panel);
                }
                used = true;
                consumed = true;
                break;
            }
        }

        if (handled_special) {
            consumed = true;
            break;
        }

        const bool inside = (lights && lights->is_visible() && lights->is_point_inside(p.x, p.y)) || (layers_preview && layers_preview->is_visible() && layers_preview->is_point_inside(p.x, p.y)) || (!lights && !layers_preview && panel->is_visible() && panel->is_point_inside(p.x, p.y));

        if ((pointer_event || wheel_event) && inside) {
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                bring_panel_to_front(panel);
            }
            used = true;
            consumed = true;
            break;
        }
    }

    if (!consumed && (pointer_event || wheel_event)) {
        for (DockableCollapsible* panel : FloatingDockableManager::instance().open_panels()) {
            if (!panel || !panel->is_visible()) {
                continue;
            }
            if (panel->is_point_inside(p.x, p.y)) {
                used = true;
                consumed = true;
                break;
            }
        }
    }

    return consumed;
}

void MapModeUI::ensure_panels() {
    if (!light_panel_) {
        light_panel_ = std::make_unique<MapLightPanel>(kDefaultPanelX, kDefaultPanelY);
        light_panel_->close();
        track_floating_panel(light_panel_.get());
    }
    if (light_panel_) {
        light_panel_->set_assets(assets_);
        light_panel_->set_map_color_sample_callback(
            [this](const utils::color::RangedColor& current,
                   std::function<void(SDL_Color)> on_sample,
                   std::function<void()> on_cancel) {
                this->begin_map_color_sampling(current, std::move(on_sample), std::move(on_cancel));
            });
        light_panel_->set_update_map_light_callback([this](bool enabled) {
            if (assets_) {
                assets_->set_update_map_light_enabled(enabled);
            }
        });
    }
    if (!layers_controller_) {
        layers_controller_ = std::make_shared<MapLayersController>();
    }
    if (layers_controller_) {
        layers_controller_->set_manifest_store(manifest_store_, map_id_);
    }
    if (!layers_panel_) {
        layers_panel_ = std::make_unique<MapLayersPanel>();
        layers_panel_->set_embedded_mode(true);
        layers_panel_->set_on_configure_room([this](const std::string& key) {
            this->open_room_configuration(key, SlidingPanel::LayerControls);
        });
        layers_panel_->set_side_panel_callback([this](MapLayersPanel::SidePanel panel) {
            SlidingPanel desired_panel = SlidingPanel::RoomsList;
            switch (panel) {
                case MapLayersPanel::SidePanel::LayerControls:
                    desired_panel = SlidingPanel::LayerControls;
                    break;
                case MapLayersPanel::SidePanel::RoomsList:
                case MapLayersPanel::SidePanel::None:
                default:
                    desired_panel = SlidingPanel::RoomsList;
                    break;
            }

            const bool room_config_open = room_configurator_ && room_configurator_->visible();
            if (room_config_open) {
                if (room_config_return_panel_ != desired_panel) {
                    room_config_return_panel_ = desired_panel;
                    update_room_config_header_controls();
                }
                return;
            }

            this->show_sliding_panel(desired_panel);
        });
        layers_panel_->set_on_close([this]() {

            if (rooms_list_container_) {
                rooms_list_container_->close();
            }
            if (layer_controls_container_) {
                layer_controls_container_->close();
            }
            this->close_room_configuration(false);
            active_panel_ = PanelType::None;
            this->set_sliding_headers_hidden(false);
            this->update_footer_visibility();
            sync_footer_button_states();
        });
    }
    if (layers_panel_) {
        layers_panel_->set_embedded_mode(true);
        layers_panel_->set_header_visibility_callback([this](bool visible) {
            this->set_sliding_headers_hidden(visible);
        });
        if (layers_controller_) {
            layers_panel_->set_controller(layers_controller_);
        }
    }
    if (layers_panel_) {
        floating_panels_.erase(std::remove(floating_panels_.begin(), floating_panels_.end(), layers_panel_.get()), floating_panels_.end());
    }

    if (!rooms_list_container_) {
        rooms_list_container_ = std::make_unique<SlidingWindowContainer>();
        rooms_list_container_->set_header_visible(true);
        rooms_list_container_->set_scrollbar_visible(true);

        rooms_list_container_->set_header_visibility_controller([this](bool visible) {
            this->set_dev_sliding_headers_hidden(visible);
        });
        rooms_list_container_->set_close_button_enabled(false);
    }
    if (!rooms_display_) {
        rooms_display_ = std::make_unique<MapRoomsDisplay>();
        rooms_display_->set_header_text("Room List");
        rooms_display_->set_on_select_room([this](const std::string& key) {
            this->open_room_configuration(key, SlidingPanel::RoomsList);
        });
        rooms_display_->set_on_rooms_changed([this]() {
            this->auto_save_layers_data();
        });
    }
    if (rooms_display_) {
        rooms_display_->attach_container(rooms_list_container_.get());
        rooms_display_->set_map_info(map_info_);
        rooms_display_->set_on_rooms_changed([this]() {
            this->auto_save_layers_data();
        });
        rooms_display_->set_on_create_room([this]() {
            this->create_room_from_panel(SlidingPanel::RoomsList);
        });
    }
    if (!layer_controls_container_) {
        layer_controls_container_ = std::make_unique<SlidingWindowContainer>();
        layer_controls_container_->set_header_visible(true);
        layer_controls_container_->set_scrollbar_visible(true);

        layer_controls_container_->set_header_visibility_controller([this](bool visible) {
            this->set_dev_sliding_headers_hidden(visible);
        });
        layer_controls_container_->set_close_button_enabled(false);
        layer_controls_container_->set_blocks_editor_interactions(true);
    }
    if (!layer_controls_display_) {
        layer_controls_display_ = std::make_unique<MapLayerControlsDisplay>();
    }
    if (layer_controls_display_) {
        layer_controls_display_->attach_container(layer_controls_container_.get());
        layer_controls_display_->set_controller(layers_controller_);
        layer_controls_display_->set_selected_layer(layers_panel_ ? layers_panel_->selected_layer() : -1);
        layer_controls_display_->set_on_change([this]() {
            this->auto_save_layers_data();
        });
        layer_controls_display_->set_on_show_rooms_list([this]() {
            this->show_sliding_panel(SlidingPanel::RoomsList);
        });
        layer_controls_display_->set_on_create_room([this]() { this->create_room_from_layers_controls(); });
    }
    if (layers_panel_) {
        layers_panel_->set_rooms_list_container(rooms_list_container_.get());
        layers_panel_->set_layer_controls_container(layer_controls_container_.get());
        layers_panel_->set_on_layer_selected([this](int index) {
            if (layer_controls_display_) {
                layer_controls_display_->set_selected_layer(index);
            }
        });
    }

    ensure_room_configurator();

    if (!layers_preview_panel_) {
        layers_preview_panel_ = std::make_unique<MapLayersPreviewPanel>(kDefaultPanelX + 352, kDefaultPanelY + 48);
        layers_preview_panel_->close();
        track_floating_panel(layers_preview_panel_.get());
        layers_preview_panel_->set_on_select_layer([this](int layer_index) {
            this->set_active_panel(PanelType::Layers);
            if (layers_panel_) {
                layers_panel_->force_layer_controls_on_next_select();
                layers_panel_->select_layer(layer_index);
            }
        });
        layers_preview_panel_->set_on_select_room([this](const std::string& room_key) {
            this->set_active_panel(PanelType::Layers);
            if (layers_panel_) {
                layers_panel_->select_room(room_key);
            }
        });
        layers_preview_panel_->set_on_show_room_list([this]() {
            this->set_active_panel(PanelType::Layers);
            if (layers_panel_) {
                layers_panel_->show_room_list();
            }
        });
    }
    if (layers_preview_panel_ && layers_controller_) {
        layers_preview_panel_->set_controller(layers_controller_);
    }
    if (layers_preview_panel_ && map_info_) {
        layers_preview_panel_->set_map_info(map_info_, [this]() { return auto_save_layers_data(); });
    }
    if (!footer_bar_) {
        footer_bar_ = std::make_unique<DevFooterBar>("");
        footer_bar_->set_bounds(screen_w_, screen_h_);
        footer_bar_->set_title_visible(false);
        footer_bar_->set_visible(footer_always_visible_ || map_mode_active_);
        footer_buttons_configured_ = false;
    }
    if (footer_bar_ && !footer_buttons_configured_) {
        configure_footer_buttons();
        sync_footer_button_states();
    }
    update_footer_visibility();
    rebuild_floating_stack();
}

void MapModeUI::configure_footer_buttons() {
    if (!footer_bar_) return;

    std::vector<DevFooterBar::Button> buttons;

    auto append_custom = [&](std::vector<HeaderButtonConfig>& configs, HeaderMode mode) {
        auto append_button = [&](HeaderButtonConfig& config) {
            DevFooterBar::Button extra;
            extra.id = config.id;
            extra.label = config.label;
            extra.active = config.active;
            extra.momentary = config.momentary;
            extra.style_override = config.style_override;
            extra.active_style_override = config.active_style_override;
            auto* cfg_ptr = &config;
            extra.on_toggle = [this, cfg_ptr, mode](bool active) {
                if (cfg_ptr->on_toggle) {
                    cfg_ptr->on_toggle(active);
                }
                if (cfg_ptr->momentary) {
                    set_button_state(mode, cfg_ptr->id, false);
                } else {
                    set_button_state(mode, cfg_ptr->id, active);
                }
};
            buttons.push_back(std::move(extra));
};

        for (auto& config : configs) {
            append_button(config);
        }
};

    if (header_mode_ == HeaderMode::Map) {
        const bool has_layers_button = std::any_of(map_mode_buttons_.begin(), map_mode_buttons_.end(),
                                                  [](const HeaderButtonConfig& cfg) {
                                                      return cfg.id == "layers";
                                                  });

        if (!has_layers_button) {
            DevFooterBar::Button layers_btn;
            layers_btn.id = "layers";
            layers_btn.label = "Layers";
            layers_btn.style_override = &DMStyles::WarnButton();
            layers_btn.active_style_override = &DMStyles::AccentButton();
            layers_btn.on_toggle = [this](bool active) {
                if (active) {
                    this->set_active_panel(PanelType::Layers);
                } else {
                    this->set_active_panel(PanelType::None);
                }
};
            buttons.push_back(std::move(layers_btn));
        }

        append_custom(map_mode_buttons_, HeaderMode::Map);

        const bool has_lights_button = std::any_of(map_mode_buttons_.begin(), map_mode_buttons_.end(),
                                                   [](const HeaderButtonConfig& cfg) {
                                                       return cfg.id == kButtonIdLights;
                                                   });

        if (!has_lights_button) {
            DevFooterBar::Button lights_btn;
            lights_btn.id = kButtonIdLights;
            lights_btn.label = "Lighting";
            lights_btn.style_override = &DMStyles::WarnButton();
            lights_btn.active_style_override = &DMStyles::AccentButton();
            lights_btn.on_toggle = [this](bool active) {
                if (active) {
                    this->open_light_panel();
                } else {
                    this->close_light_panel();
                }
};
            buttons.push_back(std::move(lights_btn));
        }
    } else if (header_mode_ == HeaderMode::Room) {
        append_custom(room_mode_buttons_, HeaderMode::Room);
    }

    footer_bar_->set_buttons(std::move(buttons));
    footer_buttons_configured_ = true;
    sync_footer_button_states();
}

void MapModeUI::sync_footer_button_states() {
    if (!footer_bar_) return;
    if (header_mode_ == HeaderMode::Map) {
        const bool lights_visible = light_panel_ && light_panel_->is_visible();
        const bool lighting_controls_visible = lights_visible;
        const bool layers_visible = layers_panel_ && layers_panel_->is_visible();
        footer_bar_->set_button_active_state(kButtonIdLights, lighting_controls_visible);
        footer_bar_->set_button_active_state("layers", layers_visible);
        for (const auto& config : map_mode_buttons_) {
            footer_bar_->set_button_active_state(config.id, config.active);
        }
    } else if (header_mode_ == HeaderMode::Room) {
        for (const auto& config : room_mode_buttons_) {
            footer_bar_->set_button_active_state(config.id, config.active);
        }
    }
}

void MapModeUI::update_footer_visibility() {
    if (!footer_bar_) return;
    footer_bar_->set_bounds(screen_w_, screen_h_);

    const bool should_show = (!headers_suppressed_) && (footer_always_visible_ || map_mode_active_);
    footer_bar_->set_visible(should_show);
}

void MapModeUI::set_active_panel(PanelType panel) {
    ensure_panels();

    if (panel == PanelType::Layers && !ensure_panel_unlocked(layers_panel_.get(), "Layers")) {
        sync_footer_button_states();
        return;
    }

    PanelType new_active = PanelType::None;

    if (panel == PanelType::Layers) {
        if (layers_panel_) {
            ensure_room_configurator();
            layers_panel_->open();
            bring_panel_to_front(layers_panel_.get());
            layers_panel_->hide_details_panel();
        }
        show_sliding_panel(SlidingPanel::RoomsList);
        new_active = PanelType::Layers;
    } else {
        if (layers_panel_) {
            layers_panel_->hide_details_panel();
        }
        show_sliding_panel(SlidingPanel::None);
        close_room_configuration(false);
    }

    active_panel_ = new_active;
    sync_footer_button_states();
}

void MapModeUI::sync_panel_map_info() {
    if (!map_info_) return;
    ensure_panels();
    if (light_panel_) {
        LightSaveCallback callback = light_save_callback_;
        if (!callback) {
            callback = [this]() { return save_map_info_to_disk(); };
        }
        light_panel_->set_map_info(map_info_, callback);
    }
    if (layers_panel_) {
        if (layers_controller_) {
            layers_controller_->set_manifest_store(manifest_store_, map_id_);
            layers_controller_->bind(map_info_, map_path_);
        }
        layers_panel_->set_map_info(map_info_, map_path_);
        layers_panel_->set_on_save([this]() { return auto_save_layers_data(); });
    }
    if (rooms_display_) {
        rooms_display_->set_map_info(map_info_);
    }
    if (layer_controls_display_) {
        layer_controls_display_->set_controller(layers_controller_);
        layer_controls_display_->set_selected_layer(layers_panel_ ? layers_panel_->selected_layer() : -1);
        layer_controls_display_->refresh();
    }
}

void MapModeUI::update(const Input& input) {
    ensure_panels();
    if (map_color_sampling_active_) {
        map_color_sampling_cursor_.x = input.getX();
        map_color_sampling_cursor_.y = input.getY();
    }
    if (footer_bar_ && footer_bar_->visible()) {
        footer_bar_->update(input);
    }
    if (layers_panel_ && layers_panel_->is_visible()) {
        layers_panel_->update(input, screen_w_, screen_h_);
    }
    for (DockableCollapsible* panel : floating_panels_) {
        if (!panel) continue;
        if (auto* lights = dynamic_cast<MapLightPanel*>(panel)) {
            if (lights->is_visible()) {
                lights->update(input, screen_w_, screen_h_);
            }
            continue;
        }
        if (auto* layers_preview = dynamic_cast<MapLayersPreviewPanel*>(panel)) {
            if (layers_preview->is_visible()) {
                layers_preview->update(input, screen_w_, screen_h_);
            }
            continue;
        }
        if (panel->is_visible()) {
            panel->update(input, screen_w_, screen_h_);
        }
    }

    PanelType visible = PanelType::None;
    if (layers_panel_ && layers_panel_->is_visible()) {
        visible = PanelType::Layers;
    }
    if (visible != active_panel_) {
        active_panel_ = visible;
        sync_footer_button_states();
    }

    const bool lights_visible = light_panel_ && light_panel_->is_visible();
    if (lights_visible != last_lights_visible_) {
        last_lights_visible_ = lights_visible;
        sync_footer_button_states();
    }

    if (room_configurator_ && room_configurator_->visible()) {
        room_configurator_->update(input, screen_w_, screen_h_);
    }
    if (room_config_container_ && room_config_container_->is_visible()) {
        room_config_container_->update(input, screen_w_, screen_h_);
    }
    if (rooms_list_container_ && rooms_list_container_->is_visible()) {
        rooms_list_container_->update(input, screen_w_, screen_h_);
    }
    if (layer_controls_container_ && layer_controls_container_->is_visible()) {
        layer_controls_container_->update(input, screen_w_, screen_h_);
    }
}

bool MapModeUI::handle_event(const SDL_Event& e) {
    ensure_panels();
    if (map_color_sampling_active_) {
        if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
            map_color_sampling_cursor_ = event_point(e);
        }
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            SDL_Color chosen = map_color_sampling_preview_valid_ ? map_color_sampling_preview_ : SDL_Color{0, 0, 0, 255};
            complete_map_color_sampling(chosen);
            return true;
        }
        if ((e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) ||
            (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
            cancel_map_color_sampling();
            return true;
        }
        switch (e.type) {
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            case SDL_MOUSEMOTION:
            case SDL_MOUSEWHEEL:
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                return true;
            default:
                break;
        }
    }
    if (room_config_container_ && room_config_container_->is_visible() && room_config_container_->handle_event(e)) {
        return true;
    }
    if (rooms_list_container_ && rooms_list_container_->is_visible() && rooms_list_container_->handle_event(e)) {
        return true;
    }
    if (layer_controls_container_ && layer_controls_container_->is_visible() && layer_controls_container_->handle_event(e)) {
        return true;
    }
    if (layers_panel_ && layers_panel_->is_visible() && layers_panel_->handle_event(e)) {
        return true;
    }
    bool floating_used = false;
    if (handle_floating_panel_event(e, floating_used)) {
        return true;
    }
    if (floating_used) {
        return true;
    }

    bool footer_used = false;
    if (footer_bar_ && footer_bar_->visible()) {
        footer_used = footer_bar_->handle_event(e);
    }
    if (footer_used) {
        return true;
    }

    return false;
}

void MapModeUI::render(SDL_Renderer* renderer) const {
    if (layers_panel_ && layers_panel_->is_visible()) {
        layers_panel_->render(renderer);
    }
    for (DockableCollapsible* panel : floating_panels_) {
        if (!panel) continue;
        if (auto* lights = dynamic_cast<MapLightPanel*>(panel)) {
            if (lights->is_visible()) {
                lights->render(renderer);
            }
            continue;
        }
        if (auto* layers_preview = dynamic_cast<MapLayersPreviewPanel*>(panel)) {
            if (layers_preview->is_visible()) {
                layers_preview->render(renderer);
            }
            continue;
        }
        if (panel->is_visible()) {
            panel->render(renderer);
        }
    }
    if (room_config_container_ && room_config_container_->is_visible()) {
        room_config_container_->render(renderer, screen_w_, screen_h_);
    }
    if (rooms_list_container_ && rooms_list_container_->is_visible()) {
        rooms_list_container_->render(renderer, screen_w_, screen_h_);
    }
    if (layer_controls_container_ && layer_controls_container_->is_visible()) {
        layer_controls_container_->render(renderer, screen_w_, screen_h_);
    }
    if (footer_bar_ && footer_bar_->visible()) {
        footer_bar_->render(renderer);
    }
    if (map_color_sampling_active_ && renderer) {
        SDL_Rect sample_rect{map_color_sampling_cursor_.x, map_color_sampling_cursor_.y, 1, 1};
        Uint32 pixel = 0;
        if (SDL_RenderReadPixels(renderer, &sample_rect, SDL_PIXELFORMAT_ARGB8888, &pixel, sizeof(pixel)) == 0) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            if (SDL_PixelFormat* format = SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888)) {
                SDL_GetRGBA(pixel, format, &r, &g, &b, &a);
                SDL_FreeFormat(format);
                map_color_sampling_preview_ = SDL_Color{r, g, b, a};
                map_color_sampling_preview_valid_ = true;
            } else {
                map_color_sampling_preview_valid_ = false;
            }
        } else {
            map_color_sampling_preview_valid_ = false;
        }
        const int preview_size = 48;
        SDL_Rect preview_rect{map_color_sampling_cursor_.x + 18,
                              map_color_sampling_cursor_.y + 18,
                              preview_size,
                              preview_size};
        SDL_Rect inner_rect{preview_rect.x + 4,
                            preview_rect.y + 4,
                            std::max(0, preview_rect.w - 8), std::max(0, preview_rect.h - 8)};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
        SDL_RenderFillRect(renderer, &preview_rect);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 220);
        SDL_RenderDrawRect(renderer, &preview_rect);
        if (map_color_sampling_preview_valid_) {
            SDL_SetRenderDrawColor(renderer, map_color_sampling_preview_.r, map_color_sampling_preview_.g, map_color_sampling_preview_.b, 255);
            SDL_RenderFillRect(renderer, &inner_rect);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 220);
            SDL_RenderDrawRect(renderer, &inner_rect);
        } else {
            SDL_SetRenderDrawColor(renderer, 120, 120, 120, 220);
            SDL_RenderDrawRect(renderer, &inner_rect);
        }
    }
}

void MapModeUI::open_layers_panel() {
    ensure_panels();
    if (!ensure_panel_unlocked(layers_panel_.get(), "Layers")) {
        return;
    }
    set_active_panel(PanelType::Layers);
}

void MapModeUI::open_light_panel() {
    ensure_panels();
    const bool light_unlocked = ensure_panel_unlocked(light_panel_.get(), "Light");
    if (!light_unlocked) {
        sync_footer_button_states();
        return;
    }
    if (!light_panel_centered_) {
        ensure_light_and_shading_positions();
    }
    if (light_panel_) {
        light_panel_->open();
        bring_panel_to_front(light_panel_.get());
    }
    sync_footer_button_states();
}

void MapModeUI::close_light_panel() {
    ensure_panels();
    if (light_panel_) {
        light_panel_->close();
    }
    sync_footer_button_states();
}

void MapModeUI::toggle_light_panel() {
    ensure_panels();
    const bool light_unlocked = ensure_panel_unlocked(light_panel_.get(), "Light");
    if (!light_unlocked) {
        sync_footer_button_states();
        return;
    }
    const bool lights_visible = light_panel_ && light_panel_->is_visible();
    if (lights_visible) {
        close_light_panel();
        return;
    }
    open_light_panel();
}

void MapModeUI::toggle_layers_panel() {
    ensure_panels();
    if (!ensure_panel_unlocked(layers_panel_.get(), "Layers")) {
        sync_footer_button_states();
        return;
    }
    if (active_panel_ == PanelType::Layers) {
        set_active_panel(PanelType::None);
    } else {
        set_active_panel(PanelType::Layers);
    }
}

void MapModeUI::close_all_panels() {
    if (light_panel_) {
        light_panel_->close();
    }
    if (layers_preview_panel_) {
        layers_preview_panel_->close();
    }
    set_active_panel(PanelType::None);
    close_room_configuration(false);
}

bool MapModeUI::is_light_panel_visible() const {
    return light_panel_ && light_panel_->is_visible();
}

void MapModeUI::ensure_light_and_shading_positions() {
    ensure_panels();

    const int fallback_w = DockableCollapsible::kDefaultFloatingContentWidth;
    const int fallback_h = 400;

    std::vector<FloatingPanelLayoutManager::PanelInfo> panels;
    panels.reserve(2);
    std::vector<bool*> updated_flags;
    updated_flags.reserve(2);

    auto add_panel = [&](DockableCollapsible* panel, bool& centered_flag) {
        if (!panel || centered_flag) {
            return;
        }
        FloatingPanelLayoutManager::PanelInfo info;
        info.panel = panel;
        info.force_layout = true;
        SDL_Rect rect = panel->rect();
        info.preferred_width = rect.w > 0 ? rect.w : fallback_w;
        int height = rect.h > 0 ? rect.h : panel->height();
        if (height <= 0) {
            height = fallback_h;
        }
        info.preferred_height = height;
        panels.push_back(info);
        updated_flags.push_back(&centered_flag);
};

    add_panel(light_panel_.get(), light_panel_centered_);

    if (panels.empty()) {
        return;
    }

    FloatingPanelLayoutManager::instance().layoutAll(panels);

    for (bool* flag : updated_flags) {
        if (flag) {
            *flag = true;
        }
    }
}

SDL_Rect MapModeUI::room_config_bounds() const {
    if (screen_w_ <= 0 || screen_h_ <= 0) {
        return SDL_Rect{0, 0, 0, 0};
    }
    SDL_Rect area = sanitize_sliding_area(sliding_area_bounds_);
    if (area.w <= 0 || area.h <= 0) {
        area = SDL_Rect{0, 0, screen_w_, screen_h_};
    }
    int area_x = area.x;
    int area_y = area.y;
    int area_w = area.w;
    int area_h = area.h;

    int panel_x = area_x + (area_w * 2) / 3;
    int panel_w = area_w - (panel_x - area_x);
    const int min_width = std::max(320, screen_w_ / 3);
    if (panel_w < min_width) {
        panel_w = std::min(min_width, area_w);
        panel_x = area_x + std::max(0, area_w - panel_w);
    }
    if (panel_w > area_w) {
        panel_w = area_w;
        panel_x = area_x;
    }
    panel_x = std::clamp(panel_x, area_x, area_x + std::max(0, area_w - panel_w));
    return SDL_Rect{panel_x, area_y, std::max(0, panel_w), std::max(0, area_h)};
}

void MapModeUI::show_sliding_panel(SlidingPanel panel, bool) {
    if (room_config_container_) {
        room_config_container_->set_visible(false);
    }
    if (rooms_list_container_) {
        rooms_list_container_->set_visible(false);
    }
    if (layer_controls_container_) {
        layer_controls_container_->set_visible(false);
    }

    switch (panel) {
        case SlidingPanel::RoomConfig:
            if (room_config_container_) {
                room_config_container_->open();
            }
            active_sliding_panel_ = SlidingPanel::RoomConfig;
            break;
        case SlidingPanel::RoomsList:
            if (rooms_list_container_) {
                rooms_list_container_->open();
            }
            active_sliding_panel_ = SlidingPanel::RoomsList;
            break;
        case SlidingPanel::LayerControls:
            if (layer_controls_container_) {
                layer_controls_container_->open();
            }
            active_sliding_panel_ = SlidingPanel::LayerControls;
            break;
        case SlidingPanel::None:
        default:
            active_sliding_panel_ = SlidingPanel::None;
            break;
    }
}

void MapModeUI::ensure_room_configurator() {
    if (!room_configurator_) {
        room_configurator_ = std::make_unique<RoomConfigurator>();
    }
    if (room_configurator_) {

        room_configurator_->set_header_visibility_controller([this](bool visible) {
            this->set_dev_sliding_headers_hidden(visible);
        });
        room_configurator_->set_on_close([this]() {
            active_room_config_key_.clear();
            if (rooms_display_) {
                rooms_display_->refresh();
            }
            this->show_sliding_panel(room_config_return_panel_);
        });
        room_configurator_->set_blocks_editor_interactions(false);
        room_configurator_->set_spawn_group_callbacks(
            {},
            [this](const std::string& spawn_id) { this->delete_active_room_spawn_group(spawn_id); },
            [this](const std::string& spawn_id, size_t index) {
                this->reorder_active_room_spawn_group(spawn_id, index);
            },
            {},
            {});
        room_configurator_->set_on_room_renamed([this](const std::string& old_name, const std::string& desired) {
            return this->rename_active_room(old_name, desired);
        });
    }
    if (!room_config_container_) {
        room_config_container_ = std::make_unique<SlidingWindowContainer>();
        if (room_config_container_) {
            room_config_container_->set_header_visible(true);
            room_config_container_->set_scrollbar_visible(true);

            room_config_container_->set_header_visibility_controller([this](bool visible) {
                this->set_dev_sliding_headers_hidden(visible);
            });
            room_config_container_->set_blocks_editor_interactions(false);
        }
    }
    if (room_config_container_) {
        room_config_container_->set_close_button_enabled(true);
    }
    if (room_configurator_ && room_config_container_) {
        room_configurator_->attach_container(room_config_container_.get());
        apply_sliding_area_bounds();
    }
    update_room_config_header_controls();
}

void MapModeUI::open_room_configuration(const std::string& room_key, SlidingPanel return_panel) {
    ensure_panels();
    ensure_room_configurator();
    if (!room_configurator_ || !map_info_) {
        return;
    }

    room_config_return_panel_ = return_panel;
    update_room_config_header_controls();

    nlohmann::json& map_info = *map_info_;
    nlohmann::json& rooms = map_info["rooms_data"];
    if (!rooms.is_object()) {
        rooms = nlohmann::json::object();
    }
    nlohmann::json& room_entry = rooms[room_key];
    if (!room_entry.is_object()) {
        room_entry = nlohmann::json::object();
        room_entry["name"] = room_key;
    }

    active_room_config_key_ = room_key;
    if (layers_panel_) {
        layers_panel_->hide_details_panel();
    }

    auto on_change = [this]() {
        if (layers_panel_) {
            layers_panel_->mark_dirty(true);
        }
        if (rooms_display_) {
            rooms_display_->refresh();
        }
};
    auto on_entry_change = [this](const nlohmann::json&, const auto&) {
        if (layers_panel_) {
            layers_panel_->mark_dirty(true);
        }
};

    apply_sliding_area_bounds();
    room_configurator_->open(room_entry, on_change, on_entry_change, {});
    show_sliding_panel(SlidingPanel::RoomConfig);
}

void MapModeUI::close_room_configuration(bool show_rooms_list) {
    if (room_configurator_) {
        room_configurator_->close();
    }
    active_room_config_key_.clear();
    if (show_rooms_list) {
        room_config_return_panel_ = SlidingPanel::RoomsList;
        show_sliding_panel(room_config_return_panel_);
    } else {
        room_config_return_panel_ = SlidingPanel::None;
        show_sliding_panel(room_config_return_panel_);
    }
    update_room_config_header_controls();
}

void MapModeUI::set_light_save_callback(LightSaveCallback cb) {
    light_save_callback_ = std::move(cb);
    ensure_panels();
    if (light_panel_) {
        LightSaveCallback callback = light_save_callback_;
        if (!callback) {
            callback = [this]() { return save_map_info_to_disk(); };
        }
        light_panel_->set_map_info(map_info_, callback);
    }
    if (layers_preview_panel_) {
        LightSaveCallback callback = light_save_callback_;
        if (!callback) {
            callback = [this]() { return auto_save_layers_data(); };
        }
        layers_preview_panel_->set_map_info(map_info_, callback);
    }
}

bool MapModeUI::is_point_inside(int x, int y) const {
    if (pointer_inside_floating_panel(x, y)) {
        return true;
    }
    if (headers_suppressed_ && !sliding_only_header_suppression_) {
        return false;
    }
    if (footer_bar_ && footer_bar_->visible() && footer_bar_->contains(x, y)) {
        return true;
    }
    if (layers_panel_ && layers_panel_->is_visible() && layers_panel_->is_point_inside(x, y)) {
        return true;
    }
    if (room_config_container_ && room_config_container_->is_visible() && room_config_container_->is_point_inside(x, y)) {
        return true;
    }
    if (rooms_list_container_ && rooms_list_container_->is_visible() && rooms_list_container_->is_point_inside(x, y)) {
        return true;
    }
    if (layer_controls_container_ && layer_controls_container_->is_visible() && layer_controls_container_->is_point_inside(x, y)) {
        return true;
    }
    return false;
}

bool MapModeUI::is_any_panel_visible() const {
    for (DockableCollapsible* panel : floating_panels_) {
        if (!panel) continue;
        if (auto* lights = dynamic_cast<MapLightPanel*>(panel)) {
            if (lights->is_visible()) return true;
            continue;
        }
        if (auto* layers_preview = dynamic_cast<MapLayersPreviewPanel*>(panel)) {
            if (layers_preview->is_visible()) return true;
            continue;
        }
        if (panel->is_visible()) return true;
    }
    if (room_config_container_ && room_config_container_->is_visible()) return true;
    if (rooms_list_container_ && rooms_list_container_->is_visible()) return true;
    if (layer_controls_container_ && layer_controls_container_->is_visible()) return true;
    return layers_panel_ && layers_panel_->is_visible();
}

bool MapModeUI::is_layers_panel_visible() const {
    return layers_panel_ && layers_panel_->is_visible();
}

bool MapModeUI::save_map_info_to_disk() const {
    if (!map_info_) return false;
    if (!manifest_store_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[MapModeUI] Cannot save map info: manifest store is not available.");
        return false;
    }
    if (map_id_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[MapModeUI] Cannot save map info: map identifier is empty.");
        return false;
    }
    if (!devmode::persist_map_manifest_entry(*manifest_store_, map_id_, *map_info_, std::cerr)) {
        return false;
    }
    manifest_store_->flush();
    return true;
}

bool MapModeUI::auto_save_layers_data() {
    bool saved = false;
    if (layers_controller_) {
        saved = layers_controller_->save();
    }
    if (!saved) {
        saved = save_map_info_to_disk();
    }
    if (rooms_display_) {
        rooms_display_->refresh();
    }
    if (layer_controls_display_) {
        layer_controls_display_->refresh();
    }
    if (layers_panel_) {
        layers_panel_->mark_dirty(true);
    }
    return saved;
}

nlohmann::json* MapModeUI::active_room_entry() {
    if (!map_info_ || active_room_config_key_.empty()) {
        return nullptr;
    }
    nlohmann::json& map_info = *map_info_;
    nlohmann::json& rooms = map_info["rooms_data"];
    if (!rooms.is_object()) {
        return nullptr;
    }
    auto it = rooms.find(active_room_config_key_);
    if (it == rooms.end() || !it->is_object()) {
        return nullptr;
    }
    return &it.value();
}

std::string MapModeUI::rename_active_room(const std::string& old_name, const std::string& desired_name) {
    std::string trimmed = trim_copy(desired_name);
    std::string base = sanitize_room_key(trimmed.empty() ? desired_name : trimmed);
    if (!map_info_) {
        return base.empty() ? old_name : base;
    }

    nlohmann::json& map_info = *map_info_;
    nlohmann::json& rooms = map_info["rooms_data"];
    if (!rooms.is_object()) {
        rooms = nlohmann::json::object();
    }

    std::string current_key = active_room_config_key_.empty() ? old_name : active_room_config_key_;
    if (!rooms.contains(current_key)) {
        current_key = old_name;
    }
    if (!rooms.contains(current_key)) {
        return base.empty() ? old_name : base;
    }

    std::string candidate = base.empty() ? current_key : base;
    if (candidate.empty()) {
        candidate = current_key;
    }

    nlohmann::json entry = rooms[current_key];
    entry["name"] = desired_name;

    const bool renaming_active = !active_room_config_key_.empty();

    auto refresh_room_binding = [&](const std::string& key) {
        if (!renaming_active || !room_configurator_) {
            return;
        }
        if (active_room_config_key_ != key) {
            return;
        }
        auto it = rooms.find(key);
        if (it == rooms.end() || !it->is_object()) {
            return;
        }
        room_configurator_->refresh_spawn_groups(it.value());
};

    if (candidate == current_key || rooms.contains(candidate)) {
        rooms[current_key] = std::move(entry);
        if (renaming_active) {
            active_room_config_key_ = current_key;
        }
        handle_rooms_data_mutated(true);
        refresh_room_binding(current_key);
        return current_key;
    }

    rooms.erase(current_key);
    rooms[candidate] = std::move(entry);
    map_layers::rename_room_references_in_layers(map_info, current_key, candidate);
    if (renaming_active) {
        active_room_config_key_ = candidate;
    }
    handle_rooms_data_mutated(true);
    refresh_room_binding(candidate);
    return candidate;
}

void MapModeUI::update_room_config_header_controls() {
    if (!room_config_container_) {
        return;
    }
    room_config_container_->set_close_button_enabled(true);
    room_config_container_->clear_header_navigation_button();
}

void MapModeUI::delete_active_room_spawn_group(const std::string& spawn_id) {
    if (spawn_id.empty()) {
        return;
    }
    nlohmann::json* room_entry = active_room_entry();
    if (!room_entry) {
        return;
    }
    nlohmann::json& groups = ensure_spawn_groups_array(*room_entry);
    auto it = std::remove_if(groups.begin(), groups.end(), [&](nlohmann::json& entry) {
        if (!entry.is_object()) return false;
        if (!entry.contains("spawn_id") || !entry["spawn_id"].is_string()) return false;
        return entry["spawn_id"].get<std::string>() == spawn_id;
    });
    if (it == groups.end()) {
        return;
    }
    groups.erase(it, groups.end());
    for (size_t i = 0; i < groups.size(); ++i) {
        if (groups[i].is_object()) {
            groups[i]["priority"] = static_cast<int>(i);
        }
    }
    sanitize_perimeter_spawn_groups(groups);
    room_configurator_->refresh_spawn_groups(*room_entry);
    handle_rooms_data_mutated(true);
    room_configurator_->notify_spawn_groups_mutated();
}

void MapModeUI::reorder_active_room_spawn_group(const std::string& spawn_id, size_t index) {
    if (spawn_id.empty()) {
        return;
    }
    nlohmann::json* room_entry = active_room_entry();
    if (!room_entry) {
        return;
    }
    nlohmann::json& groups = ensure_spawn_groups_array(*room_entry);
    if (!groups.is_array() || groups.empty()) {
        return;
    }

    auto it = std::find_if(groups.begin(), groups.end(), [&](const nlohmann::json& entry) {
        if (!entry.is_object()) return false;
        if (!entry.contains("spawn_id") || !entry["spawn_id"].is_string()) return false;
        return entry["spawn_id"].get<std::string>() == spawn_id;
    });
    if (it == groups.end()) {
        return;
    }

    nlohmann::json moved = *it;
    groups.erase(it);
    size_t clamped = std::min(index, groups.size());
    groups.insert(groups.begin() + static_cast<std::ptrdiff_t>(clamped), std::move(moved));

    for (size_t i = 0; i < groups.size(); ++i) {
        if (groups[i].is_object()) {
            groups[i]["priority"] = static_cast<int>(i);
        }
    }

    room_configurator_->refresh_spawn_groups(*room_entry);
    handle_rooms_data_mutated(false);
    room_configurator_->notify_spawn_groups_mutated();
}

void MapModeUI::handle_rooms_data_mutated(bool refresh_rooms_list) {
    if (!map_info_) {
        return;
    }
    if (layers_panel_) {
        layers_panel_->mark_dirty(true);
    }
    if (refresh_rooms_list && rooms_display_) {
        rooms_display_->refresh();
    }
    if (layer_controls_display_) {
        layer_controls_display_->refresh();
    }
}

void MapModeUI::create_room_from_layers_controls() {
    create_room_from_panel(SlidingPanel::LayerControls);
}

void MapModeUI::create_room_from_panel(SlidingPanel return_panel) {
    if (!map_info_ || !map_info_->is_object()) {
        return;
    }
    std::string new_key = map_layers::create_room_entry(*map_info_);
    if (new_key.empty()) {
        return;
    }
    handle_rooms_data_mutated(true);
    open_room_configuration(new_key, return_panel);
    auto_save_layers_data();
}

void MapModeUI::begin_map_color_sampling(const utils::color::RangedColor&,
                                         std::function<void(SDL_Color)> on_sample,
                                         std::function<void()> on_cancel) {
    if (!on_sample) {
        if (on_cancel) {
            on_cancel();
        }
        return;
    }
    cancel_map_color_sampling(true);
    map_color_sampling_active_ = true;
    map_color_sampling_preview_valid_ = false;
    map_color_sampling_apply_ = std::move(on_sample);
    map_color_sampling_cancel_ = std::move(on_cancel);
    int mx = 0;
    int my = 0;
    SDL_GetMouseState(&mx, &my);
    map_color_sampling_cursor_.x = mx;
    map_color_sampling_cursor_.y = my;
    if (!map_color_sampling_cursor_handle_) {
        map_color_sampling_cursor_handle_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    }
    map_color_sampling_prev_cursor_ = SDL_GetCursor();
    if (map_color_sampling_cursor_handle_) {
        SDL_SetCursor(map_color_sampling_cursor_handle_);
    }
}

void MapModeUI::cancel_map_color_sampling(bool silent) {
    if (!map_color_sampling_active_) {
        return;
    }
    map_color_sampling_active_ = false;
    map_color_sampling_preview_valid_ = false;
    if (map_color_sampling_prev_cursor_) {
        SDL_SetCursor(map_color_sampling_prev_cursor_);
        map_color_sampling_prev_cursor_ = nullptr;
    }
    auto cancel_cb = std::move(map_color_sampling_cancel_);
    map_color_sampling_apply_ = nullptr;
    map_color_sampling_cancel_ = nullptr;
    if (!silent && cancel_cb) {
        cancel_cb();
    }
}

void MapModeUI::complete_map_color_sampling(SDL_Color color) {
    auto apply_cb = std::move(map_color_sampling_apply_);
    cancel_map_color_sampling(true);
    if (apply_cb) {
        apply_cb(color);
    }
}
