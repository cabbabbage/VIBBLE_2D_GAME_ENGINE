#include "map_mode_ui.hpp"

#include "MapLightPanel.hpp"
#include "MapShadowPanel.hpp"
#include "MapLightPreviewPanel.hpp"
#include "map_grid_panel.hpp"
#include "DockableCollapsible.hpp"
#include "full_screen_header_bar.hpp"
#include "map_layers_controller.hpp"
#include "map_layers_panel.hpp"
#include "map_layers_preview_panel.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/widgets.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/dev_controls_persistence.hpp"
#include "dm_styles.hpp"
#include "utils/input.hpp"

#include <SDL.h>
#include <SDL_log.h>
#include <algorithm>
#include <iterator>
#include <iostream>
#include <vector>
#include <utility>
#include <nlohmann/json.hpp>

namespace {
constexpr int kDefaultPanelX = 48;
constexpr int kDefaultPanelY = 48;
constexpr const char* kButtonIdLights = "lights";
constexpr const char* kButtonIdShading = "shading";
constexpr const char* kButtonIdLightMap = "light_map";
}

MapModeUI::MapModeUI(Assets* assets)
    : assets_(assets) {}

MapModeUI::~MapModeUI() = default;

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
    shading_panel_centered_ = false;
    preview_panel_centered_ = false;
    ensure_panels();
    SDL_Rect bounds{0, 0, screen_w_, screen_h_};
    if (light_panel_) light_panel_->set_work_area(bounds);
    if (shadow_panel_) shadow_panel_->set_work_area(bounds);
    if (preview_panel_) preview_panel_->set_work_area(bounds);
    if (layers_panel_) layers_panel_->set_work_area(bounds);
    update_footer_visibility();
}

void MapModeUI::set_map_mode_active(bool active) {
    map_mode_active_ = active;
    if (active) {
        footer_buttons_configured_ = false;
    }
    ensure_panels();
    if (footer_header_) {
        footer_header_->set_expanded(false);
    }
    update_footer_visibility();
    sync_footer_button_states();
    set_active_panel(PanelType::None);
    // When entering map mode, ensure the Layers footer is expanded so the
    // embedded sliding containers are visible and interactive by default.
    if (active) {
        set_layers_footer_expanded(true);
    }
}

FullScreenHeaderBar* MapModeUI::get_footer_header() const {
    return footer_header_.get();
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
    if (sliding_headers_hidden_external_ == hidden) {
        return;
    }
    sliding_headers_hidden_external_ = hidden;
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
    const bool final_state = base_headers_suppressed_ || dev_sliding_headers_hidden_;
    const bool state_changed = (headers_suppressed_ != final_state);
    headers_suppressed_ = final_state;

    if (state_changed) {
        ensure_panels();
        if (headers_suppressed_) {
            if (layers_panel_) {
                layers_panel_->close();
            }
            layers_footer_visible_ = false;
        }
    }

    update_footer_visibility();
}

