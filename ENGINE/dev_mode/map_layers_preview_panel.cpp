#include "map_layers_preview_panel.hpp"

#include <nlohmann/json.hpp>

#include "dm_styles.hpp"
#include "map_layers_controller.hpp"
#include "map_layers_preview_widget.hpp"
#include "widgets.hpp"

MapLayersPreviewPanel::MapLayersPreviewPanel(int x, int y)
    : DockableCollapsible("Layers Preview", true, x, y) {
    build_rows();
    set_visible(false);
    set_expanded(true);
}

MapLayersPreviewPanel::~MapLayersPreviewPanel() = default;

void MapLayersPreviewPanel::set_map_info(nlohmann::json* map_info, SaveCallback on_save) {
    map_info_ = map_info;
    on_save_ = std::move(on_save);
    if (preview_widget_) {
        preview_widget_->set_map_info(map_info_);
        preview_widget_->set_on_change([this]() { this->trigger_save(); });
    }
}

void MapLayersPreviewPanel::set_controller(std::shared_ptr<MapLayersController> controller) {
    controller_ = std::move(controller);
    if (preview_widget_) {
        preview_widget_->set_controller(controller_);
    }
}

void MapLayersPreviewPanel::set_on_select_layer(std::function<void(int)> cb) {
    on_select_layer_ = std::move(cb);
    if (preview_widget_) {
        preview_widget_->set_on_select_layer(on_select_layer_);
    }
}

void MapLayersPreviewPanel::set_on_select_room(std::function<void(const std::string&)> cb) {
    on_select_room_ = std::move(cb);
    if (preview_widget_) {
        preview_widget_->set_on_select_room(on_select_room_);
    }
}

void MapLayersPreviewPanel::set_on_show_room_list(std::function<void()> cb) {
    on_show_room_list_ = std::move(cb);
    if (preview_widget_) {
        preview_widget_->set_on_show_room_list(on_show_room_list_);
    }
}

void MapLayersPreviewPanel::update(const Input& input, int screen_w, int screen_h) {
    DockableCollapsible::update(input, screen_w, screen_h);
}

bool MapLayersPreviewPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) {
        return false;
    }
    return DockableCollapsible::handle_event(e);
}

void MapLayersPreviewPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }
    DockableCollapsible::render(renderer);
}

bool MapLayersPreviewPanel::is_point_inside(int x, int y) const {
    return DockableCollapsible::is_point_inside(x, y);
}

void MapLayersPreviewPanel::build_rows() {
    owned_widgets_.clear();
    preview_widget_ = nullptr;

    if (!add_layer_btn_) {
        add_layer_btn_ = std::make_unique<DMButton>("Add Layer", &DMStyles::CreateButton(), 0, DMButton::height());
    }
    if (!create_room_btn_) {
        create_room_btn_ = std::make_unique<DMButton>("Create Room", &DMStyles::CreateButton(), 0, DMButton::height());
    }
    if (!reload_btn_) {
        reload_btn_ = std::make_unique<DMButton>("Reload", &DMStyles::ListButton(), 0, DMButton::height());
    }

    std::vector<Widget*> button_row;
    owned_widgets_.push_back(std::make_unique<ButtonWidget>(add_layer_btn_.get(), [this]() {
        if (controller_) {
            const int created = controller_->create_layer();
            if (preview_widget_) {
                preview_widget_->mark_dirty();
            }
            if (created >= 0) {
                trigger_save();
            }
        }
    }));
    button_row.push_back(owned_widgets_.back().get());

    owned_widgets_.push_back(std::make_unique<ButtonWidget>(create_room_btn_.get(), [this]() {
        if (preview_widget_) {
            preview_widget_->create_new_room_entry();
        }
        trigger_save();
    }));
    button_row.push_back(owned_widgets_.back().get());

    owned_widgets_.push_back(std::make_unique<ButtonWidget>(reload_btn_.get(), [this]() {
        if (controller_) {
            controller_->reload();
            if (preview_widget_) {
                preview_widget_->mark_dirty();
            }
        }
    }));
    button_row.push_back(owned_widgets_.back().get());

    auto preview = std::make_unique<MapLayersPreviewWidget>();
    preview->set_map_info(map_info_);
    preview->set_controller(controller_);
    preview->set_on_select_layer(on_select_layer_);
    preview->set_on_select_room(on_select_room_);
    preview->set_on_show_room_list(on_show_room_list_);
    preview->set_on_change([this]() { this->trigger_save(); });

    owned_widgets_.push_back(std::move(preview));
    preview_widget_ = static_cast<MapLayersPreviewWidget*>(owned_widgets_.back().get());

    Rows rows;
    rows.push_back(button_row);
    rows.push_back(Row{preview_widget_});
    set_rows(rows);
}

void MapLayersPreviewPanel::trigger_save() {
    bool ok = false;
    if (controller_) {
        ok = controller_->save();
    }
    if (!ok && on_save_) {
        ok = on_save_();
    }
    (void)ok;
}

