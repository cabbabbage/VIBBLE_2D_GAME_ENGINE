#include "map_mode_ui.hpp"

#include "MapLightPanel.hpp"
#include "full_screen_collapsible.hpp"
#include "map_assets_panel.hpp"
#include "map_layers_controller.hpp"
#include "map_layers_panel.hpp"
#include "core/AssetsManager.hpp"
#include "utils/input.hpp"

#include <SDL.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <nlohmann/json.hpp>

namespace {
constexpr int kDefaultPanelX = 48;
constexpr int kDefaultPanelY = 48;
constexpr const char* kButtonIdLayers = "layers";
constexpr const char* kButtonIdLights = "lights";
constexpr const char* kButtonIdAssets = "assets";
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
    if (assets_panel_) assets_panel_->set_work_area(bounds);
    if (layers_panel_) layers_panel_->set_work_area(bounds);
    if (footer_panel_) footer_panel_->set_bounds(screen_w_, screen_h_);
}

void MapModeUI::set_map_mode_active(bool active) {
    map_mode_active_ = active;
    ensure_panels();
    if (footer_panel_) {
        footer_panel_->set_visible(map_mode_active_);
        footer_panel_->set_bounds(screen_w_, screen_h_);
        if (active) {
            footer_panel_->set_expanded(false);
        }
    }
    set_active_panel(PanelType::None);
}

void MapModeUI::ensure_panels() {
    if (!light_panel_) {
        light_panel_ = std::make_unique<MapLightPanel>(kDefaultPanelX, kDefaultPanelY);
        light_panel_->close();
    }
    if (!assets_panel_) {
        assets_panel_ = std::make_unique<MapAssetsPanel>(kDefaultPanelX + 32, kDefaultPanelY + 32);
        assets_panel_->close();
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
        footer_panel_->set_visible(map_mode_active_);
        footer_panel_->set_expanded(false);
        footer_buttons_configured_ = false;
    }
    if (footer_panel_ && !footer_buttons_configured_) {
        configure_footer_buttons();
        footer_panel_->set_active_button(panel_button_id(active_panel_), false);
    }
    if (footer_panel_) {
        footer_panel_->set_visible(map_mode_active_);
    }
}


void MapModeUI::configure_footer_buttons() {
    if (!footer_panel_) return;

    std::vector<FullScreenCollapsible::HeaderButton> buttons;

    FullScreenCollapsible::HeaderButton layers_btn;
    layers_btn.id = kButtonIdLayers;
    layers_btn.label = "Layers";
    layers_btn.active = (active_panel_ == PanelType::Layers);
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
    lights_btn.active = (active_panel_ == PanelType::Lights);
    lights_btn.on_toggle = [this](bool active) {
        if (active) {
            set_active_panel(PanelType::Lights);
        } else if (active_panel_ == PanelType::Lights) {
            set_active_panel(PanelType::None);
        }
    };
    buttons.push_back(std::move(lights_btn));

    FullScreenCollapsible::HeaderButton assets_btn;
    assets_btn.id = kButtonIdAssets;
    assets_btn.label = "Assets";
    assets_btn.active = (active_panel_ == PanelType::Assets);
    assets_btn.on_toggle = [this](bool active) {
        if (active) {
            set_active_panel(PanelType::Assets);
        } else if (active_panel_ == PanelType::Assets) {
            set_active_panel(PanelType::None);
        }
    };
    buttons.push_back(std::move(assets_btn));

    footer_panel_->set_header_buttons(std::move(buttons));
    footer_buttons_configured_ = true;
    footer_panel_->set_active_button(panel_button_id(active_panel_), false);
}

