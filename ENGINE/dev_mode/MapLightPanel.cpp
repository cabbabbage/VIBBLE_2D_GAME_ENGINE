#include "MapLightPanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <SDL_ttf.h>

#include "core/AssetsManager.hpp"
#include "dev_mode/dev_ui_settings.hpp"
#include "dev_mode/dm_icons.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "color_range_widget.hpp"
#include "utils/input.hpp"
#include "utils/map_grid_settings.hpp"
#include "utils/ranged_color.hpp"
#include "utils/grid.hpp"

using nlohmann::json;

namespace {

constexpr std::string_view kUpdateMapLightSettingKey = "dev_ui.lighting.map_panel.update_map_light";
constexpr utils::color::RangedColor kDefaultMapColor{{0, 0}, {0, 0}, {0, 0}, {255, 255}};

}

class MapLightPanel::WarningLabel : public Widget {
public:
    WarningLabel() = default;

    void set_text(std::string text) { text_ = std::move(text); }
    const std::string& text() const { return text_; }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int w) const override {
        if (text_.empty()) {
            return 0;
        }
        const DMLabelStyle& style = DMStyles::Label();
        TTF_Font* font = style.open_font();
        if (!font) {
            return style.font_size;
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended_Wrapped(font, text_.c_str(), color_, std::max(10, w));
        int height = surface ? surface->h : style.font_size;
        if (surface) {
            SDL_FreeSurface(surface);
        }
        TTF_CloseFont(font);
        return height + DMSpacing::small_gap();
    }

    bool handle_event(const SDL_Event&) override { return false; }

    void render(SDL_Renderer* r) const override {
        if (text_.empty() || !r) {
            return;
        }
        const DMLabelStyle& style = DMStyles::Label();
        TTF_Font* font = style.open_font();
        if (!font) {
            return;
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended_Wrapped(font, text_.c_str(), color_, std::max(10, rect_.w));
        if (surface) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surface);
            if (tex) {
                SDL_Rect dst{rect_.x, rect_.y, surface->w, surface->h};
                SDL_RenderCopy(r, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surface);
        }
        TTF_CloseFont(font);
    }

    bool wants_full_row() const override { return true; }

    void set_color(SDL_Color color) { color_ = color; }

private:
    SDL_Rect rect_{0, 0, 0, 0};
    std::string text_;
    SDL_Color color_{255, 120, 120, 255};
};

class MapLightPanel::SectionToggleWidget : public Widget {
public:
    SectionToggleWidget(DMButton* button, std::function<void()> on_click)
        : button_(button), on_click_(std::move(on_click)) {}

    void set_rect(const SDL_Rect& r) override {
        rect_ = r;
        const int horizontal_pad = DMSpacing::small_gap();
        const int vertical_pad = DMSpacing::small_gap();
        const int button_height = DMButton::height();
        button_rect_ = SDL_Rect{
            rect_.x + horizontal_pad,
            rect_.y + vertical_pad,
            std::max(0, rect_.w - horizontal_pad * 2), button_height};
        card_rect_ = SDL_Rect{
            button_rect_.x,
            button_rect_.y - std::max(0, vertical_pad / 2), button_rect_.w, button_rect_.h + vertical_pad};
        if (button_) {
            button_->set_rect(button_rect_);
        }
    }

    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int) const override {
        return DMButton::height() + DMSpacing::small_gap() * 2;
    }

    bool handle_event(const SDL_Event& e) override {
        if (!button_) {
            return false;
        }
        button_->set_rect(button_rect_);
        bool used = button_->handle_event(e);
        if (used && on_click_ && e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            on_click_();
        }
        return used;
    }

    void render(SDL_Renderer* r) const override {
        if (!r) {
            return;
        }
        if (card_rect_.w > 0 && card_rect_.h > 0) {
            const int radius = std::min(DMStyles::CornerRadius(), 6);
            SDL_Color base = dm_draw::DarkenColor(DMStyles::PanelBG(), 0.06f);
            if (button_ && button_->is_hovered()) {
                base = dm_draw::LightenColor(base, 0.12f);
            }
            dm_draw::DrawBeveledRect( r, card_rect_, radius, 1, base, base, base, false, 0.0f, 0.0f);
            SDL_Color outline = DMStyles::Border();
            dm_draw::DrawRoundedOutline(r, card_rect_, radius, 1, outline);
        }

        if (button_) {
            button_->set_rect(button_rect_);
            button_->render(r);
        }
    }

    bool wants_full_row() const override { return true; }

private:
    DMButton* button_ = nullptr;
    std::function<void()> on_click_{};
    SDL_Rect rect_{0, 0, 0, 0};
    SDL_Rect card_rect_{0, 0, 0, 0};
    SDL_Rect button_rect_{0, 0, 0, 0};
};

class MapLightPanel::OrbitKeyWidget : public Widget {
public:
    explicit OrbitKeyWidget(MapLightPanel& owner);

    void set_enabled(bool enabled);
    bool enabled() const { return enabled_; }

    void set_rect(const SDL_Rect& r) override;
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int w) const override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* r) const override;
    bool wants_full_row() const override { return true; }

    bool handle_overlay_event(const SDL_Event& e);
    void render_overlay(SDL_Renderer* r) const;
    void update_overlays(const Input& input, int screen_w, int screen_h);

    void on_pairs_changed();
    void on_focus_changed();

private:
    struct PairEntry {
        std::unique_ptr<DMColorRangeWidget> widget;
        SDL_Rect outer_rect{0, 0, 0, 0};
        SDL_Rect widget_rect{0, 0, 0, 0};
};

    MapLightPanel& owner_;
    SDL_Rect rect_{0, 0, 0, 0};
    SDL_Rect circle_rect_{0, 0, 0, 0};
    SDL_Rect list_rect_{0, 0, 0, 0};
    std::vector<PairEntry> pair_entries_;
    bool scroll_capture_active_ = false;
    enum class HoverSource { None, Circle, List };
    int hovered_pair_index_ = -1;
    HoverSource hovered_source_ = HoverSource::None;
    bool enabled_ = true;

    void update_internal_layout();
    void rebuild_pair_entries();
    void layout_color_widgets();
    void ensure_scroll_capture();
    void release_scroll_capture();
    int pair_index_at_point(int x, int y) const;
    int line_hit_test(int x, int y) const;
    double point_angle(int x, int y) const;
    double line_distance_to_point(double angle_deg, int x, int y) const;
    void draw_orbit_circle(SDL_Renderer* r) const;
    void draw_orbit_line(SDL_Renderer* r, double angle_deg, const SDL_Color& color, bool focused, bool hovered) const;
};

MapLightPanel::OrbitKeyWidget::OrbitKeyWidget(MapLightPanel& owner)
    : owner_(owner) {
    update_internal_layout();
}

void MapLightPanel::OrbitKeyWidget::set_enabled(bool enabled) {
    if (enabled_ == enabled) {
        return;
    }
    enabled_ = enabled;
    if (!enabled_) {
        hovered_pair_index_ = -1;
        hovered_source_ = HoverSource::None;
        release_scroll_capture();
        owner_.set_focused_pair(-1);
        for (auto& entry : pair_entries_) {
            if (entry.widget) {
                entry.widget->close_overlay();
            }
        }
    }
}

void MapLightPanel::OrbitKeyWidget::set_rect(const SDL_Rect& r) {
    rect_ = r;
    update_internal_layout();
}

