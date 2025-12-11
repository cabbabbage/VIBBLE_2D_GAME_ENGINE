#pragma once

#include <functional>
#include <memory>
#include <string>

#include "shared/formatting.hpp"
#include "widgets.hpp"

class FloatSliderWidget : public Widget {
public:
    using ChangeCallback = std::function<void(float)>;

    FloatSliderWidget(std::string label, float min_val, float max_val, float step, float value, int precision = 2);

    FloatSliderWidget(const FloatSliderWidget&) = delete;
    FloatSliderWidget& operator=(const FloatSliderWidget&) = delete;

    ~FloatSliderWidget() override;

    void set_on_value_changed(ChangeCallback cb);

    void set_value(float v);

    float value() const { return current_value_; }

    void set_rect(const SDL_Rect& r) override;

    void set_tooltip(std::string text);

    const SDL_Rect& rect() const override;

    int height_for_width(int w) const override;

    bool wants_full_row() const override { return true; }

    bool handle_event(const SDL_Event& e) override;

    void render(SDL_Renderer* r) const override;

private:
    float snap_value(float v) const;
    int compute_units_for_value(float v) const;
    int value_to_slider(float v) const;
    float slider_to_value(int units) const;
    std::string_view format_units(int units, std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) const;
    std::optional<int> parse_units(const std::string& text) const;

    std::unique_ptr<class DMSlider> slider_;
    std::unique_ptr<class SliderWidget> slider_widget_;
    float min_ = 0.0f;
    float max_ = 1.0f;
    float step_ = 0.01f;
    int precision_ = 2;
    int slider_min_units_ = 0;
    int slider_max_units_ = 0;
    float current_value_ = 0.0f;
    ChangeCallback on_change_{};
};
