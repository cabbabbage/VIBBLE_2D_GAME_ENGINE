#include "map_layers_panel.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "dm_styles.hpp"
#include "font_cache.hpp"
#include "map_layers_controller.hpp"
#include "map_layers_preview_widget.hpp"
#include "map_generation/map_layers_geometry.hpp"
#include "utils/input.hpp"
#include "dev_mode_color_utils.hpp"
#include "dev_mode_sdl_event_utils.hpp"

namespace {

constexpr int kMinimumListHeight = 200;
constexpr int kRowHeight = 52;
constexpr int kDropIndicatorThickness = 3;
constexpr int kLayerDeleteButtonSize = 26;

const DMLabelStyle& summary_label_style() {
    static DMLabelStyle style{DMStyles::Label().font_path, std::max(12, DMStyles::Label().font_size - 2),
                              SDL_Color{189, 200, 214, 255}};
    return style;
}

const DMLabelStyle& validation_label_style() {
    static DMLabelStyle style{DMStyles::Label().font_path, std::max(12, DMStyles::Label().font_size - 3),
                              SDL_Color{200, 210, 225, 255}};
    return style;
}

SDL_Color error_color() { return SDL_Color{220, 53, 69, 255}; }
SDL_Color warning_color() { return SDL_Color{234, 179, 8, 255}; }
SDL_Color success_color() { return SDL_Color{16, 185, 129, 255}; }
SDL_Color info_color() { return SDL_Color{148, 163, 184, 255}; }

std::string trimmed(std::string value) {
    auto begin = std::find_if(value.begin(), value.end(), [](unsigned char ch) { return !std::isspace(ch); });
    value.erase(value.begin(), begin);
    auto end = std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) { return !std::isspace(ch); });
    value.erase(end.base(), value.end());
    return value;
}

SDL_Color severity_color(bool has_error, bool has_warning, bool highlighted) {
    if (has_error) {
        SDL_Color c = error_color();
        return highlighted ? lighten(c, 0.25f) : c;
    }
    if (has_warning) {
        SDL_Color c = warning_color();
        return highlighted ? lighten(c, 0.25f) : c;
    }
    SDL_Color neutral = DMStyles::Border();
    return highlighted ? lighten(neutral, 0.35f) : neutral;
}

SDL_Color severity_fill(bool has_error, bool has_warning, bool selected) {
    if (has_error) {
        SDL_Color base{120, 40, 48, 240};
        return selected ? lighten(base, 0.2f) : base;
    }
    if (has_warning) {
        SDL_Color base{120, 92, 40, 235};
        return selected ? lighten(base, 0.2f) : base;
    }
    SDL_Color base = DMStyles::ButtonBaseFill();
    return selected ? lighten(base, 0.22f) : base;
}

}

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

        if (owner_->is_dragging_layer()) {
            switch (e.type) {
                case SDL_MOUSEMOTION:
                    owner_->on_layers_list_mouse_motion(e.motion.y, static_cast<Uint32>(e.motion.state));
                    return true;
                case SDL_MOUSEBUTTONUP: {
                    SDL_Point p = event_point_from_event(e);
                    owner_->on_layers_list_mouse_up(p.y, e.button.button);
                    return true;
                }
                case SDL_MOUSEBUTTONDOWN:
                    if (e.button.button == SDL_BUTTON_RIGHT) {
                        owner_->cancel_drag();
                        return true;
                    }
                    break;
                default:
                    break;
            }
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
                    if (e.type == SDL_MOUSEBUTTONUP && owner_->is_dragging_layer()) {
                        owner_->cancel_drag();
                    }
                    return false;
                }

                int hit_index = -1;
                int delete_hit_index = -1;
                for (const auto& row : owner_->layer_rows_) {
                    if (SDL_PointInRect(&p, &row.rect) == SDL_TRUE) {
                        hit_index = row.index;
                        if (SDL_PointInRect(&p, &row.delete_button_rect) == SDL_TRUE) {
                            delete_hit_index = row.index;
                        }
                        break;
                    }
                }

                owner_->set_hovered_delete_layer(delete_hit_index);

                if (e.type == SDL_MOUSEMOTION) {
                    if (hit_index >= 0) {
                        owner_->set_hovered_layer(hit_index);
                    } else {
                        owner_->clear_hover();
                    }
                    return false;
                }

                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    if (hit_index >= 0) {
                        owner_->set_hovered_layer(hit_index);
                        if (delete_hit_index >= 0) {
                            owner_->on_delete_layer_clicked(delete_hit_index);
                            return true;
                        }
                        owner_->on_layers_list_mouse_down(hit_index, p.y);
                        return true;
                    }
                    return false;
                }

                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    if (delete_hit_index >= 0) {
                        owner_->set_hovered_delete_layer(-1);
                        return true;
                    }
                    owner_->on_layers_list_mouse_up(p.y, e.button.button);
                    return true;
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

class MapLayersPanel::ValidationSummaryWidget : public Widget {
public:
    explicit ValidationSummaryWidget(MapLayersPanel* owner) : owner_(owner) {}

    void set_rect(const SDL_Rect& r) override { rect_ = r; }

    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int w) const override {
        return owner_ ? owner_->validation_summary_height(w) : 0;
    }

    bool handle_event(const SDL_Event& e) override {
        (void)e;
        return false;
    }

    void render(SDL_Renderer* renderer) const override {
        if (owner_) {
            owner_->render_validation_summary(renderer, rect_);
        }
    }

    bool wants_full_row() const override { return true; }

private:
    MapLayersPanel* owner_ = nullptr;
    SDL_Rect rect_{0, 0, 0, 0};
};

class MapLayersPanel::MinEdgeWidget : public Widget {
public:
    explicit MinEdgeWidget(MapLayersPanel* owner) : owner_(owner) {}

    void mark_layout_dirty() { this->request_layout(); }