int MapLightPanel::OrbitKeyWidget::height_for_width(int) const {
    const int pad = DMSpacing::item_gap();
    const int spacing = DMSpacing::small_gap();
    const int min_circle = 200;
    const int rows = std::max<int>(owner_.orbit_key_pairs_.size(), 1);
    static const int row_height = []() {
        DMColorRangeWidget tmp("Pair");
        return tmp.height_for_width(0) + DMSpacing::small_gap() * 2;
    }();
    const int list_height = rows * row_height + (rows - 1) * spacing + spacing;
    const int content = std::max(min_circle, list_height);
    return pad * 2 + content;
}

bool MapLightPanel::OrbitKeyWidget::handle_event(const SDL_Event& e) {
    if (!enabled_) {
        return false;
    }
    bool used = false;
    const bool pointer_event =
        (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION);

    SDL_Point pointer{0, 0};
    if (pointer_event) {
        if (e.type == SDL_MOUSEMOTION) {
            pointer = SDL_Point{e.motion.x, e.motion.y};
        } else {
            pointer = SDL_Point{e.button.x, e.button.y};
        }

        HoverSource new_source = HoverSource::None;
        int new_hover = -1;
        const int hovered_line = line_hit_test(pointer.x, pointer.y);
        if (hovered_line >= 0) {
            new_hover = hovered_line;
            new_source = HoverSource::Circle;
        } else if (SDL_PointInRect(&pointer, &list_rect_)) {
            const int hovered_entry = pair_index_at_point(pointer.x, pointer.y);
            if (hovered_entry >= 0) {
                new_hover = hovered_entry;
                new_source = HoverSource::List;
            }
        }
        if (new_hover != hovered_pair_index_ || new_source != hovered_source_) {
            hovered_pair_index_ = new_hover;
            hovered_source_ = new_source;
        }
    }

    if (pointer_event && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        const int line_index = line_hit_test(pointer.x, pointer.y);
        if (line_index >= 0) {
            if (owner_.focused_pair_index_ != line_index) {
                owner_.set_focused_pair(line_index);
            }
            if (e.button.clicks >= 2) {
                owner_.delete_orbit_pair(line_index);
            }
            used = true;
        } else if (SDL_PointInRect(&pointer, &circle_rect_)) {
            if (owner_.focused_pair_index_ != -1) {
                owner_.set_focused_pair(-1);
            } else {
                const double angle = point_angle(pointer.x, pointer.y);
                const int existing = owner_.find_pair_containing_angle(angle);
                if (existing >= 0) {
                    owner_.set_focused_pair(existing);
                } else {
                    owner_.add_orbit_pair(angle);
                }
            }
            used = true;
        } else if (SDL_PointInRect(&pointer, &list_rect_)) {
            const int idx = pair_index_at_point(pointer.x, pointer.y);
            if (idx >= 0) {
                owner_.set_focused_pair(idx);
            } else if (owner_.focused_pair_index_ != -1) {
                owner_.set_focused_pair(-1);
            }
            used = true;
        } else if (SDL_PointInRect(&pointer, &rect_)) {
            if (owner_.focused_pair_index_ != -1) {
                owner_.set_focused_pair(-1);
                used = true;
            }
        }
    } else if (e.type == SDL_MOUSEWHEEL) {
        if (owner_.focused_pair_index_ >= 0) {
            const int delta = e.wheel.y;
            if (delta != 0) {
                owner_.adjust_orbit_pair_angle(owner_.focused_pair_index_, delta);
                used = true;
            }
        }
    }

    if (pointer_event && e.type == SDL_MOUSEMOTION) {
        if (!SDL_PointInRect(&pointer, &rect_) && owner_.focused_pair_index_ != -1) {
            owner_.set_focused_pair(-1);
        }
    }

    for (size_t i = 0; i < pair_entries_.size(); ++i) {
        auto& entry = pair_entries_[i];
        if (!entry.widget) {
            continue;
        }
        if (pointer_event && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            if (SDL_PointInRect(&pointer, &entry.outer_rect)) {
                owner_.set_focused_pair(static_cast<int>(i));
            }
        }
        if (entry.widget->handle_event(e)) {
            owner_.set_focused_pair(static_cast<int>(i));
            used = true;
        }
    }

    if (owner_.focused_pair_index_ == -1) {
        release_scroll_capture();
    } else {
        ensure_scroll_capture();
    }

    return used;
}

void MapLightPanel::OrbitKeyWidget::render(SDL_Renderer* r) const {
    if (!r) {
        return;
    }

    const bool disabled = !enabled_;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Color panel_bg = dm_draw::DarkenColor(DMStyles::PanelBG(), 0.08f);
    SDL_SetRenderDrawColor(r, panel_bg.r, panel_bg.g, panel_bg.b, panel_bg.a);
    SDL_RenderFillRect(r, &rect_);

    draw_orbit_circle(r);

    const SDL_Color focus_color = DMStyles::ButtonFocusOutline();
    const SDL_Color hover_color = DMStyles::HighlightColor();

    if (list_rect_.w > 0 && list_rect_.h > 0) {
        SDL_Color list_bg = dm_draw::DarkenColor(DMStyles::PanelBG(), 0.14f);
        SDL_SetRenderDrawColor(r, list_bg.r, list_bg.g, list_bg.b, list_bg.a);
        SDL_RenderFillRect(r, &list_rect_);
        SDL_Color list_border = DMStyles::Border();
        SDL_SetRenderDrawColor(r, list_border.r, list_border.g, list_border.b, list_border.a);
        SDL_RenderDrawRect(r, &list_rect_);
    }

    for (size_t i = 0; i < owner_.orbit_key_pairs_.size(); ++i) {
        const auto& pair = owner_.orbit_key_pairs_[i];
        SDL_Color color = utils::color::resolve_ranged_color(pair.color);
        if (disabled) {
            color = dm_draw::DarkenColor(color, 0.35f);
        }
        const bool focused = (owner_.focused_pair_index_ == static_cast<int>(i));
        const bool hovered_pair = (hovered_pair_index_ == static_cast<int>(i));
        const double primary = MapLightPanel::normalize_angle(pair.angle);
        const double mirror = MapLightPanel::normalize_angle(180.0 - pair.angle);
        draw_orbit_line(r, primary, color, focused && !disabled, hovered_pair && !disabled);
        draw_orbit_line(r, mirror, color, focused && !disabled, hovered_pair && !disabled);
    }

    for (size_t i = 0; i < pair_entries_.size(); ++i) {
        const auto& entry = pair_entries_[i];
        if (!entry.widget) {
            continue;
        }
        const bool focused_entry = (owner_.focused_pair_index_ == static_cast<int>(i));
        const bool hovered_entry = (hovered_pair_index_ == static_cast<int>(i));
        if (entry.outer_rect.w > 0 && entry.outer_rect.h > 0) {
            const int radius = std::min(DMStyles::CornerRadius(), 6);
            SDL_Color base = dm_draw::DarkenColor(DMStyles::PanelBG(), 0.02f);
            SDL_Color outline = DMStyles::Border();
            if (hovered_entry) {
                base = dm_draw::LightenColor(base, 0.12f);
                outline = hover_color;
            }
            if (focused_entry) {
                base = dm_draw::LightenColor(base, 0.2f);
                outline = focus_color;
            }
            dm_draw::DrawBeveledRect( r, entry.outer_rect, radius, 1, base, base, base, false, 0.0f, 0.0f);
            dm_draw::DrawRoundedOutline(r, entry.outer_rect, radius, 1, outline);
        }
        entry.widget->render(r);
    }

    if (disabled) {
        SDL_Color overlay = dm_draw::LightenColor(DMStyles::PanelBG(), 0.1f);
        overlay.a = 180;
        SDL_SetRenderDrawColor(r, overlay.r, overlay.g, overlay.b, overlay.a);
        SDL_RenderFillRect(r, &rect_);
        SDL_Color outline = DMStyles::Border();
        SDL_SetRenderDrawColor(r, outline.r, outline.g, outline.b, 170);
        SDL_RenderDrawRect(r, &rect_);
    }
}