void MapModeUI::set_mode_button_sets(std::vector<HeaderButtonConfig> map_buttons,
                                     std::vector<HeaderButtonConfig> room_buttons,
                                     std::vector<HeaderButtonConfig> area_buttons) {
    map_mode_buttons_ = std::move(map_buttons);
    room_mode_buttons_ = std::move(room_buttons);
    area_mode_buttons_ = std::move(area_buttons);
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
    auto& list = (mode == HeaderMode::Map) ? map_mode_buttons_ : (mode == HeaderMode::Room ? room_mode_buttons_ : area_mode_buttons_);
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
    if (footer_header_ && mode == header_mode_) {
        footer_header_->set_button_active_state(id, active);
    }
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
        if (auto* shadows = dynamic_cast<MapShadowPanel*>(panel)) {
            if (shadows->is_visible() && shadows->is_point_inside(p.x, p.y)) {
                return true;
            }
            continue;
        }
        if (auto* preview = dynamic_cast<MapLightPreviewPanel*>(panel)) {
            if (preview->is_visible() && preview->is_point_inside(p.x, p.y)) {
                return true;
            }
            continue;
        }
        if (auto* layers_prev = dynamic_cast<MapLayersPreviewPanel*>(panel)) {
            if (layers_prev->is_visible() && layers_prev->is_point_inside(p.x, p.y)) {
                return true;
            }
            continue;
        }
        if (panel->is_visible() && panel->is_point_inside(p.x, p.y)) {
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
        MapShadowPanel* shadows = dynamic_cast<MapShadowPanel*>(panel);
        MapLightPreviewPanel* preview = dynamic_cast<MapLightPreviewPanel*>(panel);
        MapLayersPreviewPanel* layers_prev = dynamic_cast<MapLayersPreviewPanel*>(panel);

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
        } else if (shadows) {
            handled_special = handle_and_check(shadows);
        } else if (preview) {
            handled_special = handle_and_check(preview);
        } else if (layers_prev) {
            handled_special = handle_and_check(layers_prev);
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

        const bool inside = (lights && lights->is_visible() && lights->is_point_inside(p.x, p.y)) ||
                            (shadows && shadows->is_visible() && shadows->is_point_inside(p.x, p.y)) ||
                            (preview && preview->is_visible() && preview->is_point_inside(p.x, p.y)) ||
                            (layers_prev && layers_prev->is_visible() && layers_prev->is_point_inside(p.x, p.y)) ||
                            (!lights && !shadows && !preview && !layers_prev && panel->is_visible() && panel->is_point_inside(p.x, p.y));

        if ((pointer_event || wheel_event) && inside) {
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                bring_panel_to_front(panel);
            }
            used = true;
            consumed = true;
            break;
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
        light_panel_->set_reactive_settings(assets_ ? assets_->reactive_shadow_settings() : nullptr);
    }
    if (!shadow_panel_) {
        shadow_panel_ = std::make_unique<MapShadowPanel>(assets_, kDefaultPanelX + 280, kDefaultPanelY);
        shadow_panel_->close();
        track_floating_panel(shadow_panel_.get());
    }
    if (shadow_panel_) {
        shadow_panel_->set_reactive_settings(assets_ ? assets_->reactive_shadow_settings() : nullptr);
    }
    if (!preview_panel_) {
        preview_panel_ = std::make_unique<MapLightPreviewPanel>(assets_, kDefaultPanelX + 520, kDefaultPanelY);
        preview_panel_->close();
        track_floating_panel(preview_panel_.get());
    }
    if (preview_panel_) {
        preview_panel_->set_assets(assets_);
        preview_panel_->set_reactive_settings(assets_ ? assets_->reactive_shadow_settings() : nullptr);
    }
    if (!grid_panel_) {
        grid_panel_ = std::make_unique<MapGridPanel>(kDefaultPanelX + 96, kDefaultPanelY + 48);
        grid_panel_->close();
        track_floating_panel(grid_panel_.get());

        GridSaveCallback save_cb = grid_save_callback_;
        if (!save_cb) {
            save_cb = [this]() { return save_map_info_to_disk(); };
        }
        grid_panel_->set_map_info(map_info_, save_cb, grid_regen_callback_);
    }
    if (!layers_controller_) {
        layers_controller_ = std::make_shared<MapLayersController>();
    }
    if (layers_controller_) {
        layers_controller_->set_manifest_store(manifest_store_, map_id_);
    }
    if (!layers_panel_) {
        layers_panel_ = std::make_unique<MapLayersPanel>(kDefaultPanelX + 64, kDefaultPanelY + 64);
        layers_panel_->set_embedded_mode(true);
        layers_panel_->set_header_visibility_callback([this](bool visible) {
            this->set_sliding_headers_hidden(visible);
        });
        if (layers_controller_) {
            layers_panel_->set_controller(layers_controller_);
        }
        layers_panel_->close();
    }
    // Floating preview panel for layers
    if (!layers_preview_panel_) {
        layers_preview_panel_ = std::make_unique<MapLayersPreviewPanel>(kDefaultPanelX + 24, kDefaultPanelY + 24);
        layers_preview_panel_->close();
        // Wire selection callbacks into the embedded sliding details
        layers_preview_panel_->set_on_select_layer([this](int idx) {
            if (layers_panel_) {
                layers_panel_->select_layer(idx);
                set_layers_footer_expanded(true);
            }
        });
        layers_preview_panel_->set_on_select_room([this](const std::string& key) {
            if (layers_panel_) {
                layers_panel_->select_room(key);
                set_layers_footer_expanded(true);
            }
        });
        layers_preview_panel_->set_on_show_room_list([this]() {
            if (layers_panel_) {
                layers_panel_->show_room_list();
                set_layers_footer_expanded(true);
            }
        });
        track_floating_panel(layers_preview_panel_.get());
    }
    if (!footer_header_) {
        footer_header_ = std::make_unique<FullScreenHeaderBar>("");
        footer_header_->set_bounds(screen_w_, screen_h_);
        footer_header_->set_title_visible(false);
        footer_header_->set_visible(footer_always_visible_ || map_mode_active_);
        footer_header_->set_expanded(false);

        footer_header_->set_on_toggle([this](bool expanded) {
            set_layers_footer_expanded(expanded);
            sync_footer_button_states();
        });
        footer_buttons_configured_ = false;
    }
    if (footer_header_ && !footer_buttons_configured_) {
        configure_footer_buttons();
        sync_footer_button_states();
    }
    update_footer_visibility();
    rebuild_floating_stack();
}

