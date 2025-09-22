#include "map_layers_panel.hpp"

#include "dm_styles.hpp"
#include "map_layers_controller.hpp"
#include "widgets.hpp"
#include "utils/input.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace {
constexpr int kCanvasPreferredHeight = 320;
constexpr int kCanvasPadding = 16;
constexpr int kRoomRangeMaxDefault = 64;
constexpr int kCandidateRangeMaxDefault = 128;
constexpr int kLayerRadiusStepDefault = 512;
constexpr double kTau = 6.28318530717958647692;

SDL_Color hsv_to_rgb(double h, double s, double v) {
    h = std::fmod(std::max(0.0, std::min(h, 1.0)), 1.0) * 6.0;
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
    auto to_byte = [](double x) -> Uint8 {
        x = std::max(0.0, std::min(1.0, x));
        return static_cast<Uint8>(std::lround(x * 255.0));
    };
    return SDL_Color{to_byte(r), to_byte(g), to_byte(b), 255};
}

SDL_Color level_color(int level) {
    const double hue = std::fmod(level * 0.13, 1.0);
    return hsv_to_rgb(hue, 0.6, 1.0);
}

void draw_circle(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color col, int thickness = 2) {
    if (!r || radius <= 0 || thickness <= 0) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int t = 0; t < thickness; ++t) {
        int rr = std::max(1, radius - t);
        const int segments = std::max(32, rr * 4);
        double prev_x = cx + rr;
        double prev_y = cy;
        SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
        for (int s = 1; s <= segments; ++s) {
            const double theta = (static_cast<double>(s) / segments) * kTau;
            const double x = cx + rr * std::cos(theta);
            const double y = cy + rr * std::sin(theta);
            SDL_RenderDrawLine(r, static_cast<int>(std::lround(prev_x)), static_cast<int>(std::lround(prev_y)),
                               static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
            prev_x = x;
            prev_y = y;
        }
    }
}

void draw_text(SDL_Renderer* r, const std::string& text, int x, int y, const DMLabelStyle& style) {
    if (!r) return;
    TTF_Font* font = style.open_font();
    if (!font) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), style.color);
    if (surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
        if (tex) {
            SDL_Rect dst{x, y, surf->w, surf->h};
            SDL_RenderCopy(r, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
        }
        SDL_FreeSurface(surf);
    }
    TTF_CloseFont(font);
}

void draw_text_centered(SDL_Renderer* r, const std::string& text, int x, int y, const DMLabelStyle& style) {
    if (!r) return;
    TTF_Font* font = style.open_font();
    if (!font) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), style.color);
    if (surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
        if (tex) {
            SDL_Rect dst{ x - surf->w / 2, y - surf->h / 2, surf->w, surf->h };
            SDL_RenderCopy(r, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
        }
        SDL_FreeSurface(surf);
    }
    TTF_CloseFont(font);
}

struct RoomGeometry {
    double max_width = 0.0;
    double max_height = 0.0;
    bool is_circle = false;
};

RoomGeometry fetch_room_geometry(const nlohmann::json* rooms_data, const std::string& room_name) {
    RoomGeometry geom;
    if (!rooms_data || !rooms_data->is_object()) return geom;
    auto it = rooms_data->find(room_name);
    if (it == rooms_data->end() || !it->is_object()) return geom;
    const auto& room = *it;
    geom.max_width = room.value("max_width", room.value("min_width", 0.0));
    geom.max_height = room.value("max_height", room.value("min_height", 0.0));
    std::string geometry = room.value("geometry", std::string());
    if (!geometry.empty()) {
        std::string lowered;
        lowered.reserve(geometry.size());
        for (char ch : geometry) {
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        if (lowered == "circle") {
            geom.is_circle = true;
        }
    }
    if (geom.max_width <= 0.0 && geom.max_height <= 0.0) {
        geom.max_width = 100.0;
        geom.max_height = 100.0;
    } else if (geom.max_width <= 0.0) {
        geom.max_width = geom.max_height;
    } else if (geom.max_height <= 0.0) {
        geom.max_height = geom.max_width;
    }
    return geom;
}

double room_extent_for_radius(const RoomGeometry& geom) {
    double w = std::max(0.0, geom.max_width);
    double h = std::max(0.0, geom.max_height);
    if (geom.is_circle) {
        return w * 0.5;
    }
    double diag = std::sqrt(w * w + h * h);
    return diag * 0.5;
}

std::string trim_copy_local(const std::string& input) {
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    std::string result = input;
    result.erase(result.begin(), std::find_if(result.begin(), result.end(), [&](unsigned char ch) { return !is_space(ch); }));
    result.erase(std::find_if(result.rbegin(), result.rend(), [&](unsigned char ch) { return !is_space(ch); }).base(), result.end());
    return result;
}

std::string sanitize_room_key(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    bool last_underscore = false;
    for (char ch : input) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch)) {
            out.push_back(static_cast<char>(std::tolower(uch)));
            last_underscore = false;
        } else if (ch == '_' || ch == '-') {
            if (!last_underscore && !out.empty()) {
                out.push_back('_');
                last_underscore = true;
            }
        } else if (std::isspace(uch)) {
            if (!last_underscore && !out.empty()) {
                out.push_back('_');
                last_underscore = true;
            }
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    if (out.empty()) {
        out = "room";
    }
    return out;
}

std::string make_unique_room_key(const nlohmann::json& rooms_data, const std::string& base_key) {
    std::string base = base_key.empty() ? std::string("room") : base_key;
    if (!rooms_data.is_object()) {
        return base;
    }
    std::string candidate = base;
    int suffix = 1;
    while (rooms_data.contains(candidate)) {
        candidate = base + "_" + std::to_string(suffix++);
    }
    return candidate;
}

nlohmann::json make_default_room_json(const std::string& name) {
    nlohmann::json room = nlohmann::json::object();
    room["name"] = name;
    room["min_width"] = 256;
    room["max_width"] = 256;
    room["width_min"] = 256;
    room["width_max"] = 256;
    room["min_height"] = 256;
    room["max_height"] = 256;
    room["height_min"] = 256;
    room["height_max"] = 256;
    room["edge_smoothness"] = 2;
    room["geometry"] = "Square";
    room["inherits_map_assets"] = false;
    room["is_spawn"] = false;
    room["is_boss"] = false;
    room["spawn_groups"] = nlohmann::json::array();
    return room;
}

int compute_next_layer_radius(const nlohmann::json& layers) {
    int max_radius = 0;
    bool has_layer = false;
    if (layers.is_array()) {
        for (const auto& layer : layers) {
            if (!layer.is_object()) continue;
            has_layer = true;
            max_radius = std::max(max_radius, layer.value("radius", 0));
        }
    }
    if (!has_layer) return 0;
    int step = std::max(kLayerRadiusStepDefault, max_radius / 3);
    if (max_radius <= 0) {
        return kLayerRadiusStepDefault;
    }
    return max_radius + step;
}

void clamp_layer_room_ranges(nlohmann::json& layer) {
    if (!layer.is_object()) return;
    int min_rooms = std::max(0, layer.value("min_rooms", 0));
    int max_rooms = std::max(min_rooms, layer.value("max_rooms", min_rooms));
    layer["min_rooms"] = std::min(min_rooms, kRoomRangeMaxDefault);
    layer["max_rooms"] = std::min(std::max(min_rooms, max_rooms), kRoomRangeMaxDefault);

    int min_sum = 0;
    int max_sum = 0;
    const auto rooms_it = layer.find("rooms");
    if (rooms_it != layer.end() && rooms_it->is_array()) {
        for (const auto& candidate : *rooms_it) {
            min_sum += std::max(0, candidate.value("min_instances", 0));
            max_sum += std::max(0, candidate.value("max_instances", 0));
        }
    }
    if (min_sum > layer["min_rooms"].get<int>()) {
        layer["min_rooms"] = min_sum;
    }
    if (layer["max_rooms"].get<int>() > max_sum && max_sum > 0) {
        layer["max_rooms"] = std::max(layer["min_rooms"].get<int>(), max_sum);
    }
}

struct PreviewRoomSpec {
    std::string name;
    int min_instances = 0;
    int max_instances = 0;
    std::vector<std::string> required_children;
};

struct PreviewLayerSpec {
    int level = 0;
    double radius = 0.0;
    int min_rooms = 0;
    int max_rooms = 0;
    std::vector<PreviewRoomSpec> rooms;
};

std::vector<PreviewRoomSpec> build_children_pool(const PreviewLayerSpec& layer, std::mt19937& rng) {
    std::vector<PreviewRoomSpec> pool;
    std::vector<PreviewRoomSpec> expandable;
    int min_rooms = std::max(0, layer.min_rooms);
    int max_rooms = std::max(min_rooms, layer.max_rooms);
    if (max_rooms <= 0) return pool;
    std::uniform_int_distribution<int> dist(min_rooms, max_rooms);
    int target = dist(rng);
    for (const auto& room : layer.rooms) {
        for (int i = 0; i < room.min_instances; ++i) {
            pool.push_back(room);
        }
        int extra = std::max(0, room.max_instances - room.min_instances);
        for (int i = 0; i < extra; ++i) {
            expandable.push_back(room);
        }
    }
    while (static_cast<int>(pool.size()) < target && !expandable.empty()) {
        std::uniform_int_distribution<size_t> pick(0, expandable.size() - 1);
        size_t idx = pick(rng);
        pool.push_back(expandable[idx]);
        expandable.erase(expandable.begin() + static_cast<long>(idx));
    }
    return pool;
}

uint32_t compute_preview_seed(const std::vector<PreviewLayerSpec>& layers, const std::string& map_path) {
    uint64_t seed = 0x9e3779b97f4a7c15ull;
    auto mix = [&seed](uint64_t value) {
        seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    };

    if (!map_path.empty()) {
        mix(static_cast<uint64_t>(std::hash<std::string>{}(map_path)));
    }

    for (const auto& layer : layers) {
        mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(layer.radius * 1000.0))));
        mix(static_cast<uint64_t>((static_cast<uint32_t>(layer.min_rooms) << 16) ^ static_cast<uint32_t>(layer.max_rooms)));
        mix(static_cast<uint64_t>(static_cast<uint32_t>(layer.level)));
        for (const auto& room : layer.rooms) {
            mix(static_cast<uint64_t>(std::hash<std::string>{}(room.name)));
            mix(static_cast<uint64_t>((static_cast<uint32_t>(room.min_instances) << 16) ^ static_cast<uint32_t>(room.max_instances)));
            for (const auto& child : room.required_children) {
                mix(static_cast<uint64_t>(std::hash<std::string>{}(child)));
            }
        }
    }

    seed ^= (seed >> 33);
    uint32_t result = static_cast<uint32_t>(seed ^ (seed >> 32));
    if (result == 0) {
        result = 0x6d5a56e9u;
    }
    return result;
}

} // namespace

