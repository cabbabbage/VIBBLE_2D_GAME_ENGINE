#include "map_mode_ui.hpp"

#include "MapLightPanel.hpp"
#include "DockableCollapsible.hpp"
#include "full_screen_collapsible.hpp"
#include "map_layers_controller.hpp"
#include "map_layers_panel.hpp"
#include "core/AssetsManager.hpp"
#include "utils/input.hpp"

#include <SDL.h>
#include <algorithm>
#include <iterator>
#include <fstream>
#include <iostream>
#include <vector>
#include <utility>
#include <nlohmann/json.hpp>

namespace {
constexpr int kDefaultPanelX = 48;
constexpr int kDefaultPanelY = 48;
constexpr const char* kButtonIdLayers = "layers";
constexpr const char* kButtonIdLights = "lights";
}

MapModeUI::MapModeUI(Assets* assets)
    : assets_(assets) {}

MapModeUI::~MapModeUI() = default;

void MapModeUI::set_map_context(nlohmann::json* map_info, const std::string& map_path) {
    map_info_ = map_info;
    map_path_ = map_path;
    sync_panel_map_info();
}

void MapModeUI::set_screen_dimensions(int w, int h) {
    screen_w_ = w;
    screen_h_ = h;
    ensure_panels();
    SDL_Rect bounds{0, 0, screen_w_, screen_h_};
    if (light_panel_) light_panel_->set_work_area(bounds);
    if (layers_panel_) layers_panel_->set_work_area(bounds);
    update_footer_visibility();
}

void MapModeUI::set_map_mode_active(bool active) {
    map_mode_active_ = active;
    if (active) {
        footer_buttons_configured_ = false;
    }
    ensure_panels();
    if (footer_panel_) {
        footer_panel_->set_expanded(false);
    }
    update_footer_visibility();
    sync_footer_button_states();
    set_active_panel(PanelType::None);
}

FullScreenCollapsible* MapModeUI::get_footer_panel() const {
    return footer_panel_.get();
}

