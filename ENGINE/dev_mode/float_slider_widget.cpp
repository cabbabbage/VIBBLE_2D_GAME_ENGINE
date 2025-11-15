#include "float_slider_widget.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>

FloatSliderWidget::FloatSliderWidget(std::string label,
                                     float min_val,
                                     float max_val,
                                     float step,
                                     float value,
                                     int precision)
    : min_(std::min(min_val, max_val)),
      max_(std::max(min_val, max_val)),
      step_(step > 0.0f ? step : 0.001f),
      precision_(std::max(0, precision)) {
    slider_min_units_ = 0;
    slider_max_units_ = std::max(slider_min_units_, compute_units_for_value(max_));
    slider_ = std::make_unique<DMSlider>(label, slider_min_units_, slider_max_units_, value_to_slider(value));
    slider_->set_defer_commit_until_unfocus(false);
    slider_->set_value_formatter([this](int units, std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) {
        return format_units(units, buffer);
    });
    slider_->set_value_parser([this](const std::string& text) { return parse_units(text); });
    slider_widget_ = std::make_unique<SliderWidget>(slider_.get());
    current_value_ = slider_to_value(slider_->value());
}

FloatSliderWidget::~FloatSliderWidget() = default;

void FloatSliderWidget::set_on_value_changed(ChangeCallback cb) { on_change_ = std::move(cb); }

void FloatSliderWidget::set_value(float v) {
    if (!slider_) return;
    slider_->set_value(value_to_slider(v));
    current_value_ = slider_to_value(slider_->value());
}

void FloatSliderWidget::set_rect(const SDL_Rect& r) {
    if (slider_widget_) slider_widget_->set_rect(r);
}

void FloatSliderWidget::set_tooltip(std::string text) {
    if (slider_widget_) slider_widget_->set_tooltip(std::move(text));
}

const SDL_Rect& FloatSliderWidget::rect() const {
    if (slider_widget_) {
        return slider_widget_->rect();
    }
    static SDL_Rect empty{0, 0, 0, 0};
    return empty;
}

int FloatSliderWidget::height_for_width(int w) const {
    return slider_widget_ ? slider_widget_->height_for_width(w) : DMSlider::height();
}

bool FloatSliderWidget::handle_event(const SDL_Event& e) {
    if (!slider_widget_) return false;
    const float previous_value = current_value_;
    bool handled = slider_widget_->handle_event(e);
    if (slider_) {
        current_value_ = slider_to_value(slider_->value());
        if (handled && on_change_ && std::fabs(current_value_ - previous_value) > 1e-5f) {
            on_change_(current_value_);
        }
    }
    return handled;
}

void FloatSliderWidget::render(SDL_Renderer* r) const {
    if (slider_widget_) slider_widget_->render(r);
}

float FloatSliderWidget::snap_value(float v) const {
    if (max_ <= min_ || step_ <= 0.0f) {
        return std::clamp(v, min_, max_);
    }
    float clamped = std::clamp(v, min_, max_);
    float steps = std::round((clamped - min_) / step_);
    float snapped = min_ + steps * step_;
    if (snapped < min_) snapped = min_;
    if (snapped > max_) snapped = max_;
    return snapped;
}

int FloatSliderWidget::compute_units_for_value(float v) const {
    if (step_ <= 0.0f || max_ <= min_) {
        return 0;
    }
    float snapped = snap_value(v);
    double steps = std::round((snapped - min_) / step_);
    return std::max(0, static_cast<int>(std::llround(steps)));
}

int FloatSliderWidget::value_to_slider(float v) const {
    if (step_ <= 0.0f || max_ <= min_) {
        return slider_min_units_;
    }
    float snapped = snap_value(v);
    double steps = std::round((snapped - min_) / step_);
    int units = static_cast<int>(std::llround(steps));
    return std::clamp(units, slider_min_units_, slider_max_units_);
}

float FloatSliderWidget::slider_to_value(int units) const {
    if (step_ <= 0.0f || max_ <= min_) {
        return std::clamp(min_, min_, max_);
    }
    int clamped_units = std::clamp(units, slider_min_units_, slider_max_units_);
    float raw = min_ + static_cast<float>(clamped_units) * step_;
    return snap_value(raw);
}

std::string_view FloatSliderWidget::format_units(int units, std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) const {
    return dev_mode::FormatSliderValue(slider_to_value(units), precision_, buffer);
}

std::optional<int> FloatSliderWidget::parse_units(const std::string& text) const {
    try {
        float parsed = std::stof(text);
        return value_to_slider(parsed);
    } catch (...) {
        return std::nullopt;
    }
}
