#include "map_layers_panel.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
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
constexpr int kRowHeight = 52;
constexpr int kDropIndicatorThickness = 3;

SDL_Color mix_color(SDL_Color a, SDL_Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto mix = [t](Uint8 x, Uint8 y) {
        return static_cast<Uint8>(std::lround((1.0f - t) * x + t * y));
    };
    return SDL_Color{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

SDL_Color lighten(SDL_Color c, float amount) { return mix_color(c, SDL_Color{255, 255, 255, c.a}, amount); }
SDL_Color darken(SDL_Color c, float amount) { return mix_color(c, SDL_Color{0, 0, 0, c.a}, amount); }

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
                for (const auto& row : owner_->layer_rows_) {
                    if (SDL_PointInRect(&p, &row.rect) == SDL_TRUE) {
                        hit_index = row.index;
                        break;
                    }
                }

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
                        owner_->on_layers_list_mouse_down(hit_index, p.y);
                        return true;
                    }
                    return false;
                }

                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
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

MapLayersPanel::MapLayersPanel(int x, int y)
    : DockableCollapsible("Map Layers", true, x, y) {
    add_layer_button_ = std::make_unique<DMButton>("Add Layer", &DMStyles::CreateButton(), 140, DMButton::height());
    duplicate_layer_button_ =
        std::make_unique<DMButton>("Duplicate Layer", &DMStyles::ListButton(), 160, DMButton::height());
    delete_layer_button_ = std::make_unique<DMButton>("Delete Layer", &DMStyles::DeleteButton(), 140, DMButton::height());
    reload_button_ = std::make_unique<DMButton>("Reload", &DMStyles::WarnButton(), 120, DMButton::height());
    layer_name_box_ = std::make_unique<DMTextBox>("Layer name", "");
    layer_name_box_raw_ = layer_name_box_.get();

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

    owned_widgets_.push_back(std::make_unique<ButtonWidget>(duplicate_layer_button_.get(), [this]() {
        if (!controller_ && (!map_info_ || !map_info_->is_object())) {
            return;
        }
        const int source = selected_layer_index_ >= 0 ? selected_layer_index_ : 0;
        int duplicated_index = -1;
        if (controller_) {
            duplicated_index = controller_->duplicate_layer(source);
        } else {
            nlohmann::json& layers = layers_array();
            if (layers.is_array() && source >= 0 && source < static_cast<int>(layers.size())) {
                nlohmann::json copy = layers[source];
                if (!copy.is_object()) {
                    copy = nlohmann::json::object();
                }
                std::string base_name = copy.value("name", std::string{"Layer "} + std::to_string(source));
                if (base_name.empty()) {
                    base_name = std::string{"Layer "} + std::to_string(source);
                }
                auto name_exists = [&](const std::string& candidate) {
                    return std::any_of(layers.begin(), layers.end(), [&](const nlohmann::json& entry) {
                        return entry.is_object() && entry.value("name", std::string()) == candidate;
                    });
                };
                std::string candidate = base_name + " Copy";
                int suffix = 2;
                while (name_exists(candidate)) {
                    candidate = base_name + " Copy " + std::to_string(suffix++);
                }
                copy["name"] = candidate;
                if (!copy.contains("rooms") || !copy["rooms"].is_array()) {
                    copy["rooms"] = nlohmann::json::array();
                }
                const int insert_index = std::min(source + 1, static_cast<int>(layers.size()));
                layers.insert(layers.begin() + insert_index, std::move(copy));
                duplicated_index = insert_index;
            }
        }
        if (duplicated_index >= 0) {
            mark_dirty();
            select_layer(duplicated_index);
            trigger_save();
        }
    }));
    Widget* duplicate_widget = owned_widgets_.back().get();

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
            trigger_save();
        }
    }));
    Widget* delete_widget = owned_widgets_.back().get();

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

    owned_widgets_.push_back(std::make_unique<ValidationSummaryWidget>(this));
    validation_widget_ = static_cast<ValidationSummaryWidget*>(owned_widgets_.back().get());

    Rows rows;
    rows.push_back(Row{add_widget, duplicate_widget, delete_widget, reload_widget});
    rows.push_back(Row{list_widget_});
    rows.push_back(Row{preview_widget_});
    rows.push_back(Row{name_widget});
    rows.push_back(Row{validation_widget_});
    set_rows(rows);

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
    set_close_button_enabled(!embedded_mode_);
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
    if (validation_dirty_) {
        validate_layers();
    }
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
        if (on_layer_selected_) {
            on_layer_selected_(-1);
        }
        recalculate_dependency_highlights();
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
    if (on_layer_selected_) {
        on_layer_selected_(selected_layer_index_);
    }
    recalculate_dependency_highlights();
    notify_side_panel(SidePanel::LayerControls);
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
    } else if (layer_name_box_raw_) {
        layer_name_box_raw_->set_value("");
        current_layer_name_.clear();
        apply_dependency_highlights();
        update_preview_state();
    }

    if (preview_widget_) {
        preview_widget_->mark_dirty();
    }
}

