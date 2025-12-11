#include "map_layers_preview_widget.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <functional>
#include <iomanip>
#include <random>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>
#include <SDL_ttf.h>

#include "dm_styles.hpp"
#include "draw_utils.hpp"
#include "dev_mode_color_utils.hpp"
#include "map_layers_controller.hpp"
#include "map_generation/map_layers_geometry.hpp"
#include "utils/display_color.hpp"
#include "utils/ranged_color.hpp"

namespace {
constexpr double kTau = 6.28318530717958647692;

std::uint64_t generate_preview_seed() {
    static std::random_device rd;
    if (rd.entropy() > 0.0) {
        return (static_cast<std::uint64_t>(rd()) << 32) ^ static_cast<std::uint64_t>(rd());
    }
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(now.count());
}

SDL_Color hsv_to_rgb(float hue, float saturation, float value) {
    hue = std::fmod(hue, 360.0f);
    if (hue < 0.0f) {
        hue += 360.0f;
    }
    saturation = std::clamp(saturation, 0.0f, 1.0f);
    value = std::clamp(value, 0.0f, 1.0f);

    const float chroma = value * saturation;
    const float h_prime = hue / 60.0f;
    const float x = chroma * (1.0f - std::fabs(std::fmod(h_prime, 2.0f) - 1.0f));

    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    if (0.0f <= h_prime && h_prime < 1.0f) {
        r = chroma;
        g = x;
    } else if (1.0f <= h_prime && h_prime < 2.0f) {
        r = x;
        g = chroma;
    } else if (2.0f <= h_prime && h_prime < 3.0f) {
        g = chroma;
        b = x;
    } else if (3.0f <= h_prime && h_prime < 4.0f) {
        g = x;
        b = chroma;
    } else if (4.0f <= h_prime && h_prime < 5.0f) {
        r = x;
        b = chroma;
    } else {
        r = chroma;
        b = x;
    }

    const float m = value - chroma;
    auto to_channel = [m](float c) {
        c = std::clamp(c + m, 0.0f, 1.0f);
        return static_cast<Uint8>(std::lround(c * 255.0f));
};
    return SDL_Color{to_channel(r), to_channel(g), to_channel(b), 255};
}

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

void fill_ring(SDL_Renderer* renderer, int cx, int cy, int inner_radius, int outer_radius, SDL_Color color) {
    if (!renderer || outer_radius <= 0) {
        return;
    }
    inner_radius = std::max(0, std::min(inner_radius, outer_radius));
    if (inner_radius >= outer_radius) {
        fill_circle(renderer, cx, cy, outer_radius, color);
        return;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int y = -outer_radius; y <= outer_radius; ++y) {
        int outer_dx = static_cast<int>(std::sqrt(static_cast<double>(outer_radius * outer_radius - y * y)));
        if (inner_radius == 0 || std::abs(y) > inner_radius) {
            SDL_RenderDrawLine(renderer, cx - outer_dx, cy + y, cx + outer_dx, cy + y);
            continue;
        }
        int inner_dx = static_cast<int>(std::sqrt(static_cast<double>(inner_radius * inner_radius - y * y)));
        SDL_RenderDrawLine(renderer, cx - outer_dx, cy + y, cx - inner_dx, cy + y);
        SDL_RenderDrawLine(renderer, cx + inner_dx, cy + y, cx + outer_dx, cy + y);
    }
}
}