    void set_rect(const SDL_Rect& r) override {
        rect_ = r;
        if (owner_) {
            owner_->layout_min_edge_input(rect_);
        }
    }

    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int w) const override {
        return owner_ ? owner_->min_edge_widget_height_for_width(w) : DMTextBox::height();
    }

    bool handle_event(const SDL_Event& e) override {
        if (!owner_) {
            return false;
        }
        return owner_->handle_min_edge_event(e);
    }

    void render(SDL_Renderer* renderer) const override {
        if (owner_) {
            owner_->render_min_edge_input(renderer, rect_);
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
    reload_button_ = std::make_unique<DMButton>("Reload", &DMStyles::WarnButton(), 120, DMButton::height());
    owned_widgets_.push_back(std::make_unique<ButtonWidget>(add_layer_button_.get(), [this]() {
        if (controller_) {
            const int created = controller_->create_layer();
            mark_dirty();
            if (created >= 0) {
                select_layer(created);
                trigger_save();
            }
        } else {
            nlohmann::json& layers = layers_array();
            const int new_index = static_cast<int>(layers.size());
            nlohmann::json layer = nlohmann::json::object();
            layer["name"] = std::string{"Layer "} + std::to_string(new_index);
            layers.push_back(std::move(layer));
            mark_dirty();
            select_layer(new_index);
            trigger_save();
        }
    }));
    Widget* add_widget = owned_widgets_.back().get();

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
        this->force_layer_controls_on_next_select();
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

    min_edge_textbox_ = std::make_unique<DMTextBox>("Min room edge distance (px)", "");
    if (min_edge_textbox_) {
        min_edge_textbox_->set_on_height_changed([this]() {
            if (min_edge_widget_) {
                min_edge_widget_->mark_layout_dirty();
            }
        });
    }
    owned_widgets_.push_back(std::make_unique<MinEdgeWidget>(this));
    min_edge_widget_ = static_cast<MinEdgeWidget*>(owned_widgets_.back().get());

    owned_widgets_.push_back(std::make_unique<ValidationSummaryWidget>(this));
    validation_widget_ = static_cast<ValidationSummaryWidget*>(owned_widgets_.back().get());

    Rows rows;
    rows.push_back(Row{add_widget, reload_widget});
    rows.push_back(Row{list_widget_});
    rows.push_back(Row{preview_widget_});
    rows.push_back(Row{min_edge_widget_});
    rows.push_back(Row{validation_widget_});
    set_rows(rows);
    sync_min_edge_textbox();

    set_close_button_on_left(true);
    set_close_button_enabled(true);

    set_on_close([this]() {
        if (rooms_list_container_) {
            rooms_list_container_->close();
        }
        if (layer_controls_container_) {
            layer_controls_container_->close();
        }
    });
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
    sync_min_edge_textbox();
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
    sync_min_edge_textbox();
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

}

void MapLayersPanel::show_room_list() {
    notify_side_panel(SidePanel::RoomsList);
}

void MapLayersPanel::select_room(const std::string& room_key) {
    pending_room_selection_ = room_key;
    if (on_configure_room_) {
        on_configure_room_(room_key);
    }
}

void MapLayersPanel::hide_details_panel() {
    notify_side_panel(SidePanel::RoomsList);
}

void MapLayersPanel::set_on_configure_room(std::function<void(const std::string&)> cb) {
    on_configure_room_ = std::move(cb);
}

void MapLayersPanel::set_on_layer_selected(std::function<void(int)> cb) {
    on_layer_selected_ = std::move(cb);
}

void MapLayersPanel::set_side_panel_callback(std::function<void(SidePanel)> cb) {
    side_panel_callback_ = std::move(cb);
}

void MapLayersPanel::force_layer_controls_on_next_select() {
    force_layer_controls_on_select_ = true;
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
    if (embedded_mode_) {
        if (embedded_bounds_.w > 0 && embedded_bounds_.h > 0) {
            set_rect(embedded_bounds_);
        }
        update_embedded_layout_constraints();
    } else {
        target_body_height_ = 0;
        set_available_height_override(-1);
        set_visible_height(default_visible_height_);
    }
}

void MapLayersPanel::set_embedded_bounds(const SDL_Rect& bounds) {
    embedded_bounds_ = bounds;
    if (embedded_mode_) {
        set_rect(bounds);
        update_embedded_layout_constraints();
    }
}

void MapLayersPanel::update_embedded_layout_constraints() {
    if (!embedded_mode_) {
        target_body_height_ = 0;
        set_available_height_override(-1);
        set_visible_height(default_visible_height_);
        return;
    }
    if (embedded_bounds_.w <= 0 || embedded_bounds_.h <= 0) {
        target_body_height_ = 0;
        set_available_height_override(-1);
        set_visible_height(default_visible_height_);
        return;
    }
    const int padding = DMSpacing::panel_padding();
    const int header_h = show_header() ? DMButton::height() : 0;
    const int header_gap = show_header() ? DMSpacing::header_gap() : 0;
    int available = embedded_bounds_.h - (padding * 2 + header_h + header_gap);
    if (available < 0) {
        available = 0;
    }
    target_body_height_ = available;
    set_visible_height(available);
    set_available_height_override(available);
}

void MapLayersPanel::update(const Input& input, int screen_w, int screen_h) {
    if (!is_visible()) {
        return;
    }
    if (data_dirty_) {
        rebuild_layers();
        data_dirty_ = false;
    }
    if (validation_dirty_) {
        validate_layers();
    }
    update_min_edge_note();
    DockableCollapsible::update(input, screen_w, screen_h);
    if (validation_dirty_) {
        validate_layers();
    }
    if (pending_save_ && !validation_has_errors_) {
        pending_save_ = false;
        perform_save();
    }
}

bool MapLayersPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) {
        return false;
    }
    return DockableCollapsible::handle_event(e);
}

void MapLayersPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }
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
        }
        if (on_layer_selected_) {
            on_layer_selected_(-1);
        }
        recalculate_dependency_highlights();
        force_layer_controls_on_select_ = false;
        notify_side_panel(SidePanel::RoomsList);
        return;
    }

    const int previous_selection = selected_layer_index_;
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
        force_layer_controls_on_select_ = false;
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
    if (on_layer_selected_) {
        on_layer_selected_(selected_layer_index_);
    }
    const bool notify_controls = force_layer_controls_on_select_ || selected_layer_index_ != previous_selection;
    force_layer_controls_on_select_ = false;
    recalculate_dependency_highlights();
    if (notify_controls) {
        notify_side_panel(SidePanel::LayerControls);
    }
}

void MapLayersPanel::mark_dirty(bool trigger_preview) {
    data_dirty_ = true;
    validation_dirty_ = true;
    if (trigger_preview && preview_widget_) {
        preview_widget_->mark_dirty();
    }
}

void MapLayersPanel::mark_clean() {
    data_dirty_ = false;
    validation_dirty_ = false;
}

