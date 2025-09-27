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

    bool wants_full_row() const override { return true; }

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
    changed = changed || differs(settings.render_distance, prev.render_distance) ||
              differs(settings.tripod_distance_y, prev.tripod_distance_y) ||
              differs(settings.height_at_zoom1, prev.height_at_zoom1) ||
              differs(settings.parallax_strength, prev.parallax_strength) ||
              differs(settings.foreshorten_strength, prev.foreshorten_strength) ||
              differs(settings.distance_scale_strength, prev.distance_scale_strength);

    if (changed) {
        apply_settings_to_camera(settings, effects_enabled);
        // Auto-save on change (no Save button in dev mode)
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