MapLayersPreviewWidget::MapLayersPreviewWidget()
    : preview_seed_(generate_preview_seed()) {}

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

    request_geometry_update();
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
    legend_rect_ = SDL_Rect{r.x, r.y, 0, r.h};

    const int gap = DMSpacing::panel_padding();
    const int min_preview_width = 240;
    int legend_width = 0;
    if (rect_.w > min_preview_width + gap + 120) {
        const int desired = std::clamp(rect_.w / 3, 160, 280);
        legend_width = std::min(desired, rect_.w - min_preview_width - gap);
        legend_width = std::max(0, legend_width);
    }

    const int spacing = (legend_width > 0) ? gap : 0;
    legend_rect_.x = rect_.x;
    legend_rect_.y = rect_.y;
    legend_rect_.w = legend_width;
    legend_rect_.h = rect_.h;

    preview_rect_.x = rect_.x + legend_width + spacing;
    preview_rect_.y = rect_.y;
    preview_rect_.w = std::max(0, rect_.w - legend_width - spacing);
    preview_rect_.h = rect_.h;

    preview_center_ = SDL_Point{preview_rect_.x + preview_rect_.w / 2, preview_rect_.y + preview_rect_.h / 2};

    const int button_margin = DMSpacing::panel_padding();
    const int raw_button_size = preview_rect_.w > 0 ? preview_rect_.w / 7 : 0;
    int button_size = std::clamp(raw_button_size, 26, 40);
    const int max_button_width = std::max(0, preview_rect_.w - button_margin * 2);
    const int max_button_height = std::max(0, preview_rect_.h - button_margin * 2);
    if (max_button_width > 0) {
        button_size = std::min(button_size, max_button_width);
    }
    if (max_button_height > 0) {
        button_size = std::min(button_size, max_button_height);
    }
    if (button_size > 0 && preview_rect_.w > 0 && preview_rect_.h > 0) {
        refresh_button_rect_.w = button_size;
        refresh_button_rect_.h = button_size;
        refresh_button_rect_.x = preview_rect_.x + button_margin;
        refresh_button_rect_.y = preview_rect_.y + preview_rect_.h - button_margin - button_size;
        refresh_button_rect_.y = std::max(refresh_button_rect_.y, preview_rect_.y + button_margin);
    } else {
        refresh_button_rect_ = SDL_Rect{0, 0, 0, 0};
    }
    refresh_hovered_ = false;
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
            if (refresh_hovered_) {
                refresh_hovered_ = false;
                request_geometry_update();
            }
            clear_hover_state();
        }
        return false;
    }

    const bool over_refresh = SDL_PointInRect(&p, &refresh_button_rect_) == SDL_TRUE;
    if (e.type == SDL_MOUSEMOTION) {
        if (refresh_hovered_ != over_refresh) {
            refresh_hovered_ = over_refresh;
            request_geometry_update();
        }
    }

    if (over_refresh) {
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            regenerate_preview();
            return true;
        }
        if (e.type == SDL_MOUSEMOTION) {
            clear_hover_state();
            return true;
        }
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
    preview_seed_ = generate_preview_seed();
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
    std::vector<SDL_Color> colors = utils::display_color::collect(rooms);
    nlohmann::json& entry = rooms[key];
    entry = nlohmann::json{{"name", key}};
    utils::display_color::ensure(entry, colors);
    mark_dirty();
    if (on_change_) {
        on_change_();
    }
}

void MapLayersPreviewWidget::regenerate_preview() {
    preview_seed_ = generate_preview_seed();
    dirty_ = true;
    request_geometry_update();
}