void MapLayersPanel::rebuild_layers() {
    sync_min_edge_textbox();
    const nlohmann::json& layers = controller_ ? controller_->layers() : layers_array();
    rebuild_layer_rows_from_json(layers);

    if (selected_layer_index_ >= static_cast<int>(layer_rows_.size())) {
        selected_layer_index_ = layer_rows_.empty() ? -1 : layer_rows_.back().index;
    }

    update_layer_row_geometry();
    validation_dirty_ = true;
    validate_layers();

    if (selected_layer_index_ >= 0) {
        select_layer(selected_layer_index_);
    } else {
        apply_dependency_highlights();
        update_preview_state();
    }

    if (preview_widget_) {
        preview_widget_->mark_dirty();
    }
}

void MapLayersPanel::rebuild_layer_rows_from_json(const nlohmann::json& layers) {
    layer_rows_.clear();
    hovered_delete_layer_index_ = -1;
    if (!layers.is_array()) {
        return;
    }
    layer_rows_.reserve(layers.size());

    for (std::size_t i = 0; i < layers.size(); ++i) {
        LayerRow row;
        row.index = static_cast<int>(i);
        row.rect = SDL_Rect{0, 0, 0, 0};
        row.invalid = false;
        row.warning = false;
        row.dependency_highlight = false;
        row.deletable = (i != 0);

        const auto& layer_json = layers[i];
        std::string name;
        if (layer_json.is_object()) {
            name = layer_json.value("name", std::string());
        }
        if (name.empty()) {
            name = "Layer " + std::to_string(i);
        }
        row.name = std::move(name);

        if (layer_json.is_object()) {
            int room_count = 0;
            std::string first_room_name;
            const auto rooms_it = layer_json.find("rooms");
            if (rooms_it != layer_json.end() && rooms_it->is_array()) {
                room_count = static_cast<int>(rooms_it->size());
                if (!rooms_it->empty()) {
                    const auto& first_entry = (*rooms_it)[0];
                    if (first_entry.is_object()) {
                        first_room_name = first_entry.value("name", std::string());
                    } else if (first_entry.is_string()) {
                        first_room_name = first_entry.get<std::string>();
                    }
                }
            }

            const int min_rooms = layer_json.value("min_rooms", -1);
            const int max_rooms = layer_json.value("max_rooms", -1);

            std::ostringstream summary;
            if (room_count <= 0) {
                summary << "No rooms configured";
            } else {
                summary << room_count << (room_count == 1 ? " room" : " rooms");
            }

            if (min_rooms >= 0 || max_rooms >= 0) {
                int derived_min = std::max(0, min_rooms);
                int derived_max = std::max(derived_min, max_rooms);
                summary << " • target " << derived_min << "-" << derived_max;
            }

            if (i == 0) {
                if (!first_room_name.empty()) {
                    summary << " • spawn: " << first_room_name;
                } else {
                    summary << " • spawn";
                }
            }

            row.summary = summary.str();
        } else {
            row.summary = "Layer data missing";
        }

        layer_rows_.push_back(std::move(row));
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
        const int available_height = std::max(0, row.rect.h - padding * 2);
        const int button_size = std::max(0, std::min(kLayerDeleteButtonSize, available_height));
        if (!row.deletable) {
            row.delete_button_rect = SDL_Rect{row.rect.x + row.rect.w, row.rect.y, 0, 0};
        } else if (button_size > 0) {
            const int button_x = std::max(row.rect.x + padding, row.rect.x + row.rect.w - padding - button_size);
            const int button_y = row.rect.y + (row.rect.h - button_size) / 2;
            row.delete_button_rect = SDL_Rect{button_x, button_y, button_size, button_size};
        } else {
            row.delete_button_rect = SDL_Rect{row.rect.x + row.rect.w, row.rect.y, 0, 0};
        }
        y += kRowHeight + gap;
    }
}