void MapModeUI::set_footer_always_visible(bool on) {
    footer_always_visible_ = on;
    ensure_panels();
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

void MapModeUI::set_button_state(const std::string& id, bool active) {
    set_button_state(header_mode_, id, active);
}

void MapModeUI::set_button_state(HeaderMode mode, const std::string& id, bool active) {
    if (HeaderButtonConfig* cfg = find_button(mode, id)) {
        cfg->active = active;
    }
    if (footer_panel_ && mode == header_mode_) {
        footer_panel_->set_button_active_state(id, active);
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
    floating_panels_.erase(
        std::remove(floating_panels_.begin(), floating_panels_.end(), nullptr),
        floating_panels_.end());
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

        if (lights) {
            if (handle_and_check(lights)) { consumed = true; break; }
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

        const bool inside = (lights && lights->is_visible() && lights->is_point_inside(p.x, p.y)) ||
                            (!lights && panel->is_visible() && panel->is_point_inside(p.x, p.y));

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
    if (!layers_controller_) {
        layers_controller_ = std::make_shared<MapLayersController>();
    }
    if (!layers_panel_) {
        layers_panel_ = std::make_unique<MapLayersPanel>(kDefaultPanelX + 64, kDefaultPanelY + 64);
        layers_panel_->set_embedded_mode(true);
        if (layers_controller_) {
            layers_panel_->set_controller(layers_controller_);
        }
        layers_panel_->close();
    }
    if (!footer_panel_) {
        footer_panel_ = std::make_unique<FullScreenCollapsible>("Map Mode");
        footer_panel_->set_bounds(screen_w_, screen_h_);
        footer_panel_->set_visible(footer_always_visible_ || map_mode_active_);
        footer_panel_->set_expanded(false);
        // Route content events to the embedded layers panel when visible.
        footer_panel_->set_content_event_handler([this](const SDL_Event& e) -> bool {
            if (layers_footer_visible_ && layers_panel_) {
                return layers_panel_->handle_event(e);
            }
            return false;
        });
        footer_buttons_configured_ = false;
    }
    if (footer_panel_ && !footer_buttons_configured_) {
        configure_footer_buttons();
        sync_footer_button_states();
    }
    update_footer_visibility();
    rebuild_floating_stack();
}


void MapModeUI::configure_footer_buttons() {
    if (!footer_panel_) return;

    std::vector<FullScreenCollapsible::HeaderButton> buttons;

    auto append_custom = [&](std::vector<HeaderButtonConfig>& configs, HeaderMode mode) {
        auto append_button = [&](HeaderButtonConfig& config) {
            FullScreenCollapsible::HeaderButton extra;
            extra.id = config.id;
            extra.label = config.label;
            extra.active = config.active;
            extra.momentary = config.momentary;
            extra.style_override = config.style_override;
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
            if (config.id == "switch_mode") {
                append_button(config);
            }
        }
        for (auto& config : configs) {
            if (config.id != "switch_mode") {
                append_button(config);
            }
        }
    };

    if (header_mode_ == HeaderMode::Map) {
        append_custom(map_mode_buttons_, HeaderMode::Map);

        FullScreenCollapsible::HeaderButton layers_btn;
        layers_btn.id = kButtonIdLayers;
        layers_btn.label = "Layers";
        layers_btn.on_toggle = [this](bool active) {
            if (active) {
                set_active_panel(PanelType::Layers);
            } else if (active_panel_ == PanelType::Layers) {
                set_active_panel(PanelType::None);
            }
        };
        buttons.push_back(std::move(layers_btn));

        FullScreenCollapsible::HeaderButton lights_btn;
        lights_btn.id = kButtonIdLights;
        lights_btn.label = "Lighting";
        lights_btn.on_toggle = [this](bool active) {
            if (active) {
                set_active_panel(PanelType::Lights);
            } else if (active_panel_ == PanelType::Lights) {
                set_active_panel(PanelType::None);
            }
        };
        buttons.push_back(std::move(lights_btn));

    } else {
        append_custom(room_mode_buttons_, HeaderMode::Room);
    }

    footer_panel_->set_header_buttons(std::move(buttons));
    footer_buttons_configured_ = true;
    sync_footer_button_states();
    if (header_mode_ == HeaderMode::Map) {
        for (const auto& config : map_mode_buttons_) {
            footer_panel_->set_button_active_state(config.id, config.active);
        }
    } else {
        for (const auto& config : room_mode_buttons_) {
            footer_panel_->set_button_active_state(config.id, config.active);
        }
    }
}

void MapModeUI::sync_footer_button_states() {
    if (!footer_panel_) return;
    if (header_mode_ == HeaderMode::Map) {
        footer_panel_->set_button_active_state(kButtonIdLayers, active_panel_ == PanelType::Layers);
        footer_panel_->set_button_active_state(kButtonIdLights, active_panel_ == PanelType::Lights);
        for (const auto& config : map_mode_buttons_) {
            footer_panel_->set_button_active_state(config.id, config.active);
        }
    } else {
        for (const auto& config : room_mode_buttons_) {
            footer_panel_->set_button_active_state(config.id, config.active);
        }
    }
}

void MapModeUI::update_footer_visibility() {
    if (!footer_panel_) return;
    footer_panel_->set_bounds(screen_w_, screen_h_);
    footer_panel_->set_visible(footer_always_visible_ || map_mode_active_);
}

void MapModeUI::set_active_panel(PanelType panel) {
    ensure_panels();

    PanelType new_active = PanelType::None;

    if (light_panel_) {
        if (panel == PanelType::Lights) {
            light_panel_->open();
            new_active = PanelType::Lights;
            bring_panel_to_front(light_panel_.get());
        } else {
            light_panel_->close();
        }
    }
    if (panel == PanelType::Layers) {
        layers_footer_requested_ = true;
        new_active = PanelType::Layers;
        if (footer_panel_) {
            footer_panel_->set_expanded(true);
        }
    } else {
        if (layers_footer_requested_) {
            layers_footer_requested_ = false;
        }
        if (layers_footer_visible_) {
            layers_footer_visible_ = false;
        }
        if (layers_panel_) {
            layers_panel_->close();
        }
    }

    active_panel_ = new_active;
    sync_footer_button_states();
}

const char* MapModeUI::panel_button_id(PanelType panel) const {
    switch (panel) {
    case PanelType::Lights:
        return kButtonIdLights;
    case PanelType::Layers:
        return kButtonIdLayers;
    case PanelType::None:
    default:
        return "";
    }
}

void MapModeUI::update_layers_footer(const Input& input) {
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
    if (!layers_footer_visible_ || !layers_panel_ || !footer_panel_) {
        return;
    }
    SDL_Rect content = footer_panel_->content_rect();
    layers_panel_->set_embedded_bounds(content);
    layers_panel_->update(input, screen_w_, screen_h_);
}

namespace {
bool is_mouse_button_or_motion(const SDL_Event& e) {
    return e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION;
}
} // namespace

bool MapModeUI::handle_layers_footer_event(const SDL_Event& e) {
    if (!footer_panel_ || !map_mode_active_ || !footer_panel_->visible()) return false;

    SDL_Rect header = footer_panel_->header_rect();
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

    SDL_Rect content = footer_panel_->content_rect();
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
    if (!layers_footer_visible_ || !layers_panel_) return;
    layers_panel_->render(renderer);
}

bool MapModeUI::should_show_layers_footer() const {
    if (!map_mode_active_ || !footer_panel_) return false;
    if (!layers_footer_requested_) return false;
    if (!footer_panel_->visible()) return false;
    return footer_panel_->expanded();
}


void MapModeUI::sync_panel_map_info() {
    if (!map_info_) return;
    ensure_panels();
    if (light_panel_) {
        LightSaveCallback callback = light_save_callback_;
        if (!callback) {
            callback = [this]() { save_map_info_to_disk(); };
        }
        light_panel_->set_map_info(map_info_, callback);
    }
    if (layers_panel_) {
        if (layers_controller_) {
            layers_controller_->bind(map_info_, map_path_);
        }
        layers_panel_->set_map_info(map_info_, map_path_);
        layers_panel_->set_on_save([this]() { return save_map_info_to_disk(); });
    }
}


void MapModeUI::update(const Input& input) {
    ensure_panels();
    if (footer_panel_) {
        footer_panel_->update(input);
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
        if (panel->is_visible()) {
            panel->update(input, screen_w_, screen_h_);
        }
    }

    PanelType visible = PanelType::None;
    if (layers_footer_requested_) {
        visible = PanelType::Layers;
    } else if (light_panel_ && light_panel_->is_visible()) {
        visible = PanelType::Lights;
    }
    if (visible != active_panel_) {
        active_panel_ = visible;
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
    if (footer_panel_ && footer_panel_->visible()) {
        footer_used = footer_panel_->handle_event(e);
        layers_used = handle_layers_footer_event(e);
    } else {
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
        if (panel->is_visible()) {
            panel->render(renderer);
        }
    }
    if (footer_panel_ && footer_panel_->visible()) {
        footer_panel_->render(renderer);
        render_layers_footer(renderer);
    } else {
        render_layers_footer(renderer);
    }
}


void MapModeUI::open_layers_panel() {
    ensure_panels();
    set_active_panel(PanelType::Layers);
}


void MapModeUI::toggle_light_panel() {
    ensure_panels();
    if (active_panel_ == PanelType::Lights) {
        set_active_panel(PanelType::None);
    } else {
        set_active_panel(PanelType::Lights);
    }
}


void MapModeUI::toggle_layers_panel() {
    ensure_panels();
    if (active_panel_ == PanelType::Layers) {
        set_active_panel(PanelType::None);
    } else {
        set_active_panel(PanelType::Layers);
    }
}

void MapModeUI::close_all_panels() {
    set_active_panel(PanelType::None);
}


bool MapModeUI::is_light_panel_visible() const {
    return light_panel_ && light_panel_->is_visible();
}

void MapModeUI::set_light_save_callback(LightSaveCallback cb) {
    light_save_callback_ = std::move(cb);
    ensure_panels();
    if (light_panel_) {
        LightSaveCallback callback = light_save_callback_;
        if (!callback) {
            callback = [this]() { save_map_info_to_disk(); };
        }
        light_panel_->set_map_info(map_info_, callback);
    }
}


bool MapModeUI::is_point_inside(int x, int y) const {
    if (pointer_inside_floating_panel(x, y)) {
        return true;
    }
    if (footer_panel_ && footer_panel_->visible() && footer_panel_->contains(x, y)) {
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
        if (panel->is_visible()) return true;
    }
    return layers_footer_visible_;
}

bool MapModeUI::is_layers_footer_visible() const {
    return layers_footer_visible_;
}


bool MapModeUI::save_map_info_to_disk() const {
    if (!map_info_) return false;
    std::string path = map_path_.empty() ? std::string{} : (map_path_ + "/map_info.json");
    if (path.empty()) {
        if (assets_) {
            path = assets_->map_info_path();
        }
    }
    if (path.empty()) return false;

    std::ofstream out(path);
    if (!out) {
        std::cerr << "[MapModeUI] Failed to open " << path << " for writing\n";
        return false;
    }
    try {
        out << map_info_->dump(2);
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[MapModeUI] Failed to serialize map_info.json: " << ex.what() << "\n";
        return false;
    }
}














