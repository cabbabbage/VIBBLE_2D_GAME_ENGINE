#include "map_layers_panel.hpp"

#include <algorithm>
#include <utility>

#include <nlohmann/json.hpp>

#include "dm_styles.hpp"
#include "font_cache.hpp"
#include "map_layers_controller.hpp"
#include "map_layers_preview_widget.hpp"
#include "utils/input.hpp"

namespace {

SDL_Point event_point_from_event(const SDL_Event& e) {
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

constexpr int kMinimumListHeight = 200;
constexpr int kRowHeight = 32;

}  // namespace

class MapLayersPanel::LayersListWidget : public Widget {
public:
    explicit LayersListWidget(MapLayersPanel* owner) : owner_(owner) {}

    void set_rect(const SDL_Rect& r) override {
        rect_ = r;
        if (owner_) {
            owner_->update_layer_row_geometry();
        }
    }

    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int w) const override {
        (void)w;
        if (!owner_) {
            return kMinimumListHeight;
        }
        return owner_->list_height_for_width(w);
    }

    bool handle_event(const SDL_Event& e) override {
        if (!owner_) {
            return false;
        }
        switch (e.type) {
            case SDL_MOUSEMOTION:
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                SDL_Point p = event_point_from_event(e);
                if (SDL_PointInRect(&p, &rect_) == SDL_FALSE) {
                    if (e.type == SDL_MOUSEMOTION) {
                        owner_->clear_hover();
                    }
                    return false;
                }
                int hit_index = -1;
                for (const auto& row : owner_->layer_rows_) {
                    if (SDL_PointInRect(&p, &row.rect) == SDL_TRUE) {
                        hit_index = row.index;
                        break;
                    }
                }
                if (hit_index >= 0) {
                    owner_->set_hovered_layer(hit_index);
                    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                        owner_->select_layer(hit_index);
                        return true;
                    }
                }
                return false;
            }
            default:
                break;
        }
        return false;
    }

    void render(SDL_Renderer* renderer) const override {
        if (owner_) {
            owner_->render_layers_list(renderer);
        }
    }

    bool wants_full_row() const override { return true; }

private:
    MapLayersPanel* owner_ = nullptr;
    SDL_Rect rect_{0, 0, 0, 0};
};

