#include "map_layers_panel.hpp"

#include "dm_styles.hpp"

#include "map_layers_controller.hpp"

#include "map_layers_common.hpp"

#include "room_configurator.hpp"

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

#include <limits>

#include <sstream>

#include <unordered_map>

#include <utility>

#include <tuple>

#include <vector>

#include <nlohmann/json.hpp>

namespace {

constexpr int kCanvasPreferredHeight = 320;

constexpr int kCanvasPadding = 16;

constexpr int kRoomRangeMaxDefault = 64;

using map_layers::kCandidateRangeMax;

using map_layers::clamp_candidate_max;

using map_layers::clamp_candidate_min;

constexpr int kLayerRadiusStepDefault = 512;

constexpr double kLayerRadiusSpacingPadding = 64.0;

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

            SDL_RenderDrawLine(r, static_cast<int>(std::lround(prev_x)), static_cast<int>(std::lround(prev_y)),  static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));

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

Uint8 lerp_channel(Uint8 from, Uint8 to, float t) {

    t = std::clamp(t, 0.0f, 1.0f);

    float value = static_cast<float>(from) + (static_cast<float>(to) - static_cast<float>(from)) * t;

    value = std::clamp(value, 0.0f, 255.0f);

    return static_cast<Uint8>(std::lround(value));

}

SDL_Color mix_color(SDL_Color from, SDL_Color to, float t) {

    return SDL_Color{

        lerp_channel(from.r, to.r, t),  lerp_channel(from.g, to.g, t),  lerp_channel(from.b, to.b, t),  lerp_channel(from.a, to.a, t)  };

}

SDL_Color lighten_color(SDL_Color color, float amount) {

    SDL_Color white{255, 255, 255, color.a};

    return mix_color(color, white, amount);

}

SDL_Color apply_alpha(SDL_Color color, Uint8 alpha) {

    color.a = alpha;

    return color;

}

uint32_t mix_geometry_seed(uint32_t base, const std::string& key) {

    uint64_t value = static_cast<uint64_t>(base);

    value ^= static_cast<uint64_t>(std::hash<std::string>{}(key));

    value ^= value >> 33;

    value *= 0xff51afd7ed558ccdull;

    value ^= value >> 33;

    value *= 0xc4ceb9fe1a85ec53ull;

    value ^= value >> 33;

    return static_cast<uint32_t>(value & 0xffffffffu);

}

struct RoomGeometry {

    double max_width = 0.0;

    double max_height = 0.0;

    bool is_circle = false;

    std::vector<SDL_FPoint> outline;

};

RoomGeometry fetch_room_geometry(const nlohmann::json* rooms_data, const std::string& room_name, uint32_t seed = 0) {

    RoomGeometry geom;

    if (!rooms_data || !rooms_data->is_object()) return geom;

    auto it = rooms_data->find(room_name);

    if (it == rooms_data->end() || !it->is_object()) return geom;

    const auto& room = *it;

    auto extract_dimension = [&room](const char* primary, const char* fallback1,
                                     const char* fallback2, const char* fallback3) -> double {

        if (room.contains(primary)) return room.value(primary, 0.0);

        if (room.contains(fallback1)) return room.value(fallback1, 0.0);

        if (room.contains(fallback2)) return room.value(fallback2, 0.0);

        if (room.contains(fallback3)) return room.value(fallback3, 0.0);

        return 0.0;

};

    geom.max_width = extract_dimension("max_width", "width_max", "min_width", "width_min");

    geom.max_height = extract_dimension("max_height", "height_max", "min_height", "height_min");

    std::string geometry = room.value("geometry", std::string());

    int edge_smoothness = room.value("edge_smoothness", 75);

    edge_smoothness = std::clamp(edge_smoothness, 0, 100);

    if (!geometry.empty()) {

        std::string lowered;

        lowered.reserve(geometry.size());

        for (char ch : geometry) {

            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));

        }

        if (lowered == "circle") {

            geom.is_circle = true;

        }

        geometry = std::move(lowered);

    }

    if (geom.max_width <= 0.0 && geom.max_height <= 0.0) {

        geom.max_width = 100.0;

        geom.max_height = 100.0;

    } else if (geom.max_width <= 0.0) {

        geom.max_width = geom.max_height;

    } else if (geom.max_height <= 0.0) {

        geom.max_height = geom.max_width;

    }

    geom.outline.clear();

    const double width = std::max(geom.max_width, 1.0);

    const double height = std::max(geom.max_height, 1.0);

    const bool use_randomness = (seed != 0);

    uint32_t local_seed = use_randomness ? mix_geometry_seed(seed, room_name) : 0u;

    if (geom.is_circle || geometry == "circle") {

        const double radius = std::max(width, height) * 0.5;

        if (radius > 0.0) {

            const int segments = std::max(12, 6 + edge_smoothness * 2);

            const double max_dev = 0.20 * (100 - edge_smoothness) / 100.0;

            std::mt19937 rng(local_seed == 0u ? 0x6d5a56e9u : local_seed);

            std::uniform_real_distribution<double> dist(1.0 - max_dev, 1.0 + max_dev);

            geom.outline.reserve(segments);

            for (int i = 0; i < segments; ++i) {

                const double theta = (static_cast<double>(i) / segments) * kTau;

                double scale = 1.0;

                if (use_randomness) {

                    scale = dist(rng);

                }

                const double r = radius * scale;

                geom.outline.push_back(SDL_FPoint{

                    static_cast<float>(std::cos(theta) * r),

                    static_cast<float>(std::sin(theta) * r)

                });

            }

        }

    } else if (geometry == "point") {

        geom.outline.push_back(SDL_FPoint{0.0f, 0.0f});

    } else {

        const double half_w = width * 0.5;

        const double half_h = height * 0.5;

        const double max_dev = 0.25 * (100 - edge_smoothness) / 100.0;

        std::mt19937 rng(local_seed == 0u ? 0x6d5a56e9u : local_seed);

        std::uniform_real_distribution<double> xoff(-max_dev * width, max_dev * width);

        std::uniform_real_distribution<double> yoff(-max_dev * height, max_dev * height);

        auto jitter = [&](double base, std::uniform_real_distribution<double>& dist) {

            if (!use_randomness) return base;

            return base + dist(rng);

};

        geom.outline = {

            SDL_FPoint{ static_cast<float>(jitter(-half_w, xoff)), static_cast<float>(jitter(-half_h, yoff)) },

            SDL_FPoint{ static_cast<float>(jitter( half_w, xoff)), static_cast<float>(jitter(-half_h, yoff)) },

            SDL_FPoint{ static_cast<float>(jitter( half_w, xoff)), static_cast<float>(jitter( half_h, yoff)) },

            SDL_FPoint{ static_cast<float>(jitter(-half_w, xoff)), static_cast<float>(jitter( half_h, yoff)) }

};

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

    constexpr int kDefaultRoomMin = 1500;

    constexpr int kDefaultRoomMax = 10000;

    room["min_width"] = kDefaultRoomMin;

    room["max_width"] = kDefaultRoomMax;

    room["width_min"] = kDefaultRoomMin;

    room["width_max"] = kDefaultRoomMax;

    room["min_height"] = kDefaultRoomMin;

    room["max_height"] = kDefaultRoomMax;

    room["height_min"] = kDefaultRoomMin;

    room["height_max"] = kDefaultRoomMax;

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

        return kLayerRadiusStepDefault + static_cast<int>(std::ceil(kLayerRadiusSpacingPadding));

    }

    return max_radius + step + static_cast<int>(std::ceil(kLayerRadiusSpacingPadding));

}

void clamp_layer_room_counts(nlohmann::json& layer) {

    if (!layer.is_object()) return;

    int min_sum = 0;

    int max_sum = 0;

    const auto rooms_it = layer.find("rooms");

    if (rooms_it != layer.end() && rooms_it->is_array()) {

        for (auto& candidate : *rooms_it) {

            if (!candidate.is_object()) continue;

            int min_inst = clamp_candidate_min(candidate.value("min_instances", 0));

            int max_inst = clamp_candidate_max(min_inst, candidate.value("max_instances", min_inst));

            candidate["min_instances"] = min_inst;

            candidate["max_instances"] = max_inst;

            min_sum += min_inst;

            max_sum += max_inst;

        }

    }

    int derived_min = min_sum;

    int derived_max = std::max(min_sum, max_sum);

    derived_min = std::min(derived_min, kRoomRangeMaxDefault);

    derived_max = std::min(derived_max, kRoomRangeMaxDefault);

    layer["min_rooms"] = derived_min;

    layer["max_rooms"] = derived_max;

}

struct PreviewRoomSpec {

    std::string name;

    int max_instances = 0;

    std::vector<std::string> required_children;

};