using nlohmann::json;

// -----------------------------------------------------------------------------
// LayerCanvasWidget implementation
// -----------------------------------------------------------------------------

class MapLayersPanel::LayerCanvasWidget : public Widget {
public:
    explicit LayerCanvasWidget(MapLayersPanel* owner) : owner_(owner) {}

    void refresh();
    void set_selected(int index) { selected_index_ = index; }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int w) const override {
        return std::max(kCanvasPreferredHeight, std::min(w, kCanvasPreferredHeight + 80));
    }
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* renderer) const override;

private:
    struct CircleInfo {
        int index = -1;
        int radius_px = 0;
        SDL_Color color{128, 128, 128, 255};
        std::string label;
    };

    MapLayersPanel* owner_ = nullptr;
    SDL_Rect rect_{0, 0, 0, 0};
    std::vector<CircleInfo> circles_;
    int selected_index_ = -1;
};

void MapLayersPanel::LayerCanvasWidget::refresh() {
    circles_.clear();
    if (!owner_ || !owner_->map_info_) return;
    const auto& arr = owner_->layers_array();
    if (arr.empty()) return;
    double max_radius = 1.0;
    for (const auto& layer : arr) {
        if (layer.is_object()) {
            max_radius = std::max(max_radius, static_cast<double>(layer.value("radius", 0)));
        }
    }
    for (size_t i = 0; i < arr.size(); ++i) {
        const auto& layer = arr[i];
        if (!layer.is_object()) continue;
        CircleInfo info;
        info.index = static_cast<int>(i);
        int radius = layer.value("radius", 0);
        info.radius_px = radius;
        info.color = level_color(static_cast<int>(i));
        info.label = layer.value("name", std::string("layer_") + std::to_string(i));
        circles_.push_back(std::move(info));
    }
}

bool MapLayersPanel::LayerCanvasWidget::handle_event(const SDL_Event& e) {
    if (!owner_) return false;
    if (e.type != SDL_MOUSEBUTTONUP) return false;
    SDL_Point p{ e.button.x, e.button.y };
    if (!SDL_PointInRect(&p, &rect_)) return false;
    if (circles_.empty()) return false;

    const auto& arr = owner_->layers_array();
    double max_radius = 1.0;
    for (const auto& layer : arr) {
        if (layer.is_object()) {
            max_radius = std::max(max_radius, static_cast<double>(layer.value("radius", 0)));
        }
    }
    const int center_x = rect_.x + rect_.w / 2;
    const int center_y = rect_.y + rect_.h / 2;
    const int draw_radius_max = std::max(8, std::min(rect_.w, rect_.h) / 2 - kCanvasPadding);
    double display_extent = std::max(max_radius, owner_->preview_extent_);
    if (display_extent <= 0.0) display_extent = 1.0;
    double scale = static_cast<double>(draw_radius_max) / display_extent;

    int hit_index = -1;
    for (const auto& info : circles_) {
        const json* layer = owner_->layer_at(info.index);
        if (!layer) continue;
        int current_radius = layer->value("radius", 0);
        int pixel_radius = static_cast<int>(std::lround(current_radius * scale));
        pixel_radius = std::max(12, pixel_radius);
        const int dx = p.x - center_x;
        const int dy = p.y - center_y;
        const double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));
        const double tolerance = 12.0;
        if (std::fabs(dist - pixel_radius) <= tolerance || dist < pixel_radius * 0.85) {
            hit_index = info.index;
            break;
        }
    }
    if (hit_index < 0) return false;
    if (e.button.button == SDL_BUTTON_LEFT) {
        owner_->select_layer(hit_index);
        return true;
    }
    if (e.button.button == SDL_BUTTON_RIGHT) {
        owner_->open_layer_config_internal(hit_index);
        return true;
    }
    return false;
}

void MapLayersPanel::LayerCanvasWidget::render(SDL_Renderer* renderer) const {
    if (!renderer) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 18, 18, 18, 200);
    SDL_RenderFillRect(renderer, &rect_);
    SDL_SetRenderDrawColor(renderer, 64, 64, 64, 255);
    SDL_RenderDrawRect(renderer, &rect_);
    if (!owner_ || circles_.empty()) return;

    const auto& arr = owner_->layers_array();
    double max_radius = 1.0;
    for (const auto& layer : arr) {
        if (layer.is_object()) {
            max_radius = std::max(max_radius, static_cast<double>(layer.value("radius", 0)));
        }
    }
    const int center_x = rect_.x + rect_.w / 2;
    const int center_y = rect_.y + rect_.h / 2;
    const int draw_radius_max = std::max(8, std::min(rect_.w, rect_.h) / 2 - kCanvasPadding);
    double display_extent = std::max(max_radius, owner_->preview_extent_);
    if (display_extent <= 0.0) display_extent = 1.0;
    double scale = static_cast<double>(draw_radius_max) / display_extent;

    const DMLabelStyle label_style = DMStyles::Label();
    for (const auto& info : circles_) {
        const json* layer = owner_->layer_at(info.index);
        if (!layer) continue;
        int radius_value = layer->value("radius", 0);
        int pixel_radius = static_cast<int>(std::lround(radius_value * scale));
        pixel_radius = std::max(12, pixel_radius);
        SDL_Color col = info.color;
        int thickness = (info.index == selected_index_) ? 6 : 3;
        draw_circle(renderer, center_x, center_y, pixel_radius, col, thickness);
        std::ostringstream oss;
        oss << info.label << " (" << radius_value << ")";
        draw_text(renderer, oss.str(), center_x - pixel_radius + 8, center_y - pixel_radius - 18, label_style);
    }

    if (!owner_->preview_edges_.empty()) {
        for (const auto& edge : owner_->preview_edges_) {
            if (!edge.from || !edge.to) continue;
            SDL_Point from_pt{
                static_cast<int>(std::lround(center_x + edge.from->center.x * scale)),
                static_cast<int>(std::lround(center_y + edge.from->center.y * scale))
            };
            SDL_Point to_pt{
                static_cast<int>(std::lround(center_x + edge.to->center.x * scale)),
                static_cast<int>(std::lround(center_y + edge.to->center.y * scale))
            };
            SDL_Color col = edge.color;
            SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
            SDL_RenderDrawLine(renderer, from_pt.x, from_pt.y, to_pt.x, to_pt.y);
        }
    }

    if (!owner_->preview_nodes_.empty()) {
        for (const auto& node_uptr : owner_->preview_nodes_) {
            if (!node_uptr) continue;
            const PreviewNode* node = node_uptr.get();
            SDL_Point center_pt{
                static_cast<int>(std::lround(center_x + node->center.x * scale)),
                static_cast<int>(std::lround(center_y + node->center.y * scale))
            };
            SDL_Color col = node->color;
            SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, 220);
            if (node->is_circle) {
                int radius = static_cast<int>(std::lround(std::max(2.0, (node->width * 0.5) * scale)));
                draw_circle(renderer, center_pt.x, center_pt.y, radius, col, 2);
            } else {
                int half_w = static_cast<int>(std::lround(std::max(2.0, (node->width * 0.5) * scale)));
                int half_h = static_cast<int>(std::lround(std::max(2.0, (node->height * 0.5) * scale)));
                SDL_Rect room_rect{ center_pt.x - half_w, center_pt.y - half_h, half_w * 2, half_h * 2 };
                SDL_RenderDrawRect(renderer, &room_rect);
            }
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 60);
            SDL_RenderDrawPoint(renderer, center_pt.x, center_pt.y);

            double extent_units = node->is_circle ? node->width * 0.5
                                                  : 0.5 * std::sqrt(node->width * node->width + node->height * node->height);
            double extent_pixels = std::max(2.0, extent_units * scale);
            double length = std::sqrt(node->center.x * node->center.x + node->center.y * node->center.y);
            double dir_x = 0.0;
            double dir_y = -1.0;
            if (length > 1e-3) {
                dir_x = node->center.x / length;
                dir_y = node->center.y / length;
            }
            double offset = extent_pixels + 14.0;
            int label_x = static_cast<int>(std::lround(center_pt.x + dir_x * offset));
            int label_y = static_cast<int>(std::lround(center_pt.y + dir_y * offset));
            std::string room_label = node->name.empty() ? std::string("<room>") : node->name;
            draw_text_centered(renderer, room_label, label_x, label_y, label_style);
        }
    }

    if (selected_index_ >= 0) {
        std::ostringstream oss;
        const json* layer = owner_->layer_at(selected_index_);
        if (layer) {
            oss << "Right-click layer to configure";
            draw_text(renderer, oss.str(), rect_.x + 12, rect_.y + rect_.h - 28, label_style);
        }
    }
}

// -----------------------------------------------------------------------------
// PanelSidebarWidget implementation
// -----------------------------------------------------------------------------

class MapLayersPanel::PanelSidebarWidget : public Widget {
public:
    explicit PanelSidebarWidget(MapLayersPanel* owner);

    void set_dirty(bool dirty);
    void set_selected(int index) { selected_layer_ = index; }

    void set_rect(const SDL_Rect& r) override;
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int w) const override { return std::max(kCanvasPreferredHeight, w); }
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* renderer) const override;

private:
    MapLayersPanel* owner_ = nullptr;
    SDL_Rect rect_{0,0,0,0};
    std::unique_ptr<DMButton> add_button_;
    std::unique_ptr<DMButton> new_room_button_;
    std::unique_ptr<DMButton> save_button_;
    std::unique_ptr<DMButton> reload_button_;
    std::unique_ptr<DMButton> config_button_;
    std::unique_ptr<DMButton> delete_button_;
    std::unique_ptr<DMButton> preview_button_;
    bool dirty_ = false;
    int selected_layer_ = -1;
};