MapLayersPanel::MapLayersPanel(int x, int y)
    : DockableCollapsible("Map Layers", true, x, y) {
    add_layer_button_ = std::make_unique<DMButton>("Add Layer", &DMStyles::CreateButton(), 140, DMButton::height());
    delete_layer_button_ = std::make_unique<DMButton>("Delete Layer", &DMStyles::DeleteButton(), 140, DMButton::height());
    save_button_ = std::make_unique<DMButton>("Save", &DMStyles::AccentButton(), 120, DMButton::height());
    reload_button_ = std::make_unique<DMButton>("Reload", &DMStyles::WarnButton(), 120, DMButton::height());
    layer_name_box_ = std::make_unique<DMTextBox>("Layer name", "");
    layer_name_box_raw_ = layer_name_box_.get();

    owned_widgets_.push_back(std::make_unique<ButtonWidget>(add_layer_button_.get(), [this]() {
        if (controller_) {
            const int created = controller_->create_layer();
            mark_dirty();
            if (created >= 0) {
                select_layer(created);
            }
        } else {
            nlohmann::json& layers = layers_array();
            const int new_index = static_cast<int>(layers.size());
            nlohmann::json layer = nlohmann::json::object();
            layer["name"] = std::string{"Layer "} + std::to_string(new_index);
            layers.push_back(std::move(layer));
            mark_dirty();
            select_layer(new_index);
        }
    }));
    Widget* add_widget = owned_widgets_.back().get();

    owned_widgets_.push_back(std::make_unique<ButtonWidget>(delete_layer_button_.get(), [this]() {
        if (selected_layer_index_ < 0) {
            return;
        }
        bool removed = false;
        if (controller_) {
            removed = controller_->delete_layer(selected_layer_index_);
        } else {
            nlohmann::json& layers = layers_array();
            if (selected_layer_index_ >= 0 && selected_layer_index_ < static_cast<int>(layers.size())) {
                layers.erase(layers.begin() + selected_layer_index_);
                removed = true;
            }
        }
        if (removed) {
            selected_layer_index_ = -1;
            current_layer_name_.clear();
            if (layer_name_box_raw_) {
                layer_name_box_raw_->set_value("");
            }
            mark_dirty();
        }
    }));
    Widget* delete_widget = owned_widgets_.back().get();

    owned_widgets_.push_back(std::make_unique<ButtonWidget>(save_button_.get(), [this]() {
        commit_layer_name_edit();
        bool ok = false;
        if (controller_) {
            ok = controller_->save();
        }
        if (!ok && on_save_) {
            ok = on_save_();
        }
        (void)ok;
    }));
    Widget* save_widget = owned_widgets_.back().get();

    owned_widgets_.push_back(std::make_unique<ButtonWidget>(reload_button_.get(), [this]() {
        if (controller_ && controller_->reload()) {
            mark_dirty();
        }
        rebuild_layers();
    }));
    Widget* reload_widget = owned_widgets_.back().get();

    owned_widgets_.push_back(std::make_unique<LayersListWidget>(this));
    list_widget_ = static_cast<LayersListWidget*>(owned_widgets_.back().get());

    auto preview_widget_storage = std::make_unique<MapLayersPreviewWidget>();
    preview_widget_storage->set_on_select_layer([this](int index) {
        this->select_layer(index);
    });
    preview_widget_storage->set_on_select_room([this](const std::string& room_key) {
        this->select_room(room_key);
    });
    preview_widget_storage->set_on_show_room_list([this]() {
        this->show_room_list();
    });
    owned_widgets_.push_back(std::move(preview_widget_storage));
    preview_widget_ = static_cast<MapLayersPreviewWidget*>(owned_widgets_.back().get());
    preview_widget_->set_map_info(map_info_);
    preview_widget_->set_controller(controller_);
    preview_widget_->mark_dirty();

    owned_widgets_.push_back(std::make_unique<TextBoxWidget>(layer_name_box_raw_, true));
    Widget* name_widget = owned_widgets_.back().get();

    Rows rows;
    rows.push_back(Row{add_widget, delete_widget, save_widget, reload_widget});
    rows.push_back(Row{list_widget_});
    rows.push_back(Row{preview_widget_});
    rows.push_back(Row{name_widget});
    set_rows(rows);

    set_close_button_enabled(true);

    set_expanded(true);
    set_visible(false);
}

MapLayersPanel::~MapLayersPanel() {
    remove_listener();
}

void MapLayersPanel::set_map_info(nlohmann::json* map_info, const std::string& map_path) {
    map_info_ = map_info;
    map_path_ = map_path;
    if (preview_widget_) {
        preview_widget_->set_map_info(map_info_);
        preview_widget_->mark_dirty();
    }
    mark_dirty();
}

void MapLayersPanel::set_on_save(SaveCallback cb) {
    on_save_ = std::move(cb);
}

void MapLayersPanel::set_controller(std::shared_ptr<MapLayersController> controller) {
    if (controller_ == controller) {
        return;
    }
    remove_listener();
    controller_ = std::move(controller);
    ensure_listener();
    if (preview_widget_) {
        preview_widget_->set_controller(controller_);
        preview_widget_->mark_dirty();
    }
    mark_dirty();
}

void MapLayersPanel::set_header_visibility_callback(std::function<void(bool)> cb) {
    header_visibility_callback_ = std::move(cb);
}

void MapLayersPanel::set_work_area(const SDL_Rect& bounds) {
    DockableCollapsible::set_work_area(bounds);
}

void MapLayersPanel::open() {
    set_visible(true);
    notify_header_visibility();
}

