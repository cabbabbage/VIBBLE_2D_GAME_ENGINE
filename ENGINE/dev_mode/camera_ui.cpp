#include "camera_ui.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <functional>
#include <sstream>

#include "core/AssetsManager.hpp"
#include "dev_mode/depth_cue_settings.hpp"
#include "dev_mode/dm_icons.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/font_cache.hpp"
#include "dev_mode/float_slider_widget.hpp"
#include "dev_mode/shared/formatting.hpp"
#include "dev_mode/widgets.hpp"
#include "render/warped_screen_grid.hpp"
#include "utils/input.hpp"

namespace {
    constexpr float kPi     = 3.14159265358979323846f;
    constexpr float kRadToDeg = 180.0f / kPi;
    constexpr float kDegToRad = kPi / 180.0f;
    constexpr const char* kCameraIconPath = "SRC/icons/camera.png";

    float wrap_angle_deg(float raw_value) {
        if (!std::isfinite(raw_value)) {
            return 0.0f;
        }
        float wrapped = std::fmod(raw_value, 360.0f);
        if (wrapped < 0.0f) wrapped += 360.0f;
        if (wrapped >= 360.0f) wrapped = std::fmod(wrapped, 360.0f);
        if (wrapped < 0.0f) wrapped += 360.0f;
        return wrapped;
    }

    float angle_to_pitch_deg(float angle_deg) {
        return wrap_angle_deg(angle_deg);
    }

    float angular_distance_deg(float a, float b) {
        const float diff = std::fabs(wrap_angle_deg(a) - wrap_angle_deg(b));
        const float wrapped = std::fmod(diff, 360.0f);
        return std::min(wrapped, 360.0f - wrapped);
    }

    float pitch_to_angle_deg(float pitch_deg, float preferred_angle_deg = 0.0f) {
        return wrap_angle_deg(pitch_deg);
    }

    float clamp_angle_deg(float raw_value, float min_deg, float max_deg) {
        return std::clamp(raw_value, min_deg, max_deg);
    }
}

class SpacerWidget : public Widget {
public:
    explicit SpacerWidget(int height)
        : height_(std::max(0, height)) {}

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int) const override { return height_; }
    bool handle_event(const SDL_Event&) override { return false; }
    void render(SDL_Renderer*) const override {}
    bool wants_full_row() const override { return true; }

private:
    SDL_Rect rect_{0, 0, 0, 0};
    int height_ = 0;
};

class GroupLabelWidget : public Widget {
public:
    explicit GroupLabelWidget(std::string text)
        : text_(std::move(text)) {
        style_ = DMStyles::Label();
    }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int) const override { return DMCheckbox::height(); }
    bool handle_event(const SDL_Event&) override { return false; }
    void render(SDL_Renderer* renderer) const override {
        if (!renderer) return;
        const int text_y = rect_.y + std::max(0, (DMCheckbox::height() - style_.font_size) / 2);
        DrawLabelText(renderer, text_, rect_.x, text_y, style_);
    }
    bool wants_full_row() const override { return true; }
private:
    std::string text_{};
    DMLabelStyle style_{};
    SDL_Rect rect_{0,0,0,DMCheckbox::height()};
};

class PanelBannerWidget  : public Widget {
public:
    PanelBannerWidget(std::string heading, std::string detail)
        : heading_(std::move(heading)),
          detail_(std::move(detail)) {
        heading_style_ = DMStyles::Label();
        heading_style_.font_size = std::max(heading_style_.font_size + 2, 18);
        heading_style_.color = DMStyles::AccentButton().text;

        body_style_ = DMStyles::Label();
        body_style_.font_size = std::max(12, body_style_.font_size - 2);
        body_style_.color = dm::rgba(255, 255, 255, 230);
    }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int w) const override {
        const int inner = std::max(1, w - 2 * padding());
        ensure_lines(inner);
        const int heading_h = heading_style_.font_size + kHeadingGap;
        const int body_lines = std::max(1, static_cast<int>(lines_.size()));
        const int line_h = body_style_.font_size + kLineGap;
        return padding() * 2 + heading_h + body_lines * line_h;
    }

    bool handle_event(const SDL_Event&) override { return false; }

    void render(SDL_Renderer* renderer) const override {
        if (!renderer) return;
        SDL_Color accent = DMStyles::AccentButton().bg;
        SDL_Color background{ accent.r, accent.g, accent.b, static_cast<Uint8>(220) };
        SDL_SetRenderDrawColor(renderer, background.r, background.g, background.b, background.a);
        SDL_RenderFillRect(renderer, &rect_);

        SDL_Color border = DMStyles::AccentButton().border;
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &rect_);

        const int pad = padding();
        SDL_Rect content{ rect_.x + pad, rect_.y + pad, rect_.w - 2 * pad, rect_.h - 2 * pad };
        DrawLabelText(renderer, heading_, content.x, content.y, heading_style_);
        int text_y = content.y + heading_style_.font_size + kHeadingGap;

        ensure_lines(content.w);
        for (const auto& line : lines_) {
            DrawLabelText(renderer, line, content.x, text_y, body_style_);
            text_y += body_style_.font_size + kLineGap;
        }
    }

    bool wants_full_row() const override { return true; }

private:
    static std::vector<std::string> wrap_lines(const std::string& text, int max_width, const DMLabelStyle& style) {
        std::vector<std::string> lines;
        if (text.empty() || max_width <= 0) {
            if (!text.empty()) lines.push_back(text);
            return lines;
        }
        std::istringstream stream(text);
        std::string word;
        std::string current;
        while (stream >> word) {
            std::string candidate = current.empty() ? word : current + " " + word;
            SDL_Point dims = MeasureLabelText(style, candidate);
            if (!current.empty() && dims.x > max_width) {
                lines.push_back(current);
                current = word;
                continue;
            }
            current = candidate;
        }
        if (!current.empty()) {
            lines.push_back(current);
        }
        if (lines.empty()) {
            lines.push_back(text);
        }
        return lines;
    }

    void ensure_lines(int inner_width) const {
        int width = std::max(1, inner_width);
        if (width == cached_width_) {
            return;
        }
        cached_width_ = width;
        lines_ = wrap_lines(detail_, cached_width_, body_style_);
    }

    static int padding() { return DMSpacing::item_gap(); }

private:
    static constexpr int kHeadingGap = 6;
    static constexpr int kLineGap = 4;
    SDL_Rect rect_{0, 0, 0, 0};
    std::string heading_;
    std::string detail_;
    DMLabelStyle heading_style_;
    DMLabelStyle body_style_;
    mutable std::vector<std::string> lines_;
    mutable int cached_width_ = -1;
};

class SectionToggleWidget : public Widget {
public:
    using ToggleCallback = std::function<void(bool)>;