void MapLayersPreviewWidget::rebuild_visuals() {
    dirty_ = false;
    layer_visuals_.clear();
    room_legend_entries_.clear();
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

    double min_edge_distance = map_layers::kDefaultMinEdgeDistance;
    if (map_info_ && map_info_->is_object()) {
        min_edge_distance = map_layers::min_edge_distance_from_map_manifest(*map_info_);
    }
    const map_layers::LayerRadiiResult radii = map_layers::compute_layer_radii(layers, rooms_info, min_edge_distance);
    min_edge_distance_ = radii.min_edge_distance;
    max_visual_radius_ = std::max(1.0, radii.map_radius);

    std::mt19937_64 rng(preview_seed_);

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
        if (i < radii.layer_extents.size()) {
            visual.extent = radii.layer_extents[i];
        }
        if (visual.index == 0) {
            visual.inner_radius = 0.0;
        } else {
            visual.inner_radius = std::max(0.0, visual.radius - visual.extent);
        }
        visual.color = layer_color(visual.index);
        visual.min_rooms = layer_json.value("min_rooms", 0);
        visual.max_rooms = layer_json.value("max_rooms", 0);
        if (visual.max_rooms > 0 && visual.max_rooms < visual.min_rooms) {
            visual.max_rooms = visual.min_rooms;
        }
        visual.invalid = invalid_layers_.find(visual.index) != invalid_layers_.end();
        visual.warning = warning_layers_.find(visual.index) != warning_layers_.end();
        visual.dependency = dependency_layers_.find(visual.index) != dependency_layers_.end();
        visual.selected = (visual.index == selected_layer_index_);

        const auto rooms_it = layer_json.find("rooms");
        if (rooms_it != layer_json.end() && rooms_it->is_array()) {

            for (const auto& candidate : *rooms_it) {
                if (!candidate.is_object()) {
                    continue;
                }
                const std::string name = candidate.value("name", std::string());
                if (name.empty()) {
                    continue;
                }
                RoomVisual room;
                room.layer_index = visual.index;
                room.key = name;
                room.display_name = display_name_for_room(room.key);
                room.color = room_color(room.key);
                room.extent = map_layers::room_extent_from_rooms_data(rooms_info, room.key);
                if (!(room.extent > 0.0)) {
                    room.extent = 1.0;
                }
                visual.rooms.push_back(std::move(room));
            }
        }
        if (visual.index == 0) {
            if (!visual.rooms.empty()) {
                for (auto& room : visual.rooms) {
                    room.angle = 0.0;
                    room.radius = 0.0;
                    room.position = {0.0f, 0.0f};
                }
            }
        } else if (!visual.rooms.empty()) {
            std::vector<double> extents;
            extents.reserve(visual.rooms.size());
            for (const auto& room : visual.rooms) {
                extents.push_back(room.extent > 0.0 ? room.extent : 1.0);
            }
            std::uniform_real_distribution<double> start_angle_dist(0.0, kTau);
            const double start_angle = start_angle_dist(rng);
            map_layers::RadialLayout layout = map_layers::compute_radial_layout(visual.radius, extents, min_edge_distance_, start_angle);
            if (!layout.angles.empty() && layout.angles.size() == visual.rooms.size()) {
                visual.radius = layout.radius;
                for (std::size_t idx = 0; idx < visual.rooms.size(); ++idx) {
                    const double raw_angle = layout.angles[idx];
                    const double normalized = std::fmod(raw_angle, kTau);
                    visual.rooms[idx].angle = (normalized < 0.0) ? (normalized + kTau) : normalized;
                    visual.rooms[idx].radius = layout.radius;
                    visual.rooms[idx].position.x = static_cast<float>(std::cos(raw_angle) * layout.radius);
                    visual.rooms[idx].position.y = static_cast<float>(std::sin(raw_angle) * layout.radius);
                }
            } else {
                const double step = kTau / static_cast<double>(visual.rooms.size());
                for (std::size_t idx = 0; idx < visual.rooms.size(); ++idx) {
                    const double angle = step * static_cast<double>(idx);
                    visual.rooms[idx].angle = angle;
                    visual.rooms[idx].radius = visual.radius;
                    visual.rooms[idx].position.x = static_cast<float>(std::cos(angle) * visual.radius);
                    visual.rooms[idx].position.y = static_cast<float>(std::sin(angle) * visual.radius);
                }
            }
        }
        if (visual.index == 0) {
            visual.inner_radius = 0.0;
        } else {
            visual.inner_radius = std::max(0.0, visual.radius - visual.extent);
        }
        visual.room_count = static_cast<int>(visual.rooms.size());
        layer_visuals_.push_back(std::move(visual));
        max_visual_radius_ = std::max(max_visual_radius_, layer_visuals_.back().radius + layer_visuals_.back().extent);
    }

    std::unordered_map<std::string, std::string> unique_rooms;
    for (const auto& layer : layer_visuals_) {
        for (const auto& room : layer.rooms) {
            if (room.key.empty()) {
                continue;
            }
            unique_rooms.emplace(room.key, room.display_name);
        }
    }

    room_legend_entries_.reserve(unique_rooms.size());
    for (const auto& [key, display] : unique_rooms) {
        RoomLegendEntry entry;
        entry.key = key;
        entry.display_name = display.empty() ? key : display;
        entry.color = room_color(key);
        room_legend_entries_.push_back(std::move(entry));
    }
    std::sort(room_legend_entries_.begin(), room_legend_entries_.end(), [](const RoomLegendEntry& a, const RoomLegendEntry& b) {
        return a.display_name < b.display_name;
    });
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
    if (index < 0) {
        index = 0;
    }
    const float golden_ratio = 0.61803398875f;
    const float hue = std::fmod((static_cast<float>(index) * golden_ratio) * 360.0f, 360.0f);
    return hsv_to_rgb(hue, 0.55f, 0.88f);
}