MapLayersPanel::PanelSidebarWidget::PanelSidebarWidget(MapLayersPanel* owner)
    : owner_(owner) {
    add_button_ = std::make_unique<DMButton>("Add Layer", &DMStyles::CreateButton(), 140, DMButton::height());
    new_room_button_ = std::make_unique<DMButton>("New Room", &DMStyles::CreateButton(), 140, DMButton::height());
    save_button_ = std::make_unique<DMButton>("Save", &DMStyles::HeaderButton(), 140, DMButton::height());
    reload_button_ = std::make_unique<DMButton>("Reload", &DMStyles::HeaderButton(), 140, DMButton::height());
    config_button_ = std::make_unique<DMButton>("Open Config", &DMStyles::HeaderButton(), 140, DMButton::height());
    delete_button_ = std::make_unique<DMButton>("Delete Layer", &DMStyles::DeleteButton(), 140, DMButton::height());
    preview_button_ = std::make_unique<DMButton>("Generate Preview", &DMStyles::HeaderButton(), 140, DMButton::height());
}

void MapLayersPanel::PanelSidebarWidget::set_dirty(bool dirty) {
    dirty_ = dirty;
    if (save_button_) {
        save_button_->set_text(dirty ? "Save *" : "Save");
    }
}

void MapLayersPanel::PanelSidebarWidget::set_rect(const SDL_Rect& r) {
    rect_ = r;
    const int spacing = DMSpacing::item_gap();
    SDL_Rect button_rect{ rect_.x + spacing, rect_.y + spacing, rect_.w - spacing * 2, DMButton::height() };
    if (add_button_) add_button_->set_rect(button_rect);
    button_rect.y += DMButton::height() + spacing;
    if (new_room_button_) new_room_button_->set_rect(button_rect);
    button_rect.y += DMButton::height() + spacing;
    if (preview_button_) preview_button_->set_rect(button_rect);
    button_rect.y += DMButton::height() + spacing;
    if (config_button_) config_button_->set_rect(button_rect);
    button_rect.y += DMButton::height() + spacing;
    if (delete_button_) delete_button_->set_rect(button_rect);
    button_rect.y += DMButton::height() + spacing * 2;
    if (save_button_) save_button_->set_rect(button_rect);
    button_rect.y += DMButton::height() + spacing;
    if (reload_button_) reload_button_->set_rect(button_rect);
}

bool MapLayersPanel::PanelSidebarWidget::handle_event(const SDL_Event& e) {
    bool used = false;
    auto handle_btn = [&](std::unique_ptr<DMButton>& btn, const std::function<void()>& cb) {
        if (btn && btn->handle_event(e)) {
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                cb();
            }
            used = true;
        }
    };
    handle_btn(add_button_, [this]() { if (owner_) owner_->add_layer_internal(); });
    handle_btn(new_room_button_, [this]() { if (owner_) owner_->add_room_to_selected_layer(); });
    handle_btn(preview_button_, [this]() {
        if (!owner_) return;
        owner_->request_preview_regeneration();
        owner_->regenerate_preview();
    });
    handle_btn(save_button_, [this]() { if (owner_) owner_->save_layers_to_disk(); });
    handle_btn(reload_button_, [this]() { if (owner_) owner_->reload_layers_from_disk(); });
    handle_btn(config_button_, [this]() { if (owner_ && selected_layer_ >= 0) owner_->open_layer_config_internal(selected_layer_); });
    handle_btn(delete_button_, [this]() { if (owner_ && selected_layer_ >= 0) owner_->delete_layer_internal(selected_layer_); });
    return used;
}

void MapLayersPanel::PanelSidebarWidget::render(SDL_Renderer* renderer) const {
    if (!renderer) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 14, 14, 14, 220);
    SDL_RenderFillRect(renderer, &rect_);
    SDL_SetRenderDrawColor(renderer, 64, 64, 64, 255);
    SDL_RenderDrawRect(renderer, &rect_);
    if (add_button_) add_button_->render(renderer);
    if (new_room_button_) new_room_button_->render(renderer);
    if (preview_button_) preview_button_->render(renderer);
    if (config_button_) config_button_->render(renderer);
    if (delete_button_) delete_button_->render(renderer);
    if (save_button_) save_button_->render(renderer);
    if (reload_button_) reload_button_->render(renderer);
}

// -----------------------------------------------------------------------------
// RoomSelectorPopup implementation
// -----------------------------------------------------------------------------

class MapLayersPanel::RoomSelectorPopup {
public:
    explicit RoomSelectorPopup(MapLayersPanel* owner);
    void open(const std::vector<std::string>& rooms, std::function<void(const std::string&)> cb);
    void set_rooms(const std::vector<std::string>& rooms);
    void close();
    bool visible() const { return visible_; }
    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;
    bool is_point_inside(int x, int y) const;

private:
    void rebuild_room_buttons();
    void ensure_geometry() const;
    void layout_widgets() const;
    void begin_create_room();
    void cancel_create_room();
    void finalize_create_room();
    void scroll_by(int delta);

    MapLayersPanel* owner_ = nullptr;
    bool visible_ = false;
    mutable SDL_Rect rect_{0,0,280,320};
    std::vector<std::unique_ptr<DMButton>> buttons_;
    std::vector<std::string> rooms_;
    std::function<void(const std::string&)> callback_;
    std::unique_ptr<DMButton> create_room_button_;
    std::unique_ptr<DMButton> confirm_button_;
    std::unique_ptr<DMButton> cancel_button_;
    std::unique_ptr<DMTextBox> name_input_;
    bool creating_room_ = false;
    mutable bool geometry_dirty_ = true;
    mutable int content_height_ = 0;
    mutable SDL_Rect content_clip_{0,0,0,0};
    mutable int max_scroll_ = 0;
    mutable int scroll_offset_ = 0;
};

MapLayersPanel::RoomSelectorPopup::RoomSelectorPopup(MapLayersPanel* owner)
    : owner_(owner) {
    create_room_button_ = std::make_unique<DMButton>("Create New Room", &DMStyles::CreateButton(), 220, DMButton::height());
    confirm_button_ = std::make_unique<DMButton>("Create", &DMStyles::CreateButton(), 120, DMButton::height());
    cancel_button_ = std::make_unique<DMButton>("Cancel", &DMStyles::HeaderButton(), 120, DMButton::height());
    geometry_dirty_ = true;
}

void MapLayersPanel::RoomSelectorPopup::open(const std::vector<std::string>& rooms, std::function<void(const std::string&)> cb) {
    callback_ = std::move(cb);
    creating_room_ = false;
    name_input_.reset();
    scroll_offset_ = 0;
    geometry_dirty_ = true;
    set_rooms(rooms);
    if (owner_) {
        const SDL_Rect& panel_rect = owner_->rect();
        rect_.x = panel_rect.x + panel_rect.w + 16;
        rect_.y = panel_rect.y;
    }
    visible_ = true;
    ensure_geometry();
}

void MapLayersPanel::RoomSelectorPopup::set_rooms(const std::vector<std::string>& rooms) {
    rooms_ = rooms;
    rebuild_room_buttons();
    geometry_dirty_ = true;
}

void MapLayersPanel::RoomSelectorPopup::rebuild_room_buttons() {
    buttons_.clear();
    const int margin = DMSpacing::item_gap();
    int button_width = rect_.w - margin * 2;
    if (button_width <= 0) {
        button_width = 220;
    } else {
        button_width = std::max(button_width, 220);
    }
    for (const auto& room : rooms_) {
        auto btn = std::make_unique<DMButton>(room, &DMStyles::ListButton(), button_width, DMButton::height());
        buttons_.push_back(std::move(btn));
    }
}

void MapLayersPanel::RoomSelectorPopup::ensure_geometry() const {
    if (!geometry_dirty_) return;
    const int margin = DMSpacing::item_gap();
    rect_.w = std::max(rect_.w, 220 + margin * 2);
    const int content_width = std::max(0, rect_.w - margin * 2);
    const int button_height = DMButton::height();
    const int spacing = DMSpacing::small_gap();

    int total = margin;
    if (!rooms_.empty()) {
        total += static_cast<int>(rooms_.size()) * (button_height + spacing);
        total -= spacing;
    }
    total += DMSpacing::item_gap();
    total += button_height;
    if (creating_room_) {
        total += spacing;
        int input_height = DMTextBox::height();
        if (name_input_) {
            input_height = name_input_->preferred_height(content_width);
        }
        total += input_height;
        total += spacing;
        total += button_height;
    }
    total += margin;

    content_height_ = total;
    const int min_height = button_height * 3 + margin * 2;
    const int max_height = 520;
    rect_.h = std::min(std::max(content_height_, min_height), max_height);

    content_clip_ = SDL_Rect{ rect_.x + margin, rect_.y + margin,
                               std::max(0, rect_.w - margin * 2), std::max(0, rect_.h - margin * 2) };
    max_scroll_ = std::max(0, content_height_ - rect_.h);
    if (scroll_offset_ > max_scroll_) scroll_offset_ = max_scroll_;
    if (scroll_offset_ < 0) scroll_offset_ = 0;
    geometry_dirty_ = false;
}

void MapLayersPanel::RoomSelectorPopup::layout_widgets() const {
    ensure_geometry();
    const int margin = DMSpacing::item_gap();
    const int spacing = DMSpacing::small_gap();
    const int button_height = DMButton::height();
    const int content_width = std::max(0, rect_.w - margin * 2);

    int y = rect_.y + margin - scroll_offset_;
    for (size_t i = 0; i < buttons_.size(); ++i) {
        auto& btn = buttons_[i];
        if (!btn) continue;
        btn->set_rect(SDL_Rect{ rect_.x + margin, y, content_width, button_height });
        y += button_height;
        if (i + 1 < buttons_.size()) {
            y += spacing;
        }
    }

    y += DMSpacing::item_gap();
    if (create_room_button_) {
        create_room_button_->set_rect(SDL_Rect{ rect_.x + margin, y, content_width, DMButton::height() });
    }
    y += DMButton::height();

    if (creating_room_) {
        y += spacing;
        if (name_input_) {
            name_input_->set_rect(SDL_Rect{ rect_.x + margin, y, content_width, DMTextBox::height() });
            const SDL_Rect input_rect = name_input_->rect();
            y = input_rect.y + input_rect.h;
        }
        y += spacing;
        int left_w = std::max(1, (content_width - spacing) / 2);
        int right_w = std::max(1, content_width - left_w - spacing);
        if (left_w + right_w + spacing > content_width) {
            right_w = std::max(1, content_width - left_w - spacing);
        }
        int button_y = y;
        if (confirm_button_) {
            confirm_button_->set_rect(SDL_Rect{ rect_.x + margin, button_y, left_w, DMButton::height() });
        }
        if (cancel_button_) {
            cancel_button_->set_rect(SDL_Rect{ rect_.x + margin + left_w + spacing, button_y, right_w, DMButton::height() });
        }
        y += DMButton::height();
    }
}