struct PreviewLayerSpec {

    int level = 0;

    double radius = 0.0;

    int max_rooms = 0;

    std::vector<PreviewRoomSpec> rooms;

};

std::vector<PreviewRoomSpec> build_children_pool(const PreviewLayerSpec& layer, std::mt19937& rng) {

    std::vector<PreviewRoomSpec> result;

    const int target = std::max(0, layer.max_rooms);

    if (target == 0) return result;

    std::vector<PreviewRoomSpec> candidates;

    for (const auto& room : layer.rooms) {

        const int count = std::max(0, room.max_instances);

        for (int i = 0; i < count; ++i) {

            candidates.push_back(room);

        }

    }

    if (candidates.empty()) return result;

    std::shuffle(candidates.begin(), candidates.end(), rng);

    if (static_cast<int>(candidates.size()) <= target) {

        return candidates;

    }

    result.insert(result.end(), candidates.begin(), candidates.begin() + target);

    return result;

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

        mix(static_cast<uint64_t>(static_cast<uint32_t>(layer.max_rooms)));

        mix(static_cast<uint64_t>(static_cast<uint32_t>(layer.level)));

        for (const auto& room : layer.rooms) {

            mix(static_cast<uint64_t>(std::hash<std::string>{}(room.name)));

            mix(static_cast<uint64_t>(static_cast<uint32_t>(room.max_instances)));

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

}

using nlohmann::json;

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

    auto compute_metrics = [&]() -> std::tuple<bool, int, int, double> {

        if (circles_.empty()) return { false, 0, 0, 1.0 };

        const auto& arr = owner_->layers_array();

        if (arr.empty()) return { false, 0, 0, 1.0 };

        double max_radius = 1.0;

        for (const auto& layer : arr) {

            if (layer.is_object()) {

                max_radius = std::max(max_radius, static_cast<double>(layer.value("radius", 0)));

            }

        }

        const int center_x = rect_.x + rect_.w / 2;

        const int center_y = rect_.y + rect_.h / 2;

        const int draw_radius_max = std::max(8, std::min(rect_.w, rect_.h) / 2 - kCanvasPadding);

        if (draw_radius_max <= 0) {

            return { false, 0, 0, 1.0 };

        }

        double display_extent = std::max(max_radius, owner_->preview_extent_);

        if (display_extent <= 0.0) display_extent = 1.0;

        double scale = static_cast<double>(draw_radius_max) / display_extent;

        if (scale <= 0.0) {

            return { false, 0, 0, 1.0 };

        }

        return { true, center_x, center_y, scale };

};

    auto update_hover = [&](const SDL_Point& p, int center_x, int center_y, double scale) {

        const PreviewNode* hovered_room = owner_->find_room_at(p.x, p.y, center_x, center_y, scale);

        int hovered_layer = hovered_room ? hovered_room->layer

                                         : owner_->find_layer_at(p.x, p.y, center_x, center_y, scale);

        owner_->update_hover_target(hovered_layer, hovered_room ? hovered_room->name : std::string());

};

    if (e.type == SDL_MOUSEMOTION) {

        SDL_Point p{ e.motion.x, e.motion.y };

        if (!SDL_PointInRect(&p, &rect_)) {

            owner_->clear_hover_target();

            return false;

        }

        auto [ok, cx, cy, scale] = compute_metrics();

        if (!ok) {

            owner_->clear_hover_target();

            return false;

        }

        update_hover(p, cx, cy, scale);

        return false;

    }

    if (e.type != SDL_MOUSEBUTTONUP) return false;

    SDL_Point p{ e.button.x, e.button.y };

    if (!SDL_PointInRect(&p, &rect_)) {

        owner_->clear_hover_target();

        return false;

    }

    auto [ok, center_x, center_y, scale] = compute_metrics();

    if (!ok) {

        owner_->clear_hover_target();

        return false;

    }

    if (e.button.button == SDL_BUTTON_LEFT) {

        if (owner_->handle_preview_room_click(p.x, p.y, center_x, center_y, scale)) {

            return true;

        }

    }

    if (e.button.button == SDL_BUTTON_RIGHT) {

        if (const auto* node = owner_->find_room_at(p.x, p.y, center_x, center_y, scale)) {

            owner_->update_click_target(node->layer, node->name);

            owner_->open_room_config_for(node->name);

            return true;

        }

    }

    int hit_index = owner_->find_layer_at(p.x, p.y, center_x, center_y, scale);

    if (hit_index < 0) {

        owner_->update_hover_target(-1, std::string());

        return false;

    }

    if (e.button.button == SDL_BUTTON_LEFT) {

        owner_->update_click_target(hit_index, std::string());

        owner_->select_layer(hit_index);

        return true;

    }

    if (e.button.button == SDL_BUTTON_RIGHT) {

        owner_->update_click_target(hit_index, std::string());

        owner_->open_layer_config_internal(hit_index);

        return true;

    }

    return false;

}

void MapLayersPanel::LayerCanvasWidget::render(SDL_Renderer* renderer) const {

    if (!renderer) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    const SDL_Color bg = DMStyles::PanelBG();

    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);

    SDL_RenderFillRect(renderer, &rect_);

    const SDL_Color border = DMStyles::Border();

    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);

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

    double map_radius_value = 0.0;

    if (owner_->map_info_) {

        map_radius_value = owner_->map_info_->value("map_radius", 0.0);

    }

    const int map_radius_pixels = map_radius_value > 0.0 ? std::max(12, static_cast<int>(std::lround(map_radius_value * scale))) : 0;

    const DMLabelStyle label_style = DMStyles::Label();

    if (map_radius_pixels > 0) {

        SDL_Color map_radius_color = lighten_color(DMStyles::AccentButton().bg, 0.1f);

        draw_circle(renderer, center_x, center_y, map_radius_pixels, map_radius_color, 2);

        std::ostringstream map_label;

        map_label << "Map Radius (" << static_cast<int>(std::lround(map_radius_value)) << ")";

        draw_text(renderer, map_label.str(), rect_.x + 12, rect_.y + 12, label_style);

    }

    const int hovered_layer = owner_->hovered_layer_index_;

    const int clicked_layer = owner_->clicked_layer_index_;

    const std::string& hovered_room = owner_->hovered_room_key_;

    const std::string& clicked_room = owner_->clicked_room_key_;

    const SDL_Color hover_accent = DMStyles::AccentButton().hover_bg;

    const SDL_Color clicked_layer_color = DMStyles::DeleteButton().bg;

    const SDL_Color clicked_room_color = DMStyles::DeleteButton().bg;

    for (const auto& info : circles_) {

        const json* layer = owner_->layer_at(info.index);

        if (!layer) continue;

        int radius_value = layer->value("radius", 0);

        int pixel_radius = static_cast<int>(std::lround(radius_value * scale));

        pixel_radius = std::max(12, pixel_radius);

        SDL_Color col = info.color;

        bool layer_clicked = (info.index == clicked_layer);

        bool layer_hovered = (info.index == hovered_layer);

        if (layer_clicked) {

            col = clicked_layer_color;

        } else if (layer_hovered) {

            col = lighten_color(col, 0.35f);

        }

        int thickness = 3;

        if (info.index == selected_index_) thickness = 6;

        if (layer_hovered) thickness = std::max(thickness, 5);

        if (layer_clicked) thickness = std::max(thickness, 6);

        draw_circle(renderer, center_x, center_y, pixel_radius, col, thickness);

        std::ostringstream oss;

        oss << info.label << " (" << radius_value << ")";

        draw_text(renderer, oss.str(), center_x - pixel_radius + 8, center_y - pixel_radius - 18, label_style);

    }

    if (!owner_->preview_edges_.empty()) {

        for (const auto& edge : owner_->preview_edges_) {

            if (!edge.from || !edge.to) continue;

            SDL_Point from_pt{

                static_cast<int>(std::lround(center_x + edge.from->center.x * scale)),  static_cast<int>(std::lround(center_y + edge.from->center.y * scale))  };

            SDL_Point to_pt{

                static_cast<int>(std::lround(center_x + edge.to->center.x * scale)),  static_cast<int>(std::lround(center_y + edge.to->center.y * scale))  };

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

                static_cast<int>(std::lround(center_x + node->center.x * scale)),  static_cast<int>(std::lround(center_y + node->center.y * scale))  };

            SDL_Color outline = node->color;

            bool room_clicked = (!clicked_room.empty() && clicked_room == node->name);

            bool room_hovered = (!hovered_room.empty() && hovered_room == node->name);

            if (room_clicked) {

                outline = clicked_room_color;

            } else if (room_hovered) {

                outline = lighten_color(outline, 0.45f);

            }

            if (node->is_circle) {

                int radius = static_cast<int>(std::lround(std::max(2.0, (node->width * 0.5) * scale)));

                draw_circle(renderer, center_pt.x, center_pt.y, radius, outline, room_clicked ? 4 : (room_hovered ? 3 : 2));

            } else {

                int half_w = static_cast<int>(std::lround(std::max(2.0, (node->width * 0.5) * scale)));

                int half_h = static_cast<int>(std::lround(std::max(2.0, (node->height * 0.5) * scale)));

                SDL_Rect room_rect{ center_pt.x - half_w, center_pt.y - half_h, half_w * 2, half_h * 2 };

                if (room_clicked || room_hovered) {

                    SDL_Color fill = room_clicked ? apply_alpha(clicked_room_color, 90) : apply_alpha(hover_accent, 80);

                    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);

                    SDL_RenderFillRect(renderer, &room_rect);

                }

                SDL_SetRenderDrawColor(renderer, outline.r, outline.g, outline.b, 220);

                SDL_RenderDrawRect(renderer, &room_rect);

            }

            if (!node->outline.empty()) {

                SDL_Color geom_color = lighten_color(outline, 0.25f);

                if (room_clicked) {

                    geom_color = clicked_room_color;

                } else if (room_hovered) {

                    geom_color = lighten_color(geom_color, 0.3f);

                }

                geom_color.a = static_cast<Uint8>(room_clicked ? 255 : 200);

                std::vector<SDL_Point> polygon;

                polygon.reserve(node->outline.size() + 1);

                for (const auto& offset : node->outline) {

                    double world_x = node->center.x + offset.x;

                    double world_y = node->center.y + offset.y;

                    polygon.push_back(SDL_Point{

                        static_cast<int>(std::lround(center_x + world_x * scale)),

                        static_cast<int>(std::lround(center_y + world_y * scale))

                    });

                }

                SDL_SetRenderDrawColor(renderer, geom_color.r, geom_color.g, geom_color.b, geom_color.a);

                if (polygon.size() == 1) {

                    SDL_RenderDrawPoint(renderer, polygon.front().x, polygon.front().y);

                } else if (polygon.size() >= 2) {

                    polygon.push_back(polygon.front());

                    SDL_RenderDrawLines(renderer, polygon.data(), static_cast<int>(polygon.size()));

                }

            }

            const SDL_Color accent = DMStyles::AccentButton().hover_bg;

            SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, room_clicked ? 180 : 120);

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

            if (room_clicked) {

                DMLabelStyle clicked_label = DMStyles::Label();

                clicked_label.color = clicked_room_color;

                draw_text_centered(renderer, room_label, label_x, label_y, clicked_label);

            } else if (room_hovered) {

                DMLabelStyle hover_label = DMStyles::Label();

                hover_label.color = mix_color(hover_label.color, hover_accent, 0.5f);

                draw_text_centered(renderer, room_label, label_x, label_y, hover_label);

            } else {

                draw_text_centered(renderer, room_label, label_x, label_y, label_style);

            }

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