int MapLayersPanel::list_height_for_width(int w) const {
    const int padding = DMSpacing::small_gap();
    const int gap = DMSpacing::small_gap();
    int base_total = padding * 2;
    if (!layer_rows_.empty()) {
        base_total += static_cast<int>(layer_rows_.size()) * kRowHeight;
        if (layer_rows_.size() > 1) {
            base_total += static_cast<int>(layer_rows_.size() - 1) * gap;
        }
    } else {
        base_total = kMinimumListHeight;
    }

    int required = base_total;
    if (target_body_height_ > 0) {
        const int row_gap = DMSpacing::item_gap();
        int rows_present = 0;
        int other_heights = 0;

        rows_present += 1;
        other_heights += DMButton::height();

        rows_present += 1;

        if (preview_widget_) {
            rows_present += 1;
            other_heights += preview_widget_->height_for_width(w);
        }
        if (min_edge_widget_) {
            rows_present += 1;
            other_heights += min_edge_widget_->height_for_width(w);
        }
        if (validation_widget_) {
            rows_present += 1;
            other_heights += validation_summary_height(w);
        }

        const int gap_total = std::max(0, rows_present - 1) * row_gap;
        const int needed = target_body_height_ - (other_heights + gap_total);
        if (needed > required) {
            required = needed;
        }
    }

    return std::max(kMinimumListHeight, required);
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
        const std::string message = "No layers configured. Add or duplicate a layer to begin.";
        SDL_Point size = MeasureLabelText(label_style, message);
        int text_x = area.x + padding;
        int text_y = area.y + padding;
        if (size.y < area.h) {
            text_y = area.y + (area.h - size.y) / 2;
        }
        DrawLabelText(renderer, message, text_x, text_y, label_style);
        return;
    }

    const DMLabelStyle& summary_style = summary_label_style();
    const SDL_Color selection_outline = DMStyles::AccentButton().border;
    const SDL_Color dependency_outline = DMStyles::AccentButton().hover_bg;
    const int accent_width = 4;

    for (const auto& row : layer_rows_) {
        SDL_Rect rect = row.rect;
        if (rect.w <= 0 || rect.h <= 0) {
            continue;
        }

        const bool selected = (row.index == selected_layer_index_);
        const bool hovered = (row.index == hovered_layer_index_);
        const bool dependency = row.dependency_highlight;
        const bool dragging = dragging_layer_active_ && row.index == dragging_layer_index_;

        SDL_Color fill = severity_fill(row.invalid, row.warning, selected);
        if (dependency && !selected) {
            fill = lighten(fill, 0.12f);
        }
        if (hovered && !selected) {
            fill = lighten(fill, 0.08f);
        }
        if (dragging && drag_moved_) {
            fill = lighten(fill, 0.18f);
        }

        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &rect);

        SDL_Color outline = severity_color(row.invalid, row.warning, selected || dependency);
        if (selected) {
            outline = selection_outline;
        } else if (dependency && !row.invalid && !row.warning) {
            outline = dependency_outline;
        } else if (hovered) {
            outline = lighten(outline, 0.2f);
        }
        SDL_SetRenderDrawColor(renderer, outline.r, outline.g, outline.b, outline.a);
        SDL_RenderDrawRect(renderer, &rect);

        SDL_Rect accent{rect.x, rect.y, accent_width, rect.h};
        SDL_Color accent_color = outline;
        if (selected) {
            accent_color = DMStyles::AccentButton().bg;
        } else if (row.invalid) {
            accent_color = error_color();
        } else if (row.warning) {
            accent_color = warning_color();
        } else if (dependency) {
            accent_color = dependency_outline;
        }
        SDL_SetRenderDrawColor(renderer, accent_color.r, accent_color.g, accent_color.b, accent_color.a);
        SDL_RenderFillRect(renderer, &accent);

        const int text_x = rect.x + accent_width + padding;
        const int text_y = rect.y + padding;
        DrawLabelText(renderer, row.name, text_x, text_y, label_style);

        if (!row.summary.empty()) {
            SDL_Point summary_size = MeasureLabelText(summary_style, row.summary);
            int summary_y = rect.y + rect.h - summary_size.y - padding;
            DrawLabelText(renderer, row.summary, text_x, summary_y, summary_style);
        }

        SDL_Rect delete_rect = row.delete_button_rect;
        if (delete_rect.w > 0 && delete_rect.h > 0) {
            const bool delete_hovered = (hovered_delete_layer_index_ == row.index);
            SDL_Color delete_border = error_color();
            SDL_Color delete_fill = darken(delete_border, 0.35f);
            if (delete_hovered) {
                delete_fill = lighten(delete_border, 0.25f);
            } else if (selected) {
                delete_fill = lighten(delete_fill, 0.12f);
            }

            SDL_SetRenderDrawColor(renderer, delete_fill.r, delete_fill.g, delete_fill.b, delete_fill.a);
            SDL_RenderFillRect(renderer, &delete_rect);

            SDL_Color delete_outline = delete_border;
            if (delete_hovered) {
                delete_outline = lighten(delete_outline, 0.1f);
            }
            SDL_SetRenderDrawColor(renderer, delete_outline.r, delete_outline.g, delete_outline.b, delete_outline.a);
            SDL_RenderDrawRect(renderer, &delete_rect);

            const int cross_pad = std::max(3, delete_rect.w / 4);
            SDL_Color cross_color{255, 255, 255, 255};
            SDL_SetRenderDrawColor(renderer, cross_color.r, cross_color.g, cross_color.b, cross_color.a);
            SDL_RenderDrawLine(renderer, delete_rect.x + cross_pad, delete_rect.y + cross_pad, delete_rect.x + delete_rect.w - cross_pad - 1, delete_rect.y + delete_rect.h - cross_pad - 1);
            SDL_RenderDrawLine(renderer, delete_rect.x + delete_rect.w - cross_pad - 1, delete_rect.y + cross_pad, delete_rect.x + cross_pad, delete_rect.y + delete_rect.h - cross_pad - 1);
        }

        const std::string level = std::string{"Lvl "} + std::to_string(row.index);
        SDL_Point level_size = MeasureLabelText(summary_style, level);
        int level_right_edge = rect.x + rect.w - padding;
        if (delete_rect.w > 0) {
            level_right_edge = delete_rect.x - padding;
        }
        level_right_edge = std::max(level_right_edge, text_x + level_size.x);
        int level_x = level_right_edge - level_size.x;
        int level_y = rect.y + padding;
        DrawLabelText(renderer, level, level_x, level_y, summary_style);

        if (row.invalid || row.warning) {
            SDL_Color dot = row.invalid ? error_color() : warning_color();
            int badge_right = level_x - padding / 2;
            int badge_x = std::max(text_x, badge_right - 8);
            SDL_Rect badge{badge_x, rect.y + rect.h / 2 - 4, 8, 8};
            SDL_SetRenderDrawColor(renderer, dot.r, dot.g, dot.b, dot.a);
            SDL_RenderFillRect(renderer, &badge);
        }
    }

    if (dragging_layer_active_ && drag_moved_) {
        int slot = std::clamp(drop_target_slot_, 0, static_cast<int>(layer_rows_.size()));
        int indicator_y = 0;
        if (slot < static_cast<int>(layer_rows_.size())) {
            indicator_y = layer_rows_[slot].rect.y;
        } else if (!layer_rows_.empty()) {
            indicator_y = layer_rows_.back().rect.y + layer_rows_.back().rect.h;
        }
        SDL_Rect drop_rect{area.x + padding, indicator_y - kDropIndicatorThickness / 2,
                           area.w - padding * 2, kDropIndicatorThickness};
        SDL_Color drop_color = DMStyles::AccentButton().bg;
        SDL_SetRenderDrawColor(renderer, drop_color.r, drop_color.g, drop_color.b, drop_color.a);
        SDL_RenderFillRect(renderer, &drop_rect);
    }
}

void MapLayersPanel::on_layers_list_mouse_down(int index, int mouse_y) {
    if (index == 0) {
        select_layer(index);
        dragging_layer_active_ = false;
        drag_moved_ = false;
        dragging_layer_index_ = -1;
        dragging_start_slot_ = -1;
        drop_target_slot_ = -1;
        drag_start_mouse_y_ = mouse_y;
        return;
    }
    dragging_layer_active_ = true;
    drag_moved_ = false;
    dragging_layer_index_ = index;
    dragging_start_slot_ = find_visual_position(index);
    drop_target_slot_ = dragging_start_slot_;
    drag_start_mouse_y_ = mouse_y;
    if (index >= 0) {
        select_layer(index);
    }
}

void MapLayersPanel::on_layers_list_mouse_motion(int mouse_y, Uint32 buttons) {
    if (!dragging_layer_active_) {
        return;
    }
    if ((buttons & SDL_BUTTON_LMASK) == 0) {
        cancel_drag();
        return;
    }
    if (!drag_moved_ && std::abs(mouse_y - drag_start_mouse_y_) > 4) {
        drag_moved_ = true;
    }
    if (!drag_moved_) {
        return;
    }
    drop_target_slot_ = drop_slot_for_position(mouse_y);
}