SDL_Color MapLayersPreviewWidget::room_color(const std::string& key) const {
    if (key.empty()) {
        return SDL_Color{200, 200, 200, 255};
    }
    const nlohmann::json* rooms_info = rooms_data();
    if (rooms_info && rooms_info->is_object()) {
        auto it = rooms_info->find(key);
        if (it != rooms_info->end() && it->is_object()) {
            auto color_it = it->find("display_color");
            if (color_it != it->end()) {
                if (auto parsed = utils::color::color_from_json(*color_it)) {
                    SDL_Color color = *parsed;
                    color.a = 255;
                    return color;
                }
            }
        }
    }
    std::size_t hash = std::hash<std::string>{}(key);
    const float golden_ratio = 0.61803398875f;
    float hue = std::fmod(static_cast<float>(hash % 360) + static_cast<float>(hash) * golden_ratio, 360.0f);
    float saturation = 0.6f + static_cast<float>((hash >> 8) % 40) / 100.0f;
    saturation = std::clamp(saturation, 0.55f, 0.95f);
    float value = 0.78f + static_cast<float>((hash >> 4) % 20) / 100.0f;
    value = std::clamp(value, 0.75f, 0.98f);
    return hsv_to_rgb(hue, saturation, value);
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
    SDL_Point point{x, y};
    if (SDL_PointInRect(&point, &preview_rect_) != SDL_TRUE) {
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
    const double tolerance = 6.0;
    for (const auto& layer : layer_visuals_) {
        if (layer.index == 0) {
            const double dot_radius = std::clamp(layer.extent * scale, 4.0, 18.0);
            if (dist_pixels <= dot_radius + tolerance) {
                return layer.index;
            }
            continue;
        }
        const double outer_pixels = layer.radius * scale;
        const double inner_pixels = layer.inner_radius * scale;
        const double min_radius = std::max(0.0, inner_pixels - tolerance);
        const double max_radius = std::max(outer_pixels, inner_pixels) + tolerance;
        if (outer_pixels <= 0.0 || max_radius <= 0.0) {
            continue;
        }
        if (dist_pixels >= min_radius && dist_pixels <= max_radius) {
            return layer.index;
        }
    }
    return -1;
}

std::string MapLayersPreviewWidget::hit_test_room(int x, int y) const {
    if (layer_visuals_.empty() || preview_rect_.w <= 0) {
        return {};
    }
    SDL_Point point{x, y};
    if (SDL_PointInRect(&point, &preview_rect_) != SDL_TRUE) {
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
        render_room_legend(renderer);
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
        render_refresh_button(renderer);
        render_room_legend(renderer);
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
        SDL_Color outline_color = layer.color;
        if (layer.invalid) {
            outline_color = invalid_color;
        } else if (layer.warning) {
            outline_color = warning_color;
        } else if (layer.dependency) {
            outline_color = lighten(outline_color, 0.2f);
        }
        const bool hovered_layer_active = (hovered_layer == layer.index && hovered_room.empty());
        const bool selected_layer = layer.selected;

        if (layer.index == 0) {
            const double raw_dot = std::max(layer.extent, 1.0) * preview_scale_;
            const int dot_radius = std::clamp(static_cast<int>(std::lround(raw_dot)), 4, 18);
            SDL_Color fill_color = lighten(outline_color, selected_layer ? 0.25f : 0.1f);
            fill_color.a = selected_layer ? 180 : 140;
            if (hovered_layer_active && !selected_layer) {
                fill_color = lighten(fill_color, 0.2f);
            }
            fill_circle(renderer, center.x, center.y, dot_radius, fill_color);
            SDL_Color border_color = outline_color;
            if (hovered_layer_active) {
                border_color = lighten(border_color, 0.25f);
            }
            int thickness = selected_layer ? 4 : 3;
            draw_circle(renderer, center.x, center.y, dot_radius, border_color, thickness);
            if (selected_layer) {
                draw_circle(renderer, center.x, center.y, dot_radius + 3, selection_outline, 1);
            }
        } else {
            const int radius_pixels = std::max(1, static_cast<int>(std::lround(layer.radius * preview_scale_)));
            const int inner_radius_pixels = std::max(0, static_cast<int>(std::lround(layer.inner_radius * preview_scale_)));

            if (hovered_layer_active || selected_layer) {
                SDL_Color ring_color = lighten(outline_color, selected_layer ? 0.12f : 0.25f);
                ring_color.a = selected_layer ? 140 : 100;
                fill_ring(renderer, center.x, center.y, inner_radius_pixels, radius_pixels, ring_color);
            }

            SDL_Color color = outline_color;
            int thickness = selected_layer ? 6 : 3;
            if (hovered_layer_active) {
                color = lighten(color, 0.25f);
                thickness = std::max(thickness, selected_layer ? 7 : 5);
            }
            draw_circle(renderer, center.x, center.y, radius_pixels, color, thickness);
            if (selected_layer) {
                draw_circle(renderer, center.x, center.y, radius_pixels + 4, selection_outline, 1);
            }
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

    for (const auto& layer : layer_visuals_) {
        if (layer.index == 0) {
            continue;
        }
        for (const auto& room : layer.rooms) {
            const int px = center.x + static_cast<int>(std::lround(room.position.x * preview_scale_));
            const int py = center.y + static_cast<int>(std::lround(room.position.y * preview_scale_));
            const double extent_pixels = std::max(8.0, room.extent * preview_scale_ * 0.75);
            const int radius_pixels = static_cast<int>(std::round(extent_pixels));
            SDL_Color base_fill = room.color;
            SDL_Color outline = darken(base_fill, 0.2f);
            if (layer.invalid) {
                outline = invalid_color;
            } else if (layer.warning) {
                outline = warning_color;
            } else if (layer.dependency) {
                outline = dependency_color;
            } else if (layer.selected) {
                outline = lighten(outline, 0.15f);
            }
            SDL_Color fill = base_fill;
            if (layer.selected) {
                fill = lighten(fill, 0.12f);
            }
            if (!hovered_room.empty() && hovered_room == room.key) {
                fill = lighten(fill, 0.18f);
            }
            fill.a = (hovered_room == room.key) ? 200 : 160;
            fill_circle(renderer, px, py, radius_pixels, fill);
            draw_circle(renderer, px, py, radius_pixels, outline, (hovered_room == room.key) ? 3 : 2);
        }
    }

    const int footer_gap = DMSpacing::small_gap();
    const int footer_radius_y = rect.y + rect.h - (base_label.font_size + footer_gap * 3);
    int footer_text_x = rect.x + footer_gap;
    if (refresh_button_rect_.w > 0) {
        footer_text_x = std::max(footer_text_x, refresh_button_rect_.x + refresh_button_rect_.w + footer_gap);
    }

    std::ostringstream radius_stream;
    radius_stream << "Map radius ≈ " << std::fixed << std::setprecision(0) << max_visual_radius_;
    draw_text(renderer, radius_stream.str(), footer_text_x, footer_radius_y, base_label);

    DMLabelStyle footer_label = DMStyles::Label();
    const int footer_info_y = rect.y + rect.h - (footer_label.font_size + footer_gap * 2);
    if (!hovered_room.empty()) {
        std::string label = display_name_for_room(hovered_room);
        if (label.empty()) {
            label = hovered_room;
        }
        draw_text(renderer, label, footer_text_x, footer_info_y, footer_label);
    } else if (hovered_layer >= 0) {
        auto it = std::find_if(layer_visuals_.begin(), layer_visuals_.end(), [&](const LayerVisual& v) {
            return v.index == hovered_layer;
        });
        if (it != layer_visuals_.end()) {
            std::ostringstream oss;
            oss << it->name << " • " << it->room_count << (it->room_count == 1 ? " room" : " rooms");
            oss << " • " << it->min_rooms << "-" << it->max_rooms << " total";
            draw_text(renderer, oss.str(), footer_text_x, footer_info_y, footer_label);
        }
    }

    render_refresh_button(renderer);
    render_room_legend(renderer);
}

void MapLayersPreviewWidget::render_refresh_button(SDL_Renderer* renderer) const {
    if (!renderer || refresh_button_rect_.w <= 0 || refresh_button_rect_.h <= 0) {
        return;
    }

    SDL_Rect button_rect = refresh_button_rect_;
    const DMButtonStyle& style = DMStyles::AccentButton();
    SDL_Color fill = refresh_hovered_ ? style.hover_bg : style.bg;
    const int corner_radius = std::max(4, DMStyles::CornerRadius() / 2);

    dm_draw::DrawBeveledRect(renderer, button_rect, corner_radius, DMStyles::BevelDepth(), fill, DMStyles::HighlightColor(), DMStyles::ShadowColor(), true, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
    dm_draw::DrawRoundedOutline(renderer, button_rect, corner_radius, 1, style.border);

    DMLabelStyle icon_style = style.label;
    icon_style.color = style.text;
    if (refresh_hovered_) {
        icon_style.color = lighten(icon_style.color, 0.08f);
    }

    const std::string refresh_icon = "\xE2\x86\xBB";
    int text_w = 0;
    int text_h = icon_style.font_size;
    if (TTF_Font* font = icon_style.open_font()) {
        if (TTF_SizeUTF8(font, refresh_icon.c_str(), &text_w, &text_h) != 0) {
            text_w = 0;
            text_h = icon_style.font_size;
        }
        TTF_CloseFont(font);
    }
    const int text_x = button_rect.x + (button_rect.w - text_w) / 2;
    const int text_y = button_rect.y + (button_rect.h - text_h) / 2;
    draw_text(renderer, refresh_icon, text_x, text_y, icon_style);
}

void MapLayersPreviewWidget::render_room_legend(SDL_Renderer* renderer) const {
    if (!renderer || legend_rect_.w <= 0 || legend_rect_.h <= 0) {
        return;
    }

    SDL_Rect legend = legend_rect_;
    const SDL_Color panel_bg = DMStyles::PanelBG();
    SDL_Color legend_bg = lighten(panel_bg, 0.06f);
    legend_bg.a = panel_bg.a;
    SDL_SetRenderDrawColor(renderer, legend_bg.r, legend_bg.g, legend_bg.b, legend_bg.a);
    SDL_RenderFillRect(renderer, &legend);

    const SDL_Color border_color = DMStyles::Border();
    dm_draw::DrawRoundedOutline(renderer, legend, DMStyles::CornerRadius(), 1, border_color);

    const DMLabelStyle base_label = DMStyles::Label();
    DMLabelStyle header_style = base_label;
    header_style.color = lighten(header_style.color, 0.15f);

    const int padding = DMSpacing::small_gap();
    int text_x = legend_rect_.x + padding;
    int y = legend_rect_.y + padding;

    draw_text(renderer, "Room Key", text_x, y, header_style);
    y += header_style.font_size + padding;

    if (room_legend_entries_.empty()) {
        draw_text(renderer, "No rooms", text_x, y, base_label);
        return;
    }

    const int swatch_size = 18;
    for (const auto& entry : room_legend_entries_) {
        bool hovered = (hovered_room_key_ == entry.key);
        SDL_Rect swatch{text_x, y, swatch_size, swatch_size};
        SDL_Color fill = entry.color;
        if (hovered) {
            fill = lighten(fill, 0.15f);
        }
        fill.a = hovered ? 220 : 180;
        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &swatch);
        SDL_SetRenderDrawColor(renderer, border_color.r, border_color.g, border_color.b, border_color.a);
        SDL_RenderDrawRect(renderer, &swatch);

        DMLabelStyle label_style = base_label;
        if (hovered) {
            label_style.color = lighten(label_style.color, 0.1f);
        }
        int label_x = swatch.x + swatch.w + padding;
        draw_text(renderer, entry.display_name, label_x, y, label_style);
        y += swatch_size + padding;
        if (y > legend_rect_.y + legend_rect_.h - swatch_size) {
            break;
        }
    }
}