class MapLayersPanel::PanelSidebarWidget : public Widget {

public:

    explicit PanelSidebarWidget(MapLayersPanel* owner);

    void set_layer_config(LayerConfigPanel* panel);

    void set_selected(int index) { selected_layer_ = index; }

    const SDL_Rect& config_rect() const { return config_rect_; }

    void set_rect(const SDL_Rect& r) override;

    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int w) const override { return std::max(kCanvasPreferredHeight, w); }

    bool handle_event(const SDL_Event& e) override;

    void render(SDL_Renderer* renderer) const override;

private:

    MapLayersPanel* owner_ = nullptr;

    LayerConfigPanel* config_panel_ = nullptr;

    SDL_Rect rect_{0,0,0,0};

    std::unique_ptr<DMButton> add_button_;

    std::unique_ptr<DMButton> new_room_button_;

    std::unique_ptr<DMButton> reload_button_;

    std::unique_ptr<DMButton> delete_button_;

    std::unique_ptr<DMButton> preview_button_;

    int selected_layer_ = -1;

    SDL_Rect config_rect_{0,0,0,0};

};

MapLayersPanel::PanelSidebarWidget::PanelSidebarWidget(MapLayersPanel* owner)

    : owner_(owner) {

    add_button_ = std::make_unique<DMButton>("Add Layer", &DMStyles::CreateButton(), 140, DMButton::height());

    new_room_button_ = std::make_unique<DMButton>("New Room", &DMStyles::CreateButton(), 140, DMButton::height());

    reload_button_ = std::make_unique<DMButton>("Reload", &DMStyles::HeaderButton(), 140, DMButton::height());

    delete_button_ = std::make_unique<DMButton>("Delete Layer", &DMStyles::DeleteButton(), 140, DMButton::height());

    preview_button_ = std::make_unique<DMButton>("Generate Preview", &DMStyles::WarnButton(), 140, DMButton::height());

}

void MapLayersPanel::PanelSidebarWidget::render(SDL_Renderer* renderer) const {

    if (!renderer) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    const SDL_Color bg = DMStyles::PanelBG();

    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);

    SDL_RenderFillRect(renderer, &rect_);

    const SDL_Color border = DMStyles::Border();

    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);

    SDL_RenderDrawRect(renderer, &rect_);

    if (add_button_) add_button_->render(renderer);

    if (new_room_button_) new_room_button_->render(renderer);

    if (preview_button_) preview_button_->render(renderer);

    if (delete_button_) delete_button_->render(renderer);

    if (reload_button_) reload_button_->render(renderer);

}

class SummaryRangeWidget : public Widget {

public:

    explicit SummaryRangeWidget(std::string label)

        : label_(std::move(label)) {}

    void set_values(int min_value, int max_value) {

        min_value_ = std::max(0, min_value);

        max_value_ = std::max(min_value_, max_value);

    }

    void set_rect(const SDL_Rect& r) override {

        rect_ = r;

    }

    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int ) const override {

        const DMLabelStyle label_style = DMStyles::Label();

        const int gap = DMSpacing::small_gap();

        return label_style.font_size * 2 + gap + DMSpacing::item_gap();

    }

    bool handle_event(const SDL_Event& ) override { return false; }

    void render(SDL_Renderer* renderer) const override {

        if (!renderer) return;

        const DMLabelStyle label_style = DMStyles::Label();

        const int text_x = rect_.x + DMSpacing::item_gap();

        draw_text(renderer, label_, text_x, rect_.y, label_style);

        std::ostringstream oss;

        oss << "Min " << min_value_ << " \u2022 Max " << max_value_;

        const int value_y = rect_.y + label_style.font_size + DMSpacing::small_gap();

        draw_text(renderer, oss.str(), text_x, value_y, label_style);

    }

private:

    std::string label_;

    SDL_Rect rect_{0, 0, 0, 0};

    int min_value_ = 0;

    int max_value_ = 0;

};

class MapLayersPanel::RoomCandidateWidget : public Widget {

public:

    RoomCandidateWidget(LayerConfigPanel* owner, int layer_index, int candidate_index, json* candidate, bool editable);

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

    bool editable_ = true;

    SDL_Rect rect_{0,0,0,0};

    std::unique_ptr<DMRangeSlider> range_slider_;

    std::unique_ptr<DMButton> add_child_button_;

    std::unique_ptr<DMButton> delete_button_;

    int min_count_cache_ = 0;

    int max_count_cache_ = 0;

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

    void request_refresh();

    void ensure_cleanup();

    MapLayersPanel* panel_owner() const { return owner_; }

    int current_layer() const { return layer_index_; }

    void refresh_total_summary();

private:

    void sync_from_widgets();

    void compute_totals(int* out_min, int* out_max) const;

    MapLayersPanel* owner_ = nullptr;

    int layer_index_ = -1;

    json* layer_ = nullptr;

    bool locked_ = false;

    bool cleanup_pending_ = false;

    bool refresh_pending_ = false;

    std::unique_ptr<DMTextBox> name_box_;

    std::unique_ptr<TextBoxWidget> name_widget_;

    std::string name_cache_;

    std::unique_ptr<SummaryRangeWidget> total_room_widget_;

    int total_rooms_min_cache_ = 0;

    int total_rooms_max_cache_ = 0;

    std::unique_ptr<DMButton> add_candidate_btn_;

    std::unique_ptr<ButtonWidget> add_candidate_widget_;

    std::unique_ptr<DMButton> close_btn_;

    std::unique_ptr<ButtonWidget> close_widget_;

    std::unique_ptr<DMButton> delete_layer_btn_;

    std::unique_ptr<ButtonWidget> delete_layer_widget_;

    std::vector<std::unique_ptr<RoomCandidateWidget>> candidate_widgets_;

};