void MapModeUI::set_active_panel(PanelType panel) {
    ensure_panels();

    PanelType new_active = PanelType::None;

    if (light_panel_) {
        if (panel == PanelType::Lights) {
            light_panel_->open();
            new_active = PanelType::Lights;
        } else {
            light_panel_->close();
        }
    }
    if (assets_panel_) {
        if (panel == PanelType::Assets) {
            assets_panel_->open();
            new_active = PanelType::Assets;
        } else {
            assets_panel_->close();
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
    if (footer_panel_ && footer_buttons_configured_) {
        footer_panel_->set_active_button(panel_button_id(active_panel_), false);
    }
}

const char* MapModeUI::panel_button_id(PanelType panel) const {
    switch (panel) {
    case PanelType::Assets:
        return kButtonIdAssets;
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

SDL_Point event_point(const SDL_Event& e) {
    if (e.type == SDL_MOUSEMOTION) {
        return SDL_Point{e.motion.x, e.motion.y};
    }
    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        return SDL_Point{e.button.x, e.button.y};
    }
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    return SDL_Point{mx, my};
}
} // namespace

bool MapModeUI::handle_layers_footer_event(const SDL_Event& e) {
    if (!footer_panel_ || !map_mode_active_ || !footer_panel_->visible()) return false;

    SDL_Rect header = footer_panel_->header_rect();
    if (is_mouse_button_or_motion(e)) {
        SDL_Point p = event_point(e);
        if (SDL_PointInRect(&p, &header)) {
            return true;
        }
    } else if (e.type == SDL_MOUSEWHEEL) {
        SDL_Point p = event_point(e);
        if (SDL_PointInRect(&p, &header)) {
            return true;
        }
    }

    if (!layers_footer_visible_ || !layers_panel_) {
        return false;
    }

    if (layers_panel_->handle_event(e)) {
        return true;
    }

    SDL_Rect content = footer_panel_->content_rect();
    if (is_mouse_button_or_motion(e)) {
        SDL_Point p = event_point(e);
        if (SDL_PointInRect(&p, &content)) {
            return true;
        }
    } else if (e.type == SDL_MOUSEWHEEL) {
        SDL_Point p = event_point(e);
        if (SDL_PointInRect(&p, &content)) {
            return true;
        }
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
        light_panel_->set_map_info(map_info_, [this]() { save_map_info_to_disk(); });
    }
    if (assets_panel_) {
        assets_panel_->set_map_info(map_info_, map_path_);
        assets_panel_->set_on_save([this]() { return save_map_info_to_disk(); });
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
    if (map_mode_active_ && footer_panel_) {
        footer_panel_->update(input);
    }
    update_layers_footer(input);
    if (light_panel_ && light_panel_->is_visible()) {
        light_panel_->update(input);
    }
    if (assets_panel_ && assets_panel_->is_visible()) {
        assets_panel_->update(input, screen_w_, screen_h_);
    }

    PanelType visible = PanelType::None;
    if (layers_footer_requested_) {
        visible = PanelType::Layers;
    } else if (assets_panel_ && assets_panel_->is_visible()) {
        visible = PanelType::Assets;
    } else if (light_panel_ && light_panel_->is_visible()) {
        visible = PanelType::Lights;
    }
    if (visible != active_panel_) {
        active_panel_ = visible;
        if (footer_panel_ && footer_buttons_configured_) {
            footer_panel_->set_active_button(panel_button_id(active_panel_), false);
        }
    }
}


bool MapModeUI::handle_event(const SDL_Event& e) {
    ensure_panels();
    if (map_mode_active_ && footer_panel_ && footer_panel_->visible()) {
        bool footer_used = footer_panel_->handle_event(e);
        bool layers_used = handle_layers_footer_event(e);
        if (footer_used || layers_used) {
            return true;
        }
    } else {
        if (handle_layers_footer_event(e)) {
            return true;
        }
    }
    bool used = false;
    if (assets_panel_ && assets_panel_->is_visible()) {
        used |= assets_panel_->handle_event(e);
    }
    if (light_panel_ && light_panel_->is_visible()) {
        used |= light_panel_->handle_event(e);
    }
    return used;
}


void MapModeUI::render(SDL_Renderer* renderer) const {
    if (assets_panel_ && assets_panel_->is_visible()) {
        assets_panel_->render(renderer);
    }
    if (light_panel_ && light_panel_->is_visible()) {
        light_panel_->render(renderer);
    }
    if (map_mode_active_ && footer_panel_ && footer_panel_->visible()) {
        footer_panel_->render(renderer);
        render_layers_footer(renderer);
    } else {
        render_layers_footer(renderer);
    }
}


void MapModeUI::open_assets_panel() {
    set_active_panel(PanelType::Assets);
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


bool MapModeUI::is_point_inside(int x, int y) const {
    if (map_mode_active_ && footer_panel_ && footer_panel_->visible() && footer_panel_->contains(x, y)) {
        return true;
    }
    if (light_panel_ && light_panel_->is_visible() && light_panel_->is_point_inside(x, y)) {
        return true;
    }
    if (assets_panel_ && assets_panel_->is_visible() && assets_panel_->is_point_inside(x, y)) {
        return true;
    }
    if (layers_footer_visible_ && layers_panel_ && layers_panel_->is_point_inside(x, y)) {
        return true;
    }
    return false;
}


bool MapModeUI::is_any_panel_visible() const {
    return (light_panel_ && light_panel_->is_visible()) ||
           (assets_panel_ && assets_panel_->is_visible()) ||
           layers_footer_visible_;
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