    SectionToggleWidget(std::string label, bool expanded)
        : label_(std::move(label)),
          expanded_(expanded) {
        button_ = std::make_unique<DMButton>( "", &DMStyles::HeaderButton(), DockableCollapsible::kDefaultFloatingContentWidth, DMButton::height());
        if (button_) {
            button_->set_tooltip_state(this->tooltip_state());
        }
        update_button_text();
    }

    ~SectionToggleWidget() override {
        if (button_) {
            button_->set_tooltip_state(nullptr);
        }
    }

    void set_rect(const SDL_Rect& r) override {
        rect_ = r;
        if (button_) {
            button_->set_rect(r);
        }
    }

    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int) const override { return DMButton::height(); }

    bool handle_event(const SDL_Event& e) override {
        if (!button_) return false;
        bool used = button_->handle_event(e);
        if (used && e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            set_expanded(!expanded_);
            if (on_toggle_) {
                on_toggle_(expanded_);
            }
        }
        return used;
    }

    void render(SDL_Renderer* renderer) const override {
        if (button_) button_->render(renderer);
    }

    bool wants_full_row() const override { return true; }

    void set_on_toggle(ToggleCallback cb) { on_toggle_ = std::move(cb); }

    void set_label(std::string label) {
        label_ = std::move(label);
        update_button_text();
    }

    void set_expanded(bool expanded) {
        if (expanded_ == expanded) {
            return;
        }
        expanded_ = expanded;
        update_button_text();
    }

    bool expanded() const { return expanded_; }

private:
    void update_button_text() {
        if (!button_) return;
        const std::string indicator = expanded_
            ? std::string(DMIcons::CollapseExpanded()) : std::string(DMIcons::CollapseCollapsed());
        button_->set_text(indicator + " " + label_);
        const DMButtonStyle* style = expanded_ ? &DMStyles::HeaderButton() : &DMStyles::FooterToggleButton();
        button_->set_style(style);
    }

    std::unique_ptr<DMButton> button_;
    SDL_Rect rect_{0, 0, 0, DMButton::height()};
    std::string label_;
    bool expanded_ = true;
    ToggleCallback on_toggle_{};
};

class DiscreteSliderWidget : public Widget {
public:
    using ChangeCallback = std::function<void(int)>;

    DiscreteSliderWidget(std::string label,
                         std::vector<int> values,
                         int value)
        : values_(std::move(values)) {
        if (values_.empty()) {
            values_.push_back(100);
        }
        slider_min_units_ = 0;
        slider_max_units_ = static_cast<int>(values_.size() - 1);
        slider_ = std::make_unique<DMSlider>(std::move(label), slider_min_units_, slider_max_units_, value_to_slider(value));
        slider_->set_defer_commit_until_unfocus(false);
        slider_->set_value_formatter([this](int units, std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) {
            const int idx = clamp_index(units);
            std::snprintf(buffer.data(), buffer.size(), "%d%%", values_[idx]);
            return std::string_view(buffer.data());
        });
        slider_->set_value_parser([this](const std::string& text) -> std::optional<int> {
            try {
                const int parsed = std::stoi(text);
                return value_to_slider(parsed);
            } catch (...) {
                return std::nullopt;
            }
        });
        slider_widget_ = std::make_unique<SliderWidget>(slider_.get());
        current_index_ = clamp_index(slider_->value());
    }

    void set_on_value_changed(ChangeCallback cb) { on_change_ = std::move(cb); }

    void set_value(int v) {
        if (!slider_) return;
        slider_->set_value(value_to_slider(v));
        current_index_ = clamp_index(slider_->value());
    }

    int value() const {
        if (values_.empty()) return 0;
        const int idx = clamp_index(current_index_);
        return values_[idx];
    }

    void set_rect(const SDL_Rect& r) override {
        if (slider_widget_) slider_widget_->set_rect(r);
    }

    const SDL_Rect& rect() const override {
        if (slider_widget_) {
            return slider_widget_->rect();
        }
        static SDL_Rect empty{0, 0, 0, 0};
        return empty;
    }

    int height_for_width(int w) const override {
        return slider_widget_ ? slider_widget_->height_for_width(w) : DMSlider::height();
    }

    bool wants_full_row() const override { return true; }

    bool handle_event(const SDL_Event& e) override {
        if (!slider_widget_) return false;
        const int previous_value = value();
        bool handled = slider_widget_->handle_event(e);
        if (slider_) {
            current_index_ = clamp_index(slider_->value());
            const int new_value = value();
            if (handled && on_change_ && new_value != previous_value) {
                on_change_(new_value);
            }
        }
        return handled;
    }

    void render(SDL_Renderer* renderer) const override {
        if (slider_widget_) slider_widget_->render(renderer);
    }

    void set_tooltip(std::string text) {
        if (slider_widget_) slider_widget_->set_tooltip(std::move(text));
    }

private:
    int clamp_index(int index) const {
        if (values_.empty()) return 0;
        return std::clamp(index, slider_min_units_, slider_max_units_);
    }

    int value_to_slider(int value) const {
        if (values_.empty()) return slider_min_units_;
        int best_index = slider_min_units_;
        int best_diff = std::abs(value - values_[best_index]);
        for (std::size_t i = 1; i < values_.size(); ++i) {
            const int diff = std::abs(value - values_[i]);
            if (diff < best_diff) {
                best_diff = diff;
                best_index = static_cast<int>(i);
            }
        }
        return clamp_index(best_index);
    }

    std::unique_ptr<DMSlider> slider_;
    std::unique_ptr<SliderWidget> slider_widget_;
    std::vector<int> values_;
    int slider_min_units_ = 0;
    int slider_max_units_ = 0;
    int current_index_ = 0;
    ChangeCallback on_change_{};
};

class PitchDialWidget : public Widget {
public:
    using ChangeCallback = std::function<void(float)>;

    PitchDialWidget(std::string label, float angle_degrees = 0.0f, float min_deg = 0.0f, float max_deg = 360.0f)
        : label_(std::move(label)),
          angle_deg_(clamp_angle_deg(wrap_angle_deg(angle_degrees), min_deg, max_deg)),
          min_deg_(min_deg),
          max_deg_(max_deg) {
        label_style_ = DMStyles::Label();
        value_style_ = DMStyles::Slider().value;
        value_style_.font_size = std::max(value_style_.font_size, label_style_.font_size);
    }