void MapLayersPanel::PanelSidebarWidget::set_layer_config(LayerConfigPanel* panel) { config_panel_ = panel; }

void MapLayersPanel::PanelSidebarWidget::set_rect(const SDL_Rect& r) {

    rect_ = r;

    const int spacing = DMSpacing::item_gap();

    const int col_gap = spacing;

    const int row_gap = spacing;

    const int columns = 2;

    const int col_width = std::max(1, (rect_.w - spacing * 2 - col_gap * (columns - 1)) / columns);

    const int btn_h = DMButton::height();

    std::vector<DMButton*> btns;

    if (add_button_) btns.push_back(add_button_.get());

    if (new_room_button_) btns.push_back(new_room_button_.get());

    if (preview_button_) btns.push_back(preview_button_.get());

    if (delete_button_) btns.push_back(delete_button_.get());

    if (reload_button_) btns.push_back(reload_button_.get());

    int y = rect_.y + spacing;

    for (size_t i = 0; i < btns.size(); ++i) {

        const int row = static_cast<int>(i / columns);

        const int col = static_cast<int>(i % columns);

        const int x = rect_.x + spacing + col * (col_width + col_gap);

        const int w = (col == columns - 1) ? (rect_.x + rect_.w - spacing - x) : col_width;

        btns[i]->set_rect(SDL_Rect{ x, rect_.y + spacing + row * (btn_h + row_gap), w, btn_h });

        y = rect_.y + spacing + (row + 1) * (btn_h + row_gap);

    }

    const int button_area_bottom = y;

    const int button_width = rect_.w - spacing * 2;

    const int config_top = button_area_bottom;

    const int config_height = std::max(0, rect_.y + rect_.h - config_top - spacing);

    config_rect_ = SDL_Rect{ rect_.x + spacing, config_top, button_width, config_height };

    if (config_panel_) {

        config_panel_->set_rect(config_rect_);

        const int panel_padding = DMSpacing::panel_padding();

        int available = std::max(0, config_height - panel_padding * 2);

        config_panel_->set_available_height_override(available);

        config_panel_->set_cell_width(std::max(160, button_width - panel_padding * 2));

    }

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

    handle_btn(reload_button_, [this]() { if (owner_) owner_->reload_layers_from_disk(); });

    handle_btn(delete_button_, [this]() { if (owner_ && selected_layer_ >= 0) owner_->delete_layer_internal(selected_layer_); });

    return used;

}

MapLayersPanel::LayerConfigPanel::LayerConfigPanel(MapLayersPanel* owner)

    : DockableCollapsible("Layer", false, 0, 0),

      owner_(owner) {

    set_visible(false);

    set_expanded(true);

    set_show_header(false);

    set_close_button_enabled(false);

    set_scroll_enabled(true);

    set_padding(DMSpacing::panel_padding());

    set_row_gap(DMSpacing::item_gap());

    set_col_gap(DMSpacing::item_gap());

    set_cell_width(320);

}

void MapLayersPanel::LayerConfigPanel::open(int layer_index, json* layer) {

    if (!layer) return;

    layer_index_ = layer_index;

    layer_ = layer;

    locked_ = owner_ ? owner_->is_layer_locked(layer_index_) : false;

    cleanup_pending_ = false;

    refresh_pending_ = false;

    name_cache_ = layer_->value("name", std::string("layer_") + std::to_string(layer_index));

    refresh();

    set_title(std::string("Layer: ") + name_cache_);

    set_visible(true);

    set_expanded(true);

    reset_scroll();

}

void MapLayersPanel::LayerConfigPanel::close() {

    set_visible(false);

    cleanup_pending_ = true;

    refresh_pending_ = false;

}

