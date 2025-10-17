#include "map_layers_panel.hpp"

#include <nlohmann/json.hpp>

#include "utils/input.hpp"

MapLayersPanel::MapLayersPanel(int x, int y)
    : DockableCollapsible("Map Layers", true, x, y) {
}

MapLayersPanel::~MapLayersPanel() = default;

void MapLayersPanel::set_map_info(nlohmann::json*, const std::string&) {}
void MapLayersPanel::set_on_save(SaveCallback) {}
void MapLayersPanel::set_controller(std::shared_ptr<MapLayersController>) {}
void MapLayersPanel::set_header_visibility_callback(std::function<void(bool)>) {}
void MapLayersPanel::set_work_area(const SDL_Rect&) {}
void MapLayersPanel::open() {}
void MapLayersPanel::close() {}
bool MapLayersPanel::is_visible() const { return false; }
bool MapLayersPanel::room_config_visible() const { return false; }
void MapLayersPanel::hide_main_container() {}
void MapLayersPanel::set_embedded_mode(bool) {}
void MapLayersPanel::set_embedded_bounds(const SDL_Rect&) {}
void MapLayersPanel::update(const Input&, int, int) {}
bool MapLayersPanel::handle_event(const SDL_Event&) { return false; }
void MapLayersPanel::render(SDL_Renderer*) const {}
bool MapLayersPanel::is_point_inside(int, int) const { return false; }
void MapLayersPanel::select_layer(int) {}
void MapLayersPanel::mark_dirty(bool) {}
void MapLayersPanel::mark_clean() {}