void MapLayersPanel::rebuild_layer_rows_from_json(const nlohmann::json& layers) {
    layer_rows_.clear();
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

            if (i == 0 && !first_room_name.empty()) {
                summary << " • root: " << first_room_name;
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

        const std::string level = std::string{"Lvl "} + std::to_string(row.index);
        SDL_Point level_size = MeasureLabelText(summary_style, level);
        int level_x = rect.x + rect.w - level_size.x - padding;
        int level_y = rect.y + padding;
        DrawLabelText(renderer, level, level_x, level_y, summary_style);

        if (row.invalid || row.warning) {
            SDL_Color dot = row.invalid ? error_color() : warning_color();
            SDL_Rect badge{level_x - 12, rect.y + rect.h / 2 - 4, 8, 8};
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
            errors.emplace_back("Layer '" + (layer_name.empty() ? std::string("Layer ") + std::to_string(i) : layer_name) +
                                "' is missing its room list.");
            invalid_layers_.push_back(index);
            continue;
        }

        const auto& rooms_array = *rooms_it;
        if (rooms_array.empty()) {
            if (i == 0) {
                errors.emplace_back("The root layer must include at least one room candidate.");
                invalid_layers_.push_back(index);
                layer_has_error = true;
            } else {
                warnings.emplace_back("Layer '" + (layer_name.empty() ? std::string("Layer ") + std::to_string(i) : layer_name) +
                                      "' does not contain any rooms.");
                warning_layers_.push_back(index);
            }
        }

        int min_rooms = layer.value("min_rooms", 0);
        int max_rooms = layer.value("max_rooms", 0);
        if (min_rooms < 0) {
            min_rooms = 0;
        }
        if (max_rooms < min_rooms) {
            errors.emplace_back("Layer '" + (layer_name.empty() ? std::string("Layer ") + std::to_string(i) : layer_name) +
                                "' has min_rooms greater than max_rooms.");
            invalid_layers_.push_back(index);
            layer_has_error = true;
        }

        for (const auto& candidate : rooms_array) {
            if (!candidate.is_object()) {
                warnings.emplace_back("Layer '" + (layer_name.empty() ? std::string("Layer ") + std::to_string(i) : layer_name) +
                                      "' has a room entry that is not an object.");
                warning_layers_.push_back(index);
                continue;
            }
            std::string room_name = trimmed(candidate.value("name", std::string()));
            if (room_name.empty()) {
                errors.emplace_back("Layer '" + (layer_name.empty() ? std::string("Layer ") + std::to_string(i) : layer_name) +
                                    "' has a room with an empty name.");
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
                warnings.emplace_back("Room '" + room_name + "' in layer '" +
                                      (layer_name.empty() ? std::string("Layer ") + std::to_string(i) : layer_name) +
                                      "' has max_instances <= 0.");
                warning_layers_.push_back(index);
            }

            const auto required_it = candidate.find("required_children");
            if (required_it != candidate.end() && required_it->is_array()) {
                for (const auto& child_entry : *required_it) {
                    if (!child_entry.is_string()) {
                        warnings.emplace_back("Room '" + room_name + "' in layer '" +
                                              (layer_name.empty() ? std::string("Layer ") + std::to_string(i) : layer_name) +
                                              "' has a non-string required child entry.");
                        warning_layers_.push_back(index);
                        continue;
                    }
                    std::string child_name = trimmed(child_entry.get<std::string>());
                    if (child_name.empty()) {
                        warnings.emplace_back("Room '" + room_name + "' in layer '" +
                                              (layer_name.empty() ? std::string("Layer ") + std::to_string(i) : layer_name) +
                                              "' has a blank required child name.");
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
            const std::string layer_label = (i < layer_rows_.size() ? layer_rows_[i].name
                                                                     : std::string("Layer ") + std::to_string(i));
            if (it == room_to_layer.end()) {
                errors.emplace_back("Layer '" + layer_label + "' references unknown room '" + child_name + "'.");
                invalid_layers_.push_back(index);
                continue;
            }
            const int child_layer = it->second;
            if (child_layer <= static_cast<int>(i)) {
                errors.emplace_back("Layer '" + layer_label + "' requires '" + child_name +
                                    "' from an earlier or same layer.");
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
        validation_dirty_ = true;
        mark_dirty();
        trigger_save();
    } else {
        // Restore previous value if rename failed.
        if (layer_name_box_raw_) {
            layer_name_box_raw_->set_value(current_layer_name_);
        }
    }
}

void MapLayersPanel::trigger_save() {
    commit_layer_name_edit();
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