void MapLayersPanel::LayerConfigPanel::ensure_cleanup() {

    if (!cleanup_pending_) {

        return;

    }

    cleanup_pending_ = false;

    refresh_pending_ = false;

    layer_index_ = -1;

    layer_ = nullptr;

    locked_ = false;

    name_box_.reset();

    name_widget_.reset();

    name_cache_.clear();

    total_room_widget_.reset();

    total_rooms_min_cache_ = 0;

    total_rooms_max_cache_ = 0;

    add_candidate_btn_.reset();

    add_candidate_widget_.reset();

    close_btn_.reset();

    close_widget_.reset();

    delete_layer_btn_.reset();

    delete_layer_widget_.reset();

    candidate_widgets_.clear();

    set_rows({});

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

    if (refresh_pending_) {

        refresh_pending_ = false;

        refresh();

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

    refresh_pending_ = false;

    if (layer_) {

        name_cache_ = layer_->value("name", name_cache_);

    }

    DockableCollapsible::Rows rows;

    compute_totals(&total_rooms_min_cache_, &total_rooms_max_cache_);

    total_room_widget_ = std::make_unique<SummaryRangeWidget>("Overall Room Count");

    if (total_room_widget_) {

        total_room_widget_->set_values(total_rooms_min_cache_, total_rooms_max_cache_);

        rows.push_back({ total_room_widget_.get() });

    }

    if (!locked_) {

        name_box_ = std::make_unique<DMTextBox>("Layer Name", name_cache_);

        name_widget_ = std::make_unique<TextBoxWidget>(name_box_.get());

        rows.push_back({ name_widget_.get() });

        add_candidate_btn_ = std::make_unique<DMButton>("Add Room", &DMStyles::CreateButton(), 160, DMButton::height());

        add_candidate_widget_ = std::make_unique<ButtonWidget>(add_candidate_btn_.get(), [this]() {

            if (!owner_) return;

            owner_->request_room_selection_for_layer(layer_index_, [this](const std::string& room) {

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

    } else {

        name_box_.reset();

        name_widget_.reset();

        add_candidate_btn_.reset();

        add_candidate_widget_.reset();

        delete_layer_btn_.reset();

        delete_layer_widget_.reset();

    }

    close_btn_ = std::make_unique<DMButton>("Close", &DMStyles::HeaderButton(), 120, DMButton::height());

    close_widget_ = std::make_unique<ButtonWidget>(close_btn_.get(), [this]() { this->close(); });

    if (!locked_) {

        rows.push_back({ delete_layer_widget_.get(), close_widget_.get() });

    } else {

        rows.push_back({ close_widget_.get() });

    }

    candidate_widgets_.clear();

    if (layer_ && layer_->contains("rooms")) {

        json& rooms = (*layer_)["rooms"];

        if (rooms.is_array()) {

            for (size_t i = 0; i < rooms.size(); ++i) {

                json& entry = rooms[i];

                auto widget = std::make_unique<RoomCandidateWidget>(this, layer_index_, static_cast<int>(i), &entry, !locked_);

                widget->refresh_from_json();

                candidate_widgets_.push_back(std::move(widget));

                rows.push_back({ candidate_widgets_.back().get() });

            }

        }

    }

    refresh_total_summary();

    set_rows(rows);

}

void MapLayersPanel::LayerConfigPanel::request_refresh() {

    refresh_pending_ = true;

}

void MapLayersPanel::LayerConfigPanel::sync_from_widgets() {

    if (!layer_) return;

    if (!locked_ && name_box_) {

        const std::string current = name_box_->value();

        if (current != name_cache_) {

            name_cache_ = current;

            (*layer_)["name"] = name_cache_;

            if (owner_) owner_->handle_layer_name_changed(layer_index_, name_cache_);

            set_title(std::string("Layer: ") + name_cache_);

        }

    }

    json& rooms = (*layer_)["rooms"];

    if (rooms.is_array()) {

        for (size_t i = 0; i < rooms.size() && i < candidate_widgets_.size(); ++i) {

            candidate_widgets_[i]->set_candidate_index(static_cast<int>(i));

        }

    }

}

void MapLayersPanel::LayerConfigPanel::compute_totals(int* out_min, int* out_max) const {

    if (out_min) *out_min = 0;

    if (out_max) *out_max = 0;

    if (!layer_) return;

    const auto rooms_it = layer_->find("rooms");

    if (rooms_it == layer_->end() || !rooms_it->is_array()) return;

    int min_sum = 0;

    int max_sum = 0;

    for (const auto& entry : *rooms_it) {

        if (!entry.is_object()) continue;

        int min_val = clamp_candidate_min(entry.value("min_instances", 0));

        int max_val = clamp_candidate_max(min_val, entry.value("max_instances", min_val));

        min_sum += min_val;

        max_sum += max_val;

    }

    if (out_min) *out_min = min_sum;

    if (out_max) *out_max = max_sum;

}

void MapLayersPanel::LayerConfigPanel::refresh_total_summary() {

    compute_totals(&total_rooms_min_cache_, &total_rooms_max_cache_);

    if (total_room_widget_) {

        total_room_widget_->set_values(total_rooms_min_cache_, total_rooms_max_cache_);

    }

}

MapLayersPanel::RoomCandidateWidget::RoomCandidateWidget(LayerConfigPanel* owner, int layer_index, int candidate_index, json* candidate, bool editable)

    : owner_(owner), layer_index_(layer_index), candidate_index_(candidate_index), candidate_(candidate), editable_(editable) {

    if (editable_) {

        range_slider_ = std::make_unique<DMRangeSlider>(0, kCandidateRangeMax, 0, 0);

        add_child_button_ = std::make_unique<DMButton>("Add Child", &DMStyles::HeaderButton(), 120, DMButton::height());

        delete_button_ = std::make_unique<DMButton>("Delete", &DMStyles::DeleteButton(), 120, DMButton::height());

    }

}

void MapLayersPanel::RoomCandidateWidget::refresh_from_json() {

    if (!candidate_) return;

    const int stored_min = candidate_->value("min_instances", 0);

    const int stored_max = candidate_->value("max_instances", stored_min);

    min_count_cache_ = clamp_candidate_min(stored_min);

    max_count_cache_ = clamp_candidate_max(min_count_cache_, stored_max);

    (*candidate_)["min_instances"] = min_count_cache_;

    (*candidate_)["max_instances"] = max_count_cache_;

    if (editable_) {

        const int slider_max = std::max(kCandidateRangeMax, max_count_cache_ + 8);

        range_slider_ = std::make_unique<DMRangeSlider>(0, slider_max, min_count_cache_, max_count_cache_);

    } else {

        range_slider_.reset();

    }

    child_chips_.clear();

    auto children_it = candidate_->find("required_children");

    if (children_it != candidate_->end() && children_it->is_array()) {

        for (const auto& child : *children_it) {

            ChildChip chip;

            chip.name = child.get<std::string>();

            if (editable_) {

                chip.remove_button = std::make_unique<DMButton>("x", &DMStyles::DeleteButton(), 24, DMButton::height());

            }

            child_chips_.push_back(std::move(chip));

        }

    }

}

void MapLayersPanel::RoomCandidateWidget::update() {

    if (!candidate_) return;

    if (range_slider_) {

        int slider_min = clamp_candidate_min(range_slider_->min_value());

        int slider_max = clamp_candidate_max(slider_min, range_slider_->max_value());

        bool values_changed = false;

        if (slider_min != min_count_cache_) {

            min_count_cache_ = slider_min;

            (*candidate_)["min_instances"] = min_count_cache_;

            values_changed = true;

            if (owner_ && owner_->panel_owner()) {

                owner_->panel_owner()->handle_candidate_min_changed(layer_index_, candidate_index_, min_count_cache_);

            }

        }

        if (slider_max != max_count_cache_) {

            max_count_cache_ = slider_max;

            (*candidate_)["max_instances"] = max_count_cache_;

            values_changed = true;

            if (owner_ && owner_->panel_owner()) {

                owner_->panel_owner()->handle_candidate_max_changed(layer_index_, candidate_index_, max_count_cache_);

            }

        }

        if (values_changed) {

            if (range_slider_->min_value() != min_count_cache_) {

                range_slider_->set_min_value(min_count_cache_);

            }

            if (range_slider_->max_value() != max_count_cache_) {

                range_slider_->set_max_value(max_count_cache_);

            }

        }

    }

}

void MapLayersPanel::RoomCandidateWidget::set_rect(const SDL_Rect& r) {

    rect_ = r;

    const int spacing = DMSpacing::item_gap();

    SDL_Rect slider_rect{ rect_.x + spacing, rect_.y + spacing + DMButton::height() + spacing, rect_.w - spacing * 2, DMRangeSlider::height() };

    if (range_slider_) {

        range_slider_->set_rect(slider_rect);

        slider_rect.y += slider_rect.h + spacing;

    }

    SDL_Rect buttons_rect{ rect_.x + spacing, slider_rect.y, 120, DMButton::height() };

    if (add_child_button_) add_child_button_->set_rect(buttons_rect);

    if (add_child_button_) buttons_rect.x += add_child_button_->rect().w + spacing;

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

    int height = spacing + DMButton::height() + spacing;

    if (range_slider_) height += DMRangeSlider::height() + spacing;

    if (add_child_button_ || delete_button_) height += DMButton::height() + spacing;

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

                owner_->panel_owner()->request_room_selection_for_layer(layer_index_, [this](const std::string& child) {

                    if (owner_ && owner_->panel_owner()) {

                        owner_->panel_owner()->handle_candidate_child_added(layer_index_, candidate_index_, child);

                        this->refresh_from_json();

                        owner_->request_refresh();

                    }

                });

            }

        }

        used = true;

        return true;

    }

    if (delete_button_ && delete_button_->handle_event(e)) {

        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {

            if (owner_ && owner_->panel_owner()) {

                owner_->panel_owner()->handle_candidate_removed(layer_index_, candidate_index_);

                owner_->request_refresh();

                return true;

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

                    owner_->request_refresh();

                    return true;

                }

            }

            used = true;

        }

    }

    if (!used && e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {

        SDL_Point click_point{ e.button.x, e.button.y };

        if (SDL_PointInRect(&click_point, &rect_)) {

            if (owner_ && owner_->panel_owner() && candidate_) {

                const std::string room_key = candidate_->value("name", std::string());

                if (!room_key.empty()) {

                    owner_->panel_owner()->update_click_target(layer_index_, room_key);

                }

            }

            return true;

        }

    }

    return used;

}

void MapLayersPanel::RoomCandidateWidget::render(SDL_Renderer* renderer) const {

    if (!renderer || !candidate_) return;

    SDL_Rect bg = rect_;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    const SDL_Color row_bg = DMStyles::PanelHeader();

    SDL_SetRenderDrawColor(renderer, row_bg.r, row_bg.g, row_bg.b, row_bg.a);

    SDL_RenderFillRect(renderer, &bg);

    const SDL_Color border = DMStyles::Border();

    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);

    SDL_RenderDrawRect(renderer, &bg);

    const DMLabelStyle label = DMStyles::Label();

    draw_text(renderer, candidate_->value("name", "room"), rect_.x + DMSpacing::item_gap(), rect_.y + DMSpacing::item_gap() - (label.font_size + 4), label);

    if (range_slider_) range_slider_->render(renderer);

    if (add_child_button_) add_child_button_->render(renderer);

    if (delete_button_) delete_button_->render(renderer);

    for (const auto& chip : child_chips_) {

        const DMButtonStyle& chip_style = DMStyles::ListButton();

        SDL_SetRenderDrawColor(renderer, chip_style.bg.r, chip_style.bg.g, chip_style.bg.b, chip_style.bg.a);

        SDL_RenderFillRect(renderer, &chip.rect);

        SDL_SetRenderDrawColor(renderer, chip_style.border.r, chip_style.border.g, chip_style.border.b, chip_style.border.a);

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

    layer_config_ = std::make_unique<LayerConfigPanel>(this);

    canvas_widget_ = std::make_unique<LayerCanvasWidget>(this);

    sidebar_widget_ = std::make_unique<PanelSidebarWidget>(this);

    if (sidebar_widget_) {

        sidebar_widget_->set_layer_config(layer_config_.get());

    }

    room_selector_ = std::make_unique<RoomSelectorPopup>();

    if (room_selector_) {

        room_selector_->set_create_callbacks(

            [this]() { return this->suggest_room_name(); },

            [this](const std::string& desired) { return this->create_new_room(desired, true); });

    }

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

    if (layer_config_) {

        layer_config_->close();

        layer_config_->ensure_cleanup();

    }

    if (room_configurator_) room_configurator_->close();

    active_room_config_key_.clear();

    update_click_target(-1, std::string());

    clear_hover_target();

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

    if (!is_visible()) {

        if (map_info_) {

            recalculate_radii_from_layer(0);

            compute_map_radius_from_layers();

            regenerate_preview();

            refresh_canvas();

        }

    } else if (preview_dirty_) {

        regenerate_preview();

        refresh_canvas();

    }

    set_visible(true);

    set_expanded(true);

}

void MapLayersPanel::close() {

    set_visible(false);

    if (layer_config_) {

        layer_config_->close();

        layer_config_->ensure_cleanup();

    }

    if (room_selector_) room_selector_->close();

    if (room_configurator_) room_configurator_->close();

    active_room_config_key_.clear();

    update_click_target(-1, std::string());

    clear_hover_target();

}

bool MapLayersPanel::is_visible() const {

    return DockableCollapsible::is_visible();

}

void MapLayersPanel::set_embedded_mode(bool embedded) {

    if (embedded_mode_ == embedded) return;

    embedded_mode_ = embedded;

    if (embedded_mode_) {

        floatable_ = false;

        set_show_header(false);

        set_close_button_enabled(false);

        set_scroll_enabled(true);

        set_available_height_override(-1);

        set_expanded(true);

        reset_scroll();

    } else {

        floatable_ = true;

        set_show_header(true);

        set_close_button_enabled(true);

        set_scroll_enabled(true);

        set_available_height_override(-1);

    }

    layout();

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

    screen_bounds_ = SDL_Rect{ 0, 0, std::max(0, screen_w), std::max(0, screen_h) };

    if (room_selector_) {

        room_selector_->set_screen_bounds(screen_bounds_);

    }

    if (room_configurator_) {

        room_configurator_->set_work_area(screen_bounds_);

        if (room_configurator_->visible()) {

            room_configurator_->set_bounds(compute_room_config_bounds());

        }

    }

    if (!is_visible()) {

        clear_hover_target();

        if (layer_config_) layer_config_->ensure_cleanup();

        return;

    }

    DockableCollapsible::update(input, screen_w, screen_h);

    if (layer_config_) layer_config_->update(input, screen_w, screen_h);

    if (room_selector_) {

        SDL_Rect anchor = sidebar_widget_ ? sidebar_widget_->rect() : rect();

        room_selector_->set_anchor_rect(anchor);

        room_selector_->update(input);

    }

    if (room_configurator_) {

        room_configurator_->update(input, screen_w, screen_h);

        if (room_configurator_->visible() && !active_room_config_key_.empty()) {

            if (auto* entry = ensure_room_entry(active_room_config_key_)) {

                nlohmann::json updated = room_configurator_->build_json();

                if (!entry->is_object() || *entry != updated) {

                    *entry = std::move(updated);

                    mark_dirty();

                    request_preview_regeneration();

                }

            }

        }

    }

    if (layer_config_) layer_config_->ensure_cleanup();

}

bool MapLayersPanel::handle_event(const SDL_Event& e) {

    if (!is_visible()) return false;

    bool used = false;

    if (room_configurator_ && room_configurator_->visible()) {

        used = room_configurator_->handle_event(e) || used;

    }

    used = DockableCollapsible::handle_event(e) || used;

    if (layer_config_ && layer_config_->is_visible()) {

        used = layer_config_->handle_event(e) || used;

        layer_config_->ensure_cleanup();

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

    if (room_configurator_ && room_configurator_->visible()) {

        room_configurator_->render(renderer);

    }

}

bool MapLayersPanel::is_point_inside(int x, int y) const {

    if (!is_visible()) return false;

    if (DockableCollapsible::is_point_inside(x, y)) return true;

    if (layer_config_ && layer_config_->is_visible() && layer_config_->is_point_inside(x, y)) return true;

    if (room_selector_ && room_selector_->visible() && room_selector_->is_point_inside(x, y)) return true;

    if (room_configurator_ && room_configurator_->visible() && room_configurator_->is_point_inside(x, y)) return true;

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

    }

    return max_extent;

}

void MapLayersPanel::recalculate_radii_from_layer(int layer_index) {

    if (!map_info_) return;

    auto& layers = layers_array();

    if (!layers.is_array() || layers.empty()) return;

    if (layer_index < 0) layer_index = 0;

    if (layer_index >= static_cast<int>(layers.size())) {

        layer_index = static_cast<int>(layers.size()) - 1;

    }

    const nlohmann::json* rooms_data = nullptr;

    auto rooms_it = map_info_->find("rooms_data");

    if (rooms_it != map_info_->end() && rooms_it->is_object()) {

        rooms_data = &(*rooms_it);

    }

    std::vector<double> extents(layers.size(), 0.0);

    for (size_t i = 0; i < layers.size(); ++i) {

        const auto& layer = layers[i];

        if (!layer.is_object()) continue;

        auto rooms_array_it = layer.find("rooms");

        if (rooms_array_it == layer.end() || !rooms_array_it->is_array()) continue;

        double largest_room = 0.0;

        for (const auto& candidate : *rooms_array_it) {

            if (!candidate.is_object()) continue;

            std::string room_name = candidate.value("name", std::string());

            if (room_name.empty()) continue;

            RoomGeometry geom = fetch_room_geometry(rooms_data, room_name);

            largest_room = std::max(largest_room, room_extent_for_radius(geom));

        }

        extents[i] = largest_room;

    }

    for (int i = std::max(0, layer_index); i < static_cast<int>(layers.size()); ++i) {

        auto& layer = layers[i];

        if (!layer.is_object()) continue;

        double stored_radius = layer.value("radius", 0.0);

        double largest = (i >= 0 && i < static_cast<int>(extents.size())) ? extents[i] : 0.0;

        double desired_radius = stored_radius;

        if (i > 0) {

            double prev_radius = layers[i - 1].value("radius", 0.0);

            double prev_extent = extents[i - 1];

            double separation = prev_extent + largest + kLayerRadiusSpacingPadding;

            double minimum_step = static_cast<double>(kLayerRadiusStepDefault) + kLayerRadiusSpacingPadding;

            separation = std::max(separation, minimum_step);

            double minimum = prev_radius + separation;

            desired_radius = minimum;

        }

        int final_radius = static_cast<int>(std::ceil(desired_radius));

        if (final_radius < 0) final_radius = 0;

        layer["radius"] = final_radius;

    }

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

        update_click_target(-1, std::string());

        clear_hover_target();

        return;

    }

    std::vector<PreviewLayerSpec> layer_specs;

    layer_specs.reserve(layers.size());

    for (const auto& layer_json : layers) {

        if (!layer_json.is_object()) continue;

        PreviewLayerSpec spec;

        spec.level = layer_json.value("level", static_cast<int>(layer_specs.size()));

        spec.radius = layer_json.value("radius", 0.0);

        spec.max_rooms = layer_json.value("max_rooms", 0);

        auto rooms_it = layer_json.find("rooms");

        if (rooms_it != layer_json.end() && rooms_it->is_array()) {

            for (const auto& candidate : *rooms_it) {

                if (!candidate.is_object()) continue;

                PreviewRoomSpec room_spec;

                room_spec.name = candidate.value("name", std::string());

                room_spec.max_instances = candidate.value("max_instances", 0);

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

    uint32_t seed = compute_preview_seed(layer_specs, map_path_);

    RoomGeometry root_geom = fetch_room_geometry(rooms_data, root_spec.name, seed);

    auto root_node = std::make_unique<PreviewNode>();

    root_node->center = SDL_FPoint{0.0f, 0.0f};

    root_node->width = root_geom.max_width;

    root_node->height = root_geom.max_height;

    root_node->is_circle = root_geom.is_circle;
    root_node->outline = std::move(root_geom.outline);

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

    std::mt19937 rng(seed);

    for (size_t li = 1; li < layer_specs.size(); ++li) {

        const auto& layer_spec = layer_specs[li];

        auto children = build_children_pool(layer_spec, rng);

        double radius = layer_spec.radius;

        std::vector<PreviewSector> next_sectors;

        std::vector<PreviewNode*> next_parents;

        auto create_child = [&](PreviewNode* parent, const PreviewRoomSpec& spec, float angle, float spread) {

            RoomGeometry geom = fetch_room_geometry(rooms_data, spec.name, seed);

            auto node = std::make_unique<PreviewNode>();

            node->center = SDL_FPoint{

                static_cast<float>(std::cos(angle) * radius),  static_cast<float>(std::sin(angle) * radius)  };

            node->width = geom.max_width;

            node->height = geom.max_height;

            node->is_circle = geom.is_circle;

            node->outline = std::move(geom.outline);

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

    int layer_count = layers.is_array() ? static_cast<int>(layers.size()) : 0;

    if (clicked_layer_index_ >= layer_count) clicked_layer_index_ = -1;

    if (hovered_layer_index_ >= layer_count) hovered_layer_index_ = -1;

    auto room_exists = [&](const std::string& key) {

        if (key.empty()) return true;

        for (const auto& node_uptr : preview_nodes_) {

            const PreviewNode* node = node_uptr.get();

            if (!node) continue;

            if (node->name == key) return true;

        }

        return false;

};

    if (!room_exists(clicked_room_key_)) {

        clicked_room_key_.clear();

    }

    if (!room_exists(hovered_room_key_)) {

        hovered_room_key_.clear();

    }

}

bool MapLayersPanel::handle_preview_room_click(int px, int py, int center_x, int center_y, double scale) {

    const PreviewNode* node = find_room_at(px, py, center_x, center_y, scale);

    if (!node) {

        return false;

    }

    update_click_target(node->layer, node->name);

    return true;

}

const MapLayersPanel::PreviewNode* MapLayersPanel::find_room_at(int px, int py, int center_x, int center_y, double scale) const {

    if (preview_nodes_.empty()) {

        return nullptr;

    }

    const PreviewNode* best_node = nullptr;

    double best_score = std::numeric_limits<double>::max();

    const double tolerance = 6.0;

    for (const auto& node_uptr : preview_nodes_) {

        const PreviewNode* node = node_uptr.get();

        if (!node) continue;

        if (node->name.empty() || node->name == "<room>") continue;

        int node_cx = static_cast<int>(std::lround(center_x + node->center.x * scale));

        int node_cy = static_cast<int>(std::lround(center_y + node->center.y * scale));

        double dx = static_cast<double>(px - node_cx);

        double dy = static_cast<double>(py - node_cy);

        bool hit = false;

        double score = 0.0;

        if (node->is_circle) {

            double radius_px = std::max(8.0, (node->width * 0.5) * scale);

            double dist = std::hypot(dx, dy);

            if (dist <= radius_px + tolerance) {

                hit = true;

                score = dist;

            }

        } else {

            double half_w = std::max(8.0, (node->width * 0.5) * scale);

            double half_h = std::max(8.0, (node->height * 0.5) * scale);

            if (std::fabs(dx) <= half_w + tolerance && std::fabs(dy) <= half_h + tolerance) {

                hit = true;

                double norm_w = half_w > 0.0 ? std::fabs(dx) / half_w : 0.0;

                double norm_h = half_h > 0.0 ? std::fabs(dy) / half_h : 0.0;

                score = std::max(norm_w, norm_h);

            }

        }

        if (hit && (!best_node || score < best_score)) {

            best_node = node;

            best_score = score;

        }

    }

    return best_node;

}

int MapLayersPanel::find_layer_at(int px, int py, int center_x, int center_y, double scale) const {

    const auto& layers = layers_array();

    if (!layers.is_array() || layers.empty()) return -1;

    const double tolerance = 12.0;

    for (size_t i = 0; i < layers.size(); ++i) {

        const auto& layer_json = layers[i];

        if (!layer_json.is_object()) continue;

        int current_radius = layer_json.value("radius", 0);

        int pixel_radius = static_cast<int>(std::lround(current_radius * scale));

        pixel_radius = std::max(12, pixel_radius);

        const int dx = px - center_x;

        const int dy = py - center_y;

        const double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));

        if (std::fabs(dist - pixel_radius) <= tolerance || dist < pixel_radius * 0.85) {

            return static_cast<int>(i);

        }

    }

    return -1;

}

void MapLayersPanel::update_hover_target(int layer_index, const std::string& room_key) {

    if (hovered_layer_index_ == layer_index && hovered_room_key_ == room_key) {

        return;

    }

    hovered_layer_index_ = layer_index;

    hovered_room_key_ = room_key;

}

void MapLayersPanel::update_click_target(int layer_index, const std::string& room_key) {

    clicked_layer_index_ = layer_index;

    clicked_room_key_ = room_key;

}

void MapLayersPanel::clear_hover_target() {

    hovered_layer_index_ = -1;

    hovered_room_key_.clear();

}

void MapLayersPanel::open_room_config_for(const std::string& room_name) {

    if (room_name.empty()) {

        return;

    }

    if (layer_config_) {

        layer_config_->close();

        layer_config_->ensure_cleanup();

    }

    if (room_selector_) {

        room_selector_->close();

    }

    ensure_room_configurator();

    if (!room_configurator_) {

        return;

    }

    room_configurator_->close();

    active_room_config_key_.clear();

    nlohmann::json* entry = ensure_room_entry(room_name);

    if (!entry) {

        return;

    }

    active_room_config_key_ = room_name;

    room_configurator_->set_work_area(screen_bounds_);

    room_configurator_->set_bounds(compute_room_config_bounds());

    room_configurator_->open(*entry);

}

void MapLayersPanel::ensure_room_configurator() {

    if (!room_configurator_) {

        room_configurator_ = std::make_unique<RoomConfigurator>();

        if (room_configurator_) {

            room_configurator_->set_show_header(true);

            room_configurator_->set_on_close([this]() {

                active_room_config_key_.clear();

            });

            room_configurator_->set_spawn_group_callbacks(

                [](const std::string&) {},

                [](const std::string&) {},

                [](const std::string&) {},

                []() {}

            );

            room_configurator_->set_on_room_renamed([this](const std::string& old_name, const std::string& desired) -> std::string {

                std::string final_name = this->rename_room_everywhere(old_name, desired);

                this->rebuild_available_rooms();

                this->request_preview_regeneration();

                this->mark_dirty();

                this->active_room_config_key_ = final_name;

                return final_name;

            });

        }

    }

}

nlohmann::json* MapLayersPanel::ensure_room_entry(const std::string& room_name) {

    if (!map_info_) {

        return nullptr;

    }

    if (room_name.empty()) {

        return nullptr;

    }

    nlohmann::json& rooms_data = (*map_info_)["rooms_data"];

    if (!rooms_data.is_object()) {

        rooms_data = nlohmann::json::object();

    }

    auto it = rooms_data.find(room_name);

    if (it == rooms_data.end() || !it->is_object()) {

        rooms_data[room_name] = make_default_room_json(room_name);

        it = rooms_data.find(room_name);

        mark_dirty();

        rebuild_available_rooms();

        request_preview_regeneration();

    }

    return &rooms_data[room_name];

}

SDL_Rect MapLayersPanel::compute_room_config_bounds() const {

    if (sidebar_widget_) {

        const SDL_Rect& dock = sidebar_widget_->config_rect();

        if (dock.w > 0 && dock.h > 0) {

            return dock;

        }

    }

    SDL_Rect bounds = screen_bounds_;

    const int margin = 48;

    int width = std::max(360, bounds.w / 3);

    if (bounds.w > margin * 2) {

        int max_width = bounds.w - margin * 2;

        width = std::min(width, max_width);

    } else {

        width = bounds.w;

    }

    int height = std::max(320, bounds.h - margin * 2);

    if (bounds.h > margin * 2) {

        int max_height = bounds.h - margin * 2;

        height = std::min(height, max_height);

    } else {

        height = bounds.h;

    }

    if (width <= 0) width = std::max(1, bounds.w);

    if (height <= 0) height = std::max(1, bounds.h);

    int x = bounds.x + bounds.w - width - margin;

    if (bounds.w <= margin * 2) {

        x = bounds.x;

    } else if (x < bounds.x + margin) {

        x = bounds.x + margin;

    }

    int y = bounds.y + margin;

    if (bounds.h <= margin * 2) {

        y = bounds.y;

    }

    if (x + width > bounds.x + bounds.w) {

        x = bounds.x + bounds.w - width;

    }

    if (y + height > bounds.y + bounds.h) {

        y = bounds.y + bounds.h - height;

    }

    return SDL_Rect{x, y, width, height};

}

void MapLayersPanel::select_layer(int index) {

    selected_layer_ = index;

    if (sidebar_widget_) sidebar_widget_->set_selected(index);

    if (canvas_widget_) canvas_widget_->set_selected(index);

    if (layer_config_ && layer_config_->is_visible()) {

        if (index >= 0) {

            if (auto* layer = layer_at(index)) {

                layer_config_->open(index, layer);

            } else {

                layer_config_->close();

                layer_config_->ensure_cleanup();

            }

        } else {

            layer_config_->close();

            layer_config_->ensure_cleanup();

        }

    }

}

void MapLayersPanel::mark_dirty(bool trigger_preview) {

    dirty_ = true;

    if (trigger_preview) {

        request_preview_regeneration();

    }

    save_layers_to_disk();

}

void MapLayersPanel::mark_clean() {

    dirty_ = false;

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

            if (it.key() == "room") continue;

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

    std::string suggested = suggest_room_name();

    std::string new_room_key = create_new_room(suggested, true);

    if (!new_room_key.empty() && selected_layer_ >= 0) {

        update_click_target(selected_layer_, new_room_key);

    }

}

std::string MapLayersPanel::create_new_room(const std::string& desired_name, bool open_config) {

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

    if (open_config && !unique.empty()) {

        open_room_config_for(unique);

    }

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

            {"min_instances", 0},

            {"max_instances", 1},

            {"required_children", json::array()}

};

        rooms.push_back(std::move(candidate));

        modified = true;

    } else {

        json& entry = *it;

        int current_min = clamp_candidate_min(entry.value("min_instances", 0));

        int current_max = clamp_candidate_max(current_min, entry.value("max_instances", 1));

        if (entry.value("min_instances", -1) != current_min) {

            entry["min_instances"] = current_min;

            modified = true;

        }

        if (entry.value("max_instances", -1) != current_max) {

            entry["max_instances"] = current_max;

            modified = true;

        }

    }

    clamp_layer_room_counts(*child_layer);

    if (modified) {

        recalculate_radii_from_layer(child_layer_index);

        compute_map_radius_from_layers();

    }

    return modified;

}

void MapLayersPanel::delete_layer_internal(int index) {

    if (!map_info_) return;

    if (is_layer_locked(index)) return;

    auto& arr = layers_array();

    if (index < 0 || index >= static_cast<int>(arr.size())) return;

    arr.erase(arr.begin() + index);

    ensure_layer_indices();

    refresh_canvas();

    if (selected_layer_ >= static_cast<int>(arr.size())) {

        select_layer(static_cast<int>(arr.size()) - 1);

    }

    if (!arr.empty()) {

        int start = std::min(index, static_cast<int>(arr.size()) - 1);

        recalculate_radii_from_layer(start);

    }

    compute_map_radius_from_layers();

    mark_dirty();

}

void MapLayersPanel::open_layer_config_internal(int index) {

    if (!layer_config_) return;

    if (!map_info_) return;

    if (room_configurator_) {

        room_configurator_->close();

    }

    active_room_config_key_.clear();

    layer_config_->close();

    layer_config_->ensure_cleanup();

    auto* layer = layer_at(index);

    if (!layer) return;

    select_layer(index);

    layer_config_->open(index, layer);

}

void MapLayersPanel::handle_layer_name_changed(int index, const std::string& name) {

    auto* layer = layer_at(index);

    if (!layer) return;

    (*layer)["name"] = name;

    mark_dirty();

    refresh_canvas();

}

std::string MapLayersPanel::rename_room_everywhere(const std::string& old_key, const std::string& desired_key) {

    if (!map_info_) return desired_key;

    if (old_key.empty()) return desired_key;

    std::string trimmed = trim_copy_local(desired_key);

    std::string base = sanitize_room_key(trimmed);

    if (base.empty()) base = desired_key.empty() ? old_key : desired_key;

    auto& map_info = *map_info_;

    auto rdit = map_info.find("rooms_data");

    if (rdit == map_info.end() || !rdit->is_object()) return old_key;

    auto& rooms_data = (*map_info_)["rooms_data"];

    if (!rooms_data.contains(old_key)) return old_key;

    std::string final_key = base;

    if (final_key != old_key) {

        nlohmann::json entry = rooms_data[old_key];

        rooms_data.erase(old_key);

        final_key = make_unique_room_key(rooms_data, final_key);

        rooms_data[final_key] = std::move(entry);

    }

    if (rooms_data[final_key].is_object()) {

        rooms_data[final_key]["name"] = final_key;

    }

    auto lit = map_info.find("map_layers");

    if (lit != map_info.end() && lit->is_array()) {

        for (auto& layer : *lit) {

            auto rooms_it = layer.find("rooms");

            if (rooms_it == layer.end() || !rooms_it->is_array()) continue;

            for (auto& entry : *rooms_it) {

                if (!entry.is_object()) continue;

                if (entry.value("name", std::string()) == old_key) entry["name"] = final_key;

                auto& children = entry["required_children"];

                if (children.is_array()) {

                    for (auto& c : children) {

                        if (c.is_string() && c.get<std::string>() == old_key) c = final_key;

                    }

                }

            }

        }

    }

    if (active_room_config_key_ == old_key) active_room_config_key_ = final_key;

    refresh_canvas();

    return final_key;

}

void MapLayersPanel::handle_candidate_min_changed(int layer_index, int candidate_index, int min_instances) {

    auto* layer = layer_at(layer_index);

    if (!layer) return;

    auto rooms_it = layer->find("rooms");

    if (rooms_it == layer->end() || !rooms_it->is_array()) return;

    if (candidate_index < 0 || candidate_index >= static_cast<int>(rooms_it->size())) return;

    auto& entry = (*rooms_it)[candidate_index];

    int clamped_min = clamp_candidate_min(min_instances);

    int current_max = entry.value("max_instances", clamped_min);

    entry["min_instances"] = clamped_min;

    entry["max_instances"] = clamp_candidate_max(clamped_min, current_max);

    clamp_layer_room_counts(*layer);

    mark_dirty();

    if (layer_config_) layer_config_->refresh_total_summary();

}

void MapLayersPanel::handle_candidate_max_changed(int layer_index, int candidate_index, int max_instances) {

    auto* layer = layer_at(layer_index);

    if (!layer) return;

    auto rooms_it = layer->find("rooms");

    if (rooms_it == layer->end() || !rooms_it->is_array()) return;

    if (candidate_index < 0 || candidate_index >= static_cast<int>(rooms_it->size())) return;

    auto& entry = (*rooms_it)[candidate_index];

    int current_min = clamp_candidate_min(entry.value("min_instances", 0));

    entry["min_instances"] = current_min;

    entry["max_instances"] = clamp_candidate_max(current_min, max_instances);

    clamp_layer_room_counts(*layer);

    mark_dirty();

    if (layer_config_) layer_config_->refresh_total_summary();

}

void MapLayersPanel::handle_candidate_removed(int layer_index, int candidate_index) {

    auto* layer = layer_at(layer_index);

    if (!layer) return;

    auto rooms_it = layer->find("rooms");

    if (rooms_it == layer->end() || !rooms_it->is_array()) return;

    if (candidate_index < 0 || candidate_index >= static_cast<int>(rooms_it->size())) return;

    rooms_it->erase(rooms_it->begin() + candidate_index);

    clamp_layer_room_counts(*layer);

    recalculate_radii_from_layer(layer_index);

    compute_map_radius_from_layers();

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

    if (is_layer_locked(layer_index)) {

        return;

    }

    if (is_spawn_room(room_name)) {

        const int spawn_idx = find_spawn_layer_index();

        const bool allowed_here = (spawn_idx < 0 && layer_index == 0) || (spawn_idx == layer_index);

        if (!allowed_here) return;

    }

    auto& rooms = (*layer)["rooms"];

    if (!rooms.is_array()) rooms = json::array();

    json candidate = {

        {"name", room_name},

        {"min_instances", 0},

        {"max_instances", 1},

        {"required_children", json::array()}

};

    rooms.push_back(std::move(candidate));

    clamp_layer_room_counts(*layer);

    recalculate_radii_from_layer(layer_index);

    compute_map_radius_from_layers();

    mark_dirty();

    if (layer_config_) layer_config_->refresh();

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

        if (layer_config_) {

            layer_config_->close();

            layer_config_->ensure_cleanup();

        }

        request_preview_regeneration();

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

        layer_config_->ensure_cleanup();

    }

}

bool MapLayersPanel::is_spawn_room(const std::string& room_key) const {

    if (!map_info_) return false;

    auto it = map_info_->find("rooms_data");

    if (it == map_info_->end() || !it->is_object()) return false;

    auto rit = it->find(room_key);

    if (rit == it->end() || !rit->is_object()) return false;

    return rit->value("is_spawn", false);

}

int MapLayersPanel::find_spawn_layer_index() const {

    if (!map_info_) return -1;

    const auto& arr = layers_array();

    if (!arr.is_array()) return -1;

    for (int i = 0; i < static_cast<int>(arr.size()); ++i) {

        const auto& layer = arr[i];

        auto rooms_it = layer.find("rooms");

        if (rooms_it == layer.end() || !rooms_it->is_array()) continue;

        for (const auto& entry : *rooms_it) {

            if (!entry.is_object()) continue;

            std::string name = entry.value("name", std::string());

            if (!name.empty() && is_spawn_room(name)) return i;

        }

    }

    return -1;

}

bool MapLayersPanel::is_layer_locked(int index) const {

    const int spawn_idx = find_spawn_layer_index();

    return spawn_idx >= 0 && index == spawn_idx;

}

std::vector<std::string> MapLayersPanel::available_rooms_for_layer(int layer_index) const {

    std::vector<std::string> out = available_rooms_;

    const int spawn_idx = find_spawn_layer_index();

    const bool allow_spawn_here = (spawn_idx < 0 && layer_index == 0) || (spawn_idx == layer_index);

    if (!allow_spawn_here) {

        out.erase(std::remove_if(out.begin(), out.end(), [this](const std::string& key) {

            return is_spawn_room(key);

        }), out.end());

    }

    return out;

}

void MapLayersPanel::request_room_selection_for_layer(int layer_index, const std::function<void(const std::string&)>& cb) {

    if (!room_selector_) return;

    if (available_rooms_.empty()) rebuild_available_rooms();

    SDL_Rect anchor = sidebar_widget_ ? sidebar_widget_->rect() : rect();

    room_selector_->set_screen_bounds(screen_bounds_);

    room_selector_->set_anchor_rect(anchor);

    const auto list = available_rooms_for_layer(layer_index);

    room_selector_->open(list, cb);

}