void MapLayersPanel::RoomSelectorPopup::begin_create_room() {
    std::string suggestion = owner_ ? owner_->suggest_room_name() : std::string("room");
    name_input_ = std::make_unique<DMTextBox>("Room Name", suggestion);
    creating_room_ = true;
    geometry_dirty_ = true;
    ensure_geometry();
    scroll_offset_ = max_scroll_;
    SDL_StartTextInput();
}

void MapLayersPanel::RoomSelectorPopup::cancel_create_room() {
    if (creating_room_) {
        SDL_StopTextInput();
    }
    creating_room_ = false;
    name_input_.reset();
    geometry_dirty_ = true;
}

void MapLayersPanel::RoomSelectorPopup::finalize_create_room() {
    if (!creating_room_) return;
    std::string desired = name_input_ ? name_input_->value() : std::string();
    std::string created;
    if (owner_) {
        created = owner_->create_new_room(desired);
    }
    if (created.empty()) {
        return;
    }
    SDL_StopTextInput();
    creating_room_ = false;
    name_input_.reset();
    geometry_dirty_ = true;
    if (callback_) {
        callback_(created);
    }
    close();
}

void MapLayersPanel::RoomSelectorPopup::scroll_by(int delta) {
    if (delta == 0) return;
    ensure_geometry();
    int new_offset = scroll_offset_ + delta;
    if (new_offset < 0) new_offset = 0;
    if (new_offset > max_scroll_) new_offset = max_scroll_;
    scroll_offset_ = new_offset;
}

void MapLayersPanel::RoomSelectorPopup::close() {
    SDL_StopTextInput();
    visible_ = false;
    callback_ = nullptr;
    creating_room_ = false;
    name_input_.reset();
    scroll_offset_ = 0;
    geometry_dirty_ = true;
}

void MapLayersPanel::RoomSelectorPopup::update(const Input&) {
    if (!visible_) return;
    ensure_geometry();
}

bool MapLayersPanel::RoomSelectorPopup::handle_event(const SDL_Event& e) {
    if (!visible_) return false;
    ensure_geometry();
    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION) {
        SDL_Point p{ e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x,
                     e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y };
        if (!SDL_PointInRect(&p, &rect_)) {
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                close();
            }
            return false;
        }
    }

    bool used = false;
    if (e.type == SDL_MOUSEWHEEL) {
        SDL_Point mouse_pos{0, 0};
        SDL_GetMouseState(&mouse_pos.x, &mouse_pos.y);
        if (SDL_PointInRect(&mouse_pos, &content_clip_)) {
            const int step = DMButton::height() + DMSpacing::small_gap();
            scroll_by(-e.wheel.y * step);
            used = true;
        }
    }

    layout_widgets();

    if (create_room_button_ && create_room_button_->handle_event(e)) {
        used = true;
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            begin_create_room();
        }
    }

    if (creating_room_) {
        if (name_input_ && name_input_->handle_event(e)) {
            used = true;
            geometry_dirty_ = true;
        }
        if (confirm_button_ && confirm_button_->handle_event(e)) {
            used = true;
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                finalize_create_room();
                return true;
            }
        }
        if (cancel_button_ && cancel_button_->handle_event(e)) {
            used = true;
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                cancel_create_room();
                return true;
            }
        }
        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                finalize_create_room();
                return true;
            }
            if (e.key.keysym.sym == SDLK_ESCAPE) {
                cancel_create_room();
                return true;
            }
        }
    }

    for (size_t i = 0; i < buttons_.size(); ++i) {
        auto& btn = buttons_[i];
        if (!btn) continue;
        if (btn->handle_event(e)) {
            used = true;
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                if (callback_) callback_(rooms_[i]);
                close();
                return true;
            }
        }
    }
    return used;
}

void MapLayersPanel::RoomSelectorPopup::render(SDL_Renderer* renderer) const {
    if (!visible_ || !renderer) return;
    ensure_geometry();
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 24,24,24,240);
    SDL_RenderFillRect(renderer, &rect_);
    SDL_SetRenderDrawColor(renderer, 90,90,90,255);
    SDL_RenderDrawRect(renderer, &rect_);

    SDL_Rect prev_clip;
    SDL_RenderGetClipRect(renderer, &prev_clip);
#if SDL_VERSION_ATLEAST(2,0,4)
    const SDL_bool was_clipping = SDL_RenderIsClipEnabled(renderer);
#else
    const SDL_bool was_clipping = (prev_clip.w != 0 || prev_clip.h != 0) ? SDL_TRUE : SDL_FALSE;
#endif
    SDL_RenderSetClipRect(renderer, &content_clip_);

    layout_widgets();
    for (const auto& btn : buttons_) {
        if (btn) btn->render(renderer);
    }
    if (create_room_button_) create_room_button_->render(renderer);
    if (creating_room_) {
        if (name_input_) name_input_->render(renderer);
        if (confirm_button_) confirm_button_->render(renderer);
        if (cancel_button_) cancel_button_->render(renderer);
    }

    if (was_clipping == SDL_TRUE) {
        SDL_RenderSetClipRect(renderer, &prev_clip);
    } else {
        SDL_RenderSetClipRect(renderer, nullptr);
    }
}

bool MapLayersPanel::RoomSelectorPopup::is_point_inside(int x, int y) const {
    if (!visible_) return false;
    SDL_Point p{ x, y };
    return SDL_PointInRect(&p, &rect_);
}

// -----------------------------------------------------------------------------
// LayerConfigPanel and RoomCandidateWidget declarations will follow
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// LayerConfigPanel implementation
// -----------------------------------------------------------------------------

class MapLayersPanel::RoomCandidateWidget : public Widget {
public:
    RoomCandidateWidget(LayerConfigPanel* owner, int layer_index, int candidate_index, json* candidate);

    void refresh_from_json();
    void update();

    void set_rect(const SDL_Rect& r) override;
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int w) const override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* renderer) const override;

    void set_candidate_index(int idx) { candidate_index_ = idx; }

private:
    struct ChildChip {
        std::string name;
        SDL_Rect rect{0,0,0,0};
        std::unique_ptr<DMButton> remove_button;
    };

    LayerConfigPanel* owner_ = nullptr;
    int layer_index_ = -1;
    int candidate_index_ = -1;
    json* candidate_ = nullptr;
    SDL_Rect rect_{0,0,0,0};

    std::unique_ptr<DMRangeSlider> range_slider_;
    std::unique_ptr<DMButton> add_child_button_;
    std::unique_ptr<DMButton> delete_button_;

    int min_cache_ = 0;
    int max_cache_ = 0;
    std::vector<ChildChip> child_chips_;
};

class MapLayersPanel::LayerConfigPanel : public DockableCollapsible {
public:
    explicit LayerConfigPanel(MapLayersPanel* owner);

    void open(int layer_index, json* layer);
    void close();
    bool is_visible() const;
    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;
    bool is_point_inside(int x, int y) const;
    void refresh();

    MapLayersPanel* panel_owner() const { return owner_; }

private:
    void sync_from_widgets();

    MapLayersPanel* owner_ = nullptr;
    int layer_index_ = -1;
    json* layer_ = nullptr;

    std::unique_ptr<DMTextBox> name_box_;
    std::unique_ptr<TextBoxWidget> name_widget_;
    std::string name_cache_;

    std::unique_ptr<DMRangeSlider> room_range_slider_;
    std::unique_ptr<RangeSliderWidget> room_range_widget_;
    int min_rooms_cache_ = 0;
    int max_rooms_cache_ = 0;

    std::unique_ptr<DMButton> add_candidate_btn_;
    std::unique_ptr<ButtonWidget> add_candidate_widget_;

    std::unique_ptr<DMButton> close_btn_;
    std::unique_ptr<ButtonWidget> close_widget_;

    std::unique_ptr<DMButton> delete_layer_btn_;
    std::unique_ptr<ButtonWidget> delete_layer_widget_;

    std::vector<std::unique_ptr<RoomCandidateWidget>> candidate_widgets_;
};

MapLayersPanel::LayerConfigPanel::LayerConfigPanel(MapLayersPanel* owner)
    : DockableCollapsible("Layer", true, owner ? owner->rect().x + owner->rect().w + 32 : 420, owner ? owner->rect().y : 120),
      owner_(owner) {
    set_visible(false);
    set_expanded(true);
    set_cell_width(320);
}

void MapLayersPanel::LayerConfigPanel::open(int layer_index, json* layer) {
    if (!layer) return;
    layer_index_ = layer_index;
    layer_ = layer;
    name_cache_ = layer_->value("name", std::string("layer_") + std::to_string(layer_index));
    min_rooms_cache_ = layer_->value("min_rooms", 0);
    max_rooms_cache_ = layer_->value("max_rooms", 0);
    refresh();
    set_title(std::string("Layer: ") + name_cache_);
    set_visible(true);
    set_expanded(true);
}

void MapLayersPanel::LayerConfigPanel::close() {
    set_visible(false);
    layer_ = nullptr;
    candidate_widgets_.clear();
}

bool MapLayersPanel::LayerConfigPanel::is_visible() const {
    return DockableCollapsible::is_visible();
}

void MapLayersPanel::LayerConfigPanel::update(const Input& input, int screen_w, int screen_h) {
    if (!is_visible()) return;
    DockableCollapsible::update(input, screen_w, screen_h);
    sync_from_widgets();
    for (auto& widget : candidate_widgets_) {
        if (widget) widget->update();
    }
}

bool MapLayersPanel::LayerConfigPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) return false;
    bool used = DockableCollapsible::handle_event(e);
    for (auto& widget : candidate_widgets_) {
        if (widget && widget->handle_event(e)) {
            used = true;
        }
    }
    return used;
}

void MapLayersPanel::LayerConfigPanel::render(SDL_Renderer* renderer) const {
    if (!is_visible()) return;
    DockableCollapsible::render(renderer);
    for (const auto& widget : candidate_widgets_) {
        if (widget) widget->render(renderer);
    }
}

