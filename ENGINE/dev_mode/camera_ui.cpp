#include "camera_ui.hpp"

#include <SDL.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <utility>

#include "core/AssetsManager.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/widgets.hpp"
#include "utils/input.hpp"

class SectionLabelWidget : public Widget {
public:
    explicit SectionLabelWidget(std::string text)
        : text_(std::move(text)) {}

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int) const override {
        return DMCheckbox::height();
    }

    bool handle_event(const SDL_Event&) override { return false; }

    void render(SDL_Renderer* renderer) const override {
        const DMLabelStyle& style = DMStyles::Label();
        TTF_Font* font = TTF_OpenFont(style.font_path.c_str(), style.font_size);
        if (!font) return;
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text_.c_str(), style.color);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            if (tex) {
                SDL_Rect dst{ rect_.x, rect_.y, surf->w, surf->h };
                SDL_RenderCopy(renderer, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
        TTF_CloseFont(font);
    }

private:
    std::string text_;
    SDL_Rect rect_{0, 0, 0, 0};
};
class FloatSliderWidget : public Widget {
public:
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
        slider_->set_value_formatter([this](int units) { return format_units(units); });
        slider_->set_value_parser([this](const std::string& text) { return parse_units(text); });
        slider_widget_ = std::make_unique<SliderWidget>(slider_.get());
        current_value_ = slider_to_value(slider_->value());
    }

    void set_value(float v) {
        if (!slider_) return;
        slider_->set_value(value_to_slider(v));
        current_value_ = slider_to_value(slider_->value());
    }

    float value() const { return current_value_; }

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
        bool handled = slider_widget_->handle_event(e);
        if (slider_) {
            current_value_ = slider_to_value(slider_->value());
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

    std::string format_units(int units) const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(precision_) << slider_to_value(units);
        return ss.str();
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
};

CameraUIPanel::CameraUIPanel(Assets* assets, int x, int y)
    : DockableCollapsible("Camera Settings", true, x, y),
      assets_(assets) {
    set_expanded(true);
    set_visible(false);
    set_padding(16);
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
    if (tripod_distance_slider_) tripod_distance_slider_->set_value(last_settings_.tripod_distance_y);
    if (height_zoom1_slider_) height_zoom1_slider_->set_value(last_settings_.height_at_zoom1);
    if (parallax_strength_slider_) parallax_strength_slider_->set_value(last_settings_.parallax_strength);
    if (foreshorten_strength_slider_) foreshorten_strength_slider_->set_value(last_settings_.foreshorten_strength);
    if (distance_strength_slider_) distance_strength_slider_->set_value(last_settings_.distance_scale_strength);
}

void CameraUIPanel::build_ui() {
    effects_checkbox_ = std::make_unique<DMCheckbox>("Perspective Effects", true);
    effects_widget_ = std::make_unique<CheckboxWidget>(effects_checkbox_.get());

    load_button_ = std::make_unique<DMButton>("Load", &DMStyles::HeaderButton(), 110, DMButton::height());
    reset_button_ = std::make_unique<DMButton>("Reset", &DMStyles::HeaderButton(), 110, DMButton::height());
    load_widget_ = std::make_unique<ButtonWidget>(load_button_.get(), [this]() { reload_from_json(); });
    reset_widget_ = std::make_unique<ButtonWidget>(reset_button_.get(), [this]() { reset_to_defaults(); });

    camera::RealismSettings defaults;

    render_section_label_ = std::make_unique<SectionLabelWidget>("Render Distance");
    perspective_section_label_ = std::make_unique<SectionLabelWidget>("Perspective");
    render_distance_slider_ = std::make_unique<FloatSliderWidget>("Render Distance (world units)", 0.0f, 4000.0f, 10.0f, defaults.render_distance, 0);
    tripod_distance_slider_ = std::make_unique<FloatSliderWidget>("Tripod Distance (Y)", -2000.0f, 0.0f, 5.0f, defaults.tripod_distance_y, 0);
    height_zoom1_slider_ = std::make_unique<FloatSliderWidget>("Height @ Zoom = 1 (px)", 0.0f, 1000.0f, 1.0f, defaults.height_at_zoom1, 0);
    parallax_strength_slider_ = std::make_unique<FloatSliderWidget>("Parallax Strength", 0.0f, 100.0f, 0.25f, defaults.parallax_strength, 2);
    foreshorten_strength_slider_ = std::make_unique<FloatSliderWidget>("Vertical Foreshortening Strength", 0.0f, 1.0f, 0.01f, defaults.foreshorten_strength, 2);
    distance_strength_slider_ = std::make_unique<FloatSliderWidget>("Distance Scaling Strength", 0.0f, 1.0f, 0.01f, defaults.distance_scale_strength, 2);

    rebuild_rows();
}

void CameraUIPanel::rebuild_rows() {
    Rows rows;
    rows.push_back({ effects_widget_.get() });
    rows.push_back({ render_section_label_.get() });
    rows.push_back({ render_distance_slider_.get() });
    rows.push_back({ perspective_section_label_.get() });
    rows.push_back({ tripod_distance_slider_.get(), height_zoom1_slider_.get() });
    rows.push_back({ parallax_strength_slider_.get(), foreshorten_strength_slider_.get() });
    rows.push_back({ distance_strength_slider_.get() });
    rows.push_back({ load_widget_.get(), reset_widget_.get() });
    set_rows(rows);
}

void CameraUIPanel::reset_to_defaults() {
    camera::RealismSettings defaults;
    if (effects_checkbox_) effects_checkbox_->set_value(true);

    if (render_distance_slider_) render_distance_slider_->set_value(defaults.render_distance);
    if (tripod_distance_slider_) tripod_distance_slider_->set_value(defaults.tripod_distance_y);
    if (height_zoom1_slider_) height_zoom1_slider_->set_value(defaults.height_at_zoom1);
    if (parallax_strength_slider_) parallax_strength_slider_->set_value(defaults.parallax_strength);
    if (foreshorten_strength_slider_) foreshorten_strength_slider_->set_value(defaults.foreshorten_strength);
    if (distance_strength_slider_) distance_strength_slider_->set_value(defaults.distance_scale_strength);
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
    changed = changed || differs(settings.render_distance, prev.render_distance) || differs(settings.tripod_distance_y, prev.tripod_distance_y) || differs(settings.height_at_zoom1, prev.height_at_zoom1) || differs(settings.parallax_strength, prev.parallax_strength) || differs(settings.foreshorten_strength, prev.foreshorten_strength) || differs(settings.distance_scale_strength, prev.distance_scale_strength);

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
    last_settings_ = settings;
    last_realism_enabled_ = effects_enabled;
}

camera::RealismSettings CameraUIPanel::read_settings_from_ui() const {
    camera::RealismSettings settings{};
    if (render_distance_slider_) settings.render_distance = std::max(0.0f, render_distance_slider_->value());
    if (tripod_distance_slider_) settings.tripod_distance_y = std::clamp(tripod_distance_slider_->value(), -2000.0f, 2000.0f);
    if (height_zoom1_slider_) settings.height_at_zoom1 = std::max(0.0f, height_zoom1_slider_->value());
    if (parallax_strength_slider_) settings.parallax_strength = std::max(0.0f, parallax_strength_slider_->value());
    if (foreshorten_strength_slider_) settings.foreshorten_strength = std::max(0.0f, foreshorten_strength_slider_->value());
    if (distance_strength_slider_) settings.distance_scale_strength = std::max(0.0f, distance_strength_slider_->value());
    return settings;
}

