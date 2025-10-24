#include "map_layers_preview_widget.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>
#include <SDL_ttf.h>

#include "dm_styles.hpp"
#include "draw_utils.hpp"
#include "dev_mode_color_utils.hpp"
#include "map_layers_controller.hpp"
#include "map_generation/map_layers_geometry.hpp"

namespace {
constexpr double kTau = 6.28318530717958647692;

void draw_text(SDL_Renderer* renderer, const std::string& text, int x, int y, const DMLabelStyle& style) {
    if (!renderer || text.empty()) {
        return;
    }
    TTF_Font* font = style.open_font();
    if (!font) {
        return;
    }
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
    if (!renderer || radius <= 0 || thickness <= 0) {
        return;
    }
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
    if (!renderer || radius <= 0) {
        return;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int y = -radius; y <= radius; ++y) {
        int dx = static_cast<int>(std::sqrt(static_cast<double>(radius * radius - y * y)));
        SDL_RenderDrawLine(renderer, cx - dx, cy + y, cx + dx, cy + y);
    }
}
}  // namespace

MapLayersPreviewWidget::MapLayersPreviewWidget() = default;

MapLayersPreviewWidget::~MapLayersPreviewWidget() { remove_listener(); }

void MapLayersPreviewWidget::set_map_info(nlohmann::json* map_info) {
    map_info_ = map_info;
    mark_dirty();
}

void MapLayersPreviewWidget::set_controller(std::shared_ptr<MapLayersController> controller) {
    if (controller_ == controller) {
        return;
    }
    remove_listener();
    controller_ = std::move(controller);
    ensure_listener();
    mark_dirty();
}

void MapLayersPreviewWidget::set_on_select_layer(SelectLayerCallback cb) {
    on_select_layer_ = std::move(cb);
}

void MapLayersPreviewWidget::set_on_select_room(SelectRoomCallback cb) {
    on_select_room_ = std::move(cb);
}

void MapLayersPreviewWidget::set_on_show_room_list(ShowRoomListCallback cb) {
    on_show_room_list_ = std::move(cb);
}

void MapLayersPreviewWidget::set_on_change(std::function<void()> cb) {
    on_change_ = std::move(cb);
}

void MapLayersPreviewWidget::set_selected_layer(int index) {
    if (selected_layer_index_ == index) {
        return;
    }
    selected_layer_index_ = index;
    mark_dirty();
}

void MapLayersPreviewWidget::set_layer_diagnostics(const std::vector<int>& invalid_layers,
                                                   const std::vector<int>& warning_layers,
                                                   const std::vector<int>& dependency_layers) {
    auto to_set = [](const std::vector<int>& values, std::unordered_set<int>& target) {
        target.clear();
        target.insert(values.begin(), values.end());
    };
    to_set(invalid_layers, invalid_layers_);
    to_set(warning_layers, warning_layers_);
    to_set(dependency_layers, dependency_layers_);
    mark_dirty();
}

void MapLayersPreviewWidget::set_rect(const SDL_Rect& r) {
    rect_ = r;
    preview_rect_ = rect_;
    preview_center_ = SDL_Point{rect_.x + rect_.w / 2, rect_.y + rect_.h / 2};
    recalculate_preview_scale();
}

int MapLayersPreviewWidget::height_for_width(int w) const {
    const int min_h = 280;
    const int max_h = 480;
    if (w <= min_h) {
        return min_h;
    }
    if (w >= max_h) {
        return max_h;
    }
    return w;
}

bool MapLayersPreviewWidget::handle_event(const SDL_Event& e) {
    ensure_latest_visuals();
    const bool pointer_event = (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP);
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
            clear_hover_state();
        }
        return false;
    }
    const int layer_hit = hit_test_layer(p.x, p.y);
    const std::string room_hit = hit_test_room(p.x, p.y);
    if (e.type == SDL_MOUSEMOTION) {
        update_hover_state(layer_hit, room_hit);
        return (layer_hit >= 0 || !room_hit.empty());
    }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        handle_preview_click(layer_hit, room_hit);
        return true;
    }
    return false;
}

void MapLayersPreviewWidget::render(SDL_Renderer* renderer) const {
    ensure_latest_visuals();
    render_preview(renderer);
}

void MapLayersPreviewWidget::mark_dirty() {
    dirty_ = true;
    request_geometry_update();
}

void MapLayersPreviewWidget::create_new_room_entry() {
    if (!map_info_ || !map_info_->is_object()) {
        return;
    }
    nlohmann::json& rooms = (*map_info_)["rooms_data"];
    if (!rooms.is_object()) {
        rooms = nlohmann::json::object();
    }
    std::string base = "NewRoom";
    std::string key = base;
    int suffix = 1;
    while (rooms.contains(key)) {
        key = base + std::to_string(suffix++);
    }
    rooms[key] = nlohmann::json{{"name", key}};
    mark_dirty();
    if (on_change_) {
        on_change_();
    }
}

