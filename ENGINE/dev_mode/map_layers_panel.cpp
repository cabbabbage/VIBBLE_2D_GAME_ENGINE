#include "map_layers_panel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

#include <SDL.h>
#include <SDL_log.h>
#include <SDL_ttf.h>

#include <nlohmann/json.hpp>

#include "dm_styles.hpp"
#include "draw_utils.hpp"
#include "map_layers_controller.hpp"
#include "map_generation/map_layers_geometry.hpp"
#include "utils/input.hpp"

namespace {

constexpr double kTau = 6.28318530717958647692;

SDL_Color mix_color(SDL_Color a, SDL_Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto mix = [t](Uint8 x, Uint8 y) {
        return static_cast<Uint8>(std::lround((1.0f - t) * x + t * y));
    };
    return SDL_Color{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

SDL_Color lighten(SDL_Color c, float amount) {
    return mix_color(c, SDL_Color{255, 255, 255, c.a}, amount);
}

SDL_Color darken(SDL_Color c, float amount) {
    return mix_color(c, SDL_Color{0, 0, 0, c.a}, amount);
}

void draw_text(SDL_Renderer* renderer, const std::string& text, int x, int y, const DMLabelStyle& style) {
    if (!renderer || text.empty()) return;
    TTF_Font* font = style.open_font();
    if (!font) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), style.color);
    if (!surf) {
        TTF_CloseFont(font);
        return;
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (tex) {
        SDL_Rect dst{x, y, surf->w, surf->h};
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
    TTF_CloseFont(font);
}

void draw_circle(SDL_Renderer* renderer, int cx, int cy, int radius, SDL_Color color, int thickness = 2) {
    if (!renderer || radius <= 0 || thickness <= 0) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    const int segments = std::max(32, radius * 4);
    const double step = kTau / static_cast<double>(segments);
    for (int layer = 0; layer < thickness; ++layer) {
        int r = std::max(1, radius - layer);
        int prev_x = cx + r;
        int prev_y = cy;
        for (int i = 1; i <= segments; ++i) {
            double angle = step * static_cast<double>(i);
            int x = cx + static_cast<int>(std::lround(std::cos(angle) * r));
            int y = cy + static_cast<int>(std::lround(std::sin(angle) * r));
            SDL_RenderDrawLine(renderer, prev_x, prev_y, x, y);
            prev_x = x;
            prev_y = y;
        }
    }
}

void fill_circle(SDL_Renderer* renderer, int cx, int cy, int radius, SDL_Color color) {
    if (!renderer || radius <= 0) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int y = -radius; y <= radius; ++y) {
        int dx = static_cast<int>(std::sqrt(static_cast<double>(radius * radius - y * y)));
        SDL_RenderDrawLine(renderer, cx - dx, cy + y, cx + dx, cy + y);
    }
}

}  // namespace

class MapLayersPanel::PreviewWidget : public Widget {
public:
    explicit PreviewWidget(MapLayersPanel* owner) : owner_(owner) {}

    void set_rect(const SDL_Rect& r) override {
        rect_ = r;
        if (owner_) {
            owner_->preview_rect_ = rect_;
            owner_->preview_center_ = SDL_Point{rect_.x + rect_.w / 2, rect_.y + rect_.h / 2};
            owner_->preview_dirty_ = true;
            owner_->recalculate_preview_scale();
        }
    }

    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int w) const override {
        const int min_h = 280;
        const int max_h = 480;
        if (w <= min_h) return min_h;
        if (w >= max_h) return max_h;
        return w;
    }

    bool handle_event(const SDL_Event& e) override {
        if (!owner_) return false;
        const bool pointer_event =
            (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP);
        if (!pointer_event) {
            return false;
        }

        SDL_Point p{0, 0};
        if (e.type == SDL_MOUSEMOTION) {
            p.x = e.motion.x;
            p.y = e.motion.y;
        } else {
            p.x = e.button.x;
            p.y = e.button.y;
        }

        const bool inside = SDL_PointInRect(&p, &rect_) == SDL_TRUE;
        if (!inside) {
            if (e.type == SDL_MOUSEMOTION) {
                owner_->clear_hover_state();
            }
            return false;
        }

        const int layer_hit = owner_->hit_test_layer(p.x, p.y);
        const std::string room_hit = owner_->hit_test_room(p.x, p.y);
        if (e.type == SDL_MOUSEMOTION) {
            owner_->update_hover_state(layer_hit, room_hit);
            return (layer_hit >= 0 || !room_hit.empty());
        }

        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            owner_->handle_preview_click(layer_hit, room_hit);
            return true;
        }

        return false;
    }

    void render(SDL_Renderer* renderer) const override {
        if (owner_) {
            owner_->render_preview(renderer);
        }
    }

    bool wants_full_row() const override { return true; }

    void invalidate() const { request_geometry_update(); }

private:
    MapLayersPanel* owner_ = nullptr;
    SDL_Rect rect_{0, 0, 0, 0};
};

class MapLayersPanel::DetailsWidget : public Widget {
public:
    explicit DetailsWidget(MapLayersPanel* owner) : owner_(owner) {}