void MapLayersPanel::on_layers_list_mouse_up(int mouse_y, Uint8 button) {
    if (!dragging_layer_active_) {
        if (button == SDL_BUTTON_LEFT && hovered_layer_index_ >= 0) {
            select_layer(hovered_layer_index_);
        }
        return;
    }

    const bool was_dragging = drag_moved_;
    const int start_slot = dragging_start_slot_;
    const int original_index = dragging_layer_index_;
    int target_slot = drop_target_slot_;

    dragging_layer_active_ = false;
    drag_moved_ = false;
    dragging_layer_index_ = -1;
    dragging_start_slot_ = -1;
    drop_target_slot_ = -1;

    if (button != SDL_BUTTON_LEFT) {
        return;
    }

    if (!was_dragging || start_slot < 0) {
        if (hovered_layer_index_ >= 0) {
            select_layer(hovered_layer_index_);
        } else if (original_index >= 0) {
            select_layer(original_index);
        }
        return;
    }

    if (layer_rows_.empty()) {
        return;
    }

    if (target_slot < 0) {
        target_slot = start_slot;
    }

    if (target_slot == start_slot || target_slot == start_slot + 1) {
        select_layer(original_index);
        return;
    }

    int to_slot = target_slot;
    if (to_slot > start_slot) {
        to_slot -= 1;
    }
    to_slot = std::clamp(to_slot, 0, static_cast<int>(layer_rows_.size()) - 1);
    if (to_slot == 0 && static_cast<int>(layer_rows_.size()) > 1) {
        to_slot = 1;
    }

    bool changed = false;
    if (controller_) {
        changed = controller_->reorder_layer(start_slot, to_slot);
    } else {
        nlohmann::json& layers = layers_array();
        if (layers.is_array() && !layers.empty() && start_slot >= 0 &&
            start_slot < static_cast<int>(layers.size()) && to_slot >= 0 &&
            to_slot < static_cast<int>(layers.size())) {
            nlohmann::json layer = layers[start_slot];
            layers.erase(layers.begin() + start_slot);
            layers.insert(layers.begin() + to_slot, std::move(layer));
            changed = true;
        }
    }

    if (changed) {
        selected_layer_index_ = to_slot;
        mark_dirty(false);
        rebuild_layers();
        data_dirty_ = false;
        trigger_save();
    } else {
        if (original_index >= 0) {
            select_layer(original_index);
        }
    }
}

void MapLayersPanel::cancel_drag() {
    dragging_layer_active_ = false;
    drag_moved_ = false;
    dragging_layer_index_ = -1;
    dragging_start_slot_ = -1;
    drop_target_slot_ = -1;
}

int MapLayersPanel::drop_slot_for_position(int y) const {
    int slot = 0;
    for (const auto& row : layer_rows_) {
        int midpoint = row.rect.y + row.rect.h / 2;
        if (y < midpoint) {
            return slot;
        }
        ++slot;
    }
    return slot;
}