bool MapLayersPanel::LayerConfigPanel::is_point_inside(int x, int y) const {
    if (!is_visible()) return false;
    if (DockableCollapsible::is_point_inside(x, y)) return true;
    for (const auto& widget : candidate_widgets_) {
        if (!widget) continue;
        const SDL_Rect& r = widget->rect();
        SDL_Point p{ x, y };
        if (SDL_PointInRect(&p, &r)) return true;
    }
    return false;
}

void MapLayersPanel::LayerConfigPanel::refresh() {
    if (layer_) {
        name_cache_ = layer_->value("name", name_cache_);
        min_rooms_cache_ = layer_->value("min_rooms", min_rooms_cache_);
        max_rooms_cache_ = layer_->value("max_rooms", max_rooms_cache_);
    }

    DockableCollapsible::Rows rows;

    name_box_ = std::make_unique<DMTextBox>("Layer Name", name_cache_);
    name_widget_ = std::make_unique<TextBoxWidget>(name_box_.get());
    rows.push_back({ name_widget_.get() });

    const int slider_max = std::max(kRoomRangeMaxDefault, std::max(min_rooms_cache_, max_rooms_cache_) + 8);
    room_range_slider_ = std::make_unique<DMRangeSlider>(0, slider_max, min_rooms_cache_, std::max(min_rooms_cache_, max_rooms_cache_));
    room_range_widget_ = std::make_unique<RangeSliderWidget>(room_range_slider_.get());
    rows.push_back({ room_range_widget_.get() });

    add_candidate_btn_ = std::make_unique<DMButton>("Add Room", &DMStyles::CreateButton(), 160, DMButton::height());
    add_candidate_widget_ = std::make_unique<ButtonWidget>(add_candidate_btn_.get(), [this]() {
        if (!owner_) return;
        owner_->request_room_selection([this](const std::string& room) {
            if (!owner_) return;
            owner_->handle_candidate_added(layer_index_, room);
        });
    });
    rows.push_back({ add_candidate_widget_.get() });

    delete_layer_btn_ = std::make_unique<DMButton>("Delete Layer", &DMStyles::DeleteButton(), 140, DMButton::height());
    delete_layer_widget_ = std::make_unique<ButtonWidget>(delete_layer_btn_.get(), [this]() {
        if (owner_) owner_->delete_layer_internal(layer_index_);
        this->close();
    });

    close_btn_ = std::make_unique<DMButton>("Close", &DMStyles::HeaderButton(), 120, DMButton::height());
    close_widget_ = std::make_unique<ButtonWidget>(close_btn_.get(), [this]() { this->close(); });

    rows.push_back({ delete_layer_widget_.get(), close_widget_.get() });

    candidate_widgets_.clear();
    if (layer_ && layer_->contains("rooms")) {
        json& rooms = (*layer_)["rooms"];
        if (rooms.is_array()) {
            for (size_t i = 0; i < rooms.size(); ++i) {
                json& entry = rooms[i];
                auto widget = std::make_unique<RoomCandidateWidget>(this, layer_index_, static_cast<int>(i), &entry);
                widget->refresh_from_json();
                candidate_widgets_.push_back(std::move(widget));
                rows.push_back({ candidate_widgets_.back().get() });
            }
        }
    }

    set_rows(rows);
}




void MapLayersPanel::LayerConfigPanel::sync_from_widgets() {
    if (!layer_) return;
    if (name_box_) {
        const std::string current = name_box_->value();
        if (current != name_cache_) {
            name_cache_ = current;
            (*layer_)["name"] = name_cache_;
            if (owner_) owner_->handle_layer_name_changed(layer_index_, name_cache_);
            set_title(std::string("Layer: ") + name_cache_);
        }
    }
    if (room_range_slider_) {
        int new_min = room_range_slider_->min_value();
        int new_max = room_range_slider_->max_value();
        if (new_min != min_rooms_cache_ || new_max != max_rooms_cache_) {
            min_rooms_cache_ = new_min;
            max_rooms_cache_ = std::max(new_min, new_max);
            (*layer_)["min_rooms"] = min_rooms_cache_;
            (*layer_)["max_rooms"] = max_rooms_cache_;
            if (owner_) owner_->handle_layer_range_changed(layer_index_, min_rooms_cache_, max_rooms_cache_);
        }
    }
    json& rooms = (*layer_)["rooms"];
    if (rooms.is_array()) {
        for (size_t i = 0; i < rooms.size() && i < candidate_widgets_.size(); ++i) {
            candidate_widgets_[i]->set_candidate_index(static_cast<int>(i));
        }
    }
}

// -----------------------------------------------------------------------------
// RoomCandidateWidget implementation
// -----------------------------------------------------------------------------

MapLayersPanel::RoomCandidateWidget::RoomCandidateWidget(LayerConfigPanel* owner, int layer_index, int candidate_index, json* candidate)
    : owner_(owner), layer_index_(layer_index), candidate_index_(candidate_index), candidate_(candidate) {
    range_slider_ = std::make_unique<DMRangeSlider>(0, kCandidateRangeMaxDefault, 0, 0);
    add_child_button_ = std::make_unique<DMButton>("Add Child", &DMStyles::HeaderButton(), 120, DMButton::height());
    delete_button_ = std::make_unique<DMButton>("Delete", &DMStyles::DeleteButton(), 120, DMButton::height());
}

void MapLayersPanel::RoomCandidateWidget::refresh_from_json() {
    if (!candidate_) return;
    const int min_v = candidate_->value("min_instances", 0);
    const int max_v = candidate_->value("max_instances", 0);
    min_cache_ = min_v;
    max_cache_ = std::max(min_v, max_v);
    const int slider_max = std::max(kCandidateRangeMaxDefault, max_cache_ + 8);
    range_slider_ = std::make_unique<DMRangeSlider>(0, slider_max, min_cache_, max_cache_);
    child_chips_.clear();
    auto children_it = candidate_->find("required_children");
    if (children_it != candidate_->end() && children_it->is_array()) {
        for (const auto& child : *children_it) {
            ChildChip chip;
            chip.name = child.get<std::string>();
            chip.remove_button = std::make_unique<DMButton>("x", &DMStyles::DeleteButton(), 24, DMButton::height());
            child_chips_.push_back(std::move(chip));
        }
    }
}

void MapLayersPanel::RoomCandidateWidget::update() {
    if (!candidate_) return;
    if (range_slider_) {
        int new_min = range_slider_->min_value();
        int new_max = range_slider_->max_value();
        if (new_min != min_cache_ || new_max != max_cache_) {
            min_cache_ = new_min;
            max_cache_ = std::max(new_min, new_max);
            (*candidate_)["min_instances"] = min_cache_;
            (*candidate_)["max_instances"] = max_cache_;
            if (owner_ && owner_->panel_owner()) {
                owner_->panel_owner()->handle_candidate_range_changed(layer_index_, candidate_index_, min_cache_, max_cache_);
            }
        }
    }
}

void MapLayersPanel::RoomCandidateWidget::set_rect(const SDL_Rect& r) {
    rect_ = r;
    const int spacing = DMSpacing::item_gap();
    SDL_Rect slider_rect{ rect_.x + spacing, rect_.y + spacing + DMButton::height() + spacing, rect_.w - spacing * 2, DMRangeSlider::height() };
    if (range_slider_) range_slider_->set_rect(slider_rect);
    SDL_Rect buttons_rect{ rect_.x + spacing, slider_rect.y + slider_rect.h + spacing, 120, DMButton::height() };
    if (add_child_button_) add_child_button_->set_rect(buttons_rect);
    buttons_rect.x += add_child_button_->rect().w + spacing;
    if (delete_button_) delete_button_->set_rect(buttons_rect);
    int chip_y = buttons_rect.y + DMButton::height() + spacing;
    int chip_x = rect_.x + spacing;
    const int chip_height = DMButton::height();
    const int chip_gap = DMSpacing::small_gap();
    const int max_width = rect_.w - spacing * 2;
    const DMLabelStyle label = DMStyles::Label();
    int chip_width_min = 80;
    for (auto& chip : child_chips_) {
        const int text_width = static_cast<int>(chip.name.size()) * (label.font_size / 2 + 2);
        const int chip_width = std::min(max_width, std::max(chip_width_min, text_width + 40));
        if (chip_x + chip_width > rect_.x + rect_.w - spacing) {
            chip_x = rect_.x + spacing;
            chip_y += chip_height + chip_gap;
        }
        chip.rect = SDL_Rect{ chip_x, chip_y, chip_width, chip_height };
        if (chip.remove_button) {
            SDL_Rect btn_rect{ chip.rect.x + chip.rect.w - chip_height, chip.rect.y, chip_height, chip_height };
            chip.remove_button->set_rect(btn_rect);
        }
        chip_x += chip_width + chip_gap;
    }
}

int MapLayersPanel::RoomCandidateWidget::height_for_width(int w) const {
    const int spacing = DMSpacing::item_gap();
    int height = spacing + DMButton::height() + spacing + DMRangeSlider::height() + spacing + DMButton::height() + spacing;
    int chips_needed = static_cast<int>(child_chips_.size());
    if (chips_needed > 0) {
        const int chip_height = DMButton::height();
        int per_row = std::max(1, (w - spacing * 2) / 120);
        int rows = (chips_needed + per_row - 1) / per_row;
        height += rows * (chip_height + DMSpacing::small_gap());
    }
    height += spacing;
    return height;
}

bool MapLayersPanel::RoomCandidateWidget::handle_event(const SDL_Event& e) {
    bool used = false;
    if (range_slider_ && range_slider_->handle_event(e)) {
        used = true;
    }
    if (add_child_button_ && add_child_button_->handle_event(e)) {
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            if (owner_ && owner_->panel_owner()) {
                owner_->panel_owner()->request_room_selection([this](const std::string& child) {
                    if (owner_ && owner_->panel_owner()) {
                        owner_->panel_owner()->handle_candidate_child_added(layer_index_, candidate_index_, child);
                        this->refresh_from_json();
                        owner_->refresh();
                    }
                });
            }
        }
        used = true;
    }
    if (delete_button_ && delete_button_->handle_event(e)) {
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            if (owner_ && owner_->panel_owner()) {
                owner_->panel_owner()->handle_candidate_removed(layer_index_, candidate_index_);
                owner_->refresh();
            }
        }
        used = true;
    }
    for (auto& chip : child_chips_) {
        if (!chip.remove_button) continue;
        if (chip.remove_button->handle_event(e)) {
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                if (owner_ && owner_->panel_owner()) {
                    owner_->panel_owner()->handle_candidate_child_removed(layer_index_, candidate_index_, chip.name);
                    this->refresh_from_json();
                    owner_->refresh();
                }
            }
            used = true;
        }
    }
    return used;
}