bool MapLightPanel::OrbitKeyWidget::handle_overlay_event(const SDL_Event& e) {
    if (!enabled_) {
        return false;
    }
    bool used = false;
    for (size_t i = 0; i < pair_entries_.size(); ++i) {
        auto& entry = pair_entries_[i];
        if (entry.widget && entry.widget->handle_overlay_event(e)) {
            owner_.set_focused_pair(static_cast<int>(i));
            used = true;
        }
    }
    return used;
}

void MapLightPanel::OrbitKeyWidget::render_overlay(SDL_Renderer* r) const {
    if (!enabled_) {
        return;
    }
    for (const auto& entry : pair_entries_) {
        if (entry.widget) {
            entry.widget->render_overlay(r);
        }
    }
}

void MapLightPanel::OrbitKeyWidget::update_overlays(const Input& input, int screen_w, int screen_h) {
    if (!enabled_) {
        return;
    }
    for (auto& entry : pair_entries_) {
        if (entry.widget) {
            entry.widget->update_overlay(input, screen_w, screen_h);
        }
    }
}

void MapLightPanel::OrbitKeyWidget::on_pairs_changed() {
    rebuild_pair_entries();
    update_internal_layout();
    hovered_pair_index_ = -1;
    hovered_source_ = HoverSource::None;
}

void MapLightPanel::OrbitKeyWidget::on_focus_changed() {
    if (!enabled_) {
        release_scroll_capture();
        return;
    }
    if (owner_.focused_pair_index_ >= 0) {
        ensure_scroll_capture();
    } else {
        release_scroll_capture();
    }
}

void MapLightPanel::OrbitKeyWidget::update_internal_layout() {
    const int pad = DMSpacing::item_gap();
    const int gap = DMSpacing::item_gap();
    const int min_list_width = 200;

    const int available_w = std::max(0, rect_.w - pad * 2);
    const int available_h = std::max(0, rect_.h - pad * 2);

    int circle_size = std::min(available_w, available_h);
    if (circle_size > available_w - min_list_width - gap) {
        circle_size = std::max(120, available_w - min_list_width - gap);
    }
    circle_size = std::max(120, std::min(circle_size, available_h));

    circle_rect_ = SDL_Rect{ rect_.x + pad, rect_.y + pad, circle_size, circle_size };

    int list_x = circle_rect_.x + circle_rect_.w + gap;
    int list_w = rect_.x + rect_.w - pad - list_x;
    if (list_w < min_list_width) {
        const int deficit = min_list_width - list_w;
        int adjusted_circle = std::max(80, circle_size - deficit);
        adjusted_circle = std::min(adjusted_circle, available_h);
        circle_rect_.w = circle_rect_.h = adjusted_circle;
        list_x = circle_rect_.x + circle_rect_.w + gap;
        list_w = rect_.x + rect_.w - pad - list_x;
    }
    if (list_w < 0) {
        list_w = 0;
    }
    list_rect_ = SDL_Rect{ list_x, rect_.y + pad, list_w, available_h };

    layout_color_widgets();
}

void MapLightPanel::OrbitKeyWidget::rebuild_pair_entries() {
    const size_t count = owner_.orbit_key_pairs_.size();
    pair_entries_.resize(count);
    for (size_t i = 0; i < count; ++i) {
        auto& entry = pair_entries_[i];
        if (!entry.widget) {
            entry.widget = std::make_unique<DMColorRangeWidget>("Pair " + std::to_string(i + 1));
        }
        entry.widget->set_label("Pair " + std::to_string(i + 1));
        entry.widget->set_value(owner_.orbit_key_pairs_[i].color);
        entry.widget->set_on_value_changed([this, idx = static_cast<int>(i)](const utils::color::RangedColor& value) {
            owner_.handle_pair_color_changed(idx, value);
        });
        entry.widget->set_on_sample_requested(
            [this](const utils::color::RangedColor& current,
                   std::function<void(SDL_Color)> on_sample,
                   std::function<void()> on_cancel) {
                if (owner_.map_color_sample_callback_) {
                    owner_.map_color_sample_callback_(current, std::move(on_sample), std::move(on_cancel));
                } else if (on_cancel) {
                    on_cancel();
                }
            });
    }
}

void MapLightPanel::OrbitKeyWidget::layout_color_widgets() {
    const int gap = DMSpacing::small_gap();
    const int inner_gap = DMSpacing::small_gap();
    int y = list_rect_.y + gap;
    const int width = std::max(list_rect_.w - gap * 2, 0);
    for (auto& entry : pair_entries_) {
        if (!entry.widget) {
            continue;
        }
        const int widget_height = entry.widget->height_for_width(std::max(0, width - inner_gap * 2));
        const int outer_height = widget_height + inner_gap * 2;
        entry.outer_rect = SDL_Rect{ list_rect_.x + gap, y, width, outer_height };
        entry.widget_rect = SDL_Rect{
            entry.outer_rect.x + inner_gap,
            entry.outer_rect.y + inner_gap,
            std::max(0, entry.outer_rect.w - inner_gap * 2), widget_height};
        entry.widget->set_rect(entry.widget_rect);
        y += outer_height + gap;
    }
}

void MapLightPanel::OrbitKeyWidget::ensure_scroll_capture() {
    if (!scroll_capture_active_) {
        DMWidgetsSetSliderScrollCapture(this, true);
        scroll_capture_active_ = true;
    }
}

void MapLightPanel::OrbitKeyWidget::release_scroll_capture() {
    if (scroll_capture_active_) {
        DMWidgetsSetSliderScrollCapture(this, false);
        scroll_capture_active_ = false;
    }
}

