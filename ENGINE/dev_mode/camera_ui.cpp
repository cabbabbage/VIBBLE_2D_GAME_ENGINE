#include "camera_ui.hpp"

#include <SDL.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

#include "core/AssetsManager.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/widgets.hpp"
#include "utils/input.hpp"

class FloatSliderWidget : public Widget {
public:
    FloatSliderWidget(std::string label,
                      float min_val,
                      float max_val,
                      float step,
                      float value,
                      int precision = 2)
        : label_(std::move(label)),
          min_(std::min(min_val, max_val)),
          max_(std::max(min_val, max_val)),
          step_(step > 0.0f ? step : 0.001f),
          precision_(std::max(0, precision)) {
        set_value(value);
    }

    void set_value(float v) {
        value_ = clamp_and_snap(v);
    }

    float value() const { return value_; }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }

    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int) const override { return DMSlider::height(); }

    bool handle_event(const SDL_Event& e) override {
        if (e.type == SDL_MOUSEMOTION) {
            SDL_Point p{ e.motion.x, e.motion.y };
            SDL_Rect knob = knob_rect();
            knob_hovered_ = SDL_PointInRect(&p, &knob);
            if (dragging_) {
                set_value(value_for_x(p.x));
                return true;
            }
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            SDL_Point p{ e.button.x, e.button.y };
            SDL_Rect knob = knob_rect();
            if (SDL_PointInRect(&p, &knob)) {
                dragging_ = true;
                return true;
            }
            SDL_Rect track = track_rect();
            if (SDL_PointInRect(&p, &track)) {
                set_value(value_for_x(p.x));
                dragging_ = true;
                return true;
            }
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            if (dragging_) {
                dragging_ = false;
                return true;
            }
        } else if (e.type == SDL_MOUSEWHEEL) {
            SDL_Point mouse{0, 0};
            if (SDL_GetMouseFocus() == nullptr) {
                return false;
            }
            SDL_GetMouseState(&mouse.x, &mouse.y);
            if (SDL_PointInRect(&mouse, &rect_)) {
                float delta = static_cast<float>(e.wheel.y) * step_;
                set_value(value_ + delta);
                return true;
            }
        }
        return false;
    }

    void render(SDL_Renderer* r) const override {
        const DMSliderStyle& st = DMStyles::Slider();

        auto draw_label = [&](const DMLabelStyle& ls, const std::string& text, int x, int y) {
            TTF_Font* font = TTF_OpenFont(ls.font_path.c_str(), ls.font_size);
            if (!font) return;
            SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), ls.color);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
                if (tex) {
                    SDL_Rect dst{ x, y, surf->w, surf->h };
                    SDL_RenderCopy(r, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
                }
                SDL_FreeSurface(surf);
            }
            TTF_CloseFont(font);
        };

        draw_label(st.label, label_, rect_.x, rect_.y - st.label.font_size - DMSpacing::item_gap());

        SDL_Rect track = track_rect();
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, st.track_bg.r, st.track_bg.g, st.track_bg.b, st.track_bg.a);
        SDL_RenderFillRect(r, &track);

        const double range = std::max(0.0001, static_cast<double>(max_ - min_));
        const double fill_ratio = std::clamp((static_cast<double>(value_) - static_cast<double>(min_)) / range, 0.0, 1.0);
        SDL_Rect fill{ track.x, track.y, static_cast<int>(std::lround(fill_ratio * track.w)), track.h };
        SDL_SetRenderDrawColor(r, st.track_fill.r, st.track_fill.g, st.track_fill.b, st.track_fill.a);
        SDL_RenderFillRect(r, &fill);

        SDL_Rect knob = knob_rect();
        SDL_Color knob_col = (knob_hovered_ || dragging_) ? st.knob_hover : st.knob;
        SDL_Color knob_border = (knob_hovered_ || dragging_) ? st.knob_border_hover : st.knob_border;
        SDL_SetRenderDrawColor(r, knob_col.r, knob_col.g, knob_col.b, knob_col.a);
        SDL_RenderFillRect(r, &knob);
        SDL_SetRenderDrawColor(r, knob_border.r, knob_border.g, knob_border.b, knob_border.a);
        SDL_RenderDrawRect(r, &knob);

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(precision_) << value_;
        const std::string value_text = ss.str();
        int value_y = rect_.y + (rect_.h - st.value.font_size) / 2;
        draw_label(st.value, value_text, rect_.x + rect_.w - 70, value_y);
    }

