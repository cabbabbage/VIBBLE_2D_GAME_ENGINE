#include "camera_ui.hpp"

#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <functional>

#include "core/AssetsManager.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/shared/formatting.hpp"
#include "dev_mode/widgets.hpp"
#include "utils/input.hpp"

namespace {
    constexpr float kMinTau = 1e-4f;

    float rate_from_tau(float tau) {
        if (!std::isfinite(tau) || tau <= kMinTau) {
            return 0.0f;
        }
        return 1.0f / tau;
    }

    float tau_from_rate(float rate) {
        if (!std::isfinite(rate) || rate <= kMinTau) {
            return 0.0f;
        }
        return 1.0f / rate;
    }

    int method_to_index(TransformSmoothingMethod method) {
        switch (method) {
        case TransformSmoothingMethod::Lerp:
            return 0;
        case TransformSmoothingMethod::CriticallyDampedSpring:
        default:
            return 1;
        }
    }

    TransformSmoothingMethod method_from_index(int idx) {
        return (idx == 0) ? TransformSmoothingMethod::Lerp : TransformSmoothingMethod::CriticallyDampedSpring;
    }
}

class SectionToggleWidget : public Widget {
public:
    using ToggleCallback = std::function<void(bool)>;

    SectionToggleWidget(std::string label, bool expanded)
        : label_(std::move(label)),
          expanded_(expanded) {
        button_ = std::make_unique<DMButton>(
            "",
            &DMStyles::HeaderButton(),
            DockableCollapsible::kDefaultFloatingContentWidth,
            DMButton::height());
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
        const char* indicator = expanded_ ? "[-]" : "[+]";
        button_->set_text(std::string(indicator) + " " + label_);
    }

    std::unique_ptr<DMButton> button_;
    SDL_Rect rect_{0, 0, 0, DMButton::height()};
    std::string label_;
    bool expanded_ = true;
    ToggleCallback on_toggle_{};
};
class FloatSliderWidget : public Widget {
public:
    using ChangeCallback = std::function<void(float)>;

    FloatSliderWidget(std::string label,
                      float min_val,
                      float max_val,
                      float step,
                      float value,
                      int precision = 2)
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

    void set_on_value_changed(ChangeCallback cb) { on_change_ = std::move(cb); }

    void set_value(float v) {
        if (!slider_) return;
        slider_->set_value(value_to_slider(v));
        current_value_ = slider_to_value(slider_->value());
    }

    float value() const { return current_value_; }

    void set_rect(const SDL_Rect& r) override {
        if (slider_widget_) slider_widget_->set_rect(r);
    }