int MapLightPanel::OrbitKeyWidget::pair_index_at_point(int x, int y) const {
    SDL_Point p{x, y};
    for (size_t i = 0; i < pair_entries_.size(); ++i) {
        if (SDL_PointInRect(&p, &pair_entries_[i].outer_rect)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

double MapLightPanel::OrbitKeyWidget::line_distance_to_point(double angle_deg, int x, int y) const {
    if (circle_rect_.w <= 0 || circle_rect_.h <= 0) {
        return -1.0;
    }
    const int cx = circle_rect_.x + circle_rect_.w / 2;
    const int cy = circle_rect_.y + circle_rect_.h / 2;
    const double px = static_cast<double>(x - cx);
    const double py = static_cast<double>(cy - y);
    const double radians = angle_deg * M_PI / 180.0;
    const double dir_x = std::cos(radians);
    const double dir_y = std::sin(radians);
    const double radius = circle_rect_.w * 0.5;
    const double proj = px * dir_x + py * dir_y;
    if (proj < 0.0 || proj > radius) {
        return -1.0;
    }
    const double perp_x = px - proj * dir_x;
    const double perp_y = py - proj * dir_y;
    return std::sqrt(perp_x * perp_x + perp_y * perp_y);
}

int MapLightPanel::OrbitKeyWidget::line_hit_test(int x, int y) const {
    if (owner_.orbit_key_pairs_.empty() || circle_rect_.w <= 0) {
        return -1;
    }
    const double radius = circle_rect_.w * 0.5;
    const int cx = circle_rect_.x + circle_rect_.w / 2;
    const int cy = circle_rect_.y + circle_rect_.h / 2;
    const double dx = static_cast<double>(x - cx);
    const double dy = static_cast<double>(cy - y);
    const double distance_sq = dx * dx + dy * dy;
    const double max_distance = (radius + 6.0);
    if (distance_sq > max_distance * max_distance) {
        return -1;
    }

    int best_index = -1;
    double best_distance = 6.0;
    for (size_t i = 0; i < owner_.orbit_key_pairs_.size(); ++i) {
        const auto& pair = owner_.orbit_key_pairs_[i];
        const double primary = MapLightPanel::normalize_angle(pair.angle);
        const double mirror = MapLightPanel::normalize_angle(180.0 - pair.angle);
        const double dist_primary = line_distance_to_point(primary, x, y);
        if (dist_primary >= 0.0 && dist_primary <= best_distance) {
            best_distance = dist_primary;
            best_index = static_cast<int>(i);
        }
        const double dist_mirror = line_distance_to_point(mirror, x, y);
        if (dist_mirror >= 0.0 && dist_mirror <= best_distance) {
            best_distance = dist_mirror;
            best_index = static_cast<int>(i);
        }
    }
    return best_index;
}

double MapLightPanel::OrbitKeyWidget::point_angle(int x, int y) const {
    if (circle_rect_.w <= 0 || circle_rect_.h <= 0) {
        return 0.0;
    }
    const int cx = circle_rect_.x + circle_rect_.w / 2;
    const int cy = circle_rect_.y + circle_rect_.h / 2;
    const double px = static_cast<double>(x - cx);
    const double py = static_cast<double>(cy - y);
    double angle = std::atan2(py, px) * 180.0 / M_PI;
    if (angle < 0.0) {
        angle += 360.0;
    }
    return angle;
}

void MapLightPanel::OrbitKeyWidget::draw_orbit_circle(SDL_Renderer* r) const {
    if (circle_rect_.w <= 0 || circle_rect_.h <= 0) {
        return;
    }
    SDL_Color circle_bg = dm_draw::DarkenColor(DMStyles::PanelBG(), 0.14f);
    SDL_SetRenderDrawColor(r, circle_bg.r, circle_bg.g, circle_bg.b, circle_bg.a);
    SDL_RenderFillRect(r, &circle_rect_);

    const int cx = circle_rect_.x + circle_rect_.w / 2;
    const int cy = circle_rect_.y + circle_rect_.h / 2;
    const double radius = circle_rect_.w * 0.5;
    const int segments = 96;
    std::array<SDL_Point, segments + 1> points{};
    SDL_Color border = DMStyles::Border();
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
    for (int i = 0; i <= segments; ++i) {
        const double t = (static_cast<double>(i) / segments) * 2.0 * M_PI;
        const int px = static_cast<int>(std::round(cx + std::cos(t) * radius));
        const int py = static_cast<int>(std::round(cy - std::sin(t) * radius));
        points[i] = SDL_Point{px, py};
    }
    SDL_RenderDrawLines(r, points.data(), segments + 1);
}

void MapLightPanel::OrbitKeyWidget::draw_orbit_line(SDL_Renderer* r,
                                                    double angle_deg,
                                                    const SDL_Color& color,
                                                    bool focused,
                                                    bool hovered) const {
    if (circle_rect_.w <= 0 || circle_rect_.h <= 0) {
        return;
    }
    const int cx = circle_rect_.x + circle_rect_.w / 2;
    const int cy = circle_rect_.y + circle_rect_.h / 2;
    const double radius = circle_rect_.w * 0.5;
    const double radians = angle_deg * M_PI / 180.0;
    const double end_x = cx + std::cos(radians) * radius;
    const double end_y = cy - std::sin(radians) * radius;

    SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLine(r, cx, cy, static_cast<int>(std::round(end_x)), static_cast<int>(std::round(end_y)));

    const int ex = static_cast<int>(std::round(end_x));
    const int ey = static_cast<int>(std::round(end_y));

    if (focused || hovered) {
        auto draw_glow = [&](const SDL_Color& glow_color, int offset_x, int offset_y, Uint8 alpha) {
            SDL_SetRenderDrawColor(r, glow_color.r, glow_color.g, glow_color.b, alpha);
            SDL_RenderDrawLine(r, cx + offset_x, cy + offset_y, ex + offset_x, ey + offset_y);
};

        if (focused) {
            const SDL_Color focus_glow = DMStyles::ButtonFocusOutline();
            draw_glow(focus_glow, 0, 0, 200);
            draw_glow(focus_glow, 1, 0, 130);
            draw_glow(focus_glow, -1, 0, 130);
        } else {
            const SDL_Color hover_glow{255, 255, 255, 255};
            draw_glow(hover_glow, 0, 0, 160);
            draw_glow(hover_glow, 1, 0, 100);
            draw_glow(hover_glow, -1, 0, 100);
        }
    }
}

int MapLightPanel::clamp_int(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

float MapLightPanel::clamp_float(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

float MapLightPanel::wrap_angle(float a) {

    while (a < 0.0f)   a += 360.0f;
    while (a >= 360.0f) a -= 360.0f;
    return a;
}

MapLightPanel::MapLightPanel(int x, int y)
: DockableCollapsible("Map Lighting", true, x, y) {
    set_expanded(true);
    chunk_resolution_value_ = MapGridSettings::defaults().r_chunk;
    build_ui();
    update_save_status(true);
}

MapLightPanel::~MapLightPanel() = default;

void MapLightPanel::set_map_info(json* map_info, SaveCallback on_save) {
    map_info_ = map_info;
    on_save_ = std::move(on_save);
    editing_light_ = json::object();
    map_color_ = kDefaultMapColor;
    chunk_resolution_value_ = MapGridSettings::defaults().r_chunk;
    suppress_map_color_callback_ = false;
    orbit_key_pairs_.clear();
    focused_pair_index_ = -1;
    next_pair_id_ = 1;
    if (map_info_ && map_info_->contains("map_light_data") && (*map_info_)["map_light_data"].is_object()) {
        editing_light_ = (*map_info_)["map_light_data"];
    }
    ensure_light();
    update_save_status(true);
    load_update_map_light_setting();
    sync_ui_from_json();
}

void MapLightPanel::set_assets(Assets* assets) {
    assets_ = assets;
}

void MapLightPanel::set_update_map_light_callback(std::function<void(bool)> cb) {
    update_map_light_callback_ = std::move(cb);
}

nlohmann::json& MapLightPanel::mutable_light() {
    return ensure_light();
}

bool MapLightPanel::commit_light_changes_external() {
    return commit_light_changes();
}

void MapLightPanel::open()   {
    set_visible(true);
    set_expanded(true);

    setLocked(false);
}
void MapLightPanel::close()  { set_visible(false); }
void MapLightPanel::toggle() {
    if (is_visible()) {
        close();
    } else {
        open();
    }
}
bool MapLightPanel::is_visible() const { return visible_; }

void MapLightPanel::build_ui() {

    update_map_light_checkbox_ = std::make_unique<DMCheckbox>("Update Map Light", false);
    orbit_section_btn_ = std::make_unique<DMButton>("", &DMStyles::HeaderButton(), 220, DMButton::height());
    texture_section_btn_ = std::make_unique<DMButton>("", &DMStyles::HeaderButton(), 220, DMButton::height());
    update_section_header_labels();

    orbit_x_        = std::make_unique<DMSlider>("Orbit X Radius",  0, 20000, 0);
    orbit_y_        = std::make_unique<DMSlider>("Orbit Y Radius",  0, 20000, 0);
    update_interval_= std::make_unique<DMSlider>("Update Interval", 1,   120, 10);
    chunk_resolution_ = std::make_unique<DMSlider>("Chunk Resolution (2^r px)", 0, vibble::grid::kMaxResolution, chunk_resolution_value_);

    if (update_interval_) {
        update_interval_->set_defer_commit_until_unfocus(true);
        update_interval_->set_enabled(false);
    }
    if (orbit_x_) {
        orbit_x_->set_defer_commit_until_unfocus(true);
        orbit_x_->set_enabled(false);
    }
    if (orbit_y_) {
        orbit_y_->set_defer_commit_until_unfocus(true);
        orbit_y_->set_enabled(false);
    }
    if (chunk_resolution_) {
        chunk_resolution_->set_defer_commit_until_unfocus(false);
        chunk_resolution_->set_value_formatter([](int value,
                                                  std::array<char, dev_mode::kSliderFormatBufferSize>& buffer)
                                                   -> std::string_view {
            const int clamped = std::max(0, std::min(value, vibble::grid::kMaxResolution));
            const int size_px = 1 << clamped;
            std::snprintf(buffer.data(), buffer.size(), "r=%d (%d px)", clamped, size_px);
            return buffer.data();
        });
    }

    rebuild_rows();
}

void MapLightPanel::update_section_header_labels() {
    auto label_for = [](const std::string& title, bool collapsed) {
        std::string label = collapsed ? std::string(DMIcons::CollapseCollapsed()) : std::string(DMIcons::CollapseExpanded());
        label.push_back(' ');
        label += title;
        return label;
};
    if (orbit_section_btn_) {
        orbit_section_btn_->set_text(label_for("Orbit Settings", orbit_section_collapsed_));
    }
    if (texture_section_btn_) {
        texture_section_btn_->set_text(label_for("Map Light Texture", texture_section_collapsed_));
    }
}

void MapLightPanel::rebuild_rows() {
    update_section_header_labels();

    widget_wrappers_.clear();
    widget_wrappers_.reserve(128);
    orbit_key_widget_ = nullptr;
    map_color_widget_ = nullptr;

    auto add_widget = [this](std::unique_ptr<Widget> w) -> Widget* {
        Widget* raw = w.get();
        widget_wrappers_.push_back(std::move(w));
        return raw;
};

    Rows rows;

    auto warning_label = std::make_unique<WarningLabel>();
    warning_label_ = warning_label.get();
    warning_label_->set_color(SDL_Color{255, 120, 120, 255});
    if (!persistence_warning_text_.empty()) {
        warning_label_->set_text(persistence_warning_text_);
    }
    rows.push_back({ add_widget(std::move(warning_label)) });

    load_update_map_light_setting();
    if (update_map_light_checkbox_) {
        rows.push_back({ add_widget(std::make_unique<CheckboxWidget>(update_map_light_checkbox_.get())) });
    }
    if (chunk_resolution_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(chunk_resolution_.get())) });
    }

    rows.push_back({ add_widget(std::make_unique<SectionToggleWidget>(orbit_section_btn_.get(), [this]() { toggle_orbit_section(); })) });
    if (!orbit_section_collapsed_) {
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(update_interval_.get())),
            add_widget(std::make_unique<SliderWidget>(orbit_x_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(orbit_y_.get()))
        });
    }

    rows.push_back({ add_widget(std::make_unique<SectionToggleWidget>(texture_section_btn_.get(), [this]() { toggle_texture_section(); })) });
    if (!texture_section_collapsed_) {
        auto orbit_widget = std::make_unique<OrbitKeyWidget>(*this);
        orbit_key_widget_ = orbit_widget.get();
        if (orbit_key_widget_) {
            orbit_key_widget_->set_enabled(false);
        }
        rows.push_back({ add_widget(std::move(orbit_widget)) });
    }

    auto map_color_widget = std::make_unique<DMColorRangeWidget>("Map Color");
    map_color_widget_ = map_color_widget.get();
    map_color_widget_->set_on_value_changed([this](const utils::color::RangedColor& value) {
        handle_map_color_changed(value);
    });
    map_color_widget_->set_on_sample_requested(
        [this](const utils::color::RangedColor& current,
               std::function<void(SDL_Color)> on_sample,
               std::function<void()> on_cancel) {
            if (map_color_sample_callback_) {
                map_color_sample_callback_(current, std::move(on_sample), std::move(on_cancel));
            } else if (on_cancel) {
                on_cancel();
            }
        });
    rows.push_back({ add_widget(std::move(map_color_widget)) });
    set_map_color_widget_value(map_color_);

    set_rows(rows);
}