void MapLayersPanel::RoomCandidateWidget::render(SDL_Renderer* renderer) const {
    if (!renderer || !candidate_) return;
    SDL_Rect bg = rect_;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 28, 28, 28, 220);
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
    SDL_RenderDrawRect(renderer, &bg);

    const DMLabelStyle label = DMStyles::Label();
    draw_text(renderer, candidate_->value("name", "room"), rect_.x + DMSpacing::item_gap(), rect_.y + DMSpacing::item_gap() - (label.font_size + 4), label);
    if (range_slider_) range_slider_->render(renderer);
    if (add_child_button_) add_child_button_->render(renderer);
    if (delete_button_) delete_button_->render(renderer);

    for (const auto& chip : child_chips_) {
        SDL_SetRenderDrawColor(renderer, 36, 36, 36, 240);
        SDL_RenderFillRect(renderer, &chip.rect);
        SDL_SetRenderDrawColor(renderer, 90, 90, 90, 255);
        SDL_RenderDrawRect(renderer, &chip.rect);
        draw_text(renderer, chip.name, chip.rect.x + 6, chip.rect.y + (chip.rect.h - label.font_size) / 2, label);
        if (chip.remove_button) chip.remove_button->render(renderer);
    }
}

auto MapLayersPanel::layers_array() -> nlohmann::json& {
    ensure_layers_array();
    return (*map_info_)["map_layers"];
}

auto MapLayersPanel::layers_array() const -> const nlohmann::json& {
    static json empty = json::array();
    if (!map_info_ || !map_info_->contains("map_layers") || !(*map_info_)["map_layers"].is_array()) {
        return empty;
    }
    return (*map_info_)["map_layers"];
}

MapLayersPanel::MapLayersPanel(int x, int y)
    : DockableCollapsible("Map Layers", true, x, y) {
    set_cell_width(220);
    set_visible(false);
    set_expanded(true);
    canvas_widget_ = std::make_unique<LayerCanvasWidget>(this);
    sidebar_widget_ = std::make_unique<PanelSidebarWidget>(this);
    layer_config_ = std::make_unique<LayerConfigPanel>(this);
    room_selector_ = std::make_unique<RoomSelectorPopup>(this);
    rebuild_rows();
}

MapLayersPanel::~MapLayersPanel() = default;

void MapLayersPanel::set_map_info(json* map_info, const std::string& map_path) {
    map_info_ = map_info;
    map_path_ = map_path;
    if (controller_) {
        controller_->bind(map_info, map_path);
    }
    ensure_layers_array();
    ensure_layer_indices();
    rebuild_available_rooms();
    refresh_canvas();
    if (layer_config_) layer_config_->close();
    request_preview_regeneration();
    mark_clean();
}

void MapLayersPanel::set_on_save(SaveCallback cb) {
    on_save_ = std::move(cb);
}

void MapLayersPanel::set_controller(std::shared_ptr<MapLayersController> controller) {
    controller_ = std::move(controller);
    if (controller_ && map_info_) {
        controller_->bind(map_info_, map_path_);
    }
}

void MapLayersPanel::open() {
    set_visible(true);
    set_expanded(true);
}

void MapLayersPanel::close() {
    set_visible(false);
    if (layer_config_) layer_config_->close();
    if (room_selector_) room_selector_->close();
}

bool MapLayersPanel::is_visible() const {
    return DockableCollapsible::is_visible();
}

void MapLayersPanel::set_embedded_mode(bool embedded) {
    if (embedded_mode_ == embedded) return;
    embedded_mode_ = embedded;
    if (embedded_mode_) {
        set_show_header(false);
        set_scroll_enabled(true);
        set_available_height_override(-1);
        set_expanded(true);
    } else {
        set_show_header(true);
        set_scroll_enabled(floatable_);
        set_available_height_override(-1);
    }
}

void MapLayersPanel::set_embedded_bounds(const SDL_Rect& bounds) {
    set_rect(bounds);
    if (embedded_mode_) {
        int inner_height = std::max(0, bounds.h - 2 * padding_);
        set_available_height_override(inner_height);
        set_work_area(bounds);
    } else {
        set_available_height_override(-1);
        set_work_area(SDL_Rect{0, 0, 0, 0});
    }
}

void MapLayersPanel::update(const Input& input, int screen_w, int screen_h) {
    if (preview_dirty_) {
        regenerate_preview();
    }
    if (!is_visible()) return;
    DockableCollapsible::update(input, screen_w, screen_h);
    if (layer_config_) layer_config_->update(input, screen_w, screen_h);
    if (room_selector_) room_selector_->update(input);
}

bool MapLayersPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) return false;
    bool used = DockableCollapsible::handle_event(e);
    if (layer_config_ && layer_config_->is_visible()) {
        used = layer_config_->handle_event(e) || used;
    }
    if (room_selector_ && room_selector_->visible()) {
        used = room_selector_->handle_event(e) || used;
    }
    return used;
}

void MapLayersPanel::render(SDL_Renderer* renderer) const {
    if (!is_visible()) return;
    DockableCollapsible::render(renderer);
    if (layer_config_ && layer_config_->is_visible()) {
        layer_config_->render(renderer);
    }
    if (room_selector_ && room_selector_->visible()) {
        room_selector_->render(renderer);
    }
}

bool MapLayersPanel::is_point_inside(int x, int y) const {
    if (!is_visible()) return false;
    if (DockableCollapsible::is_point_inside(x, y)) return true;
    if (layer_config_ && layer_config_->is_visible() && layer_config_->is_point_inside(x, y)) return true;
    if (room_selector_ && room_selector_->visible() && room_selector_->is_point_inside(x, y)) return true;
    return false;
}

void MapLayersPanel::request_preview_regeneration() {
    preview_dirty_ = true;
}

double MapLayersPanel::compute_map_radius_from_layers() {
    if (!map_info_) return 0.0;
    const auto& layers = layers_array();
    if (!layers.is_array() || layers.empty()) {
        return 0.0;
    }
    const nlohmann::json* rooms_data = nullptr;
    auto rooms_it = map_info_->find("rooms_data");
    if (rooms_it != map_info_->end() && rooms_it->is_object()) {
        rooms_data = &(*rooms_it);
    }
    double fallback_radius = 0.0;
    double max_extent = 0.0;
    for (const auto& layer : layers) {
        if (!layer.is_object()) continue;
        double layer_radius = layer.value("radius", 0.0);
        fallback_radius = std::max(fallback_radius, layer_radius);
        double largest_room = 0.0;
        auto rooms_array_it = layer.find("rooms");
        if (rooms_array_it != layer.end() && rooms_array_it->is_array()) {
            for (const auto& candidate : *rooms_array_it) {
                if (!candidate.is_object()) continue;
                std::string room_name = candidate.value("name", std::string());
                if (room_name.empty()) continue;
                RoomGeometry geom = fetch_room_geometry(rooms_data, room_name);
                largest_room = std::max(largest_room, room_extent_for_radius(geom));
            }
        }
        max_extent = std::max(max_extent, layer_radius + largest_room);
    }
    if (max_extent <= 0.0) {
        max_extent = fallback_radius;
    }
    if (max_extent <= 0.0) {
        max_extent = 1.0;
    }
    double current = map_info_->value("map_radius", 0.0);
    if (std::fabs(current - max_extent) > 0.5) {
        (*map_info_)["map_radius"] = max_extent;
        dirty_ = true;
        if (sidebar_widget_) sidebar_widget_->set_dirty(true);
        update_save_button_state();
    }
    return max_extent;
}