    void set_rect(const SDL_Rect& r) override {
        rect_ = r;
        if (owner_) {
            owner_->details_rect_ = rect_;
            owner_->apply_details_bounds();
        }
    }

    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int) const override { return 300; }

    bool handle_event(const SDL_Event& e) override {
        if (!owner_ || !owner_->details_container_) return false;
        return owner_->details_container_->handle_event(e);
    }

    void render(SDL_Renderer* renderer) const override {
        if (owner_) {
            owner_->render_details(renderer);
        }
    }

    bool wants_full_row() const override { return true; }

private:
    MapLayersPanel* owner_ = nullptr;
    SDL_Rect rect_{0, 0, 0, 0};
};

MapLayersPanel::MapLayersPanel(int x, int y)
    : DockableCollapsible("Map Layers", true, x, y) {
    owned_widgets_.push_back(std::make_unique<DetailsWidget>(this));
    details_widget_ = static_cast<DetailsWidget*>(owned_widgets_.back().get());

    layout_rows();
    ensure_details_container();

    set_visible(false);
    set_expanded(true);
}

MapLayersPanel::~MapLayersPanel() = default;

void MapLayersPanel::layout_rows() {
    Rows rows;
    // In embedded mode, omit the internal preview; a floating preview panel will be used instead.
    if (!embedded_mode_ && preview_widget_) {
        rows.push_back(Row{preview_widget_});
    }
    if (details_widget_) {
        rows.push_back(Row{details_widget_});
    }
    set_rows(rows);
}

void MapLayersPanel::ensure_details_container() {
    if (details_container_) {
        return;
    }

    details_container_ = std::make_unique<SlidingWindowContainer>();
    details_container_->set_header_text("Details");
    details_container_->set_header_visible(true);
    details_container_->set_scrollbar_visible(true);
    details_container_->set_layout_function([this](const SlidingWindowContainer::LayoutContext& ctx) {
        const int padding = DMSpacing::panel_padding();
        const int gap = DMSpacing::item_gap();
        const int small_gap = DMSpacing::small_gap();
        const int content_width = std::max(0, ctx.content_width - padding * 2);
        const int content_left = ctx.content_x + padding;
        int cursor = ctx.content_top - ctx.scroll_value + padding;

        if (details_mode_ == DetailsMode::RoomList) {
            // Build once per layout pass to reflect latest model state
            if (details_widgets_.empty()) {
                build_room_list_widgets();
            }
            for (const auto& w : details_widgets_) {
                if (!w) continue;
                SDL_Rect rect{content_left, cursor, content_width, w->height_for_width(content_width)};
                w->set_rect(rect);
                cursor += w->height_for_width(content_width) + gap;
            }
            // Then the room buttons list
            for (auto& entry : room_buttons_) {
                if (!entry.button) continue;
                SDL_Rect rect{content_left, cursor, content_width, DMButton::height()};
                entry.button->set_rect(rect);
                cursor += DMButton::height() + gap;
            }
            cursor += padding;
        } else {
            // Layer/Room details interactive UI
            if (details_mode_ == DetailsMode::LayerDetails) {
                if (details_widgets_.empty()) {
                    build_layer_details_widgets();
                }
                for (const auto& w : details_widgets_) {
                    if (!w) continue;
                    SDL_Rect rect{content_left, cursor, content_width, w->height_for_width(content_width)};
                    w->set_rect(rect);
                    cursor += w->height_for_width(content_width) + gap;
                }
                cursor += padding;
            } else {
                detail_line_rects_.clear();
                if (!detail_lines_.empty()) {
                    const int line_height = DMStyles::Label().font_size + small_gap;
                    for (const auto& line : detail_lines_) {
                        (void)line;
                        SDL_Rect line_rect{content_left, cursor, content_width, line_height};
                        detail_line_rects_.push_back(line_rect);
                        cursor += line_height;
                    }
                } else {
                    cursor += DMStyles::Label().font_size + padding;
                }
                cursor += padding;
            }
        }

        return cursor + ctx.gap;
    });

    details_container_->set_render_function([this](SDL_Renderer* renderer) {
        if (!renderer) return;
        if (details_mode_ == DetailsMode::RoomList) {
            // Render any control widgets
            for (const auto& w : details_widgets_) {
                if (w) w->render(renderer);
            }
            for (auto& entry : room_buttons_) {
                if (entry.button) entry.button->render(renderer);
            }
        } else {
            if (details_mode_ == DetailsMode::LayerDetails) {
                for (const auto& w : details_widgets_) {
                    if (w) w->render(renderer);
                }
            } else {
                const DMLabelStyle& style = DMStyles::Label();
                for (size_t i = 0; i < detail_lines_.size(); ++i) {
                    if (i >= detail_line_rects_.size()) break;
                    const SDL_Rect& rect = detail_line_rects_[i];
                    draw_text(renderer, detail_lines_[i], rect.x, rect.y, style);
                }
            }
        }
    });

    details_container_->set_event_function([this](const SDL_Event& e) {
        return this->handle_details_event(e);
    });

    details_container_->set_on_close([this]() {
        details_mode_ = DetailsMode::None;
    });
}