void MapLightPanel::toggle_orbit_section() {
    orbit_section_collapsed_ = !orbit_section_collapsed_;
    rebuild_rows();
    sync_ui_from_json();
}

void MapLightPanel::toggle_texture_section() {
    texture_section_collapsed_ = !texture_section_collapsed_;
    rebuild_rows();
    sync_ui_from_json();
}

nlohmann::json& MapLightPanel::ensure_light() {

    if (!editing_light_.is_object()) {
        editing_light_ = json::object();
    }
    json& L = editing_light_;

    auto parse_int = [](const json& value, int fallback) -> std::optional<int> {
        try {
            if (value.is_number_integer()) {
                return value.get<int>();
            }
            if (value.is_number_float()) {
                return static_cast<int>(std::lround(value.get<double>()));
            }
            if (value.is_string()) {
                const std::string text = value.get<std::string>();
                size_t idx = 0;
                int parsed = std::stoi(text, &idx);
                if (idx == text.size()) {
                    return parsed;
                }
            }
        } catch (...) {
        }
        return std::nullopt;
};

    auto read_int = [&](const char* key, int fallback, int lo, int hi) {
        int value = fallback;
        auto it = L.find(key);
        if (it != L.end()) {
            if (auto parsed = parse_int(*it, fallback)) {
                value = *parsed;
            }
        }
        return clamp_int(value, lo, hi);
};

    auto parse_double = [](const json& value, double fallback) -> std::optional<double> {
        try {
            if (value.is_number_float()) {
                return value.get<double>();
            }
            if (value.is_number_integer()) {
                return static_cast<double>(value.get<int>());
            }
            if (value.is_string()) {
                const std::string text = value.get<std::string>();
                size_t idx = 0;
                double parsed = std::stod(text, &idx);
                if (idx == text.size()) {
                    return parsed;
                }
            }
        } catch (...) {
        }
        return std::nullopt;
};

    auto read_double = [&](const char* key, double fallback, double lo, double hi) {
        double value = fallback;
        auto it = L.find(key);
        if (it != L.end()) {
            if (auto parsed = parse_double(*it, fallback)) {
                value = *parsed;
            }
        }
        return std::clamp(value, lo, hi);
};

    L["radius"] = read_int("radius", 0, 0, 20000);
    L["intensity"] = read_int("intensity", 255, 0, 255);
    L["fall_off"] = read_int("fall_off", 100, 0, 100);
    L["update_interval"] = read_int("update_interval", 10, 1, 120);

    double mult = read_double("mult", 0.0, 0.0, 1.0);
    L["mult"] = mult;

    L.erase("min_opacity");
    L.erase("max_opacity");

    L.erase("orbit_x");
    L.erase("orbit_y");
    L.erase("orbit_radius");

    utils::color::RangedColor base_range{{255,255},{255,255},{255,255},{255,255}};
    if (auto parsed = utils::color::ranged_color_from_json(L.value("base_color", nlohmann::json{}))) {
        base_range = *parsed;
    }
    L["base_color"] = utils::color::ranged_color_to_json(base_range);

    if (!L.contains("keys") || !L["keys"].is_array()) {

        L["keys"] = json::array();
        L["keys"].push_back(json::array({ 0.0, L["base_color"] }));
    } else {
        auto& keys = L["keys"];
        for (auto& entry : keys) {
            if (entry.is_array() && entry.size() >= 2) {
                if (auto parsed = utils::color::ranged_color_from_json(entry[1])) {
                    entry[1] = utils::color::ranged_color_to_json(*parsed);
                }
            }
        }
    }
    utils::color::RangedColor sanitized_map_color =
        utils::color::ranged_color_from_json(L.value("map_color", nlohmann::json{}))
            .value_or(kDefaultMapColor);
    sanitized_map_color = utils::color::clamp_ranged_color(sanitized_map_color);
    L["map_color"] = utils::color::ranged_color_to_json(sanitized_map_color);
    return L;
}