    ~PitchDialWidget() override {
        if (icon_texture_) {
            SDL_DestroyTexture(icon_texture_);
            icon_texture_ = nullptr;
        }
    }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int w) const override {
        const int heading_h = label_style_.font_size + DMSpacing::label_gap();
        const int dial_size = std::clamp(w - 80, 120, 180);
        return heading_h + dial_size + DMSpacing::item_gap();
    }

    bool handle_event(const SDL_Event& e) override {
        bool used = false;
        switch (e.type) {
        case SDL_MOUSEBUTTONDOWN: {
            SDL_Point p{ e.button.x, e.button.y };
            hovered_ = point_in_dial(p);
            if (e.button.button == SDL_BUTTON_LEFT && hovered_) {
                dragging_ = true;
                update_angle_from_mouse(p);
                used = true;
            }
            break;
        }
        case SDL_MOUSEBUTTONUP: {
            SDL_Point p{ e.button.x, e.button.y };
            hovered_ = point_in_dial(p);
            if (dragging_ && e.button.button == SDL_BUTTON_LEFT) {
                dragging_ = false;
                update_angle_from_mouse(p);
                used = true;
            }
            break;
        }
        case SDL_MOUSEMOTION: {
            SDL_Point p{ e.motion.x, e.motion.y };
            hovered_ = point_in_dial(p);
            if (dragging_) {
                update_angle_from_mouse(p);
                used = true;
            }
            break;
        }
        case SDL_MOUSEWHEEL: {
            if (hovered_) {
                const float delta = static_cast<float>(-e.wheel.y) * 2.5f;
                set_angle_from_user(angle_deg_ + delta);
                used = true;
            }
            break;
        }
        default:
            break;
        }
        if (tooltip_enabled() && DMWidgetTooltipHandleEvent(e, rect_, *tooltip_state())) {
            used = true;
        }
        return used;
    }

    void render(SDL_Renderer* renderer) const override {
        if (!renderer) return;
        const auto slider_style = DMStyles::Slider();
        const DialGeometry g    = compute_geometry();

        draw_heading(renderer);
        draw_ring(renderer, g, slider_style);
        draw_line(renderer, g, slider_style);
        draw_icon(renderer, g);
        draw_rotated_value(renderer, g);
        draw_knob(renderer, g, slider_style);

        if (tooltip_enabled()) {
            DMWidgetTooltipRender(renderer, rect_, *tooltip_state());
        }
    }

    bool wants_full_row() const override { return true; }

    void set_angle_degrees(float deg) { angle_deg_ = wrap_angle_deg(deg); }
    float angle_degrees() const { return angle_deg_; }
    void set_on_angle_changed(ChangeCallback cb) { on_change_ = std::move(cb); }

private:
    struct DialGeometry {
        SDL_Rect area{0, 0, 0, 0};
        SDL_Point center{0, 0};
        int radius = 0;
        int knob_size = 12;
};

    DialGeometry compute_geometry() const {
        DialGeometry g{};
        const int heading_h = label_style_.font_size + DMSpacing::label_gap();
        g.area = SDL_Rect{
            rect_.x,
            rect_.y + heading_h,
            rect_.w,
            std::max(0, rect_.h - heading_h) };
        const int padding = 12;
        const int usable_w = std::max(1, g.area.w - padding * 2);
        const int usable_h = std::max(1, g.area.h - padding * 2);
        const int diameter = std::min(usable_w, usable_h);
        g.radius = std::max(22, diameter / 2);
        g.center = SDL_Point{ g.area.x + g.area.w / 2, g.area.y + g.area.h / 2 };
        g.knob_size = std::max(12, g.radius / 3);
        return g;
    }

    void draw_heading(SDL_Renderer* renderer) const {
        const std::string heading = label_ + " (" + formatted_angle() + ")";
        DrawLabelText(renderer, heading, rect_.x, rect_.y, label_style_);
    }

    void draw_circle(SDL_Renderer* renderer, const SDL_Point& c, int radius, SDL_Color color, int thickness = 1) const {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        const int segments = 64;
        for (int t = 0; t < thickness; ++t) {
            const int r = std::max(1, radius - t);
            SDL_Point prev{ c.x + r, c.y };
            for (int i = 1; i <= segments; ++i) {
                const float theta = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * kPi;
                SDL_Point next{
                    c.x + static_cast<int>(std::round(std::cos(theta) * static_cast<float>(r))), c.y + static_cast<int>(std::round(std::sin(theta) * static_cast<float>(r))) };
                SDL_RenderDrawLine(renderer, prev.x, prev.y, next.x, next.y);
                prev = next;
            }
        }
    }

    void draw_ring(SDL_Renderer* renderer, const DialGeometry& g, const DMSliderStyle& slider_style) const {
        SDL_Color base = dm_draw::DarkenColor(slider_style.track_bg, 0.25f);
        SDL_Color accent = dragging_ ? slider_style.track_fill_active : slider_style.track_fill;
        draw_circle(renderer, g.center, g.radius + 6, base, 3);
        draw_circle(renderer, g.center, g.radius, accent, 2);
    }

    void draw_line(SDL_Renderer* renderer, const DialGeometry& g, const DMSliderStyle& slider_style) const {
        const float rad = angle_deg_ * kDegToRad;
        const float dir_x = std::cos(rad);
        const float dir_y = -std::sin(rad);
        const SDL_Point knob{
            g.center.x + static_cast<int>(std::round(dir_x * static_cast<float>(g.radius))), g.center.y + static_cast<int>(std::round(dir_y * static_cast<float>(g.radius))) };
        SDL_Color line_color = dragging_ ? slider_style.track_fill_active : slider_style.track_fill;
        SDL_SetRenderDrawColor(renderer, line_color.r, line_color.g, line_color.b, line_color.a);
        SDL_RenderDrawLine(renderer, g.center.x, g.center.y, knob.x, knob.y);
    }

    void draw_knob(SDL_Renderer* renderer, const DialGeometry& g, const DMSliderStyle& slider_style) const {
        const float rad = angle_deg_ * kDegToRad;
        const float dir_x = std::cos(rad);
        const float dir_y = -std::sin(rad);
        const SDL_Point knob_center{
            g.center.x + static_cast<int>(std::round(dir_x * static_cast<float>(g.radius))), g.center.y + static_cast<int>(std::round(dir_y * static_cast<float>(g.radius))) };
        SDL_Rect knob_rect{
            knob_center.x - g.knob_size / 2,
            knob_center.y - g.knob_size / 2,
            g.knob_size,
            g.knob_size
};
        SDL_Color knob_col   = slider_style.knob;
        SDL_Color knob_border = slider_style.knob_border;
        if (dragging_) {
            knob_col    = slider_style.knob_accent;
            knob_border = slider_style.knob_accent_border;
        } else if (hovered_) {
            knob_col    = slider_style.knob_hover;
            knob_border = slider_style.knob_border_hover;
        }
        const int bevel = std::min(DMStyles::BevelDepth(), std::max(1, g.knob_size / 3));
        const int radius = std::min(DMStyles::CornerRadius(), g.knob_size / 2);
        dm_draw::DrawBeveledRect( renderer, knob_rect, radius, bevel, knob_col, DMStyles::HighlightColor(), DMStyles::ShadowColor(), true, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
        dm_draw::DrawRoundedOutline(renderer, knob_rect, radius, 1, knob_border);
    }

    void draw_icon(SDL_Renderer* renderer, const DialGeometry& g) const {
        if (!ensure_icon(renderer)) {
            return;
        }
        const int icon_size = std::clamp(g.radius, g.radius / 2, g.radius * 2);
        SDL_Rect dst{
            g.center.x - icon_size / 2,
            g.center.y - icon_size / 2,
            icon_size,
            icon_size
};
        SDL_Point pivot{ icon_size / 2, icon_size / 2 };
        SDL_RenderCopyEx(renderer, icon_texture_, nullptr, &dst, -angle_deg_, &pivot, SDL_FLIP_NONE);
    }

    void draw_rotated_value(SDL_Renderer* renderer, const DialGeometry& g) const {
        TTF_Font* font = DMFontCache::instance().get_font(value_style_.font_path, value_style_.font_size);
        if (!font) return;
        const std::string text = formatted_angle();
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), value_style_.color);
        if (!surface) return;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surface);
        int w = surface->w;
        int h = surface->h;
        SDL_FreeSurface(surface);
        if (!tex) return;
        const float rad = angle_deg_ * kDegToRad;
        const float dir_x = std::cos(rad);
        const float dir_y = -std::sin(rad);
        const int text_radius = g.radius + g.knob_size + 12;
        SDL_Point anchor{
            g.center.x + static_cast<int>(std::round(dir_x * static_cast<float>(text_radius))), g.center.y + static_cast<int>(std::round(dir_y * static_cast<float>(text_radius))) };
        SDL_Rect dst{ anchor.x - w / 2, anchor.y - h / 2, w, h };
        SDL_Point pivot{ w / 2, h / 2 };
        SDL_RenderCopyEx(renderer, tex, nullptr, &dst, -angle_deg_, &pivot, SDL_FLIP_NONE);
        SDL_DestroyTexture(tex);
    }

    std::string formatted_angle() const {
        char buffer[16]{};
        std::snprintf(buffer, sizeof(buffer), "%.0f\u00b0", wrap_angle_deg(angle_deg_));
        return std::string(buffer);
    }

    bool point_in_dial(SDL_Point p) const {
        const DialGeometry g = compute_geometry();
        const int dx = p.x - g.center.x;
        const int dy = p.y - g.center.y;
        const int r = g.radius + g.knob_size;
        return (dx * dx + dy * dy) <= r * r;
    }

    void update_angle_from_mouse(SDL_Point p) {
        const DialGeometry g = compute_geometry();
        const int dx = p.x - g.center.x;
        const int dy = p.y - g.center.y;
        if (dx == 0 && dy == 0) {
            return;
        }
        const float deg = std::atan2(static_cast<float>(-dy), static_cast<float>(dx)) * kRadToDeg;
        set_angle_from_user(deg);
    }

    void set_angle_from_user(float deg) {
        const float clamped = clamp_angle_deg(deg, min_deg_, max_deg_);
        if (std::fabs(clamped - angle_deg_) < 0.0001f) {
            return;
        }
        angle_deg_ = clamped;
        if (on_change_) {
            on_change_(angle_deg_);
        }
    }

    bool ensure_icon(SDL_Renderer* renderer) const {
        if (icon_texture_ || icon_load_attempted_) {
            return icon_texture_ != nullptr;
        }
        icon_load_attempted_ = true;
        SDL_Surface* surface = IMG_Load(kCameraIconPath);
        if (!surface) {
            return false;
        }
        icon_texture_ = SDL_CreateTextureFromSurface(renderer, surface);
        if (icon_texture_) {
            SDL_SetTextureBlendMode(icon_texture_, SDL_BLENDMODE_BLEND);
        }
        SDL_FreeSurface(surface);
        return icon_texture_ != nullptr;
    }

    SDL_Rect rect_{0, 0, 0, 0};
    std::string label_;
    DMLabelStyle label_style_{};
    DMLabelStyle value_style_{};
    float angle_deg_ = 0.0f;
    float min_deg_ = 0.0f;
    float max_deg_ = 360.0f;
    bool dragging_ = false;
    bool hovered_ = false;
    ChangeCallback on_change_{};
    mutable SDL_Texture* icon_texture_ = nullptr;
    mutable bool icon_load_attempted_ = false;
};