    void set_tooltip(std::string text) {
        if (slider_widget_) slider_widget_->set_tooltip(std::move(text));
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

    void render(SDL_Renderer* r) const override {
        if (slider_widget_) slider_widget_->render(r);
    }

private:
    float snap_value(float v) const {
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

    int compute_units_for_value(float v) const {
        if (step_ <= 0.0f || max_ <= min_) {
            return 0;
        }
        float snapped = snap_value(v);
        double steps = std::round((snapped - min_) / step_);
        return std::max(0, static_cast<int>(std::llround(steps)));
    }

    int value_to_slider(float v) const {
        if (step_ <= 0.0f || max_ <= min_) {
            return slider_min_units_;
        }
        float snapped = snap_value(v);
        double steps = std::round((snapped - min_) / step_);
        int units = static_cast<int>(std::llround(steps));
        return std::clamp(units, slider_min_units_, slider_max_units_);
    }

    float slider_to_value(int units) const {
        if (step_ <= 0.0f || max_ <= min_) {
            return std::clamp(min_, min_, max_);
        }
        int clamped_units = std::clamp(units, slider_min_units_, slider_max_units_);
        float raw = min_ + static_cast<float>(clamped_units) * step_;
        return snap_value(raw);
    }

    std::string_view format_units(int units, std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) const {
        return dev_mode::FormatSliderValue(slider_to_value(units), precision_, buffer);
    }

    std::optional<int> parse_units(const std::string& text) const {
        try {
            float parsed = std::stof(text);
            return value_to_slider(parsed);
        } catch (...) {
            return std::nullopt;
        }
    }

    std::unique_ptr<DMSlider> slider_;
    std::unique_ptr<SliderWidget> slider_widget_;
    float min_ = 0.0f;
    float max_ = 1.0f;
    float step_ = 0.01f;
    int precision_ = 2;
    int slider_min_units_ = 0;
    int slider_max_units_ = 0;
    float current_value_ = 0.0f;
    ChangeCallback on_change_{};
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

CameraUIPanel::CameraUIPanel(Assets* assets, int x, int y)
    : DockableCollapsible("Camera Settings", true, x, y),
      assets_(assets) {
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

void CameraUIPanel::open() {
    set_visible(true);
    suppress_apply_once_ = true;
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
    DockableCollapsible::update(input, screen_w, screen_h);
    if (!is_visible()) return;
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
    if (!is_visible()) return;
    DockableCollapsible::render(renderer);
}

void CameraUIPanel::sync_from_camera() {
    if (!assets_) return;
    camera& cam = assets_->getView();
    last_settings_ = cam.realism_settings();
    bool effects_enabled = cam.realism_enabled() && cam.parallax_enabled();
    last_realism_enabled_ = effects_enabled;
    if (effects_checkbox_) effects_checkbox_->set_value(effects_enabled);

    if (render_distance_slider_) render_distance_slider_->set_value(last_settings_.render_distance);
    if (min_render_size_slider_) min_render_size_slider_->set_value(last_settings_.min_visible_screen_ratio);
    if (tripod_distance_slider_) tripod_distance_slider_->set_value(last_settings_.tripod_distance_y);
    if (height_zoom1_slider_) height_zoom1_slider_->set_value(last_settings_.height_at_zoom1);
    if (parallax_strength_slider_) parallax_strength_slider_->set_value(last_settings_.parallax_strength);
    if (foreshorten_strength_slider_) foreshorten_strength_slider_->set_value(last_settings_.foreshorten_strength);
    if (distance_strength_slider_) distance_strength_slider_->set_value(last_settings_.distance_scale_strength);
    if (render_quality_slider_) render_quality_slider_->set_value(last_settings_.render_quality_percent);
    if (smoothing_checkbox_) smoothing_checkbox_->set_value(last_settings_.smooth_motion_zoom);
    if (smoothing_method_dropdown_) smoothing_method_dropdown_->set_selected(method_to_index(last_settings_.motion_smoothing_method));
    if (motion_tau_slider_) motion_tau_slider_->set_value(last_settings_.motion_smoothing_tau);
    if (motion_stiffness_slider_) motion_stiffness_slider_->set_value(last_settings_.motion_smoothing_spring_frequency);
    if (motion_max_step_slider_) motion_max_step_slider_->set_value(last_settings_.motion_smoothing_max_step);
    if (motion_snap_slider_) motion_snap_slider_->set_value(last_settings_.motion_smoothing_snap_threshold);
    if (parallax_smoothing_slider_) {
        const float slider_value = (last_settings_.parallax_smoothing.method == TransformSmoothingMethod::Lerp)
            ? tau_from_rate(last_settings_.parallax_smoothing.lerp_rate)
            : last_settings_.parallax_smoothing.spring_frequency;
        parallax_smoothing_slider_->set_value(slider_value);
    }
    if (hysteresis_margin_slider_) hysteresis_margin_slider_->set_value(last_settings_.scale_variant_hysteresis_margin);
    if (min_zoom_multiplier_slider_) min_zoom_multiplier_slider_->set_value(last_settings_.min_zoom_multiplier);
    if (max_zoom_multiplier_slider_) max_zoom_multiplier_slider_->set_value(last_settings_.max_zoom_multiplier);
}

void CameraUIPanel::build_ui() {
    set_padding(DMSpacing::panel_padding());
    set_row_gap(DMSpacing::item_gap());
    set_col_gap(DMSpacing::small_gap());

    effects_checkbox_ = std::make_unique<DMCheckbox>("Depth Effects", true);
    effects_widget_ = std::make_unique<CheckboxWidget>(effects_checkbox_.get());
    effects_widget_->set_tooltip("Enable parallax, foreshortening, and distance-based scaling.");

    load_button_ = std::make_unique<DMButton>("Load", &DMStyles::HeaderButton(), 110, DMButton::height());
    reset_button_ = std::make_unique<DMButton>("Reset", &DMStyles::HeaderButton(), 110, DMButton::height());
    load_widget_ = std::make_unique<ButtonWidget>(load_button_.get(), [this]() { reload_from_json(); });
    reset_widget_ = std::make_unique<ButtonWidget>(reset_button_.get(), [this]() { reset_to_defaults(); });
    load_widget_->set_tooltip("Reload the current settings from disk.");
    reset_widget_->set_tooltip("Restore the built-in camera defaults.");

    camera::RealismSettings defaults;

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
    configure_section(zoom_section_header_,       "Zoom Range",               &zoom_section_expanded_);
    configure_section(smoothing_section_header_,  "Motion & Smoothing",       &smoothing_section_expanded_);

    render_distance_slider_ = std::make_unique<FloatSliderWidget>("Render Buffer (px)", 0.0f, 4000.0f, 10.0f, defaults.render_distance, 0);
    render_distance_slider_->set_tooltip("Keeps this many extra pixels alive outside the viewport so objects never snap in.");
    render_distance_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });
    min_render_size_slider_ = std::make_unique<FloatSliderWidget>("Min On-Screen Size", 0.0f, 0.05f, 0.001f, defaults.min_visible_screen_ratio, 3);
    min_render_size_slider_->set_tooltip("Cull sprites once their height drops below this fraction of the screen (0.01 = 1%).");
    min_render_size_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });
    tripod_distance_slider_ = std::make_unique<FloatSliderWidget>("Depth Offset (px)", -2000.0f, 0.0f, 5.0f, defaults.tripod_distance_y, 0);
    tripod_distance_slider_->set_tooltip("Shifts the parallax anchor up or down to bias how layers separate.");
    tripod_distance_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });
    height_zoom1_slider_ = std::make_unique<FloatSliderWidget>("Base Camera Height (px)", 0.0f, 1000.0f, 1.0f, defaults.height_at_zoom1, 0);
    height_zoom1_slider_->set_tooltip("Camera height when zoom is 1.0; higher values flatten the scene.");
    height_zoom1_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });
    parallax_strength_slider_ = std::make_unique<FloatSliderWidget>("Parallax Strength", 0.0f, 100.0f, 0.25f, defaults.parallax_strength, 2);
    parallax_strength_slider_->set_tooltip("Amount of parallax offset applied relative to camera movement.");
    parallax_strength_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });
    foreshorten_strength_slider_ = std::make_unique<FloatSliderWidget>("Vertical Stretch", 0.0f, 2.0f, 0.01f, defaults.foreshorten_strength, 2);
    foreshorten_strength_slider_->set_tooltip("Controls how much tall sprites stretch or compress with depth.");
    foreshorten_strength_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });
    distance_strength_slider_ = std::make_unique<FloatSliderWidget>("Distance Scale", 0.0f, 1.0f, 0.01f, defaults.distance_scale_strength, 2);
    distance_strength_slider_->set_tooltip("Higher values shrink faraway sprites more aggressively.");
    distance_strength_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });
    render_quality_slider_ = std::make_unique<DiscreteSliderWidget>("Render Quality (%)", std::vector<int>{100, 75, 50, 25, 10}, defaults.render_quality_percent);
    render_quality_slider_->set_tooltip("Trade fidelity for speed; lowers the number of sprites drawn each frame.");
    render_quality_slider_->set_on_value_changed([this](int) { on_control_value_changed(); });

    smoothing_checkbox_ = std::make_unique<DMCheckbox>("Smooth Motion", defaults.smooth_motion_zoom);
    smoothing_widget_   = std::make_unique<CheckboxWidget>(smoothing_checkbox_.get());
    smoothing_widget_->set_tooltip("Blend camera motion and zoom instead of stepping directly to the target.");

    const std::vector<std::string> method_options{"Smooth Lerp", "Spring"};
    smoothing_method_dropdown_ = std::make_unique<DMDropdown>(
        "Smoothing Type",
        method_options,
        method_to_index(defaults.motion_smoothing_method));
    smoothing_method_widget_ = std::make_unique<DropdownWidget>(smoothing_method_dropdown_.get());
    smoothing_method_widget_->set_tooltip("Pick between a simple lerp or a spring-like response for smoothing.");

    motion_tau_slider_ = std::make_unique<FloatSliderWidget>("Lerp Response (s)", 0.0f, 1.0f, 0.01f, defaults.motion_smoothing_tau, 3);
    motion_tau_slider_->set_tooltip("When using lerp smoothing, this is how long it takes to settle (smaller reacts faster).");
    motion_tau_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });
    motion_stiffness_slider_ = std::make_unique<FloatSliderWidget>(
        "Spring Frequency (Hz)", 0.0f, 10.0f, 0.05f, defaults.motion_smoothing_spring_frequency, 2);
    motion_stiffness_slider_->set_tooltip("When using the spring method, higher values track the target faster.");
    motion_stiffness_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });
    motion_max_step_slider_ = std::make_unique<FloatSliderWidget>(
        "Max Catch-Up Speed", 0.0f, 12000.0f, 25.0f, defaults.motion_smoothing_max_step, 0);
    motion_max_step_slider_->set_tooltip("Largest distance the smoothing can cover per second while chasing the target.");
    motion_max_step_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });
    motion_snap_slider_ = std::make_unique<FloatSliderWidget>("Snap Distance", 0.0f, 5.0f, 0.01f, defaults.motion_smoothing_snap_threshold, 2);
    motion_snap_slider_->set_tooltip("When closer than this amount, skip smoothing and snap immediately.");
    motion_snap_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });

    const float default_parallax_value = (defaults.parallax_smoothing.method == TransformSmoothingMethod::Lerp)
        ? tau_from_rate(defaults.parallax_smoothing.lerp_rate)
        : defaults.parallax_smoothing.spring_frequency;
    parallax_smoothing_slider_ = std::make_unique<FloatSliderWidget>(
        "Parallax Ease", 0.0f, 12.0f, 0.05f, default_parallax_value, 2);
    parallax_smoothing_slider_->set_tooltip(
        "Extra smoothing just for parallax offsets (seconds for lerp, Hz for spring).");
    parallax_smoothing_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });

    hysteresis_margin_slider_ = std::make_unique<FloatSliderWidget>(
        "Texture Switch Cushion", 0.0f, 0.5f, 0.005f, defaults.scale_variant_hysteresis_margin, 3);
    hysteresis_margin_slider_->set_tooltip(
        "Padding before swapping between pre-scaled sprite variants to avoid flicker.");
    hysteresis_margin_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });

    min_zoom_multiplier_slider_ = std::make_unique<FloatSliderWidget>(
        "Minimum Zoom", 0.1f, 2.0f, 0.01f, defaults.min_zoom_multiplier, 2);
    min_zoom_multiplier_slider_->set_tooltip(
        "Lower bound for automatic zooming (smaller = closer look).");
    min_zoom_multiplier_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });

    max_zoom_multiplier_slider_ = std::make_unique<FloatSliderWidget>(
        "Maximum Zoom", 0.1f, 3.0f, 0.01f, defaults.max_zoom_multiplier, 2);
    max_zoom_multiplier_slider_->set_tooltip(
        "Upper bound for automatic zooming (larger = wider view).");
    max_zoom_multiplier_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });

    rebuild_rows();
}

