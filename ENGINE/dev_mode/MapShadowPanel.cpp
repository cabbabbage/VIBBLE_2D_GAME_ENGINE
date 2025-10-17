#include "MapShadowPanel.hpp"

#include "MapLightPanel.hpp"

MapShadowPanel::MapShadowPanel(MapLightPanel* light_panel, Assets* assets, int x, int y)
    : DockableCollapsible("Light Map Shadows", true, x, y),
      light_panel_(light_panel),
      assets_(assets) {
    set_floating_content_width(320);
    set_visible_height(220);
}

MapShadowPanel::~MapShadowPanel() = default;

void MapShadowPanel::set_map_info(nlohmann::json* map_info, SaveCallback on_save) {
    map_info_ = map_info;
    on_save_  = std::move(on_save);
}

void MapShadowPanel::set_reactive_settings(render_pipeline::shading::ReactiveShadowSettings* settings) {
    reactive_settings_shared_ = settings;
}

void MapShadowPanel::open() { DockableCollapsible::open(); }

void MapShadowPanel::close() { DockableCollapsible::close(); }

void MapShadowPanel::toggle() {
    if (is_visible()) {
        close();
    } else {
        open();
    }
}

bool MapShadowPanel::is_visible() const { return DockableCollapsible::is_visible(); }

void MapShadowPanel::update(const Input& input, int screen_w, int screen_h) {
    DockableCollapsible::update(input, screen_w, screen_h);
}

bool MapShadowPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) {
        return false;
    }
    return DockableCollapsible::handle_event(e);
}

void MapShadowPanel::render(SDL_Renderer* renderer) const {
    if (!is_visible()) {
        return;
    }
    DockableCollapsible::render(renderer);
}

bool MapShadowPanel::is_point_inside(int x, int y) const { return DockableCollapsible::is_point_inside(x, y); }

void MapShadowPanel::render_content(SDL_Renderer* renderer) const {
    DockableCollapsible::render_content(renderer);
}

void MapShadowPanel::layout_custom_content(int, int) const {}