class ZoomKeyPointWidget : public Widget {
public:
    struct Values {
        float zoom = 1.0f;
};

    ZoomKeyPointWidget(std::string label, const Values& values, bool expanded, float zoom_min, float zoom_max)
        : label_(std::move(label)),
          expanded_(expanded),
          zoom_min_(zoom_min),
          zoom_max_(zoom_max) {
        header_toggle_ = std::make_unique<SectionToggleWidget>(label_, expanded_);
        if (header_toggle_) {
            header_toggle_->set_on_toggle([this](bool v) {
                expanded_ = v;
                layout_children();
                if (on_expanded_changed_) {
                    on_expanded_changed_(expanded_);
                }
            });
        }
        set_zoom_button_ = std::make_unique<DMButton>("Set Zoom", &DMStyles::SecondaryButton(), 120, DMButton::height());

        zoom_slider_ = std::make_unique<FloatSliderWidget>( "Zoom", zoom_min_, zoom_max_, 0.01f, values.zoom, 2);
        if (zoom_slider_) {
            zoom_slider_->set_tooltip("Zoom anchor for this key point.");
            zoom_slider_->set_on_value_changed([this](float) { notify_change(); });
        }
    }

    void set_on_value_changed(std::function<void()> cb) { on_change_ = std::move(cb); }
    void set_on_expanded_changed(std::function<void(bool)> cb) { on_expanded_changed_ = std::move(cb); }
    void set_on_set_zoom(std::function<void(float)> cb) { on_set_zoom_ = std::move(cb); }

    void set_values(const Values& values) {
        if (zoom_slider_) zoom_slider_->set_value(values.zoom);
        layout_children();
    }

    Values values() const {
        Values v{};
        v.zoom = zoom_slider_ ? zoom_slider_->value() : 1.0f;
        return v;
    }

    void set_expanded(bool expanded) {
        if (expanded_ == expanded) return;
        expanded_ = expanded;
        if (header_toggle_) {
            header_toggle_->set_expanded(expanded_);
        }
        layout_children();
    }

    bool expanded() const { return expanded_; }