void MapLayersPanel::set_map_info(nlohmann::json* map_info, const std::string& map_path) {
    map_info_ = map_info;
    map_path_ = map_path;
    if (controller_) {
        controller_->bind(map_info_, map_path_);
    }
    preview_dirty_ = true;
    rebuild_visuals();
    open_room_list();
}

void MapLayersPanel::set_on_save(SaveCallback cb) {
    on_save_ = std::move(cb);
}

void MapLayersPanel::set_controller(std::shared_ptr<MapLayersController> controller) {
    controller_ = std::move(controller);
    if (controller_) {
        controller_->add_listener([this]() {
            this->rebuild_visuals();
            this->update_details_container();
        });
        if (map_info_) {
            controller_->bind(map_info_, map_path_);
        }
    }
}

void MapLayersPanel::set_header_visibility_callback(std::function<void(bool)> cb) {
    header_visibility_callback_ = std::move(cb);
    notify_header_visibility();
}

void MapLayersPanel::set_work_area(const SDL_Rect& bounds) {
    work_area_ = bounds;
    DockableCollapsible::set_work_area(bounds);
    apply_details_bounds();
}

void MapLayersPanel::open() {
    DockableCollapsible::open();
    set_visible(true);
    notify_header_visibility();
    open_room_list();
}

void MapLayersPanel::close() {
    DockableCollapsible::close();
    notify_header_visibility();
}

bool MapLayersPanel::is_visible() const {
    return DockableCollapsible::is_visible();
}

bool MapLayersPanel::room_config_visible() const {
    return details_mode_ == DetailsMode::RoomDetails;
}

void MapLayersPanel::hide_main_container() {
    set_visible(false);
    notify_header_visibility();
}

void MapLayersPanel::show_room_list() { open_room_list(); }

void MapLayersPanel::select_room(const std::string& room_key) { open_room_details(room_key); }

void MapLayersPanel::set_embedded_mode(bool embedded) {
    if (embedded_mode_ == embedded) return;
    embedded_mode_ = embedded;
    set_floatable(!embedded_mode_);
    set_show_header(!embedded_mode_);
    set_close_button_enabled(!embedded_mode_);
    if (embedded_mode_) {
        set_visible(true);
    }
    // Rebuild rows to omit the internal preview when embedded.
    layout_rows();
    notify_header_visibility();
}

void MapLayersPanel::set_embedded_bounds(const SDL_Rect& bounds) {
    embedded_bounds_ = bounds;
    if (embedded_mode_) {
        set_rect(bounds);
    }
    apply_details_bounds();
}

void MapLayersPanel::update(const Input& input, int screen_w, int screen_h) {
    screen_w_ = screen_w;
    screen_h_ = screen_h;

    DockableCollapsible::update(input, screen_w, screen_h);

    if (!is_visible()) {
        return;
    }

    if (preview_dirty_) {
        rebuild_visuals();
    }

    ensure_details_container();
    apply_details_bounds();

    if (details_container_ && details_container_->is_visible()) {
        details_container_->update(input, screen_w, screen_h);
    }
}

bool MapLayersPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) return false;
    return DockableCollapsible::handle_event(e);
}

void MapLayersPanel::render(SDL_Renderer* renderer) const {
    DockableCollapsible::render(renderer);
}

bool MapLayersPanel::is_point_inside(int x, int y) const {
    return DockableCollapsible::is_point_inside(x, y);
}

void MapLayersPanel::select_layer(int index) {
    selected_layer_index_ = index;
    open_layer_details(index);
}

void MapLayersPanel::mark_dirty(bool trigger_preview) {
    dirty_ = true;
    if (trigger_preview) {
        preview_dirty_ = true;
    }
}

void MapLayersPanel::mark_clean() {
    dirty_ = false;
}

void MapLayersPanel::rebuild_visuals() {
    preview_dirty_ = false;
    layer_visuals_.clear();
    max_visual_radius_ = 1.0;

    if (!map_info_) {
        preview_scale_ = 1.0;
        return;
    }

    const nlohmann::json& layers = layers_array();
    const nlohmann::json* rooms_info = rooms_data();

    if (!layers.is_array() || layers.empty()) {
        preview_scale_ = 1.0;
        return;
    }

    const map_layers::LayerRadiiResult radii = map_layers::compute_layer_radii(layers, rooms_info);
    max_visual_radius_ = std::max(1.0, radii.map_radius);

    layer_visuals_.reserve(layers.size());

    for (size_t i = 0; i < layers.size(); ++i) {
        const auto& layer_json = layers[i];
        if (!layer_json.is_object()) continue;

        LayerVisual visual;
        visual.index = static_cast<int>(i);
        visual.name = layer_json.value("name", std::string("Layer ") + std::to_string(i + 1));
        if (i < radii.layer_radii.size()) {
            visual.radius = radii.layer_radii[i];
        }
        visual.color = layer_color(visual.index);

        const auto rooms_it = layer_json.find("rooms");
        if (rooms_it != layer_json.end() && rooms_it->is_array()) {
            const auto& rooms_array = *rooms_it;
            const size_t room_count = rooms_array.size();
            const double angle_step = room_count > 0 ? kTau / static_cast<double>(room_count) : 0.0;
            size_t room_idx = 0;
            for (const auto& candidate : rooms_array) {
                if (!candidate.is_object()) continue;
                RoomVisual room;
                room.layer_index = visual.index;
                room.key = candidate.value("name", std::string());
                room.display_name = display_name_for_room(room.key);
                room.radius = visual.radius;
                room.extent = map_layers::room_extent_from_rooms_data(rooms_info, room.key);
                room.angle = angle_step > 0.0 ? angle_step * static_cast<double>(room_idx) : 0.0;
                room.position.x = static_cast<float>(std::cos(room.angle) * room.radius);
                room.position.y = static_cast<float>(std::sin(room.angle) * room.radius);
                visual.rooms.push_back(room);
                ++room_idx;
            }
        }

        layer_visuals_.push_back(std::move(visual));
    }

    recalculate_preview_scale();
}