void CameraUIPanel::on_control_value_changed() {
    if (!assets_ || !is_visible()) {
        return;
    }
    apply_settings_if_needed();
}

void CameraUIPanel::rebuild_rows() {
    Rows rows;
    rows.push_back({ effects_widget_.get() });

    rows.push_back({ visibility_section_header_.get() });
    if (visibility_section_expanded_) {
        rows.push_back({ render_distance_slider_.get() });
        rows.push_back({ min_render_size_slider_.get() });
        rows.push_back({ render_quality_slider_.get() });
    }

    rows.push_back({ depth_section_header_.get() });
    if (depth_section_expanded_) {
        rows.push_back({ tripod_distance_slider_.get() });
        rows.push_back({ height_zoom1_slider_.get() });
        rows.push_back({ parallax_strength_slider_.get() });
        rows.push_back({ foreshorten_strength_slider_.get() });
        rows.push_back({ distance_strength_slider_.get() });
    }

    rows.push_back({ zoom_section_header_.get() });
    if (zoom_section_expanded_) {
        rows.push_back({ min_zoom_multiplier_slider_.get() });
        rows.push_back({ max_zoom_multiplier_slider_.get() });
    }

    rows.push_back({ smoothing_section_header_.get() });
    if (smoothing_section_expanded_) {
        rows.push_back({ smoothing_widget_.get() });
        rows.push_back({ smoothing_method_widget_.get() });
        rows.push_back({ motion_tau_slider_.get() });
        rows.push_back({ motion_stiffness_slider_.get() });
        rows.push_back({ motion_max_step_slider_.get() });
        rows.push_back({ motion_snap_slider_.get() });
        rows.push_back({ parallax_smoothing_slider_.get() });
        rows.push_back({ hysteresis_margin_slider_.get() });
    }
    rows.push_back({ load_widget_.get(), reset_widget_.get() });
    set_rows(rows);
}