void MapLayersPanel::close() {
    set_visible(false);
    notify_header_visibility();
}

bool MapLayersPanel::is_visible() const {
    return DockableCollapsible::is_visible();
}

bool MapLayersPanel::room_config_visible() const {
    return false;
}

void MapLayersPanel::hide_main_container() {
    // No-op in simplified panel.
}

void MapLayersPanel::show_room_list() {
    notify_side_panel(SidePanel::RoomsList);
}

void MapLayersPanel::select_room(const std::string& room_key) {
    pending_room_selection_ = room_key;
    if (on_configure_room_) {
        on_configure_room_(room_key);
    }
    notify_side_panel(SidePanel::LayerControls);
}

void MapLayersPanel::hide_details_panel() {
    notify_side_panel(SidePanel::None);
}

void MapLayersPanel::set_on_configure_room(std::function<void(const std::string&)> cb) {
    on_configure_room_ = std::move(cb);
}

void MapLayersPanel::set_side_panel_callback(std::function<void(SidePanel)> cb) {
    side_panel_callback_ = std::move(cb);
}

void MapLayersPanel::set_rooms_list_container(SlidingWindowContainer* container) {
    rooms_list_container_ = container;
}

void MapLayersPanel::set_layer_controls_container(SlidingWindowContainer* container) {
    layer_controls_container_ = container;
}

void MapLayersPanel::set_embedded_mode(bool embedded) {
    if (embedded_mode_ == embedded) {
        return;
    }
    embedded_mode_ = embedded;
    set_floatable(!embedded_mode_);
    if (embedded_mode_ && embedded_bounds_.w > 0 && embedded_bounds_.h > 0) {
        set_rect(embedded_bounds_);
    }
}

void MapLayersPanel::set_embedded_bounds(const SDL_Rect& bounds) {
    embedded_bounds_ = bounds;
    if (embedded_mode_) {
        set_rect(bounds);
    }
}

void MapLayersPanel::update(const Input& input, int screen_w, int screen_h) {
    if (!is_visible()) {
        return;
    }
    if (data_dirty_) {
        rebuild_layers();
        data_dirty_ = false;
    }
    commit_layer_name_edit();
    DockableCollapsible::update(input, screen_w, screen_h);
}

bool MapLayersPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) {
        return false;
    }
    return DockableCollapsible::handle_event(e);
}

void MapLayersPanel::render(SDL_Renderer* renderer) const {
    if (!is_visible()) {
        return;
    }
    DockableCollapsible::render(renderer);
}

bool MapLayersPanel::is_point_inside(int x, int y) const {
    return DockableCollapsible::is_point_inside(x, y);
}

void MapLayersPanel::select_layer(int index) {
    if (index < 0) {
        if (selected_layer_index_ != -1) {
            selected_layer_index_ = -1;
            current_layer_name_.clear();
            if (layer_name_box_raw_) {
                layer_name_box_raw_->set_value("");
            }
        }
        return;
    }

    int resolved_index = index;
    const int count = static_cast<int>(layer_rows_.size());
    bool found = false;
    for (const auto& row : layer_rows_) {
        if (row.index == index) {
            found = true;
            break;
        }
    }
    if (!found && index >= 0 && index < count) {
        resolved_index = layer_rows_[index].index;
        found = true;
    }
    if (!found) {
        return;
    }

    selected_layer_index_ = resolved_index;
    std::string name;
    if (controller_) {
        if (const nlohmann::json* layer = controller_->layer(selected_layer_index_)) {
            name = layer->value("name", std::string{});
        }
    } else {
        const nlohmann::json& layers = layers_array();
        if (selected_layer_index_ >= 0 && selected_layer_index_ < static_cast<int>(layers.size())) {
            name = layers[selected_layer_index_].value("name", std::string{});
        }
    }
    if (name.empty()) {
        name = "Layer " + std::to_string(selected_layer_index_);
    }
    current_layer_name_ = name;
    if (layer_name_box_raw_) {
        layer_name_box_raw_->set_value(name);
    }
    notify_side_panel(SidePanel::LayerControls);
}