    void set_rect(const SDL_Rect& r) override {
        rect_ = r;
        layout_children();
    }

    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int w) const override {
        const int width = std::max(1, w);
        const int header_h = DMButton::height();
        int height = header_h;
        if (expanded_) {
            const int gap = DMSpacing::item_gap();
            height += gap;
            auto add_height = [&](const Widget* wgt) {
                if (!wgt) return;
                height += wgt->height_for_width(width) + gap;
};
            add_height(zoom_slider_.get());
        }
        return height;
    }

    bool handle_event(const SDL_Event& e) override {
        if (header_toggle_ && header_toggle_->handle_event(e)) {
            expanded_ = header_toggle_->expanded();
            layout_children();
            if (on_expanded_changed_) {
                on_expanded_changed_(expanded_);
            }
            return true;
        }
        if (set_zoom_button_ && set_zoom_button_->handle_event(e)) {
            if (on_set_zoom_) {
                on_set_zoom_(zoom_slider_ ? zoom_slider_->value() : 1.0f);
            }
            return true;
        }

        if (!expanded_) {
            return false;
        }

        bool used = false;
        auto handle_child = [&](Widget* w) {
            if (!w) return false;
            return w->handle_event(e);
};
        used = handle_child(zoom_slider_.get()) || used;
        return used;
    }

    void render(SDL_Renderer* renderer) const override {
        if (header_toggle_) header_toggle_->render(renderer);
        if (set_zoom_button_) set_zoom_button_->render(renderer);
        if (!expanded_) return;
        if (zoom_slider_) zoom_slider_->render(renderer);
    }

    bool wants_full_row() const override { return true; }

private:
    void notify_change() {
        if (on_change_) {
            on_change_();
        }
    }

    void layout_children() {
        const int gap = DMSpacing::item_gap();
        const int width = std::max(1, rect_.w);
        int x = rect_.x;
        int y = rect_.y;

        const int header_h = DMButton::height();
        const int button_w = set_zoom_button_
            ? std::min(width / 3, std::max(set_zoom_button_->preferred_width(), 110)) : 0;
        const int toggle_w = std::max(0, width - button_w - (button_w > 0 ? gap : 0));

        if (header_toggle_) {
            header_toggle_->set_rect(SDL_Rect{ x, y, toggle_w, header_h });
        }
        if (set_zoom_button_) {
            const int btn_x = x + width - button_w;
            set_zoom_button_->set_rect(SDL_Rect{ btn_x, y, button_w, header_h });
        }
        y += header_h;

        if (!expanded_) {
            return;
        }

        y += gap;
        auto place_child = [&](Widget* child) {
            if (!child) return;
            int h = child->height_for_width(width);
            child->set_rect(SDL_Rect{ x, y, width, h });
            y += h + gap;
};

        place_child(zoom_slider_.get());
    }

private:
    std::string label_;
    bool expanded_ = true;
    SDL_Rect rect_{0, 0, 0, 0};
    float zoom_min_ = 0.0f;
    float zoom_max_ = 0.0f;

    std::unique_ptr<SectionToggleWidget> header_toggle_;
    std::unique_ptr<DMButton> set_zoom_button_;
    std::unique_ptr<FloatSliderWidget> zoom_slider_;

    std::function<void()> on_change_{};
    std::function<void(bool)> on_expanded_changed_{};
    std::function<void(float)> on_set_zoom_{};
};

CameraUIPanel::CameraUIPanel(Assets* assets, int x, int y)
    : DockableCollapsible("Camera Settings", true, x, y),
      assets_(assets) {
    last_depthcue_enabled_ = devmode::camera_prefs::load_depthcue_enabled();
    set_expanded(true);
    set_visible(false);
    set_padding(16);
    set_close_button_enabled(true);
    set_close_button_on_left(false);
    set_floatable(true);
    build_ui();
    sync_from_camera();
}

CameraUIPanel::~CameraUIPanel() = default;

void CameraUIPanel::set_assets(Assets* assets) {
    assets_ = assets;
    sync_from_camera();
}

void CameraUIPanel::set_image_effects_panel_callback(std::function<void()> cb) {
    open_image_effects_cb_ = std::move(cb);
}

void CameraUIPanel::open() {
    set_visible(true);
    suppress_apply_once_ = true;

    visibility_section_expanded_ = false;
    depth_section_expanded_ = false;
    depthcue_section_expanded_ = false;
    if (visibility_section_header_) visibility_section_header_->set_expanded(false);
    if (depth_section_header_)      depth_section_header_->set_expanded(false);
    if (depthcue_section_header_)   depthcue_section_header_->set_expanded(false);
    rebuild_rows();
    sync_from_camera();
}

void CameraUIPanel::close() {
    set_visible(false);
}

void CameraUIPanel::toggle() {
    set_visible(!is_visible());
    if (is_visible()) {
        suppress_apply_once_ = true;
        sync_from_camera();
    }
}

bool CameraUIPanel::is_point_inside(int x, int y) const {
    return DockableCollapsible::is_point_inside(x, y);
}

void CameraUIPanel::update(const Input& input, int screen_w, int screen_h) {
    last_screen_w_ = screen_w;
    last_screen_h_ = screen_h;
    const bool previously_visible = was_visible_;
    DockableCollapsible::update(input, screen_w, screen_h);
    const bool currently_visible = is_visible();
    if (currently_visible && !previously_visible) {

        suppress_apply_once_ = true;

        visibility_section_expanded_ = false;
        depth_section_expanded_ = false;
        depthcue_section_expanded_ = false;
        if (visibility_section_header_) visibility_section_header_->set_expanded(false);
        if (depth_section_header_)      depth_section_header_->set_expanded(false);
        if (depthcue_section_header_)   depthcue_section_header_->set_expanded(false);
        rebuild_rows();
        sync_from_camera();
    }
    was_visible_ = currently_visible;

    if (!currently_visible) return;
    if (!assets_) return;
    if (suppress_apply_once_) {
        suppress_apply_once_ = false;
        return;
    }
    apply_settings_if_needed();
}

bool CameraUIPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) return false;
    bool used = DockableCollapsible::handle_event(e);
    if (used) {
        apply_settings_if_needed();
    }
    return used;
}

void CameraUIPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) return;
    if (is_visible()) {
        DockableCollapsible::render(renderer);
    }

    DMDropdown::render_active_options(renderer);
}

void CameraUIPanel::layout_custom_content(int screen_w, int screen_h) const {

    if (hero_banner_widget_) {
        set_drag_handle_rect(hero_banner_widget_->rect());
    } else {
        set_drag_handle_rect(SDL_Rect{0,0,0,0});
    }
}