void MapLightPanel::sync_chunk_slider_from_json() {
    int chunk_value = chunk_resolution_value_;
    if (map_info_ && map_info_->is_object()) {
        auto grid_it = map_info_->find("map_grid_settings");
        if (grid_it != map_info_->end() && grid_it->is_object()) {
            MapGridSettings grid_settings = MapGridSettings::from_json(&(*grid_it));
            chunk_value = grid_settings.r_chunk;
        }
    }
    chunk_value = clamp_int(chunk_value, 0, vibble::grid::kMaxResolution);
    chunk_resolution_value_ = chunk_value;
    if (chunk_resolution_) {
        chunk_resolution_->set_value(chunk_value);
    }
}

void MapLightPanel::persist_chunk_resolution() {
    if (!map_info_) {
        return;
    }
    if (!map_info_->is_object()) {
        *map_info_ = json::object();
    }
    nlohmann::json& grid_section = (*map_info_)["map_grid_settings"];
    if (!grid_section.is_object()) {
        grid_section = json::object();
    }
    MapGridSettings grid_settings = MapGridSettings::from_json(&grid_section);
    const int slider_value = chunk_resolution_ ? chunk_resolution_->value() : chunk_resolution_value_;
    const int previous_value = chunk_resolution_value_;
    grid_settings.r_chunk = clamp_int(slider_value, 0, vibble::grid::kMaxResolution);
    grid_settings.resolution = grid_settings.r_chunk;
    grid_settings.clamp();
    chunk_resolution_value_ = grid_settings.r_chunk;
    grid_settings.apply_to_json(grid_section);
    if (chunk_resolution_ && chunk_resolution_->value() != grid_settings.r_chunk) {
        chunk_resolution_->set_value(grid_settings.r_chunk);
    }
    const bool chunk_changed = (chunk_resolution_value_ != previous_value);
    if (chunk_changed && assets_) {
        assets_->apply_map_grid_settings(grid_settings);
        assets_->force_shaded_assets_rerender();
    }
    if (chunk_changed && !update_map_light_enabled_ && on_save_) {
        bool ok = on_save_();
        update_save_status(ok);
    }
}

void MapLightPanel::sync_ui_from_json() {
    json& L = ensure_light();

    OrbitSettings orbit{};
    orbit.update_interval = clamp_int(L.value("update_interval", 10), 1, 120);
    orbit.orbit_x = 0;
    orbit.orbit_y = 0;
    orbit = sanitize_orbit_settings(orbit);
    set_orbit_sliders(orbit);
    last_applied_orbit_ = orbit;

    utils::color::RangedColor base_range{
        {255, 255}, {255, 255}, {255, 255}, {255, 255}
};
    if (auto parsed = utils::color::ranged_color_from_json(L.value("base_color", nlohmann::json{}))) {
        base_range = *parsed;
    }
    rebuild_orbit_key_pairs_from_json();
    refresh_orbit_widget();

    map_color_ = utils::color::ranged_color_from_json(L.value("map_color", nlohmann::json{})).value_or(kDefaultMapColor);
    map_color_ = utils::color::clamp_ranged_color(map_color_);
    set_map_color_widget_value(map_color_);
    sync_chunk_slider_from_json();

    needs_sync_to_json_ = false;
}

void MapLightPanel::sync_json_from_ui() {
    json& L = ensure_light();

    OrbitSettings orbit = sanitize_orbit_settings(current_orbit_settings_from_ui());
    write_orbit_settings_to_json(orbit);

    set_orbit_sliders(orbit);

    ensure_keys_array();
    write_orbit_pairs_to_json();
    write_map_color_to_json();
    persist_chunk_resolution();

    if (update_map_light_enabled_) {
        if (commit_light_changes()) {
            last_applied_orbit_ = orbit;
        }
    }

    needs_sync_to_json_ = false;
}

void MapLightPanel::load_update_map_light_setting() {

    update_map_light_enabled_ = devmode::ui_settings::load_bool(kUpdateMapLightSettingKey, true);
    if (update_map_light_checkbox_) {
        update_map_light_checkbox_->set_value(update_map_light_enabled_);
    }
    if (update_map_light_callback_) {
        update_map_light_callback_(update_map_light_enabled_);
    }
}

void MapLightPanel::ensure_keys_array() {
    json& L = ensure_light();
    if (!L.contains("keys") || !L["keys"].is_array()) {
        L["keys"] = json::array();
    }
}

MapLightPanel::OrbitSettings MapLightPanel::sanitize_orbit_settings(const OrbitSettings& raw) const {
    OrbitSettings out = raw;
    out.update_interval = clamp_int(out.update_interval, 1, 120);
    out.orbit_x = clamp_int(out.orbit_x, 0, 20000);
    out.orbit_y = clamp_int(out.orbit_y, 0, 20000);
    return out;
}

MapLightPanel::OrbitSettings MapLightPanel::current_orbit_settings_from_ui() const {
    OrbitSettings current;
    current.update_interval = update_interval_ ? update_interval_->displayed_value() : 10;
    current.orbit_x = orbit_x_ ? orbit_x_->displayed_value() : 0;
    current.orbit_y = orbit_y_ ? orbit_y_->displayed_value() : current.orbit_x;
    return current;
}

void MapLightPanel::set_orbit_sliders(const OrbitSettings& orbit) {
    if (update_interval_) update_interval_->set_value(orbit.update_interval);
    if (orbit_x_)         orbit_x_->set_value(orbit.orbit_x);
    if (orbit_y_)         orbit_y_->set_value(orbit.orbit_y);
}