void MapLayersPanel::refresh_room_list() {
    room_buttons_.clear();
    if (!map_info_) {
        return;
    }

    const nlohmann::json* rooms_info = rooms_data();
    if (!rooms_info || !rooms_info->is_object()) {
        return;
    }

    std::vector<std::string> keys;
    keys.reserve(rooms_info->size());
    for (auto it = rooms_info->begin(); it != rooms_info->end(); ++it) {
        keys.push_back(it.key());
    }
    std::sort(keys.begin(), keys.end());

    room_buttons_.reserve(keys.size());
    for (const auto& key : keys) {
        std::string label = display_name_for_room(key);
        if (label.empty()) label = key;
        RoomButtonEntry entry;
        entry.key = key;
        entry.button = std::make_unique<DMButton>(label, &DMStyles::ListButton(), 200, DMButton::height());
        room_buttons_.push_back(std::move(entry));
    }
}

void MapLayersPanel::refresh_layer_details() {
    detail_lines_.clear();
    if (selected_layer_index_ < 0) {
        return;
    }
    auto it = std::find_if(layer_visuals_.begin(), layer_visuals_.end(), [&](const LayerVisual& v) {
        return v.index == selected_layer_index_;
    });
    if (it == layer_visuals_.end()) {
        return;
    }

    const LayerVisual& layer = *it;
    detail_lines_.push_back("Layer: " + layer.name);
    detail_lines_.push_back("Radius: " + std::to_string(static_cast<int>(layer.radius)));
    detail_lines_.push_back("Rooms: " + std::to_string(layer.rooms.size()));
    if (!layer.rooms.empty()) {
        detail_lines_.push_back(" ");
        detail_lines_.push_back("Rooms in this layer:");
        for (const auto& room : layer.rooms) {
            detail_lines_.push_back(" - " + (room.display_name.empty() ? room.key : room.display_name));
        }
    }
}

void MapLayersPanel::refresh_room_details() {
    detail_lines_.clear();
    if (selected_room_key_.empty()) {
        return;
    }
    const nlohmann::json* entry = room_entry(selected_room_key_);
    if (!entry || !entry->is_object()) {
        detail_lines_.push_back("Room: " + selected_room_key_);
        detail_lines_.push_back("No metadata available.");
        return;
    }

    std::string name = entry->value("name", selected_room_key_);
    detail_lines_.push_back("Room: " + name);
    detail_lines_.push_back("Key: " + selected_room_key_);

    const std::array<const char*, 4> dims{{"min_width", "max_width", "min_height", "max_height"}};
    for (const char* dim : dims) {
        if (entry->contains(dim) && ((*entry)[dim].is_number_float() || (*entry)[dim].is_number_integer())) {
            double value = (*entry)[dim].get<double>();
            detail_lines_.push_back(std::string(dim) + ": " + std::to_string(static_cast<int>(std::round(value))));
        }
    }

    if (entry->contains("geometry")) {
        detail_lines_.push_back("Geometry: " + entry->value("geometry", std::string()));
    }
    if (entry->contains("tags") && (*entry)["tags"].is_array()) {
        std::ostringstream oss;
        oss << "Tags: ";
        bool first = true;
        for (const auto& tag : (*entry)["tags"]) {
            if (!tag.is_string()) continue;
            if (!first) oss << ", ";
            first = false;
            oss << tag.get<std::string>();
        }
        if (!first) {
            detail_lines_.push_back(oss.str());
        }
    }
}

void MapLayersPanel::recalculate_preview_scale() {
    preview_scale_ = compute_preview_scale();
}

double MapLayersPanel::compute_preview_scale() const {
    if (preview_rect_.w <= 0 || preview_rect_.h <= 0 || max_visual_radius_ <= 0.0) {
        return 1.0;
    }
    const int padding = DMSpacing::panel_padding();
    int usable = std::max(1, std::min(preview_rect_.w, preview_rect_.h) / 2 - padding);
    if (usable <= 0) usable = 1;
    return static_cast<double>(usable) / std::max(1.0, max_visual_radius_);
}