void MapLayersPanel::mark_dirty(bool trigger_preview) {
    data_dirty_ = true;
    if (trigger_preview && preview_widget_) {
        preview_widget_->mark_dirty();
    }
}

void MapLayersPanel::mark_clean() {
    data_dirty_ = false;
}

void MapLayersPanel::rebuild_layers() {
    layer_rows_.clear();

    if (controller_) {
        const int count = controller_->layer_count();
        layer_rows_.reserve(std::max(count, 0));
        for (int i = 0; i < count; ++i) {
            std::string name;
            if (const nlohmann::json* layer = controller_->layer(i)) {
                name = layer->value("name", std::string{});
            }
            if (name.empty()) {
                name = "Layer " + std::to_string(i);
            }
            layer_rows_.push_back(LayerRow{i, std::move(name), SDL_Rect{0, 0, 0, 0}});
        }
    } else {
        const nlohmann::json& layers = layers_array();
        if (layers.is_array()) {
            layer_rows_.reserve(layers.size());
            for (std::size_t i = 0; i < layers.size(); ++i) {
                std::string name = layers[i].value("name", std::string{});
                if (name.empty()) {
                    name = "Layer " + std::to_string(i);
                }
                layer_rows_.push_back(LayerRow{static_cast<int>(i), std::move(name), SDL_Rect{0, 0, 0, 0}});
            }
        }
    }

    if (selected_layer_index_ >= static_cast<int>(layer_rows_.size())) {
        selected_layer_index_ = layer_rows_.empty() ? -1 : layer_rows_.back().index;
    }

    if (selected_layer_index_ >= 0) {
        select_layer(selected_layer_index_);
    } else if (layer_name_box_raw_) {
        layer_name_box_raw_->set_value("");
        current_layer_name_.clear();
    }

    update_layer_row_geometry();
    if (preview_widget_) {
        preview_widget_->mark_dirty();
    }
}

void MapLayersPanel::update_layer_row_geometry() {
    if (!list_widget_) {
        return;
    }
    SDL_Rect area = list_widget_->rect();
    const int padding = DMSpacing::small_gap();
    const int gap = DMSpacing::small_gap();
    int y = area.y + padding;
    const int width = std::max(0, area.w - padding * 2);
    for (auto& row : layer_rows_) {
        row.rect = SDL_Rect{area.x + padding, y, width, kRowHeight};
        y += kRowHeight + gap;
    }
}

int MapLayersPanel::list_height_for_width(int) const {
    const int padding = DMSpacing::small_gap();
    const int gap = DMSpacing::small_gap();
    if (layer_rows_.empty()) {
        return kMinimumListHeight;
    }
    int total = padding * 2;
    total += static_cast<int>(layer_rows_.size()) * kRowHeight;
    if (layer_rows_.size() > 1) {
        total += static_cast<int>(layer_rows_.size() - 1) * gap;
    }
    return std::max(kMinimumListHeight, total);
}