private:
    float clamp_and_snap(float v) const {
        float clamped = std::clamp(v, min_, max_);
        const float offset = clamped - min_;
        const float steps = std::round(offset / step_);
        float snapped = min_ + steps * step_;
        return std::clamp(snapped, min_, max_);
    }

    SDL_Rect track_rect() const {
        return SDL_Rect{ rect_.x, rect_.y + rect_.h / 2 - 4, rect_.w - 80, 8 };
    }

    SDL_Rect knob_rect() const {
        SDL_Rect track = track_rect();
        const double range = std::max(0.0001, static_cast<double>(max_ - min_));
        const double ratio = std::clamp((static_cast<double>(value_) - static_cast<double>(min_)) / range, 0.0, 1.0);
        int x = track.x + static_cast<int>(std::lround(ratio * (track.w - 12)));
        return SDL_Rect{ x, track.y - 4, 12, 16 };
    }

    float value_for_x(int x) const {
        SDL_Rect track = track_rect();
        if (track.w <= 0) return value_;
        const double ratio = std::clamp((static_cast<double>(x) - static_cast<double>(track.x)) /
                                        static_cast<double>(std::max(1, track.w - 12)),
                                        0.0, 1.0);
        const double raw = static_cast<double>(min_) + ratio * static_cast<double>(max_ - min_);
        return clamp_and_snap(static_cast<float>(raw));
    }

    std::string label_;
    float min_ = 0.0f;
    float max_ = 1.0f;
    float step_ = 0.01f;
    int precision_ = 2;
    float value_ = 0.0f;
    bool dragging_ = false;
    bool knob_hovered_ = false;
    SDL_Rect rect_{0, 0, 0, 0};
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
    last_realism_enabled_ = cam.realism_enabled();
    last_parallax_enabled_ = cam.parallax_enabled();

    if (realism_checkbox_) realism_checkbox_->set_value(last_realism_enabled_);
    if (parallax_checkbox_) parallax_checkbox_->set_value(last_parallax_enabled_);

    if (parallax_vertical_slider_) parallax_vertical_slider_->set_value(last_settings_.parallax_vertical_strength);
    if (parallax_horizontal_slider_) parallax_horizontal_slider_->set_value(last_settings_.parallax_horizontal_strength);
    if (parallax_zoom_slider_) parallax_zoom_slider_->set_value(last_settings_.parallax_zoom_influence);
    if (perspective_angle_slider_) perspective_angle_slider_->set_value(last_settings_.perspective_angle_degrees);
    if (perspective_zoom_slider_) perspective_zoom_slider_->set_value(last_settings_.perspective_zoom_influence);
    if (distance_strength_slider_) distance_strength_slider_->set_value(last_settings_.distance_scale_strength);
    if (distance_exponent_slider_) distance_exponent_slider_->set_value(last_settings_.distance_scale_exponent);
    if (distance_offset_slider_) distance_offset_slider_->set_value(last_settings_.distance_scale_offset);
    if (distance_min_slider_) distance_min_slider_->set_value(last_settings_.distance_scale_min);
    if (distance_max_slider_) distance_max_slider_->set_value(last_settings_.distance_scale_max);
    if (squash_position_slider_) squash_position_slider_->set_value(last_settings_.squash_position_strength);
    if (squash_height_slider_) squash_height_slider_->set_value(last_settings_.squash_height_strength);
    if (squash_overall_slider_) squash_overall_slider_->set_value(last_settings_.squash_overall_strength);
    if (squash_zoom_slider_) squash_zoom_slider_->set_value(last_settings_.squash_zoom_influence);
    if (squash_curve_slider_) squash_curve_slider_->set_value(last_settings_.squash_curve_exponent);
    if (stretch_top_slider_) stretch_top_slider_->set_value(last_settings_.stretch_top_strength);
    if (max_squash_slider_) max_squash_slider_->set_value(last_settings_.max_squash_ratio);
    if (render_distance_slider_) render_distance_slider_->set_value(last_settings_.render_distance_factor);
}

