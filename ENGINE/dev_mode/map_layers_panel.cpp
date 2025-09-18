#include "map_layers_panel.hpp"

#include "dm_styles.hpp"
#include "map_layers_controller.hpp"
#include "widgets.hpp"
#include "utils/input.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>

#include <nlohmann/json.hpp>

namespace {
constexpr int kCanvasPreferredHeight = 320;
constexpr int kCanvasPadding = 16;
constexpr int kRoomRangeMaxDefault = 64;
constexpr int kCandidateRangeMaxDefault = 128;
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

int sum_candidate_field(const nlohmann::json& layer, const char* key) {
    if (!layer.is_object()) return 0;
    const auto it = layer.find("rooms");
    if (it == layer.end() || !it->is_array()) return 0;
    int sum = 0;
    for (const auto& entry : *it) {
        sum += entry.value(key, 0);
    }
    return sum;
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
    int hit_index = -1;
    for (const auto& info : circles_) {
        const json* layer = owner_->layer_at(info.index);
        if (!layer) continue;
        int current_radius = layer->value("radius", 0);
        double norm = max_radius > 0.0 ? (static_cast<double>(current_radius) / max_radius) : 0.0;
        int pixel_radius = static_cast<int>(std::lround(norm * draw_radius_max));
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

    const DMLabelStyle label_style = DMStyles::Label();
    for (const auto& info : circles_) {
        const json* layer = owner_->layer_at(info.index);
        if (!layer) continue;
        int radius_value = layer->value("radius", 0);
        double norm = max_radius > 0.0 ? (static_cast<double>(radius_value) / max_radius) : 0.0;
        int pixel_radius = static_cast<int>(std::lround(norm * draw_radius_max));
        pixel_radius = std::max(12, pixel_radius);
        SDL_Color col = info.color;
        int thickness = (info.index == selected_index_) ? 6 : 3;
        draw_circle(renderer, center_x, center_y, pixel_radius, col, thickness);
        std::ostringstream oss;
        oss << info.label << " (" << radius_value << ")";
        draw_text(renderer, oss.str(), center_x - pixel_radius + 8, center_y - pixel_radius - 18, label_style);
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
    std::unique_ptr<DMButton> save_button_;
    std::unique_ptr<DMButton> reload_button_;
    std::unique_ptr<DMButton> config_button_;
    std::unique_ptr<DMButton> delete_button_;
    bool dirty_ = false;
    int selected_layer_ = -1;
};

MapLayersPanel::PanelSidebarWidget::PanelSidebarWidget(MapLayersPanel* owner)
    : owner_(owner) {
    add_button_ = std::make_unique<DMButton>("Add Layer", &DMStyles::CreateButton(), 140, DMButton::height());
    save_button_ = std::make_unique<DMButton>("Save", &DMStyles::HeaderButton(), 140, DMButton::height());
    reload_button_ = std::make_unique<DMButton>("Reload", &DMStyles::HeaderButton(), 140, DMButton::height());
    config_button_ = std::make_unique<DMButton>("Open Config", &DMStyles::HeaderButton(), 140, DMButton::height());
    delete_button_ = std::make_unique<DMButton>("Delete Layer", &DMStyles::DeleteButton(), 140, DMButton::height());
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
    void close();
    bool visible() const { return visible_; }
    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;
    bool is_point_inside(int x, int y) const;

private:
    MapLayersPanel* owner_ = nullptr;
    bool visible_ = false;
    SDL_Rect rect_{0,0,280,320};
    std::vector<std::unique_ptr<DMButton>> buttons_;
    std::vector<std::string> rooms_;
    std::function<void(const std::string&)> callback_;
};

MapLayersPanel::RoomSelectorPopup::RoomSelectorPopup(MapLayersPanel* owner)
    : owner_(owner) {}

void MapLayersPanel::RoomSelectorPopup::open(const std::vector<std::string>& rooms, std::function<void(const std::string&)> cb) {
    rooms_ = rooms;
    callback_ = std::move(cb);
    buttons_.clear();
    const int button_width = 220;
    const int button_height = DMButton::height();
    const int margin = DMSpacing::item_gap();
    rect_.w = std::max(rect_.w, button_width + margin * 2);
    int content_height = margin;
    for (const auto& room : rooms_) {
        auto btn = std::make_unique<DMButton>(room, &DMStyles::ListButton(), rect_.w - margin * 2, button_height);
        buttons_.push_back(std::move(btn));
        content_height += button_height + DMSpacing::small_gap();
    }
    content_height += margin;
    rect_.h = std::min(std::max(content_height, button_height + margin * 2), 520);
    if (owner_) {
        const SDL_Rect& panel_rect = owner_->rect();
        rect_.x = panel_rect.x + panel_rect.w + 16;
        rect_.y = panel_rect.y;
    }
    visible_ = true;
}

void MapLayersPanel::RoomSelectorPopup::close() {
    visible_ = false;
    callback_ = nullptr;
}

void MapLayersPanel::RoomSelectorPopup::update(const Input&) {
    if (!visible_) return;
}

bool MapLayersPanel::RoomSelectorPopup::handle_event(const SDL_Event& e) {
    if (!visible_) return false;
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
    SDL_Rect btn_rect{ rect_.x + DMSpacing::item_gap(), rect_.y + DMSpacing::item_gap(), rect_.w - DMSpacing::item_gap() * 2, DMButton::height() };
    for (size_t i = 0; i < buttons_.size(); ++i) {
        auto& btn = buttons_[i];
        if (!btn) continue;
        btn->set_rect(btn_rect);
        if (btn->handle_event(e)) {
            used = true;
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                if (callback_) callback_(rooms_[i]);
                close();
            }
        }
        btn_rect.y += DMButton::height() + DMSpacing::small_gap();
    }
    return used;
}

void MapLayersPanel::RoomSelectorPopup::render(SDL_Renderer* renderer) const {
    if (!visible_ || !renderer) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 24,24,24,240);
    SDL_RenderFillRect(renderer, &rect_);
    SDL_SetRenderDrawColor(renderer, 90,90,90,255);
    SDL_RenderDrawRect(renderer, &rect_);
    SDL_Rect btn_rect{ rect_.x + DMSpacing::item_gap(), rect_.y + DMSpacing::item_gap(), rect_.w - DMSpacing::item_gap() * 2, DMButton::height() };
    for (const auto& btn : buttons_) {
        if (!btn) continue;
        btn->set_rect(btn_rect);
        btn->render(renderer);
        btn_rect.y += DMButton::height() + DMSpacing::small_gap();
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

void MapLayersPanel::select_layer(int index) {
    selected_layer_ = index;
    if (sidebar_widget_) sidebar_widget_->set_selected(index);
    if (canvas_widget_) canvas_widget_->set_selected(index);
}

void MapLayersPanel::mark_dirty() {
    dirty_ = true;
    if (sidebar_widget_) sidebar_widget_->set_dirty(true);
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
}

void MapLayersPanel::refresh_canvas() {
    if (canvas_widget_) canvas_widget_->refresh();
}

void MapLayersPanel::add_layer_internal() {
    if (!map_info_) return;
    auto& arr = layers_array();
    const int idx = static_cast<int>(arr.size());
    json new_layer = {
        {"level", idx},
        {"name", std::string("layer_") + std::to_string(idx)},
        {"radius", 0},
        {"min_rooms", 0},
        {"max_rooms", 0},
        {"rooms", json::array()}
    };
    arr.push_back(std::move(new_layer));
    ensure_layer_indices();
    refresh_canvas();
    select_layer(idx);
    mark_dirty();
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
    int min_sum = sum_candidate_field(*layer, "min_instances");
    int max_sum = sum_candidate_field(*layer, "max_instances");
    int current_min = layer->value("min_rooms", 0);
    int current_max = layer->value("max_rooms", 0);
    if (min_sum > current_min) {
        (*layer)["min_rooms"] = min_sum;
    }
    if (current_max > max_sum) {
        current_max = max_sum;
        (*layer)["max_rooms"] = std::max(min_sum, current_max);
    }
    mark_dirty();
}

void MapLayersPanel::handle_candidate_removed(int layer_index, int candidate_index) {
    auto* layer = layer_at(layer_index);
    if (!layer) return;
    auto rooms_it = layer->find("rooms");
    if (rooms_it == layer->end() || !rooms_it->is_array()) return;
    if (candidate_index < 0 || candidate_index >= static_cast<int>(rooms_it->size())) return;
    rooms_it->erase(rooms_it->begin() + candidate_index);
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
    if (std::find(children.begin(), children.end(), child) == children.end()) {
        children.push_back(child);
        mark_dirty();
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
        {"min_instances", 0},
        {"max_instances", 0},
        {"required_children", json::array()}
    };
    rooms.push_back(std::move(candidate));
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