void MapLayersPanel::render_layers_list(SDL_Renderer* renderer) const {
    if (!renderer || !list_widget_) {
        return;
    }
    SDL_Rect area = list_widget_->rect();
    if (area.w <= 0 || area.h <= 0) {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    const SDL_Color panel_bg = DMStyles::PanelBG();
    SDL_SetRenderDrawColor(renderer, panel_bg.r, panel_bg.g, panel_bg.b, panel_bg.a);
    SDL_RenderFillRect(renderer, &area);

    const SDL_Color border = DMStyles::Border();
    SDL_RenderDrawRect(renderer, &area);

    const int padding = DMSpacing::small_gap();
    const DMLabelStyle& label_style = DMStyles::Label();

    if (layer_rows_.empty()) {
        const std::string message = "No layers configured";
        SDL_Point size = MeasureLabelText(label_style, message);
        int text_x = area.x + padding;
        int text_y = area.y + padding;
        if (size.y < area.h) {
            text_y = area.y + (area.h - size.y) / 2;
        }
        DrawLabelText(renderer, message, text_x, text_y, label_style);
        return;
    }

    for (const auto& row : layer_rows_) {
        SDL_Rect rect = row.rect;
        SDL_Color fill = DMStyles::ButtonBaseFill();
        if (row.index == selected_layer_index_) {
            fill = DMStyles::ButtonPressedFill();
        } else if (row.index == hovered_layer_index_) {
            fill = DMStyles::ButtonHoverFill();
        }
        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &rect);
        SDL_RenderDrawRect(renderer, &rect);

        SDL_Point text_size = MeasureLabelText(label_style, row.name);
        int text_x = rect.x + padding;
        int text_y = rect.y + (rect.h - text_size.y) / 2;
        DrawLabelText(renderer, row.name, text_x, text_y, label_style);
    }
}

void MapLayersPanel::commit_layer_name_edit() {
    if (!layer_name_box_raw_ || selected_layer_index_ < 0) {
        return;
    }
    if (layer_name_box_raw_->is_editing()) {
        return;
    }
    const std::string value = layer_name_box_raw_->value();
    if (value == current_layer_name_) {
        return;
    }

    bool renamed = false;
    if (controller_) {
        renamed = controller_->rename_layer(selected_layer_index_, value);
    } else {
        nlohmann::json& layers = layers_array();
        if (selected_layer_index_ >= 0 && selected_layer_index_ < static_cast<int>(layers.size())) {
            layers[selected_layer_index_]["name"] = value;
            renamed = true;
        }
    }

    if (renamed) {
        current_layer_name_ = value;
        for (auto& row : layer_rows_) {
            if (row.index == selected_layer_index_) {
                row.name = value;
                break;
            }
        }
        mark_dirty();
    } else {
        // Restore previous value if rename failed.
        if (layer_name_box_raw_) {
            layer_name_box_raw_->set_value(current_layer_name_);
        }
    }
}

void MapLayersPanel::ensure_listener() {
    if (!controller_ || controller_listener_id_ != 0) {
        return;
    }
    controller_listener_id_ = controller_->add_listener([this]() {
        this->mark_dirty();
    });
}

void MapLayersPanel::remove_listener() {
    if (controller_ && controller_listener_id_ != 0) {
        controller_->remove_listener(controller_listener_id_);
    }
    controller_listener_id_ = 0;
}

void MapLayersPanel::notify_header_visibility() const {
    if (header_visibility_callback_) {
        header_visibility_callback_(is_visible());
    }
}

void MapLayersPanel::notify_side_panel(SidePanel panel) const {
    if (side_panel_callback_) {
        side_panel_callback_(panel);
    }
}

void MapLayersPanel::set_hovered_layer(int index) {
    hovered_layer_index_ = index;
}

void MapLayersPanel::clear_hover() {
    hovered_layer_index_ = -1;
}

const nlohmann::json& MapLayersPanel::layers_array() const {
    static const nlohmann::json kEmpty = nlohmann::json::array();
    if (!map_info_ || !map_info_->is_object()) {
        return kEmpty;
    }
    auto it = map_info_->find("map_layers");
    if (it == map_info_->end() || !it->is_array()) {
        return kEmpty;
    }
    return *it;
}

nlohmann::json& MapLayersPanel::layers_array() {
    static nlohmann::json dummy = nlohmann::json::array();
    if (!map_info_ || !map_info_->is_object()) {
        dummy = nlohmann::json::array();
        return dummy;
    }
    if (!map_info_->contains("map_layers") || !(*map_info_)["map_layers"].is_array()) {
        (*map_info_)["map_layers"] = nlohmann::json::array();
    }
    return (*map_info_)["map_layers"];
}