void CameraUIPanel::build_ui() {
    realism_checkbox_ = std::make_unique<DMCheckbox>("Realism Enabled", true);
    parallax_checkbox_ = std::make_unique<DMCheckbox>("Parallax Enabled", true);
    realism_widget_ = std::make_unique<CheckboxWidget>(realism_checkbox_.get());
    parallax_widget_ = std::make_unique<CheckboxWidget>(parallax_checkbox_.get());

    load_button_ = std::make_unique<DMButton>("Load", &DMStyles::HeaderButton(), 110, DMButton::height());
    save_button_ = std::make_unique<DMButton>("Save", &DMStyles::HeaderButton(), 110, DMButton::height());
    reset_button_ = std::make_unique<DMButton>("Reset", &DMStyles::HeaderButton(), 110, DMButton::height());
    load_widget_ = std::make_unique<ButtonWidget>(load_button_.get(), [this]() { reload_from_json(); });
    save_widget_ = std::make_unique<ButtonWidget>(save_button_.get(), [this]() { save_to_json(); });
    reset_widget_ = std::make_unique<ButtonWidget>(reset_button_.get(), [this]() { reset_to_defaults(); });

    parallax_vertical_slider_ = std::make_unique<FloatSliderWidget>("Parallax Vertical", -500.0f, 500.0f, 0.5f, 0.0f, 2);
    parallax_horizontal_slider_ = std::make_unique<FloatSliderWidget>("Parallax Horizontal", -500.0f, 500.0f, 0.5f, 0.0f, 2);
    parallax_zoom_slider_ = std::make_unique<FloatSliderWidget>("Parallax Zoom Influence", 0.0f, 3.0f, 0.01f, 1.0f, 2);
    perspective_angle_slider_ = std::make_unique<FloatSliderWidget>("Perspective Angle", 0.0f, 360.0f, 1.0f, 90.0f, 1);
    perspective_zoom_slider_ = std::make_unique<FloatSliderWidget>("Perspective Zoom Influence", 0.0f, 3.0f, 0.01f, 0.35f, 2);
    distance_strength_slider_ = std::make_unique<FloatSliderWidget>("Distance Scale Strength", 0.0f, 3.0f, 0.01f, 0.2f, 2);
    distance_exponent_slider_ = std::make_unique<FloatSliderWidget>("Distance Scale Exponent", 0.01f, 5.0f, 0.01f, 1.0f, 2);
    distance_offset_slider_ = std::make_unique<FloatSliderWidget>("Distance Offset", -2.0f, 2.0f, 0.01f, 0.0f, 2);
    distance_min_slider_ = std::make_unique<FloatSliderWidget>("Distance Scale Min", 0.1f, 3.0f, 0.01f, 0.6f, 2);
    distance_max_slider_ = std::make_unique<FloatSliderWidget>("Distance Scale Max", 0.1f, 4.0f, 0.01f, 1.4f, 2);
    squash_position_slider_ = std::make_unique<FloatSliderWidget>("Squash Position Strength", 0.0f, 5.0f, 0.01f, 1.0f, 2);
    squash_height_slider_ = std::make_unique<FloatSliderWidget>("Squash Height Strength", 0.0f, 5.0f, 0.01f, 1.0f, 2);
    squash_overall_slider_ = std::make_unique<FloatSliderWidget>("Squash Overall Strength", 0.0f, 3.0f, 0.01f, 0.45f, 2);
    squash_zoom_slider_ = std::make_unique<FloatSliderWidget>("Squash Zoom Influence", 0.0f, 3.0f, 0.01f, 1.0f, 2);
    squash_curve_slider_ = std::make_unique<FloatSliderWidget>("Squash Curve Exponent", 0.1f, 5.0f, 0.01f, 1.35f, 2);
    stretch_top_slider_ = std::make_unique<FloatSliderWidget>("Stretch Top Strength", 0.0f, 3.0f, 0.01f, 0.55f, 2);
    max_squash_slider_ = std::make_unique<FloatSliderWidget>("Max Squash Ratio", 0.0f, 1.5f, 0.01f, 0.6f, 2);
    render_distance_slider_ = std::make_unique<FloatSliderWidget>("Render Distance Factor", 0.0f, 5.0f, 0.01f, 1.0f, 2);

    rebuild_rows();
}