void MapLayersPanel::update_details_container() {
    ensure_details_container();

    // Always reset per-mode widgets so layout can rebuild fresh
    clear_detail_ui();

    switch (details_mode_) {
        case DetailsMode::RoomList:
            refresh_room_list();
            details_container_->set_header_text("Rooms");
            break;
        case DetailsMode::LayerDetails:
            refresh_layer_details();
            if (selected_layer_index_ >= 0) {
                const auto it = std::find_if(layer_visuals_.begin(), layer_visuals_.end(), [&](const LayerVisual& v) {
                    return v.index == selected_layer_index_;
                });
                if (it != layer_visuals_.end()) {
                    details_container_->set_header_text(it->name);
                } else {
                    details_container_->set_header_text("Layer");
                }
            } else {
                details_container_->set_header_text("Layer");
            }
            break;
        case DetailsMode::RoomDetails:
            refresh_room_details();
            if (!selected_room_key_.empty()) {
                std::string label = display_name_for_room(selected_room_key_);
                if (label.empty()) label = selected_room_key_;
                details_container_->set_header_text(label);
            } else {
                details_container_->set_header_text("Room");
            }
            break;
        case DetailsMode::None:
            details_container_->set_header_text("Details");
            break;
    }

    const bool visible = details_mode_ != DetailsMode::None;
    details_container_->set_visible(visible);
    if (visible) {
        details_container_->request_layout();
    }
}

void MapLayersPanel::apply_details_bounds() {
    if (!details_container_) return;
    if (details_rect_.w > 0 && details_rect_.h > 0) {
        details_container_->set_panel_bounds_override(details_rect_);
    } else {
        details_container_->clear_panel_bounds_override();
    }
}

void MapLayersPanel::open_room_list() {
    details_mode_ = DetailsMode::RoomList;
    selected_room_key_.clear();
    update_details_container();
}

void MapLayersPanel::open_layer_details(int layer_index) {
    selected_layer_index_ = layer_index;
    details_mode_ = (layer_index >= 0) ? DetailsMode::LayerDetails : DetailsMode::RoomList;
    selected_room_key_.clear();
    update_details_container();
}

void MapLayersPanel::open_room_details(const std::string& room_key) {
    selected_room_key_ = room_key;
    if (!room_key.empty()) {
        details_mode_ = DetailsMode::RoomDetails;
    } else {
        details_mode_ = DetailsMode::RoomList;
    }
    update_details_container();
}

void MapLayersPanel::update_hover_state(int layer_index, const std::string& room_key) {
    bool changed = false;
    if (hovered_layer_index_ != layer_index) {
        hovered_layer_index_ = layer_index;
        changed = true;
    }
    if (hovered_room_key_ != room_key) {
        hovered_room_key_ = room_key;
        changed = true;
    }
    if (changed && preview_widget_) {
        preview_widget_->invalidate();
    }
}

void MapLayersPanel::clear_hover_state() {
    update_hover_state(-1, std::string());
}

void MapLayersPanel::handle_preview_click(int layer_index, const std::string& room_key) {
    if (!room_key.empty()) {
        open_room_details(room_key);
        return;
    }
    if (layer_index >= 0) {
        open_layer_details(layer_index);
        return;
    }
    open_room_list();
}

int MapLayersPanel::hit_test_layer(int x, int y) const {
    if (layer_visuals_.empty() || preview_rect_.w <= 0) {
        return -1;
    }
    double scale = preview_scale_;
    if (scale <= 0.0) {
        scale = compute_preview_scale();
    }
    if (scale <= 0.0) {
        return -1;
    }
    const double dx = static_cast<double>(x - preview_center_.x);
    const double dy = static_cast<double>(y - preview_center_.y);
    const double dist_pixels = std::sqrt(dx * dx + dy * dy);
    const double threshold = 14.0;
    for (const auto& layer : layer_visuals_) {
        const double radius_pixels = layer.radius * scale;
        if (std::abs(dist_pixels - radius_pixels) <= threshold) {
            return layer.index;
        }
    }
    return -1;
}

std::string MapLayersPanel::hit_test_room(int x, int y) const {
    if (layer_visuals_.empty() || preview_rect_.w <= 0) {
        return {};
    }
    double scale = preview_scale_;
    if (scale <= 0.0) {
        scale = compute_preview_scale();
    }
    if (scale <= 0.0) {
        return {};
    }
    const double px = static_cast<double>(x);
    const double py = static_cast<double>(y);
    const double base_radius = 12.0;
    for (const auto& layer : layer_visuals_) {
        for (const auto& room : layer.rooms) {
            const double rx = preview_center_.x + static_cast<double>(room.position.x) * scale;
            const double ry = preview_center_.y + static_cast<double>(room.position.y) * scale;
            const double dx = px - rx;
            const double dy = py - ry;
            const double dist = std::sqrt(dx * dx + dy * dy);
            const double room_radius = std::max(base_radius, room.extent * scale * 0.6);
            if (dist <= room_radius) {
                return room.key;
            }
        }
    }
    return {};
}

void MapLayersPanel::notify_header_visibility() const {
    if (header_visibility_callback_) {
        header_visibility_callback_(is_visible());
    }
}