void MapLightPanel::write_orbit_settings_to_json(const OrbitSettings& orbit) {
    json& L = ensure_light();
    L["update_interval"] = orbit.update_interval;
    L.erase("orbit_x");
    L.erase("orbit_y");
    L.erase("orbit_radius");
    L.erase("min_opacity");
    L.erase("max_opacity");
}

void MapLightPanel::apply_immediate_settings() {
    if (!map_info_) {
        return;
    }

    OrbitSettings orbit = sanitize_orbit_settings(current_orbit_settings_from_ui());
    bool orbit_changed = !(orbit == last_applied_orbit_);
    if (!orbit_changed) {
        return;
    }

    write_orbit_settings_to_json(orbit);
    set_orbit_sliders(orbit);

    bool ok = commit_light_changes();
    if (ok) {
        last_applied_orbit_ = orbit;
    }
}

bool MapLightPanel::commit_light_changes() {
    if (!map_info_) {
        return false;
    }
    if (!map_info_->is_object()) {
        *map_info_ = json::object();
    }
    (*map_info_)["map_light_data"] = ensure_light();

    bool ok = true;
    if (on_save_) {
        ok = on_save_();
    }
    update_save_status(ok);
    return ok;
}

void MapLightPanel::refresh_orbit_widget() {
    if (orbit_key_widget_) {
        orbit_key_widget_->on_pairs_changed();
        orbit_key_widget_->on_focus_changed();
    }
}

void MapLightPanel::set_focused_pair(int index) {
    if (index < 0 || index >= static_cast<int>(orbit_key_pairs_.size())) {
        focused_pair_index_ = -1;
    } else {
        focused_pair_index_ = index;
    }
    if (orbit_key_widget_) {
        orbit_key_widget_->on_focus_changed();
    }
}

void MapLightPanel::set_focused_pair_by_id(int id) {
    if (id < 0) {
        set_focused_pair(-1);
        return;
    }
    for (size_t i = 0; i < orbit_key_pairs_.size(); ++i) {
        if (orbit_key_pairs_[i].id == id) {
            set_focused_pair(static_cast<int>(i));
            return;
        }
    }
    set_focused_pair(-1);
}

void MapLightPanel::add_orbit_pair(double angle_degrees) {
    const int existing = find_pair_containing_angle(angle_degrees);
    if (existing >= 0) {
        set_focused_pair(existing);
        return;
    }

    utils::color::RangedColor default_color = default_pair_color();

    OrbitKeyPair pair;
    pair.id = next_pair_id_++;
    pair.angle = normalize_angle(angle_degrees);
    pair.color = utils::color::clamp_ranged_color(default_color);
    orbit_key_pairs_.push_back(pair);

    sort_orbit_pairs();
    set_focused_pair_by_id(pair.id);
    needs_sync_to_json_ = true;
    refresh_orbit_widget();
}

void MapLightPanel::delete_orbit_pair(int index) {
    if (index < 0 || index >= static_cast<int>(orbit_key_pairs_.size())) {
        return;
    }

    orbit_key_pairs_.erase(orbit_key_pairs_.begin() + index);
    if (orbit_key_pairs_.empty()) {
        utils::color::RangedColor default_color = default_pair_color();
        OrbitKeyPair fallback;
        fallback.id = next_pair_id_++;
        fallback.angle = 0.0;
        fallback.color = utils::color::clamp_ranged_color(default_color);
        orbit_key_pairs_.push_back(fallback);
    }

    set_focused_pair(-1);
    sort_orbit_pairs();
    needs_sync_to_json_ = true;
    refresh_orbit_widget();
}

void MapLightPanel::adjust_orbit_pair_angle(int index, int delta_degrees) {
    if (index < 0 || index >= static_cast<int>(orbit_key_pairs_.size())) {
        return;
    }
    OrbitKeyPair& pair = orbit_key_pairs_[index];
    pair.angle = normalize_angle(pair.angle + static_cast<double>(delta_degrees));
    const int id = pair.id;
    sort_orbit_pairs();
    set_focused_pair_by_id(id);
    needs_sync_to_json_ = true;
    refresh_orbit_widget();
}

void MapLightPanel::handle_pair_color_changed(int index, const utils::color::RangedColor& color) {
    if (index < 0 || index >= static_cast<int>(orbit_key_pairs_.size())) {
        return;
    }
    orbit_key_pairs_[index].color = utils::color::clamp_ranged_color(color);
    needs_sync_to_json_ = true;
}

void MapLightPanel::handle_map_color_changed(const utils::color::RangedColor& color) {
    if (suppress_map_color_callback_) {
        return;
    }
    map_color_ = utils::color::clamp_ranged_color(color);
    suppress_map_color_callback_ = true;
    set_map_color_widget_value(map_color_);
    suppress_map_color_callback_ = false;
    write_map_color_to_json();
    needs_sync_to_json_ = true;
}

void MapLightPanel::write_map_color_to_json() {
    json& L = ensure_light();
    L["map_color"] = utils::color::ranged_color_to_json(map_color_);
}

void MapLightPanel::set_map_color_widget_value(const utils::color::RangedColor& color) {
    if (!map_color_widget_) {
        return;
    }
    suppress_map_color_callback_ = true;
    map_color_widget_->set_value(utils::color::clamp_ranged_color(color));
    suppress_map_color_callback_ = false;
}

void MapLightPanel::set_map_color_sample_callback(ColorSampleRequestCallback cb) {
    map_color_sample_callback_ = std::move(cb);
    if (map_color_widget_) {
        map_color_widget_->set_on_sample_requested(
            [this](const utils::color::RangedColor& current,
                   std::function<void(SDL_Color)> on_sample,
                   std::function<void()> on_cancel) {
                if (map_color_sample_callback_) {
                    map_color_sample_callback_(current, std::move(on_sample), std::move(on_cancel));
                } else if (on_cancel) {
                    on_cancel();
                }
            });
    }
}