void CameraUIPanel::sync_from_camera() {
    if (!assets_) return;
    WarpedScreenGrid& cam = assets_->getView();
    last_settings_ = cam.realism_settings();
    bool effects_enabled = cam.realism_enabled() && cam.parallax_enabled();
    last_realism_enabled_ = effects_enabled;

    if (min_render_size_slider_) min_render_size_slider_->set_value(last_settings_.min_visible_screen_ratio);
    if (render_quality_slider_) render_quality_slider_->set_value(last_settings_.render_quality_percent);
    if (cull_margin_slider_) cull_margin_slider_->set_value(last_settings_.extra_cull_margin);
    if (zoom_in_keypoint_ || zoom_out_keypoint_) {
        ZoomKeyPointWidget::Values min_values;
        min_values.zoom = last_settings_.zoom_low;
        if (zoom_in_keypoint_) {
            zoom_in_keypoint_->set_values(min_values);
        }

        ZoomKeyPointWidget::Values max_values;
        max_values.zoom = last_settings_.zoom_high;
        if (zoom_out_keypoint_) {
            zoom_out_keypoint_->set_values(max_values);
        }
    }

    if (foreground_texture_opacity_slider_) {
        foreground_texture_opacity_slider_->set_value(static_cast<float>(last_settings_.foreground_texture_max_opacity));
    }
    if (background_texture_opacity_slider_) {
        background_texture_opacity_slider_->set_value(static_cast<float>(last_settings_.background_texture_max_opacity));
    }
    if (texture_opacity_interp_dropdown_) {
        texture_opacity_interp_dropdown_->set_selected(static_cast<int>(last_settings_.texture_opacity_falloff_method));
    }
    if (realism_enabled_checkbox_) {
        realism_enabled_checkbox_->set_value(last_realism_enabled_);
    }
    if (depthcue_checkbox_) {
        depthcue_checkbox_->set_value(last_depthcue_enabled_);
    }
}

void CameraUIPanel::build_ui() {
    set_header_button_style(&DMStyles::AccentButton());
    set_header_highlight_color(DMStyles::AccentButton().bg);
    set_padding(DMSpacing::panel_padding());
    set_row_gap(DMSpacing::item_gap());
    set_col_gap(DMSpacing::item_gap());
    set_floating_content_width(460);

    header_spacer_ = std::make_unique<SpacerWidget>(DMSpacing::header_gap());
    hero_banner_widget_ = std::make_unique<PanelBannerWidget>( "Camera realism", "Dial in render buffers and parallax depth without leaving the editor.");
    realism_enabled_checkbox_ = std::make_unique<DMCheckbox>("Enable Realism Effects", last_realism_enabled_);
    realism_widget_ = std::make_unique<CheckboxWidget>(realism_enabled_checkbox_.get());
    realism_widget_->set_tooltip("Toggle perspective effects, grid warping, and parallax depth.");

    controls_spacer_ = std::make_unique<SpacerWidget>(DMSpacing::small_gap());

    depthcue_checkbox_ = std::make_unique<DMCheckbox>("Enable Depth Cue", last_depthcue_enabled_);
    depthcue_widget_ = std::make_unique<CheckboxWidget>(depthcue_checkbox_.get());
    depthcue_widget_->set_tooltip("Toggle depth cue texture compositing.\nDoes not affect parallax or perspective scaling.");

    WarpedScreenGrid::RealismSettings defaults = last_settings_;
    if (assets_) {
        defaults = assets_->getView().realism_settings();
    }

    auto configure_section = [this](std::unique_ptr<SectionToggleWidget>& target,
                                    const std::string& label,
                                    bool* expanded_flag) {
        target = std::make_unique<SectionToggleWidget>(label, *expanded_flag);
        target->set_on_toggle([this, expanded_flag](bool expanded) {
            *expanded_flag = expanded;
            rebuild_rows();
        });
        target->set_tooltip("Click to collapse or expand this section.");
};

    configure_section(visibility_section_header_, "Visibility & Performance", &visibility_section_expanded_);
    configure_section(depth_section_header_,      "Depth & Perspective",      &depth_section_expanded_);
    configure_section(depthcue_section_header_,   "Depth Cue",               &depthcue_section_expanded_);
    if (depth_section_header_) {
        depth_section_header_->set_on_toggle([this](bool expanded) {
            depth_section_expanded_ = expanded;
            rebuild_rows();
        });
    }

    min_render_size_slider_ = std::make_unique<FloatSliderWidget>("Min On-Screen Size", 0.0f, 0.05f, 0.001f, defaults.min_visible_screen_ratio, 3);
    min_render_size_slider_->set_tooltip("Cull sprites once their height drops below this fraction of the screen (0.01 = 1%).");
    min_render_size_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });

    cull_margin_slider_ = std::make_unique<FloatSliderWidget>("Cull Margin (px)", 0.0f, 1000.0f, 1.0f, defaults.extra_cull_margin, 0);
    cull_margin_slider_->set_tooltip("Extra margin below the screen for culling (for perspective/warping). Increase if assets pop in/out at the bottom edge.");
    cull_margin_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });

    perspective_zero_distance_slider_ = std::make_unique<FloatSliderWidget>( "Perspective Scale 0 Distance", -5000.0f, 5000.0f, 1.0f, defaults.perspective_distance_at_scale_zero, 0);
    perspective_zero_distance_slider_->set_tooltip( "World-space distance at which perspective scale reaches 0 (far point).");
    perspective_zero_distance_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });

    perspective_hundred_distance_slider_ = std::make_unique<FloatSliderWidget>( "Perspective Scale 100 Distance", -5000.0f, 5000.0f, 1.0f, defaults.perspective_distance_at_scale_hundred, 0);
    perspective_hundred_distance_slider_->set_tooltip( "World-space distance at which perspective scale is 100 (near point).");
    perspective_hundred_distance_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });

    render_quality_slider_ = std::make_unique<DiscreteSliderWidget>("Render Quality (%)", std::vector<int>{100, 75, 50, 25, 10}, defaults.render_quality_percent);
    render_quality_slider_->set_tooltip("Trade fidelity for speed; lowers the number of sprites drawn each frame.");
    render_quality_slider_->set_on_value_changed([this](int) { on_control_value_changed(); });
    if (cull_margin_slider_) cull_margin_slider_->set_value(last_settings_.extra_cull_margin);

    ZoomKeyPointWidget::Values zoom_in_defaults;
    zoom_in_defaults.zoom = defaults.zoom_low;
    zoom_in_keypoint_ = std::make_unique<ZoomKeyPointWidget>( "Zoomed In Settings", zoom_in_defaults, zoom_in_settings_expanded_, 0.1f, WarpedScreenGrid::kMaxZoomAnchors);
    if (zoom_in_keypoint_) {
        zoom_in_keypoint_->set_on_value_changed([this]() { on_control_value_changed(); });
        zoom_in_keypoint_->set_on_expanded_changed([this](bool expanded) {
            zoom_in_settings_expanded_ = expanded;
            rebuild_rows();
        });
        zoom_in_keypoint_->set_on_set_zoom([this](float target_zoom) {
            snap_zoom_to_anchor(target_zoom, true);
        });
    }

    ZoomKeyPointWidget::Values zoom_out_defaults;
    zoom_out_defaults.zoom = defaults.zoom_high;
    zoom_out_keypoint_ = std::make_unique<ZoomKeyPointWidget>( "Zoomed Out Settings", zoom_out_defaults, zoom_out_settings_expanded_, 0.1f, WarpedScreenGrid::kMaxZoomAnchors);
    if (zoom_out_keypoint_) {
        zoom_out_keypoint_->set_on_value_changed([this]() { on_control_value_changed(); });
        zoom_out_keypoint_->set_on_expanded_changed([this](bool expanded) {
            zoom_out_settings_expanded_ = expanded;
            rebuild_rows();
        });
        zoom_out_keypoint_->set_on_set_zoom([this](float target_zoom) {
            snap_zoom_to_anchor(target_zoom, false);
        });
    }

    const int stored_fg_opacity = devmode::camera_prefs::load_foreground_texture_max_opacity();
    const int stored_bg_opacity = devmode::camera_prefs::load_background_texture_max_opacity();
    foreground_texture_opacity_slider_ = std::make_unique<FloatSliderWidget>( "Foreground Texture Max Opacity", 0.0f, 255.0f, 1.0f, static_cast<float>(stored_fg_opacity), 0);
    foreground_texture_opacity_slider_->set_tooltip("Maximum opacity when blending the foreground texture.");
    foreground_texture_opacity_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });

    background_texture_opacity_slider_ = std::make_unique<FloatSliderWidget>( "Background Texture Max Opacity", 0.0f, 255.0f, 1.0f, static_cast<float>(stored_bg_opacity), 0);
    background_texture_opacity_slider_->set_tooltip("Maximum opacity when blending the background texture.");
    background_texture_opacity_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });

    {
        const int default_interp_index = std::clamp( static_cast<int>(defaults.texture_opacity_falloff_method), 0, 4);
        std::vector<std::string> options{ "Linear", "Quadratic", "Cubic", "Logarithmic", "Exponential" };
        texture_opacity_interp_dropdown_ = std::make_unique<DMDropdown>("Depth Cue Opacity Interpolation", options, default_interp_index);
        texture_opacity_interp_widget_   = std::make_unique<DropdownWidget>(texture_opacity_interp_dropdown_.get());
        texture_opacity_interp_widget_->set_tooltip("Curve used when blending precomputed textures by depth.");
        texture_opacity_interp_dropdown_->set_on_selection_changed([this](int) { on_control_value_changed(); });
    }

    image_effect_button_ = std::make_unique<DMButton>("Configure Image Effects", &DMStyles::AccentButton(), DockableCollapsible::kDefaultFloatingContentWidth, DMButton::height());
    image_effect_widget_ = std::make_unique<ButtonWidget>(image_effect_button_.get(), [this]() {
        if (open_image_effects_cb_) {
            open_image_effects_cb_();
        }
    });
    if (image_effect_widget_) {
        image_effect_widget_->set_tooltip("Open the global image effect editor to regenerate depth cue textures.");
    }
    rebuild_rows();
}