int MapLayersPanel::find_visual_position(int layer_index) const {
    for (std::size_t i = 0; i < layer_rows_.size(); ++i) {
        if (layer_rows_[i].index == layer_index) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void MapLayersPanel::apply_dependency_highlights() {
    std::unordered_set<int> highlight_set(dependency_highlight_layers_.begin(), dependency_highlight_layers_.end());
    for (auto& row : layer_rows_) {
        row.dependency_highlight = highlight_set.find(row.index) != highlight_set.end();
    }
}

bool MapLayersPanel::validate_layers() {
    if (!validation_dirty_) {
        return !validation_has_errors_;
    }

    validation_dirty_ = false;
    validation_lines_.clear();
    invalid_layers_.clear();
    warning_layers_.clear();
    dependency_highlight_layers_.clear();
    layer_dependency_children_.clear();
    layer_dependency_parents_.clear();
    root_room_summary_.clear();
    estimated_map_radius_ = 0.0;

    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    const nlohmann::json& layers = controller_ ? controller_->layers() : layers_array();
    if (!layers.is_array() || layers.empty()) {
        errors.emplace_back("At least one layer is required for map generation.");
        validation_has_errors_ = true;
        validation_has_warnings_ = false;
        update_validation_summary_layout(errors, warnings);
        apply_dependency_highlights();
        update_preview_state();
        return false;
    }

    const std::size_t layer_count = layers.size();
    layer_dependency_children_.assign(layer_count, {});
    layer_dependency_parents_.assign(layer_count, {});
    std::vector<std::vector<std::string>> required_children_names(layer_count);

    std::unordered_set<std::string> layer_names;
    std::unordered_map<std::string, int> room_to_layer;
    std::unordered_map<std::string, int> room_occurrences;

    for (std::size_t i = 0; i < layer_count; ++i) {
        const auto& layer = layers[i];
        const int index = static_cast<int>(i);
        bool layer_has_error = false;

        if (!layer.is_object()) {
            errors.emplace_back("Layer " + std::to_string(i) + " is not an object.");
            invalid_layers_.push_back(index);
            continue;
        }

        std::string layer_name = trimmed(layer.value("name", std::string()));
        const std::string layer_label =
            layer_name.empty() ? std::string("Layer ") + std::to_string(i) : layer_name;
        if (layer_name.empty()) {
            errors.emplace_back("Layer " + std::to_string(i) + " is missing a name.");
            invalid_layers_.push_back(index);
            layer_has_error = true;
        } else {
            if (!layer_names.insert(layer_name).second) {
                warnings.emplace_back("Layer name '" + layer_name + "' is duplicated.");
                warning_layers_.push_back(index);
            }
        }

        const auto rooms_it = layer.find("rooms");
        if (rooms_it == layer.end() || !rooms_it->is_array()) {
            errors.emplace_back("Layer '" + layer_label + "' is missing its room list.");
            invalid_layers_.push_back(index);
            continue;
        }

        const auto& rooms_array = *rooms_it;
        if (rooms_array.empty()) {
            if (i == 0) {
                errors.emplace_back("The spawn layer must include exactly one room candidate.");
                invalid_layers_.push_back(index);
                layer_has_error = true;
            } else {
                warnings.emplace_back("Layer '" + (layer_name.empty() ? std::string("Layer ") + std::to_string(i) : layer_name) + "' does not contain any rooms.");
                warning_layers_.push_back(index);
            }
        } else if (i == 0) {
            if (rooms_array.size() != 1) {
                errors.emplace_back("Layer '" + layer_label + "' must contain exactly one room candidate.");
                invalid_layers_.push_back(index);
                layer_has_error = true;
            } else {
                const auto& spawn_entry = rooms_array.front();
                if (!spawn_entry.is_object()) {
                    errors.emplace_back("Layer '" + layer_label + "' has an invalid spawn room entry.");
                    invalid_layers_.push_back(index);
                    layer_has_error = true;
                } else {
                    const int min_instances = spawn_entry.value("min_instances", 0);
                    const int max_instances = spawn_entry.value("max_instances", 0);
                    if (min_instances != 1 || max_instances != 1) {
                        errors.emplace_back("Layer '" + layer_label + "' spawn room must allow exactly one instance.");
                        invalid_layers_.push_back(index);
                        layer_has_error = true;
                    }
                }
            }
        }

        int min_rooms = layer.value("min_rooms", 0);
        int max_rooms = layer.value("max_rooms", 0);
        if (min_rooms < 0) {
            min_rooms = 0;
        }
        if (max_rooms < min_rooms) {
            errors.emplace_back("Layer '" + layer_label + "' has min_rooms greater than max_rooms.");
            invalid_layers_.push_back(index);
            layer_has_error = true;
        }
        if (i == 0 && (min_rooms != 1 || max_rooms != 1)) {
            errors.emplace_back("Layer '" + layer_label + "' must require exactly one room.");
            invalid_layers_.push_back(index);
            layer_has_error = true;
        }

        for (const auto& candidate : rooms_array) {
            if (!candidate.is_object()) {
                warnings.emplace_back("Layer '" + (layer_name.empty() ? std::string("Layer ") + std::to_string(i) : layer_name) + "' has a room entry that is not an object.");
                warning_layers_.push_back(index);
                continue;
            }
            std::string room_name = trimmed(candidate.value("name", std::string()));
            if (room_name.empty()) {
                errors.emplace_back("Layer '" + (layer_name.empty() ? std::string("Layer ") + std::to_string(i) : layer_name) + "' has a room with an empty name.");
                invalid_layers_.push_back(index);
                layer_has_error = true;
                continue;
            }
            room_occurrences[room_name]++;
            if (!room_to_layer.count(room_name)) {
                room_to_layer[room_name] = index;
            }
            if (i == 0 && root_room_summary_.empty()) {
                root_room_summary_ = room_name;
            }

            int max_instances = candidate.value("max_instances", 1);
            if (max_instances <= 0) {
                warnings.emplace_back("Room '" + room_name + "' in layer '" + (layer_name.empty() ? std::string("Layer ") + std::to_string(i) : layer_name) + "' has max_instances <= 0.");
                warning_layers_.push_back(index);
            }

            const auto required_it = candidate.find("required_children");
            if (required_it != candidate.end() && required_it->is_array()) {
                for (const auto& child_entry : *required_it) {
                    if (!child_entry.is_string()) {
                        warnings.emplace_back("Room '" + room_name + "' in layer '" + (layer_name.empty() ? std::string("Layer ") + std::to_string(i) : layer_name) + "' has a non-string required child entry.");
                        warning_layers_.push_back(index);
                        continue;
                    }
                    std::string child_name = trimmed(child_entry.get<std::string>());
                    if (child_name.empty()) {
                        warnings.emplace_back("Room '" + room_name + "' in layer '" + (layer_name.empty() ? std::string("Layer ") + std::to_string(i) : layer_name) + "' has a blank required child name.");
                        warning_layers_.push_back(index);
                        continue;
                    }
                    required_children_names[i].push_back(child_name);
                }
            }
        }

        if (layer_has_error) {
            invalid_layers_.push_back(index);
        }
    }

    auto deduplicate_indices = [](std::vector<int>& vec) {
        std::sort(vec.begin(), vec.end());
        vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
};

    deduplicate_indices(invalid_layers_);
    deduplicate_indices(warning_layers_);

    for (const auto& entry : room_occurrences) {
        if (entry.second > 1) {
            warnings.emplace_back("Room '" + entry.first + "' appears in multiple layers.");
        }
    }

    for (std::size_t i = 0; i < required_children_names.size(); ++i) {
        std::unordered_set<int> unique_children;
        for (const std::string& child_name : required_children_names[i]) {
            auto it = room_to_layer.find(child_name);
            const int index = static_cast<int>(i);
            const std::string layer_label = (i < layer_rows_.size() ? layer_rows_[i].name : std::string("Layer ") + std::to_string(i));
            if (it == room_to_layer.end()) {
                errors.emplace_back("Layer '" + layer_label + "' references unknown room '" + child_name + "'.");
                invalid_layers_.push_back(index);
                continue;
            }
            const int child_layer = it->second;
            if (child_layer <= static_cast<int>(i)) {
                errors.emplace_back("Layer '" + layer_label + "' requires '" + child_name + "' from an earlier or same layer.");
                invalid_layers_.push_back(index);
                continue;
            }
            if (unique_children.insert(child_layer).second) {
                layer_dependency_children_[i].push_back(child_layer);
                if (child_layer >= 0 && child_layer < static_cast<int>(layer_dependency_parents_.size())) {
                    layer_dependency_parents_[child_layer].push_back(static_cast<int>(i));
                }
            }
        }
    }

    deduplicate_indices(invalid_layers_);
    deduplicate_indices(warning_layers_);
    for (auto& children : layer_dependency_children_) {
        std::sort(children.begin(), children.end());
        children.erase(std::unique(children.begin(), children.end()), children.end());
    }
    for (auto& parents : layer_dependency_parents_) {
        std::sort(parents.begin(), parents.end());
        parents.erase(std::unique(parents.begin(), parents.end()), parents.end());
    }

    validation_has_errors_ = !errors.empty();
    validation_has_warnings_ = !warnings.empty();

    if (map_info_ && map_info_->is_object()) {
        estimated_map_radius_ = map_layers::map_radius_from_map_info(*map_info_);
    } else {
        estimated_map_radius_ = 0.0;
    }

    for (auto& row : layer_rows_) {
        row.invalid = std::binary_search(invalid_layers_.begin(), invalid_layers_.end(), row.index);
        row.warning = std::binary_search(warning_layers_.begin(), warning_layers_.end(), row.index);
    }

    update_validation_summary_layout(errors, warnings);
    recalculate_dependency_highlights();
    return !validation_has_errors_;
}

void MapLayersPanel::recalculate_dependency_highlights() {
    dependency_highlight_layers_.clear();
    const int layer_count = static_cast<int>(layer_dependency_children_.size());
    if (selected_layer_index_ < 0 || selected_layer_index_ >= layer_count) {
        apply_dependency_highlights();
        update_preview_state();
        return;
    }

    std::unordered_set<int> highlight_set;
    if (selected_layer_index_ >= 0 && selected_layer_index_ < layer_count) {
        for (int child : layer_dependency_children_[selected_layer_index_]) {
            if (child != selected_layer_index_) {
                highlight_set.insert(child);
            }
        }
    }
    if (selected_layer_index_ >= 0 &&
        selected_layer_index_ < static_cast<int>(layer_dependency_parents_.size())) {
        for (int parent : layer_dependency_parents_[selected_layer_index_]) {
            if (parent != selected_layer_index_) {
                highlight_set.insert(parent);
            }
        }
    }

    dependency_highlight_layers_.assign(highlight_set.begin(), highlight_set.end());
    std::sort(dependency_highlight_layers_.begin(), dependency_highlight_layers_.end());
    apply_dependency_highlights();
    update_preview_state();
}

void MapLayersPanel::perform_save() {
    bool ok = false;
    if (controller_) {
        ok = controller_->save();
    }
    if (!ok && on_save_) {
        ok = on_save_();
    }
    save_blocked_ = !ok;
}

void MapLayersPanel::update_preview_state() {
    if (!preview_widget_) {
        return;
    }
    preview_widget_->set_selected_layer(selected_layer_index_);
    preview_widget_->set_layer_diagnostics(invalid_layers_, warning_layers_, dependency_highlight_layers_);
}

int MapLayersPanel::validation_summary_height(int) const {
    if (validation_lines_.empty()) {
        return validation_label_style().font_size + DMSpacing::small_gap() * 2;
    }
    const int line_height = validation_label_style().font_size + DMSpacing::small_gap();
    return static_cast<int>(validation_lines_.size()) * line_height + DMSpacing::small_gap();
}

void MapLayersPanel::render_validation_summary(SDL_Renderer* renderer, const SDL_Rect& rect) const {
    if (!renderer) {
        return;
    }
    SDL_Rect area = rect;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 18, 26, 42, 230);
    SDL_RenderFillRect(renderer, &area);
    SDL_SetRenderDrawColor(renderer, DMStyles::Border().r, DMStyles::Border().g, DMStyles::Border().b, DMStyles::Border().a);
    SDL_RenderDrawRect(renderer, &area);

    int y = area.y + DMSpacing::small_gap();
    const DMLabelStyle base_style = validation_label_style();
    for (const auto& line : validation_lines_) {
        DMLabelStyle style = base_style;
        style.color = line.color;
        DrawLabelText(renderer, line.text, area.x + DMSpacing::small_gap(), y, style);
        y += base_style.font_size + DMSpacing::small_gap();
    }
}

void MapLayersPanel::update_validation_summary_layout(const std::vector<std::string>& errors,
                                                      const std::vector<std::string>& warnings) {
    validation_lines_.clear();

    if (!errors.empty()) {
        validation_lines_.push_back({"Resolve the highlighted issues before saving.", error_color()});
        const std::size_t limit = std::min<std::size_t>(errors.size(), 3);
        for (std::size_t i = 0; i < limit; ++i) {
            validation_lines_.push_back({"• " + errors[i], error_color()});
        }
        if (errors.size() > limit) {
            validation_lines_.push_back({"• " + std::to_string(errors.size() - limit) + " more issue(s)...", error_color()});
        }
    } else if (!warnings.empty()) {
        validation_lines_.push_back({"Warnings detected. Review before publishing.", warning_color()});
        const std::size_t limit = std::min<std::size_t>(warnings.size(), 3);
        for (std::size_t i = 0; i < limit; ++i) {
            validation_lines_.push_back({"• " + warnings[i], warning_color()});
        }
        if (warnings.size() > limit) {
            validation_lines_.push_back({"• " + std::to_string(warnings.size() - limit) + " additional warning(s)...",
                                        warning_color()});
        }
    } else {
        validation_lines_.push_back({"Layers ready. No validation issues detected.", success_color()});
    }

    if (!root_room_summary_.empty()) {
        validation_lines_.push_back({"Root room: " + root_room_summary_, info_color()});
    }

    if (estimated_map_radius_ > 0.0) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(0) << estimated_map_radius_;
        validation_lines_.push_back({"Estimated map radius ≈ " + oss.str(), info_color()});
    }

    if (save_blocked_) {
        validation_lines_.push_back({"Save paused until issues are resolved.", error_color()});
    }

    validation_lines_.push_back({"Tip: Drag layers to reorder. Use Duplicate to branch quickly.", info_color()});
}