void CameraUIPanel::rebuild_rows() {
    Rows rows;
    rows.push_back({ realism_widget_.get(), parallax_widget_.get() });
    rows.push_back({ load_widget_.get(), save_widget_.get(), reset_widget_.get() });
    rows.push_back({ parallax_vertical_slider_.get(), parallax_horizontal_slider_.get() });
    rows.push_back({ parallax_zoom_slider_.get(), perspective_zoom_slider_.get() });
    rows.push_back({ perspective_angle_slider_.get(), distance_strength_slider_.get() });
    rows.push_back({ distance_exponent_slider_.get(), distance_offset_slider_.get() });
    rows.push_back({ distance_min_slider_.get(), distance_max_slider_.get() });
    rows.push_back({ squash_position_slider_.get(), squash_height_slider_.get() });
    rows.push_back({ squash_overall_slider_.get(), squash_zoom_slider_.get() });
    rows.push_back({ squash_curve_slider_.get(), stretch_top_slider_.get() });
    rows.push_back({ max_squash_slider_.get(), render_distance_slider_.get() });
    set_rows(rows);
}

void CameraUIPanel::reset_to_defaults() {
    camera::RealismSettings defaults;
    if (realism_checkbox_) realism_checkbox_->set_value(true);
    if (parallax_checkbox_) parallax_checkbox_->set_value(true);
    if (parallax_vertical_slider_) parallax_vertical_slider_->set_value(defaults.parallax_vertical_strength);
    if (parallax_horizontal_slider_) parallax_horizontal_slider_->set_value(defaults.parallax_horizontal_strength);
    if (parallax_zoom_slider_) parallax_zoom_slider_->set_value(defaults.parallax_zoom_influence);
    if (perspective_angle_slider_) perspective_angle_slider_->set_value(defaults.perspective_angle_degrees);
    if (perspective_zoom_slider_) perspective_zoom_slider_->set_value(defaults.perspective_zoom_influence);
    if (distance_strength_slider_) distance_strength_slider_->set_value(defaults.distance_scale_strength);
    if (distance_exponent_slider_) distance_exponent_slider_->set_value(defaults.distance_scale_exponent);
    if (distance_offset_slider_) distance_offset_slider_->set_value(defaults.distance_scale_offset);
    if (distance_min_slider_) distance_min_slider_->set_value(defaults.distance_scale_min);
    if (distance_max_slider_) distance_max_slider_->set_value(defaults.distance_scale_max);
    if (squash_position_slider_) squash_position_slider_->set_value(defaults.squash_position_strength);
    if (squash_height_slider_) squash_height_slider_->set_value(defaults.squash_height_strength);
    if (squash_overall_slider_) squash_overall_slider_->set_value(defaults.squash_overall_strength);
    if (squash_zoom_slider_) squash_zoom_slider_->set_value(defaults.squash_zoom_influence);
    if (squash_curve_slider_) squash_curve_slider_->set_value(defaults.squash_curve_exponent);
    if (stretch_top_slider_) stretch_top_slider_->set_value(defaults.stretch_top_strength);
    if (max_squash_slider_) max_squash_slider_->set_value(defaults.max_squash_ratio);
    if (render_distance_slider_) render_distance_slider_->set_value(defaults.render_distance_factor);
    apply_settings_if_needed();
}

void CameraUIPanel::save_to_json() {
    if (!assets_) return;
    apply_settings_if_needed();
    assets_->on_camera_settings_changed();
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
    const bool realism_enabled = realism_checkbox_ ? realism_checkbox_->value() : last_realism_enabled_;
    const bool parallax_enabled = parallax_checkbox_ ? parallax_checkbox_->value() : last_parallax_enabled_;

    auto differs = [](float a, float b) {
        return std::fabs(a - b) > 0.0001f;
    };

    bool changed = realism_enabled != last_realism_enabled_ ||
                   parallax_enabled != last_parallax_enabled_;

    const camera::RealismSettings& prev = last_settings_;
    changed = changed || differs(settings.parallax_vertical_strength, prev.parallax_vertical_strength) ||
              differs(settings.parallax_horizontal_strength, prev.parallax_horizontal_strength) ||
              differs(settings.parallax_zoom_influence, prev.parallax_zoom_influence) ||
              differs(settings.perspective_angle_degrees, prev.perspective_angle_degrees) ||
              differs(settings.perspective_zoom_influence, prev.perspective_zoom_influence) ||
              differs(settings.distance_scale_strength, prev.distance_scale_strength) ||
              differs(settings.distance_scale_exponent, prev.distance_scale_exponent) ||
              differs(settings.distance_scale_offset, prev.distance_scale_offset) ||
              differs(settings.distance_scale_min, prev.distance_scale_min) ||
              differs(settings.distance_scale_max, prev.distance_scale_max) ||
              differs(settings.squash_position_strength, prev.squash_position_strength) ||
              differs(settings.squash_height_strength, prev.squash_height_strength) ||
              differs(settings.squash_overall_strength, prev.squash_overall_strength) ||
              differs(settings.squash_zoom_influence, prev.squash_zoom_influence) ||
              differs(settings.squash_curve_exponent, prev.squash_curve_exponent) ||
              differs(settings.stretch_top_strength, prev.stretch_top_strength) ||
              differs(settings.max_squash_ratio, prev.max_squash_ratio) ||
              differs(settings.render_distance_factor, prev.render_distance_factor);

    if (changed) {
        apply_settings_to_camera(settings, realism_enabled, parallax_enabled);
    }
}