void CameraUIPanel::reset_to_defaults() {
    camera::RealismSettings defaults;
    if (effects_checkbox_) effects_checkbox_->set_value(true);

    if (render_distance_slider_) render_distance_slider_->set_value(defaults.render_distance);
    if (min_render_size_slider_) min_render_size_slider_->set_value(defaults.min_visible_screen_ratio);
    if (tripod_distance_slider_) tripod_distance_slider_->set_value(defaults.tripod_distance_y);
    if (height_zoom1_slider_) height_zoom1_slider_->set_value(defaults.height_at_zoom1);
    if (parallax_strength_slider_) parallax_strength_slider_->set_value(defaults.parallax_strength);
    if (foreshorten_strength_slider_) foreshorten_strength_slider_->set_value(defaults.foreshorten_strength);
    if (distance_strength_slider_) distance_strength_slider_->set_value(defaults.distance_scale_strength);
    if (render_quality_slider_) render_quality_slider_->set_value(defaults.render_quality_percent);
    if (smoothing_checkbox_) smoothing_checkbox_->set_value(defaults.smooth_motion_zoom);
    if (smoothing_method_dropdown_) smoothing_method_dropdown_->set_selected(method_to_index(defaults.motion_smoothing_method));
    if (motion_tau_slider_) motion_tau_slider_->set_value(defaults.motion_smoothing_tau);
    if (motion_stiffness_slider_) motion_stiffness_slider_->set_value(defaults.motion_smoothing_spring_frequency);
    if (motion_max_step_slider_) motion_max_step_slider_->set_value(defaults.motion_smoothing_max_step);
    if (motion_snap_slider_) motion_snap_slider_->set_value(defaults.motion_smoothing_snap_threshold);
    const float default_parallax_value = (defaults.parallax_smoothing.method == TransformSmoothingMethod::Lerp)
        ? tau_from_rate(defaults.parallax_smoothing.lerp_rate)
        : defaults.parallax_smoothing.spring_frequency;
    if (parallax_smoothing_slider_) parallax_smoothing_slider_->set_value(default_parallax_value);
    if (hysteresis_margin_slider_) hysteresis_margin_slider_->set_value(defaults.scale_variant_hysteresis_margin);
    if (min_zoom_multiplier_slider_) min_zoom_multiplier_slider_->set_value(defaults.min_zoom_multiplier);
    if (max_zoom_multiplier_slider_) max_zoom_multiplier_slider_->set_value(defaults.max_zoom_multiplier);
    apply_settings_if_needed();
}