void CameraUIPanel::on_control_value_changed() {
    if (!assets_ || !is_visible()) {
        return;
    }
    apply_settings_if_needed();
}

void CameraUIPanel::snap_zoom_to_anchor(float target_zoom, bool anchor_is_min_section) {
    if (!assets_ || !is_visible()) return;
    (void)anchor_is_min_section;

    WarpedScreenGrid& cam = assets_->getView();
    const float clamped_target = std::clamp(target_zoom, WarpedScreenGrid::kMinZoomAnchors, WarpedScreenGrid::kMaxZoomAnchors);
    SDL_Point focus = cam.get_screen_center();
    cam.set_manual_zoom_override(true);
    if (assets_->player) {
        focus = SDL_Point{ assets_->player->pos.x, assets_->player->pos.y };
        cam.set_focus_override(focus);
        cam.set_screen_center(focus);
    }
    cam.set_scale(clamped_target);
    cam.recompute_current_view();
    assets_->apply_camera_runtime_settings();
}

void CameraUIPanel::rebuild_rows() {
    Rows rows;
    if (header_spacer_) rows.push_back({ header_spacer_.get() });
    if (hero_banner_widget_) rows.push_back({ hero_banner_widget_.get() });
    if (realism_widget_) rows.push_back({ realism_widget_.get() });
    if (controls_spacer_) rows.push_back({ controls_spacer_.get() });
    if (depthcue_widget_) rows.push_back({ depthcue_widget_.get() });

    if (visibility_section_header_) rows.push_back({ visibility_section_header_.get() });
    if (visibility_section_expanded_) {
        if (min_render_size_slider_) rows.push_back({ min_render_size_slider_.get() });
        if (cull_margin_slider_) rows.push_back({ cull_margin_slider_.get() });
        if (render_quality_slider_) rows.push_back({ render_quality_slider_.get() });
    }

    if (depth_section_header_) rows.push_back({ depth_section_header_.get() });
    if (depth_section_expanded_) {
        if (zoom_in_keypoint_) rows.push_back({ zoom_in_keypoint_.get() });
        if (zoom_out_keypoint_) rows.push_back({ zoom_out_keypoint_.get() });
        if (perspective_zero_distance_slider_) rows.push_back({ perspective_zero_distance_slider_.get() });
        if (perspective_hundred_distance_slider_) rows.push_back({ perspective_hundred_distance_slider_.get() });
    }

    if (depthcue_section_header_) rows.push_back({ depthcue_section_header_.get() });
    if (depthcue_section_expanded_) {
        if (foreground_texture_opacity_slider_) rows.push_back({ foreground_texture_opacity_slider_.get() });
        if (background_texture_opacity_slider_) rows.push_back({ background_texture_opacity_slider_.get() });
        if (texture_opacity_interp_widget_) rows.push_back({ texture_opacity_interp_widget_.get() });
        if (image_effect_widget_) rows.push_back({ image_effect_widget_.get() });
    }

    set_rows(rows);
}