void MapLayersPanel::regenerate_preview() {
    preview_dirty_ = false;
    preview_nodes_.clear();
    preview_edges_.clear();

    double computed_radius = compute_map_radius_from_layers();
    preview_extent_ = std::max(computed_radius, 1.0);

    const auto& layers = layers_array();
    if (!layers.is_array() || layers.empty()) {
        if (canvas_widget_) canvas_widget_->refresh();
        return;
    }

    std::vector<PreviewLayerSpec> layer_specs;
    layer_specs.reserve(layers.size());
    for (const auto& layer_json : layers) {
        if (!layer_json.is_object()) continue;
        PreviewLayerSpec spec;
        spec.level = layer_json.value("level", static_cast<int>(layer_specs.size()));
        spec.radius = layer_json.value("radius", 0.0);
        spec.min_rooms = layer_json.value("min_rooms", 0);
        spec.max_rooms = layer_json.value("max_rooms", spec.min_rooms);
        auto rooms_it = layer_json.find("rooms");
        if (rooms_it != layer_json.end() && rooms_it->is_array()) {
            for (const auto& candidate : *rooms_it) {
                if (!candidate.is_object()) continue;
                PreviewRoomSpec room_spec;
                room_spec.name = candidate.value("name", std::string());
                room_spec.min_instances = candidate.value("min_instances", 0);
                room_spec.max_instances = candidate.value("max_instances", room_spec.min_instances);
                auto required_it = candidate.find("required_children");
                if (required_it != candidate.end() && required_it->is_array()) {
                    for (const auto& child : *required_it) {
                        if (child.is_string()) {
                            room_spec.required_children.push_back(child.get<std::string>());
                        }
                    }
                }
                spec.rooms.push_back(std::move(room_spec));
            }
        }
        layer_specs.push_back(std::move(spec));
    }

    if (layer_specs.empty() || layer_specs.front().rooms.empty()) {
        if (canvas_widget_) canvas_widget_->refresh();
        return;
    }

    const nlohmann::json* rooms_data = nullptr;
    if (map_info_) {
        auto it = map_info_->find("rooms_data");
        if (it != map_info_->end() && it->is_object()) {
            rooms_data = &(*it);
        }
    }

    const PreviewRoomSpec& root_spec = layer_specs.front().rooms.front();
    RoomGeometry root_geom = fetch_room_geometry(rooms_data, root_spec.name);
    auto root_node = std::make_unique<PreviewNode>();
    root_node->center = SDL_FPoint{0.0f, 0.0f};
    root_node->width = root_geom.max_width;
    root_node->height = root_geom.max_height;
    root_node->is_circle = root_geom.is_circle;
    root_node->layer = layer_specs.front().level;
    root_node->color = level_color(root_node->layer);
    root_node->name = root_spec.name.empty() ? std::string("<root>") : root_spec.name;
    PreviewNode* root_ptr = root_node.get();
    preview_nodes_.push_back(std::move(root_node));

    std::unordered_map<PreviewNode*, PreviewNode*> last_child_for_parent;
    std::unordered_map<int, std::vector<PreviewNode*>> nodes_by_level;
    nodes_by_level[root_ptr->layer].push_back(root_ptr);

    struct PreviewSector {
        PreviewNode* node = nullptr;
        float start = 0.0f;
        float span = static_cast<float>(kTau);
    };

    std::vector<PreviewNode*> current_parents{ root_ptr };
    std::vector<PreviewSector> current_sectors{ PreviewSector{ root_ptr, 0.0f, static_cast<float>(kTau) } };
    uint32_t seed = compute_preview_seed(layer_specs, map_path_);
    std::mt19937 rng(seed);

    for (size_t li = 1; li < layer_specs.size(); ++li) {
        const auto& layer_spec = layer_specs[li];
        auto children = build_children_pool(layer_spec, rng);
        double radius = layer_spec.radius;
        std::vector<PreviewSector> next_sectors;
        std::vector<PreviewNode*> next_parents;

        auto create_child = [&](PreviewNode* parent, const PreviewRoomSpec& spec, float angle, float spread) {
            RoomGeometry geom = fetch_room_geometry(rooms_data, spec.name);
            auto node = std::make_unique<PreviewNode>();
            node->center = SDL_FPoint{
                static_cast<float>(std::cos(angle) * radius),
                static_cast<float>(std::sin(angle) * radius)
            };
            node->width = geom.max_width;
            node->height = geom.max_height;
            node->is_circle = geom.is_circle;
            node->layer = layer_spec.level;
            node->color = level_color(layer_spec.level);
            node->name = spec.name.empty() ? std::string("<room>") : spec.name;
            PreviewNode* ptr = node.get();
            preview_nodes_.push_back(std::move(node));
            ptr->parent = parent;
            if (parent) {
                parent->children.push_back(ptr);
                auto it = last_child_for_parent.find(parent);
                if (it != last_child_for_parent.end()) {
                    PreviewNode* prev = it->second;
                    if (prev) {
                        prev->right_sibling = ptr;
                        ptr->left_sibling = prev;
                    }
                }
                last_child_for_parent[parent] = ptr;
            }
            preview_edges_.push_back(PreviewEdge{ parent, ptr, SDL_Color{200, 200, 200, 255}, false });
            nodes_by_level[layer_spec.level].push_back(ptr);
            next_parents.push_back(ptr);
            next_sectors.push_back(PreviewSector{ ptr, angle - spread * 0.5f, spread });
        };

        if (li == 1) {
            if (!children.empty()) {
                std::shuffle(children.begin(), children.end(), rng);
                float slice = static_cast<float>(kTau / children.size());
                float buffer = slice * 0.05f;
                float spread = std::max(slice - buffer * 2.0f, 0.01f);
                for (size_t idx = 0; idx < children.size(); ++idx) {
                    float angle = static_cast<float>(idx) * slice + buffer;
                    create_child(root_ptr, children[idx], angle, spread);
                }
            }
        } else {
            if (current_sectors.empty()) continue;

            std::unordered_map<PreviewNode*, std::vector<PreviewRoomSpec>> assignments;
            const auto& prev_layer = layer_specs[li - 1];
            for (const auto& sector : current_sectors) {
                for (const auto& prev_room : prev_layer.rooms) {
                    if (sector.node->name == prev_room.name) {
                        for (const auto& child : prev_room.required_children) {
                            PreviewRoomSpec required;
                            required.name = child;
                            required.min_instances = 1;
                            required.max_instances = 1;
                            assignments[sector.node].push_back(required);
                        }
                    }
                }
            }

            std::vector<PreviewNode*> parent_order;
            parent_order.reserve(current_sectors.size());
            for (const auto& sector : current_sectors) {
                parent_order.push_back(sector.node);
                assignments.try_emplace(sector.node);
            }

            if (!parent_order.empty()) {
                std::vector<int> counts(parent_order.size(), 0);
                for (const auto& child_spec : children) {
                    auto min_it = std::min_element(counts.begin(), counts.end());
                    size_t parent_index = static_cast<size_t>(std::distance(counts.begin(), min_it));
                    PreviewNode* parent = parent_order[parent_index];
                    assignments[parent].push_back(child_spec);
                    counts[parent_index] += 1;
                }
            }

            for (const auto& sector : current_sectors) {
                auto assignment_it = assignments.find(sector.node);
                if (assignment_it == assignments.end()) continue;
                auto kids = assignment_it->second;
                if (kids.empty()) continue;
                std::shuffle(kids.begin(), kids.end(), rng);
                float slice = kids.size() > 0 ? sector.span / static_cast<float>(kids.size()) : sector.span;
                if (slice <= 0.0f) slice = sector.span;
                float buffer = slice * 0.05f;
                float spread = std::max(slice - buffer * 2.0f, 0.01f);
                for (size_t idx = 0; idx < kids.size(); ++idx) {
                    float angle = sector.start + static_cast<float>(idx) * slice + buffer;
                    create_child(sector.node, kids[idx], angle, spread);
                }
            }
        }

        current_parents = std::move(next_parents);
        current_sectors = std::move(next_sectors);
    }

    SDL_Color trail_color{120, 170, 240, 180};
    for (const auto& node_uptr : preview_nodes_) {
        if (!node_uptr) continue;
        PreviewNode* parent = node_uptr.get();
        if (parent->children.size() > 1) {
            for (size_t i = 0; i + 1 < parent->children.size(); ++i) {
                preview_edges_.push_back(PreviewEdge{ parent->children[i], parent->children[i + 1], trail_color, true });
            }
            if (parent->children.size() > 2) {
                preview_edges_.push_back(PreviewEdge{ parent->children.back(), parent->children.front(), trail_color, true });
            }
        }
    }

    for (auto& [level, nodes] : nodes_by_level) {
        if (nodes.size() <= 1) continue;
        std::sort(nodes.begin(), nodes.end(), [](const PreviewNode* a, const PreviewNode* b) {
            double angle_a = std::atan2(a->center.y, a->center.x);
            double angle_b = std::atan2(b->center.y, b->center.x);
            return angle_a < angle_b;
        });
        for (size_t i = 0; i + 1 < nodes.size(); ++i) {
            if (nodes[i]->parent == nodes[i + 1]->parent) continue;
            preview_edges_.push_back(PreviewEdge{ nodes[i], nodes[i + 1], trail_color, true });
        }
        if (nodes.size() > 2 && nodes.back()->parent != nodes.front()->parent) {
            preview_edges_.push_back(PreviewEdge{ nodes.back(), nodes.front(), trail_color, true });
        }
    }

    double node_extent = 0.0;
    for (const auto& node_uptr : preview_nodes_) {
        if (!node_uptr) continue;
        const PreviewNode* node = node_uptr.get();
        double distance = std::sqrt(node->center.x * node->center.x + node->center.y * node->center.y);
        double half_diag = 0.5 * std::sqrt(node->width * node->width + node->height * node->height);
        node_extent = std::max(node_extent, distance + half_diag);
    }
    if (node_extent > preview_extent_) {
        preview_extent_ = node_extent;
    }

    if (canvas_widget_) {
        canvas_widget_->refresh();
    }
}

void MapLayersPanel::select_layer(int index) {
    selected_layer_ = index;
    if (sidebar_widget_) sidebar_widget_->set_selected(index);
    if (canvas_widget_) canvas_widget_->set_selected(index);
}

void MapLayersPanel::mark_dirty() {
    dirty_ = true;
    if (sidebar_widget_) sidebar_widget_->set_dirty(true);
    request_preview_regeneration();
}

void MapLayersPanel::mark_clean() {
    dirty_ = false;
    if (sidebar_widget_) sidebar_widget_->set_dirty(false);
}

void MapLayersPanel::ensure_layers_array() {
    if (!map_info_) return;
    if (!map_info_->contains("map_layers") || !(*map_info_)["map_layers"].is_array()) {
        (*map_info_)["map_layers"] = json::array();
    }
}

void MapLayersPanel::ensure_layer_indices() {
    if (!map_info_) return;
    auto& arr = layers_array();
    for (size_t i = 0; i < arr.size(); ++i) {
        if (!arr[i].is_object()) arr[i] = json::object();
        arr[i]["level"] = static_cast<int>(i);
        if (!arr[i].contains("name")) {
            std::ostringstream oss;
            oss << "layer_" << i;
            arr[i]["name"] = oss.str();
        }
        if (!arr[i].contains("min_rooms")) arr[i]["min_rooms"] = 0;
        if (!arr[i].contains("max_rooms")) arr[i]["max_rooms"] = 0;
        if (!arr[i].contains("radius")) arr[i]["radius"] = 0;
        if (!arr[i].contains("rooms") || !arr[i]["rooms"].is_array()) {
            arr[i]["rooms"] = json::array();
        }
    }
}

nlohmann::json* MapLayersPanel::layer_at(int index) {
    if (!map_info_) return nullptr;
    auto& arr = layers_array();
    if (index < 0 || index >= static_cast<int>(arr.size())) return nullptr;
    return &arr[index];
}

const nlohmann::json* MapLayersPanel::layer_at(int index) const {
    if (!map_info_) return nullptr;
    const auto& arr = layers_array();
    if (index < 0 || index >= static_cast<int>(arr.size())) return nullptr;
    return &arr[index];
}

void MapLayersPanel::rebuild_rows() {
    DockableCollapsible::Rows rows;
    rows.push_back({ canvas_widget_.get(), sidebar_widget_.get() });
    set_rows(rows);
}

void MapLayersPanel::rebuild_available_rooms() {
    available_rooms_.clear();
    if (!map_info_) return;
    const auto rooms_it = map_info_->find("rooms_data");
    if (rooms_it != map_info_->end() && rooms_it->is_object()) {
        for (auto it = rooms_it->begin(); it != rooms_it->end(); ++it) {
            available_rooms_.push_back(it.key());
        }
        std::sort(available_rooms_.begin(), available_rooms_.end());
    }
    if (room_selector_ && room_selector_->visible()) {
        room_selector_->set_rooms(available_rooms_);
    }
}

void MapLayersPanel::refresh_canvas() {
    if (canvas_widget_) canvas_widget_->refresh();
}