const nlohmann::json& MapLayersPanel::layers_array() const {
    static const nlohmann::json kEmpty = nlohmann::json::array();
    if (!map_info_ || !map_info_->is_object()) return kEmpty;
    auto it = map_info_->find("map_layers");
    if (it == map_info_->end() || !it->is_array()) return kEmpty;
    return *it;
}

nlohmann::json& MapLayersPanel::layers_array() {
    if (!map_info_ || !map_info_->is_object()) {
        static nlohmann::json dummy = nlohmann::json::array();
        return dummy;
    }
    if (!map_info_->contains("map_layers")) {
        (*map_info_)["map_layers"] = nlohmann::json::array();
    }
    return (*map_info_)["map_layers"];
}

const nlohmann::json* MapLayersPanel::rooms_data() const {
    if (!map_info_ || !map_info_->is_object()) return nullptr;
    auto it = map_info_->find("rooms_data");
    if (it == map_info_->end() || !it->is_object()) return nullptr;
    return &(*it);
}

nlohmann::json* MapLayersPanel::room_entry(const std::string& key) {
    if (!map_info_ || !map_info_->is_object()) return nullptr;
    auto it = map_info_->find("rooms_data");
    if (it == map_info_->end() || !it->is_object()) return nullptr;
    auto entry_it = it->find(key);
    if (entry_it == it->end()) return nullptr;
    return &(*entry_it);
}

std::string MapLayersPanel::display_name_for_room(const std::string& key) const {
    const nlohmann::json* rooms_info = rooms_data();
    if (!rooms_info || !rooms_info->is_object()) return key;
    auto it = rooms_info->find(key);
    if (it == rooms_info->end() || !it->is_object()) return key;
    return it->value("name", key);
}

void MapLayersPanel::render_preview(SDL_Renderer* renderer) const {
    if (!renderer) return;

    SDL_Rect rect = preview_rect_;
    if (rect.w <= 0 || rect.h <= 0) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const SDL_Color bg = DMStyles::PanelBG();
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &rect);

    const SDL_Color border = DMStyles::Border();
    dm_draw::DrawRoundedOutline(renderer, rect, DMStyles::CornerRadius(), 1, border);

    if (layer_visuals_.empty() || max_visual_radius_ <= 0.0) {
        draw_text(renderer, "No layers configured.", rect.x + 16, rect.y + 16, DMStyles::Label());
        return;
    }

    preview_scale_ = compute_preview_scale();
    const SDL_Point center = preview_center_;

    const int hovered_layer = hovered_layer_index_;
    const std::string hovered_room = hovered_room_key_;

    for (const auto& layer : layer_visuals_) {
        SDL_Color color = layer.color;
        int thickness = 3;
        if (selected_layer_index_ == layer.index) {
            thickness = 6;
            color = lighten(color, 0.35f);
        }
        if (hovered_layer == layer.index && hovered_room.empty()) {
            color = lighten(color, 0.25f);
            thickness = std::max(thickness, 5);
        }
        const int radius_pixels = std::max(8, static_cast<int>(std::lround(layer.radius * preview_scale_)));
        draw_circle(renderer, center.x, center.y, radius_pixels, color, thickness);

        std::ostringstream oss;
        oss << layer.name;
        draw_text(renderer, oss.str(), center.x - radius_pixels + 8, rect.y + 8, DMStyles::Label());
    }

    const SDL_Color hover_fill = DMStyles::AccentButton().hover_bg;
    const SDL_Color selected_fill = DMStyles::AccentButton().bg;

    for (const auto& layer : layer_visuals_) {
        for (const auto& room : layer.rooms) {
            const int px = center.x + static_cast<int>(std::lround(room.position.x * preview_scale_));
            const int py = center.y + static_cast<int>(std::lround(room.position.y * preview_scale_));
            const double extent_pixels = std::max(8.0, room.extent * preview_scale_ * 0.75);
            const int radius_pixels = static_cast<int>(std::round(extent_pixels));

            SDL_Color outline = darken(layer.color, 0.15f);
            SDL_Color fill_color{0, 0, 0, 0};
            bool filled = false;

            if (!hovered_room.empty() && hovered_room == room.key) {
                fill_color = hover_fill;
                filled = true;
            }
            if (!selected_room_key_.empty() && selected_room_key_ == room.key) {
                fill_color = selected_fill;
                filled = true;
            }

            if (filled) {
                SDL_Color fill = fill_color;
                fill.a = 120;
                fill_circle(renderer, px, py, radius_pixels, fill);
            }
            draw_circle(renderer, px, py, radius_pixels, outline, filled ? 3 : 2);
        }
    }

    if (!hovered_room.empty()) {
        std::string label = display_name_for_room(hovered_room);
        if (label.empty()) label = hovered_room;
        draw_text(renderer, label, rect.x + 12, rect.y + rect.h - (DMStyles::Label().font_size + 12), DMStyles::Label());
    } else if (hovered_layer >= 0) {
        auto it = std::find_if(layer_visuals_.begin(), layer_visuals_.end(), [&](const LayerVisual& v) {
            return v.index == hovered_layer;
        });
        if (it != layer_visuals_.end()) {
            draw_text(renderer, it->name, rect.x + 12, rect.y + rect.h - (DMStyles::Label().font_size + 12), DMStyles::Label());
        }
    }
}