void CameraUIPanel::reload_from_json() {
    if (!assets_) return;
    assets_->reload_camera_settings();
    suppress_apply_once_ = true;
    sync_from_camera();
}

void CameraUIPanel::apply_settings_if_needed() {
    if (!assets_) return;
    camera::RealismSettings settings = read_settings_from_ui();
    const bool effects_enabled = effects_checkbox_ ? effects_checkbox_->value() : last_realism_enabled_;

    auto differs = [](float a, float b) {
        return std::fabs(a - b) > 0.0001f;
};

    bool changed = effects_enabled != last_realism_enabled_;
    const camera::RealismSettings& prev = last_settings_;
    changed = changed || differs(settings.render_distance, prev.render_distance) || differs(settings.tripod_distance_y, prev.tripod_distance_y) || differs(settings.height_at_zoom1, prev.height_at_zoom1) || differs(settings.parallax_strength, prev.parallax_strength) || differs(settings.foreshorten_strength, prev.foreshorten_strength) || differs(settings.distance_scale_strength, prev.distance_scale_strength) || differs(settings.min_visible_screen_ratio, prev.min_visible_screen_ratio);
    if (render_quality_slider_) {
        changed = changed || settings.render_quality_percent != prev.render_quality_percent;
    }
    changed = changed || settings.smooth_motion_zoom != prev.smooth_motion_zoom;
    changed = changed || settings.motion_smoothing_method != prev.motion_smoothing_method;
    changed = changed || differs(settings.motion_smoothing_tau, prev.motion_smoothing_tau);
    changed = changed || differs(settings.motion_smoothing_spring_frequency, prev.motion_smoothing_spring_frequency);
    changed = changed || differs(settings.motion_smoothing_max_step, prev.motion_smoothing_max_step);
    changed = changed || differs(settings.motion_smoothing_snap_threshold, prev.motion_smoothing_snap_threshold);
    changed = changed || differs(settings.scale_variant_hysteresis_margin, prev.scale_variant_hysteresis_margin);
    changed = changed || differs(settings.min_zoom_multiplier, prev.min_zoom_multiplier);
    changed = changed || differs(settings.max_zoom_multiplier, prev.max_zoom_multiplier);
    changed = changed || settings.parallax_smoothing.method != prev.parallax_smoothing.method ||
        differs(settings.parallax_smoothing.lerp_rate, prev.parallax_smoothing.lerp_rate) ||
        differs(settings.parallax_smoothing.spring_frequency, prev.parallax_smoothing.spring_frequency);

    if (changed) {
        apply_settings_to_camera(settings, effects_enabled);

        assets_->on_camera_settings_changed();
    }
}