int MapLayersPanel::append_layer_entry(const std::string& display_name) {
    if (!map_info_) return -1;
    auto& arr = layers_array();
    const int idx = static_cast<int>(arr.size());
    std::string name = display_name.empty() ? std::string("layer_") + std::to_string(idx) : display_name;
    int radius = arr.empty() ? 0 : compute_next_layer_radius(arr);
    json new_layer = {
        {"level", idx},
        {"name", name},
        {"radius", arr.empty() ? 0 : radius},
        {"min_rooms", 0},
        {"max_rooms", 0},
        {"rooms", json::array()}
    };
    arr.push_back(std::move(new_layer));
    ensure_layer_indices();
    return idx;
}

void MapLayersPanel::add_layer_internal() {
    if (!map_info_) return;
    const int idx = append_layer_entry();
    if (idx < 0) return;
    refresh_canvas();
    select_layer(idx);
    mark_dirty();
}

void MapLayersPanel::add_room_to_selected_layer() {
    if (selected_layer_ < 0) return;
    int layer_index = selected_layer_;
    request_room_selection([this, layer_index](const std::string& room) {
        this->handle_candidate_added(layer_index, room);
    });
}

std::string MapLayersPanel::create_new_room(const std::string& desired_name) {
    if (!map_info_) return {};
    std::string trimmed = trim_copy_local(desired_name);
    std::string key = sanitize_room_key(trimmed);
    nlohmann::json& rooms_data = (*map_info_)["rooms_data"];
    if (!rooms_data.is_object()) {
        rooms_data = nlohmann::json::object();
    }
    std::string unique = make_unique_room_key(rooms_data, key);
    rooms_data[unique] = make_default_room_json(unique);
    rebuild_available_rooms();
    mark_dirty();
    return unique;
}

std::string MapLayersPanel::suggest_room_name() const {
    if (!map_info_) return std::string("room");
    auto it = map_info_->find("rooms_data");
    if (it != map_info_->end() && it->is_object()) {
        return make_unique_room_key(*it, std::string("room"));
    }
    return std::string("room");
}

bool MapLayersPanel::ensure_child_room_exists(int parent_layer_index, const std::string& child, bool* layer_created) {
    if (layer_created) *layer_created = false;
    if (!map_info_) return false;
    if (child.empty()) return false;

    auto& arr = layers_array();
    int child_layer_index = parent_layer_index + 1;
    bool modified = false;
    bool new_layer = false;

    if (child_layer_index >= static_cast<int>(arr.size())) {
        int appended_index = append_layer_entry();
        if (appended_index < 0) return false;
        child_layer_index = appended_index;
        new_layer = true;
        modified = true;
    }

    if (layer_created) *layer_created = new_layer;

    json* child_layer = layer_at(child_layer_index);
    if (!child_layer) return modified;
    auto& rooms = (*child_layer)["rooms"];
    if (!rooms.is_array()) {
        rooms = json::array();
        modified = true;
    }

    auto it = std::find_if(rooms.begin(), rooms.end(), [&](const json& entry) {
        return entry.is_object() && entry.value("name", std::string()) == child;
    });
    if (it == rooms.end()) {
        json candidate = {
            {"name", child},
            {"min_instances", 1},
            {"max_instances", 1},
            {"required_children", json::array()}
        };
        rooms.push_back(std::move(candidate));
        modified = true;
    } else {
        json& entry = *it;
        int min_inst = entry.value("min_instances", 0);
        int max_inst = entry.value("max_instances", min_inst);
        if (min_inst < 1) {
            entry["min_instances"] = 1;
            modified = true;
        }
        if (max_inst < 1) {
            entry["max_instances"] = 1;
            modified = true;
        }
    }

    clamp_layer_room_ranges(*child_layer);
    return modified;
}

void MapLayersPanel::delete_layer_internal(int index) {
    if (!map_info_) return;
    auto& arr = layers_array();
    if (index < 0 || index >= static_cast<int>(arr.size())) return;
    arr.erase(arr.begin() + index);
    ensure_layer_indices();
    refresh_canvas();
    if (selected_layer_ >= static_cast<int>(arr.size())) {
        select_layer(static_cast<int>(arr.size()) - 1);
    }
    mark_dirty();
}

void MapLayersPanel::open_layer_config_internal(int index) {
    if (!layer_config_) return;
    if (!map_info_) return;
    auto* layer = layer_at(index);
    if (!layer) return;
    select_layer(index);
    layer_config_->open(index, layer);
}

void MapLayersPanel::handle_layer_range_changed(int index, int min_rooms, int max_rooms) {
    auto* layer = layer_at(index);
    if (!layer) return;
    (*layer)["min_rooms"] = min_rooms;
    (*layer)["max_rooms"] = max_rooms;
    mark_dirty();
}

void MapLayersPanel::handle_layer_name_changed(int index, const std::string& name) {
    auto* layer = layer_at(index);
    if (!layer) return;
    (*layer)["name"] = name;
    mark_dirty();
    refresh_canvas();
}

void MapLayersPanel::handle_candidate_range_changed(int layer_index, int candidate_index, int min_instances, int max_instances) {
    auto* layer = layer_at(layer_index);
    if (!layer) return;
    auto rooms_it = layer->find("rooms");
    if (rooms_it == layer->end() || !rooms_it->is_array()) return;
    if (candidate_index < 0 || candidate_index >= static_cast<int>(rooms_it->size())) return;
    (*rooms_it)[candidate_index]["min_instances"] = min_instances;
    (*rooms_it)[candidate_index]["max_instances"] = max_instances;
    clamp_layer_room_ranges(*layer);
    mark_dirty();
}

void MapLayersPanel::handle_candidate_removed(int layer_index, int candidate_index) {
    auto* layer = layer_at(layer_index);
    if (!layer) return;
    auto rooms_it = layer->find("rooms");
    if (rooms_it == layer->end() || !rooms_it->is_array()) return;
    if (candidate_index < 0 || candidate_index >= static_cast<int>(rooms_it->size())) return;
    rooms_it->erase(rooms_it->begin() + candidate_index);
    clamp_layer_room_ranges(*layer);
    mark_dirty();
    if (layer_config_) layer_config_->refresh();
}

void MapLayersPanel::handle_candidate_child_added(int layer_index, int candidate_index, const std::string& child) {
    auto* layer = layer_at(layer_index);
    if (!layer) return;
    auto rooms_it = layer->find("rooms");
    if (rooms_it == layer->end() || !rooms_it->is_array()) return;
    if (candidate_index < 0 || candidate_index >= static_cast<int>(rooms_it->size())) return;
    auto& entry = (*rooms_it)[candidate_index];
    auto& children = entry["required_children"];
    if (!children.is_array()) children = json::array();
    bool changed = false;
    if (std::find(children.begin(), children.end(), child) == children.end()) {
        children.push_back(child);
        changed = true;
    }
    bool layer_created = false;
    bool child_changed = ensure_child_room_exists(layer_index, child, &layer_created);
    if (layer_created) {
        refresh_canvas();
    }
    if (changed || child_changed) {
        mark_dirty();
        if (layer_config_) layer_config_->refresh();
    }
}

void MapLayersPanel::handle_candidate_child_removed(int layer_index, int candidate_index, const std::string& child) {
    auto* layer = layer_at(layer_index);
    if (!layer) return;
    auto rooms_it = layer->find("rooms");
    if (rooms_it == layer->end() || !rooms_it->is_array()) return;
    if (candidate_index < 0 || candidate_index >= static_cast<int>(rooms_it->size())) return;
    auto& entry = (*rooms_it)[candidate_index];
    auto& children = entry["required_children"];
    if (!children.is_array()) return;
    auto it = std::find(children.begin(), children.end(), child);
    if (it != children.end()) {
        children.erase(it);
        mark_dirty();
    }
}

void MapLayersPanel::handle_candidate_added(int layer_index, const std::string& room_name) {
    auto* layer = layer_at(layer_index);
    if (!layer) return;
    auto& rooms = (*layer)["rooms"];
    if (!rooms.is_array()) rooms = json::array();
    json candidate = {
        {"name", room_name},
        {"min_instances", 1},
        {"max_instances", 1},
        {"required_children", json::array()}
    };
    rooms.push_back(std::move(candidate));
    clamp_layer_room_ranges(*layer);
    mark_dirty();
    if (layer_config_) layer_config_->refresh();
}

void MapLayersPanel::update_save_button_state() {
    if (sidebar_widget_) sidebar_widget_->set_dirty(dirty_);
}

bool MapLayersPanel::save_layers_to_disk() {
    if (!map_info_) return false;
    if (on_save_) {
        bool ok = on_save_();
        if (ok) mark_clean();
        return ok;
    }
    std::string path = map_path_.empty() ? std::string{} : (map_path_ + "/map_info.json");
    if (path.empty()) return false;
    std::ofstream out(path);
    if (!out) {
        std::cerr << "[MapLayersPanel] Failed to open " << path << " for writing\n";
        return false;
    }
    try {
        out << map_info_->dump(2);
        mark_clean();
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[MapLayersPanel] Serialize error: " << ex.what() << "\n";
        return false;
    }
}

bool MapLayersPanel::reload_layers_from_disk() {
    std::string path = map_path_.empty() ? std::string{} : (map_path_ + "/map_info.json");
    if (path.empty() || !map_info_) return false;
    std::ifstream in(path);
    if (!in) {
        std::cerr << "[MapLayersPanel] Failed to open " << path << " for reading\n";
        return false;
    }
    try {
        json fresh;
        in >> fresh;
        *map_info_ = std::move(fresh);
        ensure_layers_array();
        ensure_layer_indices();
        rebuild_available_rooms();
        refresh_canvas();
        if (layer_config_) layer_config_->close();
        request_preview_regeneration();
        regenerate_preview();
        mark_clean();
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[MapLayersPanel] Parse error: " << ex.what() << "\n";
        return false;
    }
}

void MapLayersPanel::ensure_layer_config_valid() {
    if (!layer_config_ || !layer_config_->is_visible()) return;
    if (selected_layer_ < 0 || !layer_at(selected_layer_)) {
        layer_config_->close();
    }
}

void MapLayersPanel::request_room_selection(const std::function<void(const std::string&)>& cb) {
    if (!room_selector_) return;
    if (available_rooms_.empty()) rebuild_available_rooms();
    room_selector_->open(available_rooms_, cb);
}