void MapLayersPanel::render_details(SDL_Renderer* renderer) const {
    if (!details_container_ || !details_container_->is_visible()) {
        return;
    }
    int sw = screen_w_ > 0 ? screen_w_ : (details_rect_.x + details_rect_.w);
    int sh = screen_h_ > 0 ? screen_h_ : (details_rect_.y + details_rect_.h);
    if (sw <= 0) sw = details_rect_.w;
    if (sh <= 0) sh = details_rect_.h;
    if (sw <= 0) sw = 1;
    if (sh <= 0) sh = 1;
    details_container_->render(renderer, sw, sh);
}

bool MapLayersPanel::handle_details_event(const SDL_Event& e) {
    if (!details_container_ || !details_container_->is_visible()) {
        return false;
    }

    bool used_any = false;
    // Always route to details widgets first (buttons, sliders, dropdowns)
    for (auto& w : details_widgets_) {
        if (!w) continue;
        bool used = w->handle_event(e);
        used_any = used_any || used;
        // If a slider was used and the event is a mouse release, commit changes
        if (used && details_mode_ == DetailsMode::LayerDetails) {
            if (e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEWHEEL || e.type == SDL_KEYUP) {
                // Apply slider counts to controller
                if (controller_ && selected_layer_index_ >= 0) {
                    for (const auto& row : candidate_rows_) {
                        if (row.count_slider) {
                            controller_->set_candidate_instance_count(selected_layer_index_, row.candidate_index, row.count_slider->displayed_value());
                        }
                    }
                }
            }
        }
    }

    if (details_mode_ == DetailsMode::RoomList) {
        for (auto& entry : room_buttons_) {
            if (!entry.button) continue;
            bool used = entry.button->handle_event(e);
            if (used && e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                open_room_details(entry.key);
            }
            used_any = used_any || used;
        }
    }

    return used_any;
}

SDL_Color MapLayersPanel::layer_color(int index) const {
    const double hue = std::fmod(static_cast<double>(index) * 0.135, 1.0);
    const double s = 0.6;
    const double v = 0.95;
    double h = hue * 6.0;
    const int i = static_cast<int>(std::floor(h)) % 6;
    const double f = h - std::floor(h);
    const double p = v * (1.0 - s);
    const double q = v * (1.0 - f * s);
    const double t = v * (1.0 - (1.0 - f) * s);
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    switch (i) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        case 5: default: r = v; g = p; b = q; break;
    }
    auto to_byte = [](double value) {
        value = std::clamp(value, 0.0, 1.0);
        return static_cast<Uint8>(std::lround(value * 255.0));
    };
    return SDL_Color{to_byte(r), to_byte(g), to_byte(b), 255};
}

void MapLayersPanel::clear_detail_ui() {
    details_widgets_.clear();
    candidate_rows_.clear();
}

void MapLayersPanel::build_room_list_widgets() {
    clear_detail_ui();
    // In embedded footer mode, global action buttons live in the floating preview panel.
    if (!embedded_mode_) {
        if (!add_layer_btn_) add_layer_btn_ = std::make_unique<DMButton>("Add Layer", &DMStyles::CreateButton(), 0, DMButton::height());
        if (!create_room_btn_) create_room_btn_ = std::make_unique<DMButton>("Create Room", &DMStyles::CreateButton(), 0, DMButton::height());
        if (!save_btn_) save_btn_ = std::make_unique<DMButton>("Save", &DMStyles::AccentButton(), 0, DMButton::height());
        if (!reload_btn_) reload_btn_ = std::make_unique<DMButton>("Reload", &DMStyles::ListButton(), 0, DMButton::height());

        details_widgets_.push_back(std::make_unique<ButtonWidget>(add_layer_btn_.get(), [this]() {
            if (controller_) {
                controller_->create_layer();
            }
        }));
        details_widgets_.push_back(std::make_unique<ButtonWidget>(create_room_btn_.get(), [this]() {
            this->create_new_room_entry();
        }));
        details_widgets_.push_back(std::make_unique<ButtonWidget>(save_btn_.get(), [this]() {
            bool ok = false;
            if (controller_) ok = controller_->save();
            if (!ok && on_save_) ok = on_save_();
            (void)ok;
        }));
        details_widgets_.push_back(std::make_unique<ButtonWidget>(reload_btn_.get(), [this]() {
            if (controller_) controller_->reload();
        }));
    }
}