void CameraUIPanel::apply_settings_to_camera(const camera::RealismSettings& settings,
                                             bool effects_enabled) {
    if (!assets_) return;
    camera& cam = assets_->getView();
    cam.set_realism_settings(settings);
    cam.set_realism_enabled(effects_enabled);
    cam.set_parallax_enabled(effects_enabled);
    if (assets_) {
        assets_->apply_camera_runtime_settings();
    }
    last_settings_ = settings;
    last_realism_enabled_ = effects_enabled;
}

camera::RealismSettings CameraUIPanel::read_settings_from_ui() const {
    camera::RealismSettings settings = last_settings_;
    if (render_distance_slider_) settings.render_distance = std::max(0.0f, render_distance_slider_->value());
    if (min_render_size_slider_) settings.min_visible_screen_ratio = std::clamp(min_render_size_slider_->value(), 0.0f, 0.5f);
    if (tripod_distance_slider_) settings.tripod_distance_y = std::clamp(tripod_distance_slider_->value(), -2000.0f, 2000.0f);
    if (height_zoom1_slider_) settings.height_at_zoom1 = std::max(0.0f, height_zoom1_slider_->value());
    if (parallax_strength_slider_) settings.parallax_strength = std::max(0.0f, parallax_strength_slider_->value());
    if (foreshorten_strength_slider_) settings.foreshorten_strength = std::max(0.0f, foreshorten_strength_slider_->value());
    if (distance_strength_slider_) settings.distance_scale_strength = std::max(0.0f, distance_strength_slider_->value());
    if (render_quality_slider_) settings.render_quality_percent = render_quality_slider_->value();
    if (smoothing_checkbox_) settings.smooth_motion_zoom = smoothing_checkbox_->value();

    TransformSmoothingMethod method = settings.motion_smoothing_method;
    if (smoothing_method_dropdown_) {
        method = method_from_index(smoothing_method_dropdown_->selected());
    }
    settings.motion_smoothing_method = method;

    if (motion_tau_slider_) settings.motion_smoothing_tau = std::max(0.0f, motion_tau_slider_->value());
    if (motion_stiffness_slider_) settings.motion_smoothing_spring_frequency = std::max(0.0f, motion_stiffness_slider_->value());
    if (motion_max_step_slider_) settings.motion_smoothing_max_step = std::max(0.0f, motion_max_step_slider_->value());
    if (motion_snap_slider_) settings.motion_smoothing_snap_threshold = std::max(0.0f, motion_snap_slider_->value());

    if (parallax_smoothing_slider_) {
        const float slider_value = std::max(0.0f, parallax_smoothing_slider_->value());
        if (method == TransformSmoothingMethod::Lerp) {
            settings.parallax_smoothing.lerp_rate = rate_from_tau(slider_value);
            settings.parallax_smoothing.spring_frequency = 0.0f;
        } else {
            settings.parallax_smoothing.spring_frequency = slider_value;
            settings.parallax_smoothing.lerp_rate = 0.0f;
        }
    }
    settings.parallax_smoothing.method = method;
    if (hysteresis_margin_slider_) {
        settings.scale_variant_hysteresis_margin = std::max(0.0f, hysteresis_margin_slider_->value());
    }
    if (min_zoom_multiplier_slider_) {
        settings.min_zoom_multiplier = std::max(0.1f, min_zoom_multiplier_slider_->value());
    }
    if (max_zoom_multiplier_slider_) {
        settings.max_zoom_multiplier = std::max(0.1f, max_zoom_multiplier_slider_->value());
    }
    return settings;
}