void MapModeUI::configure_footer_buttons() {
    if (!footer_header_) return;

    std::vector<FullScreenHeaderBar::HeaderButton> buttons;

    auto append_custom = [&](std::vector<HeaderButtonConfig>& configs, HeaderMode mode) {
        auto append_button = [&](HeaderButtonConfig& config) {
            FullScreenHeaderBar::HeaderButton extra;
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
        // Always include a Layers button to control the map layers panel.
        {
            FullScreenHeaderBar::HeaderButton layers_btn;
            layers_btn.id = "layers";
            layers_btn.label = "Layers";
            layers_btn.on_toggle = [this](bool active) {
                if (active) {
                    this->set_active_panel(PanelType::Layers);
                } else {
                    // Collapse layers footer when deactivated
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
        const bool has_shading_button = std::any_of(map_mode_buttons_.begin(), map_mode_buttons_.end(),
                                                    [](const HeaderButtonConfig& cfg) {
                                                        return cfg.id == kButtonIdShading;
                                                    });
        const bool has_light_map_button = false; // Light Map panel removed in dev mode

        if (!has_lights_button) {
            FullScreenHeaderBar::HeaderButton lights_btn;
            lights_btn.id = kButtonIdLights;
            lights_btn.label = "Lighting";
            lights_btn.on_toggle = [this](bool active) {
                if (active) {
                    this->open_light_panel();
                } else {
                    this->close_light_panel();
                }
            };
            buttons.push_back(std::move(lights_btn));
        }

        if (!has_shading_button) {
            FullScreenHeaderBar::HeaderButton shading_btn;
            shading_btn.id = kButtonIdShading;
            shading_btn.label = "Shading";
            shading_btn.on_toggle = [this](bool active) {
                if (active) {
                    this->open_shading_panel();
                } else {
                    this->close_shading_panel();
                }
            };
            buttons.push_back(std::move(shading_btn));
        }

        // Omit Light Map button from footer in dev mode

    } else if (header_mode_ == HeaderMode::Room) {
        append_custom(room_mode_buttons_, HeaderMode::Room);
    } else {
        append_custom(area_mode_buttons_, HeaderMode::Area);
    }

    footer_header_->set_header_buttons(std::move(buttons));
    footer_buttons_configured_ = true;
    sync_footer_button_states();
    if (header_mode_ == HeaderMode::Map) {
        // Reflect current visibility of the layers footer in the button state
        footer_header_->set_button_active_state("layers", layers_footer_visible_);
        for (const auto& config : map_mode_buttons_) {
            footer_header_->set_button_active_state(config.id, config.active);
        }
    } else if (header_mode_ == HeaderMode::Room) {
        for (const auto& config : room_mode_buttons_) {
            footer_header_->set_button_active_state(config.id, config.active);
        }
    } else {
        for (const auto& config : area_mode_buttons_) {
            footer_header_->set_button_active_state(config.id, config.active);
        }
    }
}

void MapModeUI::sync_footer_button_states() {
    if (!footer_header_) return;
    if (header_mode_ == HeaderMode::Map) {
        const bool lights_visible = light_panel_ && light_panel_->is_visible();
        const bool shading_visible = (shadow_panel_ && shadow_panel_->is_visible());
        const bool light_map_visible = preview_panel_ && preview_panel_->is_visible();
        footer_header_->set_button_active_state(kButtonIdLights, lights_visible);
        footer_header_->set_button_active_state(kButtonIdShading, shading_visible);
        footer_header_->set_button_active_state(kButtonIdLightMap, light_map_visible);
        footer_header_->set_button_active_state("layers", layers_footer_visible_);
        for (const auto& config : map_mode_buttons_) {
            footer_header_->set_button_active_state(config.id, config.active);
        }
    } else if (header_mode_ == HeaderMode::Room) {
        for (const auto& config : room_mode_buttons_) {
            footer_header_->set_button_active_state(config.id, config.active);
        }
    } else {
        for (const auto& config : area_mode_buttons_) {
            footer_header_->set_button_active_state(config.id, config.active);
        }
    }
}

void MapModeUI::update_footer_visibility() {
    if (!footer_header_) return;
    footer_header_->set_bounds(screen_w_, screen_h_);
    const bool should_show = !headers_suppressed_ && (footer_always_visible_ || map_mode_active_);
    footer_header_->set_visible(should_show);
}

void MapModeUI::set_layers_footer_expanded(bool expanded) {
    const bool previous_visible = layers_footer_visible_;

    if (!ensure_panel_unlocked(layers_panel_.get(), "Layers")) {
        if (footer_header_) {
            footer_header_->set_expanded(previous_visible);
        }
        return;
    }

    layers_footer_requested_ = expanded;
    layers_footer_visible_ = expanded;

    if (expanded) {
        active_panel_ = PanelType::Layers;
        if (layers_panel_) {
            layers_panel_->open();
        }
    } else {
        if (active_panel_ == PanelType::Layers) {
            active_panel_ = PanelType::None;
        }
        if (layers_panel_) {
            layers_panel_->close();
        }
    }
}

void MapModeUI::set_active_panel(PanelType panel) {
    ensure_panels();

    if (panel == PanelType::Layers && !ensure_panel_unlocked(layers_panel_.get(), "Layers")) {
        if (footer_header_) {
            footer_header_->set_expanded(layers_footer_visible_);
        }
        sync_footer_button_states();
        return;
    }
    if (panel == PanelType::Grid && !ensure_panel_unlocked(grid_panel_.get(), "Grid")) {
        sync_footer_button_states();
        return;
    }

    PanelType new_active = PanelType::None;

    if (grid_panel_) {
        if (panel == PanelType::Grid) {
            grid_panel_->open();
            bring_panel_to_front(grid_panel_.get());
            new_active = PanelType::Grid;
        } else {
            grid_panel_->close();
        }
    }

    if (panel == PanelType::Layers) {
        // Ensure the footer is expanded and the Layers footer is actually requested
        // so the embedded panel becomes visible and interactive.
        if (footer_header_) {
            if (!footer_header_->expanded()) {
                footer_header_->set_expanded(true);
            }
        }
        set_layers_footer_expanded(true);
        new_active = PanelType::Layers;
    } else {
        if (footer_header_) {
            if (footer_header_->expanded()) {
                footer_header_->set_expanded(false);
            } else {
                set_layers_footer_expanded(false);
            }
        } else {
            set_layers_footer_expanded(false);
        }
    }

    active_panel_ = new_active;
    sync_footer_button_states();
}

void MapModeUI::update_layers_footer(const Input& input) {
    if (headers_suppressed_) {
        return;
    }
    bool should_show = should_show_layers_footer();
    if (layers_footer_visible_ != should_show) {
        layers_footer_visible_ = should_show;
        if (layers_panel_) {
            if (layers_footer_visible_) {
                layers_panel_->open();
            } else {
                layers_panel_->close();
            }
        }
    }
    if (!layers_footer_visible_ || !layers_panel_ || !footer_header_) {
        return;
    }
    SDL_Rect content = footer_content_rect();
    layers_panel_->set_embedded_bounds(content);
    layers_panel_->update(input, screen_w_, screen_h_);
}

namespace {
bool is_mouse_button_or_motion(const SDL_Event& e) {
    return e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION;
}
}

bool MapModeUI::handle_layers_footer_event(const SDL_Event& e) {
    if (headers_suppressed_) return false;
    if (!footer_header_ || !footer_header_->visible()) return false;

    SDL_Rect header = footer_header_->header_rect();
    SDL_Point p = event_point(e);
    if (is_mouse_button_or_motion(e)) {
        if (SDL_PointInRect(&p, &header)) {
            return true;
        }
    } else if (e.type == SDL_MOUSEWHEEL) {
        if (SDL_PointInRect(&p, &header)) {
            return true;
        }
    }

    if (!layers_footer_visible_ || !layers_panel_) {
        return false;
    }

    SDL_Rect content = footer_content_rect();
    const bool pointer_event = is_mouse_button_or_motion(e);
    const bool wheel_event = (e.type == SDL_MOUSEWHEEL);
    const bool pointer_in_content = pointer_event && SDL_PointInRect(&p, &content);
    const bool wheel_in_content = wheel_event && SDL_PointInRect(&p, &content);

    if ((pointer_event && !pointer_in_content) || (wheel_event && !wheel_in_content)) {
        if (layers_panel_->handle_event(e)) {
            return true;
        }
    }

    if (pointer_in_content || wheel_in_content) {
        return true;
    }

    return false;
}

void MapModeUI::render_layers_footer(SDL_Renderer* renderer) const {
    if (headers_suppressed_) return;
    if (!layers_footer_visible_ || !layers_panel_ || !footer_header_) return;
    if (!footer_header_->visible() || !footer_header_->expanded()) return;
    layers_panel_->render(renderer);
}

bool MapModeUI::should_show_layers_footer() const {
    if (headers_suppressed_) return false;
    if (!footer_header_) return false;
    if (!layers_footer_requested_) return false;
    if (!footer_header_->visible()) return false;
    return footer_header_->expanded();
}

SDL_Rect MapModeUI::footer_content_rect() const {
    if (!footer_header_) {
        return SDL_Rect{0, 0, 0, 0};
    }
    SDL_Rect header = footer_header_->header_rect();
    int y = header.y + header.h;
    int h = std::max(0, screen_h_ - y);
    return SDL_Rect{0, y, screen_w_, h};
}

void MapModeUI::sync_panel_map_info() {
    if (!map_info_) return;
    ensure_panels();
    if (light_panel_) {
        light_panel_->set_reactive_settings(assets_ ? assets_->reactive_shadow_settings() : nullptr);
        LightSaveCallback callback = light_save_callback_;
        if (!callback) {
            callback = [this]() { return save_map_info_to_disk(); };
        }
        light_panel_->set_map_info(map_info_, callback);
    }
    if (shadow_panel_) {
        shadow_panel_->set_reactive_settings(assets_ ? assets_->reactive_shadow_settings() : nullptr);
        LightSaveCallback callback = light_save_callback_;
        if (!callback) {
            callback = [this]() { return save_map_info_to_disk(); };
        }
        shadow_panel_->set_map_info(map_info_, callback);
    }
    if (preview_panel_) {
        preview_panel_->set_assets(assets_);
        preview_panel_->set_reactive_settings(assets_ ? assets_->reactive_shadow_settings() : nullptr);
        LightSaveCallback callback = light_save_callback_;
        if (!callback) {
            callback = [this]() { return save_map_info_to_disk(); };
        }
        preview_panel_->set_map_info(map_info_, callback);
    }
    if (grid_panel_) {
        GridSaveCallback save_cb = grid_save_callback_;
        if (!save_cb) {
            save_cb = [this]() { return save_map_info_to_disk(); };
        }
        grid_panel_->set_map_info(map_info_, save_cb, grid_regen_callback_);
    }
    if (layers_panel_) {
        if (layers_controller_) {
            layers_controller_->set_manifest_store(manifest_store_, map_id_);
            layers_controller_->bind(map_info_, map_path_);
        }
        layers_panel_->set_map_info(map_info_, map_path_);
        layers_panel_->set_on_save([this]() { return save_map_info_to_disk(); });
    }
    if (layers_preview_panel_) {
        MapModeUI::LightSaveCallback callback = light_save_callback_;
        if (!callback) {
            callback = [this]() { return save_map_info_to_disk(); };
        }
        layers_preview_panel_->set_controller(layers_controller_);
        layers_preview_panel_->set_map_info(map_info_, callback);
    }
}

void MapModeUI::update(const Input& input) {
    ensure_panels();
    if (footer_header_ && footer_header_->visible()) {
        footer_header_->update(input);
    }
    update_layers_footer(input);
    for (DockableCollapsible* panel : floating_panels_) {
        if (!panel) continue;
        if (auto* lights = dynamic_cast<MapLightPanel*>(panel)) {
            if (lights->is_visible()) {
                lights->update(input, screen_w_, screen_h_);
            }
            continue;
        }
        if (auto* layers_prev = dynamic_cast<MapLayersPreviewPanel*>(panel)) {
            if (layers_prev->is_visible()) {
                layers_prev->update(input, screen_w_, screen_h_);
            }
            continue;
        }
        if (auto* preview = dynamic_cast<MapLightPreviewPanel*>(panel)) {
            if (preview->is_visible()) {
                preview->update(input, screen_w_, screen_h_);
            }
            continue;
        }
        if (auto* shadows = dynamic_cast<MapShadowPanel*>(panel)) {
            if (shadows->is_visible()) {
                shadows->update(input, screen_w_, screen_h_);
            }
            continue;
        }
        if (panel->is_visible()) {
            panel->update(input, screen_w_, screen_h_);
        }
    }

    PanelType visible = PanelType::None;
    if (layers_footer_requested_) {
        visible = PanelType::Layers;
    } else if (grid_panel_ && grid_panel_->is_visible()) {
        visible = PanelType::Grid;
    }
    if (visible != active_panel_) {
        active_panel_ = visible;
        sync_footer_button_states();
    }

    const bool lights_visible = light_panel_ && light_panel_->is_visible();
    const bool shading_visible = (shadow_panel_ && shadow_panel_->is_visible());
    const bool light_map_visible = preview_panel_ && preview_panel_->is_visible();
    if (lights_visible != last_lights_visible_ ||
        shading_visible != last_shading_visible_ ||
        light_map_visible != last_preview_visible_) {
        last_lights_visible_ = lights_visible;
        last_shading_visible_ = shading_visible;
        last_preview_visible_ = light_map_visible;
        sync_footer_button_states();
    }
}

bool MapModeUI::handle_event(const SDL_Event& e) {
    ensure_panels();
    bool floating_used = false;
    if (handle_floating_panel_event(e, floating_used)) {
        return true;
    }
    if (floating_used) {
        return true;
    }

    bool footer_used = false;
    bool layers_used = false;
    const bool allow_footer = !headers_suppressed_;
    if (allow_footer && footer_header_ && footer_header_->visible()) {
        footer_used = footer_header_->handle_event(e);
        layers_used = handle_layers_footer_event(e);
    } else if (allow_footer) {
        layers_used = handle_layers_footer_event(e);
    }
    if (footer_used || layers_used) {
        return true;
    }

    return false;
}

void MapModeUI::render(SDL_Renderer* renderer) const {
    for (DockableCollapsible* panel : floating_panels_) {
        if (!panel) continue;
        if (auto* lights = dynamic_cast<MapLightPanel*>(panel)) {
            if (lights->is_visible()) {
                lights->render(renderer);
            }
            continue;
        }
        if (auto* layers_prev = dynamic_cast<MapLayersPreviewPanel*>(panel)) {
            if (layers_prev->is_visible()) {
                layers_prev->render(renderer);
            }
            continue;
        }
        if (auto* preview = dynamic_cast<MapLightPreviewPanel*>(panel)) {
            if (preview->is_visible()) {
                preview->render(renderer);
            }
            continue;
        }
        if (auto* shadows = dynamic_cast<MapShadowPanel*>(panel)) {
            if (shadows->is_visible()) {
                shadows->render(renderer);
            }
            continue;
        }
        if (panel->is_visible()) {
            panel->render(renderer);
        }
    }
    if (footer_header_ && footer_header_->visible()) {
        footer_header_->render(renderer);
        render_layers_footer(renderer);
    } else {
        render_layers_footer(renderer);
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
    if (!ensure_panel_unlocked(light_panel_.get(), "Light")) {
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
    if (!ensure_panel_unlocked(light_panel_.get(), "Light")) {
        sync_footer_button_states();
        return;
    }
    if (light_panel_ && light_panel_->is_visible()) {
        light_panel_->close();
        sync_footer_button_states();
        return;
    }
    open_light_panel();
}

void MapModeUI::open_light_map_panel() {
    ensure_panels();
    if (!ensure_panel_unlocked(preview_panel_.get(), "Light Map")) {
        sync_footer_button_states();
        return;
    }
    if (!preview_panel_centered_) {
        ensure_light_and_shading_positions();
    }
    if (preview_panel_) {
        preview_panel_->open();
        bring_panel_to_front(preview_panel_.get());
    }
    sync_footer_button_states();
}

void MapModeUI::close_light_map_panel() {
    ensure_panels();
    if (preview_panel_) {
        preview_panel_->close();
    }
    preview_panel_centered_ = false;
    sync_footer_button_states();
}

void MapModeUI::toggle_light_map_panel() {
    ensure_panels();
    if (!ensure_panel_unlocked(preview_panel_.get(), "Light Map")) {
        sync_footer_button_states();
        return;
    }
    if (preview_panel_ && preview_panel_->is_visible()) {
        preview_panel_->close();
        preview_panel_centered_ = false;
        sync_footer_button_states();
        return;
    }
    open_light_map_panel();
}

void MapModeUI::open_shading_panel() {
    ensure_panels();
    if (!ensure_panel_unlocked(shadow_panel_.get(), "Shading")) {
        sync_footer_button_states();
        return;
    }
    if (!shading_panel_centered_) {
        ensure_light_and_shading_positions();
    }
    // Open only the shading panel now.
    if (shadow_panel_) {
        shadow_panel_->open();
        // Bring shading panel to the front last for safer interaction
        bring_panel_to_front(shadow_panel_.get());
    }
    sync_footer_button_states();
}

void MapModeUI::close_shading_panel() {
    ensure_panels();
    if (shadow_panel_) {
        shadow_panel_->close();
    }
    shading_panel_centered_ = false;
    sync_footer_button_states();
}

void MapModeUI::toggle_shading_panel() {
    ensure_panels();
    if (!ensure_panel_unlocked(shadow_panel_.get(), "Shading")) {
        sync_footer_button_states();
        return;
    }
    const bool shading_visible = shadow_panel_ && shadow_panel_->is_visible();
    if (shading_visible) {
        shadow_panel_->close();
        shading_panel_centered_ = false;
        sync_footer_button_states();
        return;
    }
    open_shading_panel();
}

void MapModeUI::open_grid_panel() {
    ensure_panels();
    if (!ensure_panel_unlocked(grid_panel_.get(), "Grid")) {
        return;
    }
    if (active_panel_ != PanelType::Grid) {
        set_active_panel(PanelType::Grid);
    }
}

void MapModeUI::close_grid_panel() {
    ensure_panels();
    if (active_panel_ == PanelType::Grid) {
        set_active_panel(PanelType::None);
    } else if (grid_panel_) {
        grid_panel_->close();
    }
}

void MapModeUI::toggle_grid_panel() {
    ensure_panels();
    if (!ensure_panel_unlocked(grid_panel_.get(), "Grid")) {
        sync_footer_button_states();
        return;
    }
    if (active_panel_ == PanelType::Grid) {
        set_active_panel(PanelType::None);
    } else {
        set_active_panel(PanelType::Grid);
    }
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
    if (shadow_panel_) {
        shadow_panel_->close();
    }
    if (preview_panel_) {
        preview_panel_->close();
    }
    shading_panel_centered_ = false;
    preview_panel_centered_ = false;
    set_active_panel(PanelType::None);
}

bool MapModeUI::is_light_panel_visible() const {
    return light_panel_ && light_panel_->is_visible();
}

bool MapModeUI::is_shading_panel_visible() const {
    return (shadow_panel_ && shadow_panel_->is_visible());
}

bool MapModeUI::is_light_map_panel_visible() const {
    return preview_panel_ && preview_panel_->is_visible();
}

bool MapModeUI::is_grid_panel_visible() const {
    return grid_panel_ && grid_panel_->is_visible();
}

void MapModeUI::ensure_light_and_shading_positions() {
    ensure_panels();
    if (screen_w_ <= 0 || screen_h_ <= 0) {
        return;
    }

    constexpr int kPanelGap = 40;
    const int fallback_w = DockableCollapsible::kDefaultFloatingContentWidth;
    const int fallback_h = 400;

    const auto resolve_dimensions = [&](DockableCollapsible* panel, int fallbackWidth, int fallbackHeight) {
        int w = fallbackWidth;
        int h = fallbackHeight;
        if (panel) {
            w = panel->rect().w > 0 ? panel->rect().w : fallbackWidth;
            h = panel->rect().h > 0 ? panel->rect().h : panel->height();
            if (h <= 0) h = fallbackHeight;
        }
        return std::pair<int, int>{w, h};
    };

    auto [light_w, light_h] = resolve_dimensions(light_panel_.get(), fallback_w, fallback_h);
    auto [shading_w, shading_h] = resolve_dimensions(shadow_panel_.get(), fallback_w, fallback_h);
    auto [preview_w, preview_h] = resolve_dimensions(preview_panel_.get(), fallback_w, fallback_h);

    if (!light_panel_ && !shadow_panel_ && !preview_panel_) {
        return;
    }

    struct PanelLayout {
        DockableCollapsible* panel = nullptr;
        int width = 0;
        int height = 0;
        bool* centered_flag = nullptr;
    };

    std::vector<PanelLayout> layout_sequence;
    layout_sequence.reserve(3);

    if (light_panel_) {
        layout_sequence.push_back({light_panel_.get(), light_w, light_h, &light_panel_centered_});
    }
    if (shadow_panel_) {
        layout_sequence.push_back({shadow_panel_.get(), shading_w, shading_h, &shading_panel_centered_});
    }
    if (preview_panel_) {
        layout_sequence.push_back({preview_panel_.get(), preview_w, preview_h, &preview_panel_centered_});
    }

    if (layout_sequence.empty()) {
        return;
    }

    int total_width = 0;
    int base_height = 0;
    for (std::size_t i = 0; i < layout_sequence.size(); ++i) {
        const PanelLayout& entry = layout_sequence[i];
        total_width += entry.width;
        if (i > 0) {
            total_width += kPanelGap;
        }
        base_height = std::max(base_height, entry.height);
    }

    int start_x = (screen_w_ - total_width) / 2;
    if (start_x < 0) start_x = 0;
    int base_y = (screen_h_ - base_height) / 2;
    if (base_y < 0) base_y = 0;

    int current_x = start_x;
    for (PanelLayout& entry : layout_sequence) {
        if (!entry.panel) {
            continue;
        }
        int panel_x = current_x;
        int panel_w = entry.width;
        if (panel_x + panel_w > screen_w_) {
            panel_x = std::max(0, screen_w_ - panel_w);
        }
        int panel_y = base_y + (base_height - entry.height) / 2;
        if (panel_y < 0) panel_y = 0;
        entry.panel->set_position(panel_x, panel_y);
        if (entry.centered_flag) {
            *entry.centered_flag = true;
        }
        current_x = panel_x + panel_w + kPanelGap;
    }
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
    if (shadow_panel_) {
        LightSaveCallback callback = light_save_callback_;
        if (!callback) {
            callback = [this]() { return save_map_info_to_disk(); };
        }
        shadow_panel_->set_map_info(map_info_, callback);
    }
    if (preview_panel_) {
        LightSaveCallback callback = light_save_callback_;
        if (!callback) {
            callback = [this]() { return save_map_info_to_disk(); };
        }
        preview_panel_->set_map_info(map_info_, callback);
    }
}

void MapModeUI::set_map_grid_callbacks(GridSaveCallback save_cb, GridRegenCallback regen_cb) {
    grid_save_callback_ = std::move(save_cb);
    grid_regen_callback_ = std::move(regen_cb);
    ensure_panels();
    if (grid_panel_) {
        GridSaveCallback callback = grid_save_callback_;
        if (!callback) {
            callback = [this]() { return save_map_info_to_disk(); };
        }
        grid_panel_->set_map_info(map_info_, callback, grid_regen_callback_);
    }
}

bool MapModeUI::is_point_inside(int x, int y) const {
    if (pointer_inside_floating_panel(x, y)) {
        return true;
    }
    if (headers_suppressed_) {
        return false;
    }
    if (footer_header_ && footer_header_->visible() && footer_header_->contains(x, y)) {
        return true;
    }
    if (layers_footer_visible_ && layers_panel_ && layers_panel_->is_point_inside(x, y)) {
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
        if (auto* preview = dynamic_cast<MapLightPreviewPanel*>(panel)) {
            if (preview->is_visible()) return true;
            continue;
        }
        if (auto* shadows = dynamic_cast<MapShadowPanel*>(panel)) {
            if (shadows->is_visible()) return true;
            continue;
        }
        if (panel->is_visible()) return true;
    }
    return layers_footer_visible_;
}

bool MapModeUI::is_layers_footer_visible() const {
    return layers_footer_visible_;
}

bool MapModeUI::save_map_info_to_disk() const {
    if (!map_info_) return false;
    if (!manifest_store_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[MapModeUI] Cannot save map info: manifest store is not available.");
        return false;
    }
    if (map_id_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[MapModeUI] Cannot save map info: map identifier is empty.");
        return false;
    }
    if (!devmode::persist_map_manifest_entry(*manifest_store_, map_id_, *map_info_, std::cerr)) {
        return false;
    }
    manifest_store_->flush();
    return true;
}