void MapLayersPanel::trigger_save() {
    if (!validate_layers()) {
        pending_save_ = true;
        save_blocked_ = true;
        return;
    }
    pending_save_ = false;
    save_blocked_ = false;
    perform_save();
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

void MapLayersPanel::set_hovered_delete_layer(int index) {
    hovered_delete_layer_index_ = index;
}

void MapLayersPanel::on_delete_layer_clicked(int index) {
    if (delete_layer_at(index)) {
        hovered_layer_index_ = -1;
        hovered_delete_layer_index_ = -1;
    }
}

bool MapLayersPanel::delete_layer_at(int index) {
    if (index < 0) {
        return false;
    }
    if (index == 0) {
        return false;
    }

    bool removed = false;
    if (controller_) {
        removed = controller_->delete_layer(index);
    } else {
        nlohmann::json& layers = layers_array();
        if (layers.is_array() && index >= 0 && index < static_cast<int>(layers.size())) {
            layers.erase(layers.begin() + index);
            removed = true;
        }
    }

    if (!removed) {
        return false;
    }

    if (selected_layer_index_ == index) {
        selected_layer_index_ = -1;
    } else if (selected_layer_index_ > index) {
        --selected_layer_index_;
    }

    hovered_layer_index_ = -1;
    hovered_delete_layer_index_ = -1;

    mark_dirty();
    trigger_save();
    return true;
}

void MapLayersPanel::clear_hover() {
    hovered_layer_index_ = -1;
    hovered_delete_layer_index_ = -1;
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

void MapLayersPanel::sync_min_edge_textbox() {
    int value = map_layers::kDefaultMinEdgeDistance;
    if (controller_) {
        value = static_cast<int>(std::lround(controller_->min_edge_distance()));
    } else if (map_info_) {
        value = static_cast<int>(std::lround(map_layers::min_edge_distance_from_map_manifest(*map_info_)));
    }
    value = std::clamp(value, 0, static_cast<int>(map_layers::kMinEdgeDistanceMax));
    min_edge_value_ = value;
    last_valid_min_edge_text_ = std::to_string(value);
    if (min_edge_textbox_ && !min_edge_textbox_->is_editing()) {
        min_edge_textbox_->set_value(last_valid_min_edge_text_);
    }
    if (min_edge_widget_) {
        min_edge_widget_->mark_layout_dirty();
    }
}

bool MapLayersPanel::handle_min_edge_event(const SDL_Event& e) {
    if (!min_edge_textbox_) {
        return false;
    }
    const bool was_editing = min_edge_textbox_->is_editing();
    const bool changed = min_edge_textbox_->handle_event(e);
    const bool now_editing = min_edge_textbox_->is_editing();
    if (changed && now_editing) {
        on_min_edge_text_changed();
    }
    if (was_editing && !now_editing) {
        on_min_edge_edit_finished();
    }
    return changed || was_editing != now_editing;
}

void MapLayersPanel::on_min_edge_text_changed() {
    if (!min_edge_textbox_) {
        return;
    }
    clear_min_edge_note();
    if (min_edge_widget_) {
        min_edge_widget_->mark_layout_dirty();
    }
}

void MapLayersPanel::on_min_edge_edit_finished() {
    if (!min_edge_textbox_) {
        return;
    }
    std::string raw_value = min_edge_textbox_->value();
    std::string trimmed_value = trimmed(raw_value);
    if (trimmed_value.empty()) {
        min_edge_textbox_->set_value(last_valid_min_edge_text_);
        show_min_edge_note("Enter a number between 0 and 10000.", error_color());
        if (min_edge_widget_) {
            min_edge_widget_->mark_layout_dirty();
        }
        return;
    }
    int parsed = 0;
    const char* begin = trimmed_value.data();
    const char* end = begin + trimmed_value.size();
    auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc() || ptr != end) {
        min_edge_textbox_->set_value(last_valid_min_edge_text_);
        show_min_edge_note("Enter a number between 0 and 10000.", error_color());
        if (min_edge_widget_) {
            min_edge_widget_->mark_layout_dirty();
        }
        return;
    }
    int clamped = std::clamp(parsed, 0, static_cast<int>(map_layers::kMinEdgeDistanceMax));
    if (clamped != min_edge_value_) {
        apply_min_edge_value(clamped);
    }
    std::string normalized = std::to_string(clamped);
    if (normalized != raw_value) {
        min_edge_textbox_->set_value(normalized);
    }
    last_valid_min_edge_text_ = normalized;
    if (clamped != parsed) {
        show_min_edge_note("Value clamped to 0–10000.", warning_color());
    } else {
        clear_min_edge_note();
    }
    if (min_edge_widget_) {
        min_edge_widget_->mark_layout_dirty();
    }
}

void MapLayersPanel::apply_min_edge_value(int value) {
    value = std::clamp(value, 0, static_cast<int>(map_layers::kMinEdgeDistanceMax));
    if (value == min_edge_value_) {
        return;
    }
    min_edge_value_ = value;
    last_valid_min_edge_text_ = std::to_string(value);
    if (controller_) {
        controller_->set_min_edge_distance(static_cast<double>(value));
    } else if (map_info_ && map_info_->is_object()) {
        (*map_info_)["map_layers_settings"]["min_edge_distance"] = value;
        mark_dirty();
    }
    if (preview_widget_) {
        preview_widget_->mark_dirty();
    }
    validation_dirty_ = true;
    clear_min_edge_note();
    trigger_save();
}

void MapLayersPanel::show_min_edge_note(const std::string& message, SDL_Color color) {
    min_edge_note_ = message;
    min_edge_note_color_ = color;
    min_edge_note_expiration_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    if (min_edge_widget_) {
        min_edge_widget_->mark_layout_dirty();
    }
}

void MapLayersPanel::clear_min_edge_note() {
    if (min_edge_note_.empty()) {
        return;
    }
    min_edge_note_.clear();
    min_edge_note_color_ = DMStyles::Label().color;
    if (min_edge_widget_) {
        min_edge_widget_->mark_layout_dirty();
    }
}

void MapLayersPanel::update_min_edge_note() {
    if (min_edge_note_.empty()) {
        return;
    }
    if (min_edge_note_expiration_ == std::chrono::steady_clock::time_point{}) {
        return;
    }
    if (std::chrono::steady_clock::now() >= min_edge_note_expiration_) {
        clear_min_edge_note();
        min_edge_note_expiration_ = std::chrono::steady_clock::time_point{};
    }
}

bool MapLayersPanel::min_edge_note_visible() const {
    return !min_edge_note_.empty();
}

int MapLayersPanel::min_edge_widget_height_for_width(int w) const {
    const int padding = DMSpacing::small_gap();
    const int inner_width = std::max(0, w - padding * 2);
    int height = padding * 2;
    if (min_edge_textbox_) {
        height += min_edge_textbox_->preferred_height(inner_width);
    } else {
        height += DMTextBox::height();
    }
    if (min_edge_note_visible()) {
        height += DMStyles::Label().font_size + DMSpacing::small_gap();
    }
    return height;
}

void MapLayersPanel::layout_min_edge_input(const SDL_Rect& bounds) {
    if (!min_edge_textbox_) {
        return;
    }
    const int padding = DMSpacing::small_gap();
    const int inner_width = std::max(0, bounds.w - padding * 2);
    const int box_height = min_edge_textbox_->preferred_height(inner_width);
    SDL_Rect text_rect{bounds.x + padding, bounds.y + padding, inner_width, box_height};
    min_edge_textbox_->set_rect(text_rect);
    if (min_edge_note_visible()) {
        int note_y = text_rect.y + text_rect.h + DMSpacing::small_gap();
        min_edge_note_rect_ = SDL_Rect{text_rect.x, note_y, inner_width, DMStyles::Label().font_size};
    } else {
        min_edge_note_rect_ = SDL_Rect{text_rect.x, text_rect.y + text_rect.h, inner_width, 0};
    }
}

void MapLayersPanel::render_min_edge_input(SDL_Renderer* renderer, const SDL_Rect&) const {
    if (min_edge_textbox_) {
        min_edge_textbox_->render(renderer);
    }
    if (min_edge_note_visible() && min_edge_note_rect_.w > 0) {
        DMLabelStyle style = DMStyles::Label();
        style.color = min_edge_note_color_;
        DrawLabelText(renderer, min_edge_note_, min_edge_note_rect_.x, min_edge_note_rect_.y, style);
    }
}