void MapLayersPanel::build_layer_details_widgets() {
    clear_detail_ui();
    if (!controller_ || selected_layer_index_ < 0) return;
    const nlohmann::json* layer_json = controller_->layer(selected_layer_index_);
    std::string layer_name = layer_json && layer_json->is_object() ? layer_json->value("name", std::string{}) : std::string{};

    if (!layer_name_box_) layer_name_box_ = std::make_unique<DMTextBox>("Layer Name", layer_name);
    else layer_name_box_->set_value(layer_name);
    details_widgets_.push_back(std::make_unique<TextBoxWidget>(layer_name_box_.get(), true));

    // Add room dropdown + add button
    std::vector<std::string> options;
    if (controller_) {
        options = controller_->available_rooms();
    }
    if (!add_room_dropdown_) add_room_dropdown_ = std::make_unique<DMDropdown>("Add Room", options, 0);
    else {
        // Recreate dropdown to refresh options if sizes changed
        add_room_dropdown_ = std::make_unique<DMDropdown>("Add Room", options, 0);
    }
    if (!add_room_btn_) add_room_btn_ = std::make_unique<DMButton>("Add", &DMStyles::CreateButton(), 0, DMButton::height());

    details_widgets_.push_back(std::make_unique<DropdownWidget>(add_room_dropdown_.get()));
    details_widgets_.push_back(std::make_unique<ButtonWidget>(add_room_btn_.get(), [this]() {
        if (!controller_ || selected_layer_index_ < 0 || !add_room_dropdown_) return;
        auto options = controller_->available_rooms();
        int idx = add_room_dropdown_->pending_index();
        if (idx < 0 || idx >= static_cast<int>(options.size())) return;
        const std::string& room_key = options[idx];
        if (controller_->add_candidate(selected_layer_index_, room_key)) {
            rebuild_visuals();
            update_details_container();
        }
    }));

    // Candidates list with sliders and remove buttons
    if (layer_json && layer_json->is_object()) {
        const auto it = layer_json->find("rooms");
        if (it != layer_json->end() && it->is_array()) {
            int candidate_index = 0;
            for (const auto& candidate : *it) {
                if (!candidate.is_object()) { ++candidate_index; continue; }
                CandidateRowWidgets row;
                row.candidate_index = candidate_index;
                row.room_key = candidate.value("name", std::string{});
                int max_instances = 1;
                if (candidate.contains("max_instances")) {
                    if (candidate["max_instances"].is_number_integer()) max_instances = candidate["max_instances"].get<int>();
                }
                if (max_instances < 0) max_instances = 0;
                if (max_instances > 64) max_instances = 64;
                row.count_slider = std::make_unique<DMSlider>("Max Instances", 0, 64, max_instances);
                row.count_slider->set_defer_commit_until_unfocus(true);
                row.remove_btn = std::make_unique<DMButton>(std::string("Remove ") + row.room_key, &DMStyles::DeleteButton(), 0, DMButton::height());

                // Wrap and add to widget list
                details_widgets_.push_back(std::make_unique<SliderWidget>(row.count_slider.get()));
                details_widgets_.push_back(std::make_unique<ButtonWidget>(row.remove_btn.get(), [this, idx=candidate_index]() {
                    if (controller_ && selected_layer_index_ >= 0) {
                        controller_->remove_candidate(selected_layer_index_, idx);
                    }
                }));
                candidate_rows_.push_back(std::move(row));
                ++candidate_index;
            }
        }
    }

    // Save/Reload buttons at end (skip in embedded mode; live in preview)
    if (!embedded_mode_) {
        if (!save_btn_) save_btn_ = std::make_unique<DMButton>("Save", &DMStyles::AccentButton(), 0, DMButton::height());
        if (!reload_btn_) reload_btn_ = std::make_unique<DMButton>("Reload", &DMStyles::ListButton(), 0, DMButton::height());
        details_widgets_.push_back(std::make_unique<ButtonWidget>(save_btn_.get(), [this]() {
            apply_layer_rename_if_needed();
            bool ok = false;
            if (controller_) ok = controller_->save();
            if (!ok && on_save_) ok = on_save_();
            (void)ok;
        }));
        details_widgets_.push_back(std::make_unique<ButtonWidget>(reload_btn_.get(), [this]() {
            if (controller_) controller_->reload();
        }));
    }
}

void MapLayersPanel::apply_layer_rename_if_needed() {
    if (!controller_ || selected_layer_index_ < 0 || !layer_name_box_) return;
    const nlohmann::json* layer_json = controller_->layer(selected_layer_index_);
    if (!layer_json || !layer_json->is_object()) return;
    const std::string old_name = layer_json->value("name", std::string{});
    const std::string new_name = layer_name_box_->value();
    if (new_name != old_name && !new_name.empty()) {
        controller_->rename_layer(selected_layer_index_, new_name);
    }
}

void MapLayersPanel::create_new_room_entry() {
    if (!map_info_ || !map_info_->is_object()) return;
    nlohmann::json& rooms = (*map_info_)["rooms_data"];
    if (!rooms.is_object()) {
        rooms = nlohmann::json::object();
    }
    // Generate a unique key: NewRoom, NewRoom1, ...
    std::string base = "NewRoom";
    std::string key = base;
    int suffix = 1;
    while (rooms.contains(key)) {
        key = base + std::to_string(suffix++);
    }
    // Create a minimal entry
    rooms[key] = nlohmann::json{{"name", key}};
    // Refresh UI
    rebuild_visuals();
    update_details_container();
}
