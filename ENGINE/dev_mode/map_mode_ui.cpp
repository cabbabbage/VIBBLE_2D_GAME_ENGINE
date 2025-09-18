#include "map_mode_ui.hpp"

#include "MapLightPanel.hpp"
#include "map_assets_panel.hpp"
#include "map_layers_controller.hpp"
#include "map_layers_panel.hpp"
#include "core/AssetsManager.hpp"
#include "utils/input.hpp"

#include <SDL.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {
constexpr int kDefaultPanelX = 48;
constexpr int kDefaultPanelY = 48;
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
        if (layers_controller_) {
            layers_panel_->set_controller(layers_controller_);
        }
        layers_panel_->close();
    }
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
    if (light_panel_ && light_panel_->is_visible()) {
        light_panel_->update(input);
    }
    if (assets_panel_ && assets_panel_->is_visible()) {
        assets_panel_->update(input, screen_w_, screen_h_);
    }
    if (layers_panel_ && layers_panel_->is_visible()) {
        layers_panel_->update(input, screen_w_, screen_h_);
    }
}


bool MapModeUI::handle_event(const SDL_Event& e) {
    ensure_panels();
    bool used = false;
    if (assets_panel_ && assets_panel_->is_visible()) {
        used |= assets_panel_->handle_event(e);
    }
    if (light_panel_ && light_panel_->is_visible()) {
        used |= light_panel_->handle_event(e);
    }
    if (layers_panel_ && layers_panel_->is_visible()) {
        used |= layers_panel_->handle_event(e);
    }
    return used;
}


void MapModeUI::render(SDL_Renderer* renderer) const {
    if (assets_panel_ && assets_panel_->is_visible()) {
        assets_panel_->render(renderer);
    }
    if (layers_panel_ && layers_panel_->is_visible()) {
        layers_panel_->render(renderer);
    }
    if (light_panel_ && light_panel_->is_visible()) {
        light_panel_->render(renderer);
    }
}


void MapModeUI::open_assets_panel() {
    ensure_panels();
    if (light_panel_) light_panel_->close();
    if (layers_panel_) layers_panel_->close();
    if (assets_panel_) {
        assets_panel_->open();
    }
}


void MapModeUI::toggle_light_panel() {
    ensure_panels();
    if (!light_panel_) return;
    if (light_panel_->is_visible()) {
        light_panel_->close();
    } else {
        if (assets_panel_) assets_panel_->close();
        if (layers_panel_) layers_panel_->close();
        light_panel_->open();
    }
}


void MapModeUI::toggle_layers_panel() {
    ensure_panels();
    if (!layers_panel_) return;
    if (layers_panel_->is_visible()) {
        layers_panel_->close();
    } else {
        if (assets_panel_) assets_panel_->close();
        if (light_panel_) light_panel_->close();
        layers_panel_->open();
    }
}

void MapModeUI::close_all_panels() {
    if (light_panel_) light_panel_->close();
    if (assets_panel_) assets_panel_->close();
    if (layers_panel_) layers_panel_->close();
}


bool MapModeUI::is_point_inside(int x, int y) const {
    if (light_panel_ && light_panel_->is_visible() && light_panel_->is_point_inside(x, y)) {
        return true;
    }
    if (assets_panel_ && assets_panel_->is_visible() && assets_panel_->is_point_inside(x, y)) {
        return true;
    }
    if (layers_panel_ && layers_panel_->is_visible() && layers_panel_->is_point_inside(x, y)) {
        return true;
    }
    return false;
}


bool MapModeUI::is_any_panel_visible() const {
    return (light_panel_ && light_panel_->is_visible()) ||
           (assets_panel_ && assets_panel_->is_visible()) ||
           (layers_panel_ && layers_panel_->is_visible());
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