void MapLayersPreviewWidget::rebuild_visuals() {
    dirty_ = false;
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
        if (!layer_json.is_object()) {
            continue;
        }
        LayerVisual visual;
        visual.index = static_cast<int>(i);
        visual.name = layer_json.value("name", std::string("Layer ") + std::to_string(i + 1));
        if (i < radii.layer_radii.size()) {
            visual.radius = radii.layer_radii[i];
        }
        visual.color = layer_color(visual.index);
        visual.min_rooms = layer_json.value("min_rooms", 0);
        visual.max_rooms = layer_json.value("max_rooms", 0);
        visual.invalid = invalid_layers_.find(visual.index) != invalid_layers_.end();
        visual.warning = warning_layers_.find(visual.index) != warning_layers_.end();
        visual.dependency = dependency_layers_.find(visual.index) != dependency_layers_.end();
        visual.selected = (visual.index == selected_layer_index_);

        const auto rooms_it = layer_json.find("rooms");
        if (rooms_it != layer_json.end() && rooms_it->is_array()) {
            const auto& rooms_array = *rooms_it;
            visual.room_count = static_cast<int>(rooms_array.size());
            const size_t room_count = rooms_array.size();
            const double angle_step = room_count > 0 ? kTau / static_cast<double>(room_count) : 0.0;
            size_t room_idx = 0;
            for (const auto& candidate : rooms_array) {
                if (!candidate.is_object()) {
                    continue;
                }
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
        visual.room_count = static_cast<int>(visual.rooms.size());
        layer_visuals_.push_back(std::move(visual));
    }
    recalculate_preview_scale();
}

void MapLayersPreviewWidget::ensure_latest_visuals() const {
    if (!dirty_) {
        return;
    }
    const_cast<MapLayersPreviewWidget*>(this)->rebuild_visuals();
}

void MapLayersPreviewWidget::recalculate_preview_scale() {
    preview_scale_ = compute_preview_scale();
}

double MapLayersPreviewWidget::compute_preview_scale() const {
    if (preview_rect_.w <= 0 || preview_rect_.h <= 0 || max_visual_radius_ <= 0.0) {
        return 1.0;
    }
    const int padding = DMSpacing::panel_padding();
    int usable = std::max(1, std::min(preview_rect_.w, preview_rect_.h) / 2 - padding);
    if (usable <= 0) {
        usable = 1;
    }
    return static_cast<double>(usable) / std::max(1.0, max_visual_radius_);
}

SDL_Color MapLayersPreviewWidget::layer_color(int index) const {
    static const SDL_Color palette[] = {
        {120, 180, 255, 255},
        {160, 220, 120, 255},
        {240, 180, 120, 255},
        {200, 140, 240, 255},
        {240, 120, 180, 255},
        {180, 180, 120, 255},
    };
    if (index < 0) {
        index = 0;
    }
    return palette[index % (sizeof(palette) / sizeof(palette[0]))];
}

std::string MapLayersPreviewWidget::display_name_for_room(const std::string& key) const {
    const nlohmann::json* rooms_info = rooms_data();
    if (!rooms_info || !rooms_info->is_object()) {
        return key;
    }
    auto it = rooms_info->find(key);
    if (it == rooms_info->end() || !it->is_object()) {
        return key;
    }
    return it->value("name", key);
}

const nlohmann::json& MapLayersPreviewWidget::layers_array() const {
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

const nlohmann::json* MapLayersPreviewWidget::rooms_data() const {
    if (!map_info_ || !map_info_->is_object()) {
        return nullptr;
    }
    auto it = map_info_->find("rooms_data");
    if (it == map_info_->end() || !it->is_object()) {
        return nullptr;
    }
    return &(*it);
}

void MapLayersPreviewWidget::update_hover_state(int layer_index, const std::string& room_key) {
    bool changed = false;
    if (hovered_layer_index_ != layer_index) {
        hovered_layer_index_ = layer_index;
        changed = true;
    }
    if (hovered_room_key_ != room_key) {
        hovered_room_key_ = room_key;
        changed = true;
    }
    if (changed) {
        request_geometry_update();
    }
}

void MapLayersPreviewWidget::clear_hover_state() {
    update_hover_state(-1, std::string());
}

void MapLayersPreviewWidget::handle_preview_click(int layer_index, const std::string& room_key) {
    if (!room_key.empty()) {
        if (on_select_room_) {
            on_select_room_(room_key);
        }
        return;
    }
    if (layer_index >= 0) {
        if (on_select_layer_) {
            on_select_layer_(layer_index);
        }
        return;
    }
    if (on_show_room_list_) {
        on_show_room_list_();
    }
}

int MapLayersPreviewWidget::hit_test_layer(int x, int y) const {
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

std::string MapLayersPreviewWidget::hit_test_room(int x, int y) const {
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

void MapLayersPreviewWidget::ensure_listener() {
    if (!controller_ || controller_listener_id_ != 0) {
        return;
    }
    controller_listener_id_ = controller_->add_listener([this]() { this->mark_dirty(); });
}

void MapLayersPreviewWidget::remove_listener() {
    if (controller_ && controller_listener_id_ != 0) {
        controller_->remove_listener(controller_listener_id_);
    }
    controller_listener_id_ = 0;
}

void MapLayersPreviewWidget::render_preview(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }
    SDL_Rect rect = preview_rect_;
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }

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

    const SDL_Point center = preview_center_;
    const int hovered_layer = hovered_layer_index_;
    const std::string hovered_room = hovered_room_key_;
    const_cast<MapLayersPreviewWidget*>(this)->preview_scale_ = compute_preview_scale();

    const SDL_Color invalid_color{214, 63, 87, 255};
    const SDL_Color warning_color{234, 179, 8, 255};
    const SDL_Color dependency_color{125, 200, 255, 255};
    const SDL_Color selection_outline = DMStyles::AccentButton().border;

    const DMLabelStyle base_label = DMStyles::Label();
    const int label_line_height = base_label.font_size + DMSpacing::small_gap();

    for (const auto& layer : layer_visuals_) {
        SDL_Color color = layer.color;
        if (layer.invalid) {
            color = invalid_color;
        } else if (layer.warning) {
            color = warning_color;
        } else if (layer.dependency) {
            color = lighten(color, 0.2f);
        }
        int thickness = layer.selected ? 6 : 3;
        if (hovered_layer == layer.index && hovered_room.empty()) {
            color = lighten(color, 0.25f);
            thickness = std::max(thickness, layer.selected ? 7 : 5);
        }
        const int radius_pixels = std::max(8, static_cast<int>(std::lround(layer.radius * preview_scale_)));
        draw_circle(renderer, center.x, center.y, radius_pixels, color, thickness);
        if (layer.selected) {
            draw_circle(renderer, center.x, center.y, radius_pixels + 4, selection_outline, 1);
        }

        std::ostringstream oss;
        oss << layer.name;
        oss << " • " << layer.room_count << (layer.room_count == 1 ? " room" : " rooms");
        oss << " • " << layer.min_rooms << "-" << layer.max_rooms << " total";
        if (layer.invalid) {
            oss << " • fix issues";
        } else if (layer.warning) {
            oss << " • review";
        }
        DMLabelStyle label_style = base_label;
        if (layer.invalid) {
            label_style.color = invalid_color;
        } else if (layer.warning) {
            label_style.color = warning_color;
        } else if (layer.selected) {
            label_style.color = lighten(label_style.color, 0.1f);
        }
        int text_x = rect.x + DMSpacing::small_gap();
        int text_y = rect.y + DMSpacing::small_gap() + layer.index * label_line_height;
        draw_text(renderer, oss.str(), text_x, text_y, label_style);
    }

    const SDL_Color hover_fill = DMStyles::AccentButton().hover_bg;
    for (const auto& layer : layer_visuals_) {
        for (const auto& room : layer.rooms) {
            const int px = center.x + static_cast<int>(std::lround(room.position.x * preview_scale_));
            const int py = center.y + static_cast<int>(std::lround(room.position.y * preview_scale_));
            const double extent_pixels = std::max(8.0, room.extent * preview_scale_ * 0.75);
            const int radius_pixels = static_cast<int>(std::round(extent_pixels));
            SDL_Color outline = darken(layer.color, 0.15f);
            if (layer.invalid) {
                outline = invalid_color;
            } else if (layer.warning) {
                outline = warning_color;
            } else if (layer.dependency) {
                outline = dependency_color;
            } else if (layer.selected) {
                outline = lighten(outline, 0.15f);
            }
            SDL_Color fill_color{0, 0, 0, 0};
            bool filled = false;
            if (!hovered_room.empty() && hovered_room == room.key) {
                fill_color = hover_fill;
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

    std::ostringstream radius_stream;
    radius_stream << "Map radius ≈ " << std::fixed << std::setprecision(0) << max_visual_radius_;
    draw_text(renderer, radius_stream.str(), rect.x + 12,
              rect.y + rect.h - (base_label.font_size + DMSpacing::small_gap() * 3), base_label);

    if (!hovered_room.empty()) {
        std::string label = display_name_for_room(hovered_room);
        if (label.empty()) {
            label = hovered_room;
        }
        draw_text(renderer, label, rect.x + 12, rect.y + rect.h - (DMStyles::Label().font_size + 12), DMStyles::Label());
    } else if (hovered_layer >= 0) {
        auto it = std::find_if(layer_visuals_.begin(), layer_visuals_.end(), [&](const LayerVisual& v) {
            return v.index == hovered_layer;
        });
        if (it != layer_visuals_.end()) {
            std::ostringstream oss;
            oss << it->name << " • " << it->room_count << (it->room_count == 1 ? " room" : " rooms");
            oss << " • " << it->min_rooms << "-" << it->max_rooms << " total";
            draw_text(renderer, oss.str(), rect.x + 12,
                      rect.y + rect.h - (DMStyles::Label().font_size + 12), DMStyles::Label());
        }
    }
}