void CameraUIPanel::apply_settings_if_needed() {
    if (!assets_) return;
    if (applying_settings_) {
        return;
    }
    struct ScopedApplyingGuard {
        bool& flag;
        explicit ScopedApplyingGuard(bool& f) : flag(f) { flag = true; }
        ~ScopedApplyingGuard() { flag = false; }
    } guard(applying_settings_);
    WarpedScreenGrid::RealismSettings settings = read_settings_from_ui();
    const bool reported_effects_enabled = realism_enabled_checkbox_
        ? realism_enabled_checkbox_->value() : last_realism_enabled_;
    const bool reported_depthcue_enabled = depthcue_checkbox_
        ? depthcue_checkbox_->value() : last_depthcue_enabled_;

    const bool effects_enabled = WarpedScreenGrid::kForceDepthPerspectiveDisabled
        ? false
        : reported_effects_enabled;
    const bool depthcue_enabled = WarpedScreenGrid::kForceDepthPerspectiveDisabled
        ? false
        : reported_depthcue_enabled;

    auto differs = [](float a, float b) {
        return std::fabs(a - b) > 0.0001f;
};
    bool changed = (effects_enabled != last_realism_enabled_) || (depthcue_enabled != last_depthcue_enabled_);
    const WarpedScreenGrid::RealismSettings& prev = last_settings_;
    changed = changed || differs(settings.zoom_low, prev.zoom_low) || differs(settings.zoom_high, prev.zoom_high);
    changed = changed || differs(settings.min_visible_screen_ratio, prev.min_visible_screen_ratio);
    changed = changed || differs(settings.extra_cull_margin, prev.extra_cull_margin);
    changed = changed || differs(settings.perspective_distance_at_scale_zero, prev.perspective_distance_at_scale_zero);
    changed = changed || differs(settings.perspective_distance_at_scale_hundred, prev.perspective_distance_at_scale_hundred);
    if (render_quality_slider_) {
        changed = changed || settings.render_quality_percent != prev.render_quality_percent;
    }
    changed = changed || differs(settings.scale_variant_hysteresis_margin, prev.scale_variant_hysteresis_margin);

    changed = changed || (settings.foreground_texture_max_opacity != prev.foreground_texture_max_opacity);
    changed = changed || (settings.background_texture_max_opacity != prev.background_texture_max_opacity);
    changed = changed || differs(settings.foreground_plane_screen_y, prev.foreground_plane_screen_y);
    changed = changed || differs(settings.background_plane_screen_y, prev.background_plane_screen_y);
    changed = changed || static_cast<int>(settings.texture_opacity_falloff_method) != static_cast<int>(prev.texture_opacity_falloff_method);

    if (changed) {
        apply_settings_to_camera(settings, effects_enabled, depthcue_enabled);

        assets_->on_camera_settings_changed();
    }
}

void CameraUIPanel::apply_settings_to_camera(const WarpedScreenGrid::RealismSettings& settings,
                                             bool effects_enabled,
                                             bool depthcue_enabled) {
    if (!assets_) return;
    WarpedScreenGrid& cam = assets_->getView();
    WarpedScreenGrid::RealismSettings effective = settings;
    if (!depthcue_enabled) {
        effective.foreground_texture_max_opacity = 0;
        effective.background_texture_max_opacity = 0;
    }
    cam.set_realism_settings(effective);
    cam.set_realism_enabled(effects_enabled);
    cam.set_parallax_enabled(effects_enabled);

    cam.update_geometry_cache(cam.compute_geometry());

    const float kZoomGuard = 0.01f;
    const float span = std::max(0.0002f, effective.zoom_high - effective.zoom_low);
    const float guard = std::clamp(kZoomGuard, 0.0001f, span * 0.25f);
    const float min_zoom = effective.zoom_low + guard;
    const float max_zoom = effective.zoom_high - guard;
    float current_zoom = cam.get_scale();
    float clamped_zoom = std::clamp(current_zoom, min_zoom, max_zoom);
    if (!std::isfinite(clamped_zoom)) {
        clamped_zoom = min_zoom;
    }
    if (std::fabs(clamped_zoom - current_zoom) > 1e-4f) {
        cam.set_scale(clamped_zoom);
    }

    if (assets_) {
        assets_->set_depth_effects_enabled(depthcue_enabled);
        assets_->apply_camera_runtime_settings();
    } else if (depthcue_enabled != last_depthcue_enabled_) {
        devmode::camera_prefs::save_depthcue_enabled(depthcue_enabled);
    }
    last_settings_ = settings;
    last_realism_enabled_ = effects_enabled;
    if (on_realism_enabled_changed_) {
        on_realism_enabled_changed_(effects_enabled);
    }
    if (depthcue_enabled != last_depthcue_enabled_) {
        if (on_depth_effects_enabled_changed_) {
            on_depth_effects_enabled_changed_(depthcue_enabled);
        }
    }
    devmode::camera_prefs::save_foreground_texture_max_opacity(settings.foreground_texture_max_opacity);
    devmode::camera_prefs::save_background_texture_max_opacity(settings.background_texture_max_opacity);
    last_depthcue_enabled_ = depthcue_enabled;
}

WarpedScreenGrid::RealismSettings CameraUIPanel::read_settings_from_ui() const {
    WarpedScreenGrid::RealismSettings settings = last_settings_;
    if (min_render_size_slider_) settings.min_visible_screen_ratio = std::clamp(min_render_size_slider_->value(), 0.0f, 0.5f);
    if (render_quality_slider_) settings.render_quality_percent = render_quality_slider_->value();
    if (cull_margin_slider_) settings.extra_cull_margin = std::clamp(cull_margin_slider_->value(), 0.0f, 1000.0f);
    ZoomKeyPointWidget::Values min_values{};
    ZoomKeyPointWidget::Values max_values{};
    if (zoom_in_keypoint_) {
        min_values = zoom_in_keypoint_->values();
        settings.zoom_low = min_values.zoom;
    }
    if (zoom_out_keypoint_) {
        max_values = zoom_out_keypoint_->values();
        settings.zoom_high = max_values.zoom;
    }

    settings.zoom_low = std::clamp(settings.zoom_low, WarpedScreenGrid::kMinZoomAnchors, WarpedScreenGrid::kMaxZoomAnchors);
    const float min_high = std::min(WarpedScreenGrid::kMaxZoomAnchors, settings.zoom_low + 0.0001f);
    settings.zoom_high = std::clamp(settings.zoom_high, min_high, WarpedScreenGrid::kMaxZoomAnchors);
    if (perspective_zero_distance_slider_) {
        settings.perspective_distance_at_scale_zero = std::clamp( perspective_zero_distance_slider_->value(), -5000.0f, 5000.0f);
    }
    if (perspective_hundred_distance_slider_) {
        settings.perspective_distance_at_scale_hundred = std::clamp( perspective_hundred_distance_slider_->value(), -5000.0f, 5000.0f);
    }

    auto slider_to_opacity = [](const FloatSliderWidget* slider) -> int {
        if (!slider) return 0;
        const float clamped = std::clamp(slider->value(), 0.0f, 255.0f);
        return static_cast<int>(std::round(clamped));
};
    settings.foreground_texture_max_opacity = slider_to_opacity(foreground_texture_opacity_slider_.get());
    settings.background_texture_max_opacity = slider_to_opacity(background_texture_opacity_slider_.get());
    auto clamp_curve_selection = [](DMDropdown* dropdown) -> WarpedScreenGrid::BlurFalloffMethod {
        if (!dropdown) return WarpedScreenGrid::BlurFalloffMethod::Linear;
        int sel = dropdown->selected();
        sel = std::clamp(sel, 0, 4);
        return static_cast<WarpedScreenGrid::BlurFalloffMethod>(sel);
};
    settings.texture_opacity_falloff_method = clamp_curve_selection(texture_opacity_interp_dropdown_.get());
    return settings;
}