int MapLightPanel::find_pair_containing_angle(double angle_degrees) const {
    if (orbit_key_pairs_.empty()) {
        return -1;
    }
    const double target = normalize_angle(angle_degrees);
    const double epsilon = 2.0;
    for (size_t i = 0; i < orbit_key_pairs_.size(); ++i) {
        const double primary = normalize_angle(orbit_key_pairs_[i].angle);
        const double mirror = normalize_angle(180.0 - orbit_key_pairs_[i].angle);
        auto diff = [](double a, double b) {
            double delta = std::fabs(a - b);
            return std::min(delta, 360.0 - delta);
};
        if (diff(primary, target) <= epsilon || diff(mirror, target) <= epsilon) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

utils::color::RangedColor MapLightPanel::default_pair_color() {
    if (!orbit_key_pairs_.empty()) {
        return orbit_key_pairs_.front().color;
    }
    utils::color::RangedColor fallback{{255,255},{255,255},{255,255},{255,255}};
    if (auto parsed = utils::color::ranged_color_from_json(ensure_light().value("base_color", nlohmann::json{}))) {
        return utils::color::clamp_ranged_color(*parsed);
    }
    return fallback;
}

void MapLightPanel::rebuild_orbit_key_pairs_from_json() {
    json& L = ensure_light();
    ensure_keys_array();

    const int previous_focus_id =
        (focused_pair_index_ >= 0 && focused_pair_index_ < static_cast<int>(orbit_key_pairs_.size())) ? orbit_key_pairs_[focused_pair_index_].id : -1;

    utils::color::RangedColor base_range{{255,255},{255,255},{255,255},{255,255}};
    if (auto parsed = utils::color::ranged_color_from_json(L.value("base_color", nlohmann::json{}))) {
        base_range = *parsed;
    }

    orbit_key_pairs_.clear();
    next_pair_id_ = 1;

    const auto& keys = L["keys"];
    std::vector<std::pair<double, utils::color::RangedColor>> parsed_keys;
    parsed_keys.reserve(keys.size());
    for (const auto& entry : keys) {
        if (!entry.is_array() || entry.size() < 2) {
            continue;
        }
        double angle = 0.0;
        try {
            angle = entry[0].get<double>();
        } catch (...) {
            continue;
        }
        utils::color::RangedColor color = base_range;
        if (auto parsed_color = utils::color::ranged_color_from_json(entry[1])) {
            color = *parsed_color;
        }
        parsed_keys.emplace_back(normalize_angle(angle), utils::color::clamp_ranged_color(color));
    }

    std::vector<bool> used(parsed_keys.size(), false);
    const double epsilon = 0.5;
    for (size_t i = 0; i < parsed_keys.size(); ++i) {
        if (used[i]) {
            continue;
        }
        used[i] = true;
        const double angle = parsed_keys[i].first;
        const utils::color::RangedColor color = parsed_keys[i].second;
        const double mirror_target = normalize_angle(180.0 - angle);
        for (size_t j = i + 1; j < parsed_keys.size(); ++j) {
            if (used[j]) {
                continue;
            }
            const double candidate = parsed_keys[j].first;
            double diff = std::fabs(candidate - mirror_target);
            diff = std::min(diff, 360.0 - diff);
            if (diff <= epsilon) {
                used[j] = true;
                break;
            }
        }
        OrbitKeyPair pair;
        pair.id = next_pair_id_++;
        pair.angle = angle;
        pair.color = color;
        orbit_key_pairs_.push_back(pair);
    }

    if (orbit_key_pairs_.empty()) {
        OrbitKeyPair fallback;
        fallback.id = next_pair_id_++;
        fallback.angle = 0.0;
        fallback.color = base_range;
        orbit_key_pairs_.push_back(fallback);
    }

    sort_orbit_pairs();
    set_focused_pair_by_id(previous_focus_id);
    refresh_orbit_widget();
}

void MapLightPanel::write_orbit_pairs_to_json() {
    json& L = ensure_light();
    ensure_keys_array();

    nlohmann::json keys = nlohmann::json::array();
    for (const auto& pair : orbit_key_pairs_) {
        const double primary = normalize_angle(pair.angle);
        const double mirror = normalize_angle(180.0 - pair.angle);
        auto color_json = utils::color::ranged_color_to_json(utils::color::clamp_ranged_color(pair.color));
        std::array<double, 2> angles{primary, mirror};
        std::sort(angles.begin(), angles.end());
        if (std::fabs(angles[0] - angles[1]) < 1e-4) {
            keys.push_back(nlohmann::json::array({ angles[0], color_json }));
        } else {
            keys.push_back(nlohmann::json::array({ angles[0], color_json }));
            keys.push_back(nlohmann::json::array({ angles[1], color_json }));
        }
    }

    std::sort(keys.begin(), keys.end(), [](const nlohmann::json& A, const nlohmann::json& B) {
        double a = 0.0;
        double b = 0.0;
        try { a = A[0].get<double>(); } catch (...) {}
        try { b = B[0].get<double>(); } catch (...) {}
        return a < b;
    });

    L["keys"] = keys;
}

void MapLightPanel::sort_orbit_pairs() {
    if (orbit_key_pairs_.empty()) {
        focused_pair_index_ = -1;
        return;
    }

    int focus_id = -1;
    if (focused_pair_index_ >= 0 && focused_pair_index_ < static_cast<int>(orbit_key_pairs_.size())) {
        focus_id = orbit_key_pairs_[focused_pair_index_].id;
    }

    std::sort(orbit_key_pairs_.begin(), orbit_key_pairs_.end(), [](const OrbitKeyPair& a, const OrbitKeyPair& b) {
        return normalize_angle(a.angle) < normalize_angle(b.angle);
    });

    if (focus_id >= 0) {
        for (size_t i = 0; i < orbit_key_pairs_.size(); ++i) {
            if (orbit_key_pairs_[i].id == focus_id) {
                focused_pair_index_ = static_cast<int>(i);
                return;
            }
        }
    }

    focused_pair_index_ = -1;
}

double MapLightPanel::normalize_angle(double angle) {
    double result = std::fmod(angle, 360.0);
    if (result < 0.0) {
        result += 360.0;
    }
    return result;
}

void MapLightPanel::update(const Input& input, int screen_w, int screen_h) {
    if (!visible_) return;

    DockableCollapsible::update(input, screen_w, screen_h);
    if (orbit_key_widget_) {
        orbit_key_widget_->update_overlays(input, screen_w, screen_h);
    }
    if (map_color_widget_) {
        map_color_widget_->update_overlay(input, screen_w, screen_h);
    }

    apply_immediate_settings();

}

bool MapLightPanel::handle_event(const SDL_Event& e) {
    if (!visible_) return false;

    bool overlay_used = false;
    bool used = false;
    if (orbit_key_widget_ && orbit_key_widget_->handle_overlay_event(e)) {
        overlay_used = true;
        used = true;
    }
    if (!overlay_used && map_color_widget_ && map_color_widget_->handle_overlay_event(e)) {
        overlay_used = true;
        used = true;
    }
    if (!overlay_used) {
        used = DockableCollapsible::handle_event(e);
    }

    if (used) {
        if (!overlay_used) {
            needs_sync_to_json_ = true;
        }
        if (update_map_light_checkbox_) {
            bool current = update_map_light_checkbox_->value();
            if (current != update_map_light_enabled_) {
                update_map_light_enabled_ = current;
                devmode::ui_settings::save_bool(kUpdateMapLightSettingKey, update_map_light_enabled_);
                if (update_map_light_callback_) {
                    update_map_light_callback_(update_map_light_enabled_);
                }
            }
        }
    }

    if (needs_sync_to_json_) {
        sync_json_from_ui();
    }

    return used;
}

void MapLightPanel::render(SDL_Renderer* r) const {
    if (!r) return;
    if (!visible_) return;
    DockableCollapsible::render(r);
    if (orbit_key_widget_) {
        orbit_key_widget_->render_overlay(r);
    }
    if (map_color_widget_) {
        map_color_widget_->render_overlay(r);
    }
}

bool MapLightPanel::is_point_inside(int x, int y) const {
    return DockableCollapsible::is_point_inside(x, y);
}

void MapLightPanel::update_save_status(bool success) const {
    if (!warning_label_) {
        return;
    }
    const std::string failure_message = "Failed to save map lighting changes. Check logs.";
    if (success) {
        if (!persistence_warning_text_.empty()) {
            persistence_warning_text_.clear();
            warning_label_->set_text({});
            const_cast<MapLightPanel*>(this)->layout();
        }
        return;
    }
    if (persistence_warning_text_ != failure_message) {
        persistence_warning_text_ = failure_message;
        warning_label_->set_text(persistence_warning_text_);
        const_cast<MapLightPanel*>(this)->layout();
    }
}

void MapLightPanel::render_content(SDL_Renderer* r) const {
    (void)r;
}