void CameraUIPanel::apply_settings_to_camera(const camera::RealismSettings& settings,
                                             bool realism_enabled,
                                             bool parallax_enabled) {
    if (!assets_) return;
    camera& cam = assets_->getView();
    cam.set_realism_settings(settings);
    cam.set_realism_enabled(realism_enabled);
    cam.set_parallax_enabled(parallax_enabled);
    last_settings_ = settings;
    last_realism_enabled_ = realism_enabled;
    last_parallax_enabled_ = parallax_enabled;
}

camera::RealismSettings CameraUIPanel::read_settings_from_ui() const {
    camera::RealismSettings settings{};
    if (parallax_vertical_slider_) settings.parallax_vertical_strength = parallax_vertical_slider_->value();
    if (parallax_horizontal_slider_) settings.parallax_horizontal_strength = parallax_horizontal_slider_->value();
    if (parallax_zoom_slider_) settings.parallax_zoom_influence = parallax_zoom_slider_->value();
    if (perspective_angle_slider_) settings.perspective_angle_degrees = perspective_angle_slider_->value();
    if (perspective_zoom_slider_) settings.perspective_zoom_influence = perspective_zoom_slider_->value();
    if (distance_strength_slider_) settings.distance_scale_strength = distance_strength_slider_->value();
    if (distance_exponent_slider_) settings.distance_scale_exponent = std::max(0.01f, distance_exponent_slider_->value());
    if (distance_offset_slider_) settings.distance_scale_offset = std::clamp(distance_offset_slider_->value(), -5.0f, 5.0f);
    float dist_min = distance_min_slider_ ? distance_min_slider_->value() : settings.distance_scale_min;
    float dist_max = distance_max_slider_ ? distance_max_slider_->value() : settings.distance_scale_max;
    if (dist_min > dist_max) std::swap(dist_min, dist_max);
    settings.distance_scale_min = std::max(0.0f, dist_min);
    settings.distance_scale_max = std::max(settings.distance_scale_min, dist_max);
    if (squash_position_slider_) settings.squash_position_strength = squash_position_slider_->value();
    if (squash_height_slider_) settings.squash_height_strength = squash_height_slider_->value();
    if (squash_overall_slider_) settings.squash_overall_strength = squash_overall_slider_->value();
    if (squash_zoom_slider_) settings.squash_zoom_influence = squash_zoom_slider_->value();
    if (squash_curve_slider_) settings.squash_curve_exponent = std::max(0.1f, squash_curve_slider_->value());
    if (stretch_top_slider_) settings.stretch_top_strength = std::max(0.0f, stretch_top_slider_->value());
    if (max_squash_slider_) settings.max_squash_ratio = std::max(0.0f, max_squash_slider_->value());
    if (render_distance_slider_) settings.render_distance_factor = std::max(0.0f, render_distance_slider_->value());

    if (std::isfinite(settings.perspective_angle_degrees)) {
        settings.perspective_angle_degrees = std::fmod(settings.perspective_angle_degrees, 360.0f);
        if (settings.perspective_angle_degrees < 0.0f) {
            settings.perspective_angle_degrees += 360.0f;
        }
    } else {
        settings.perspective_angle_degrees = 90.0f;
    }

    settings.distance_scale_exponent = std::max(0.01f, settings.distance_scale_exponent);
    settings.distance_scale_strength = std::max(0.0f, settings.distance_scale_strength);
    settings.perspective_zoom_influence = std::max(0.0f, settings.perspective_zoom_influence);
    settings.render_distance_factor = std::max(0.0f, settings.render_distance_factor);

    return settings;
}

