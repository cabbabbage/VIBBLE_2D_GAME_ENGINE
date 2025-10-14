#pragma once

#include "../DockableCollapsible.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

#include "asset/asset_info.hpp"
#include "asset_info_methods/lighting_loader.hpp"
#include "dev_mode/shared/formatting.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/asset_info_sections.hpp"

class AssetInfoUI;

class Section_Shading : public DockableCollapsible {
public:
    Section_Shading() : DockableCollapsible("Shading", false) {}
    void set_ui(AssetInfoUI* ui) { ui_ = ui; }
    ~Section_Shading() override = default;

    void build() override {
        shading_factor_ = 100;
        s_base_shadow_height_.reset();
        s_sh_intensity_.reset();
        s_sh_radius_.reset();
        s_sh_x_radius_.reset();
        s_sh_y_radius_.reset();
        s_sh_apex_bias_.reset();
        s_sh_offset_x_.reset();
        s_sh_offset_y_.reset();
        s_sh_falloff_.reset();
        s_sh_factor_.reset();
        c_is_shaded_.reset();
        shading_label_.reset();
        const AssetInfo* current_info = info_.get();
        if (current_info != last_info_) {
            base_shadow_height_value_ = default_base_shadow_height_value_;
            last_info_ = current_info;
        }
        if (!info_) return;

        shading_factor_ = std::clamp(info_->shading_factor, 1, 200);
        if (!info_->orbital_light_sources.empty()) {
            shading_light_ = info_->orbital_light_sources[0];
        } else {
            shading_light_ = LightSource{};
        }
        c_is_shaded_ = std::make_unique<DMCheckbox>("Has Shading", info_->is_shaded);
        shading_label_ = std::make_unique<DMButton>("Shading Source", &DMStyles::HeaderButton(), 150, DMButton::height());

        s_base_shadow_height_ = std::make_unique<DMSlider>("Base_shadow_height factor", 0, 220, base_shadow_height_value_);
        s_base_shadow_height_->set_value_formatter([](int v, std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) {
            return dev_mode::FormatSliderValue(static_cast<double>(v) / 100.0, 2, buffer);
        });
        s_base_shadow_height_->set_value_parser([](const std::string& text) -> std::optional<int> {
            try {
                double value = std::stod(text);
                if (!std::isfinite(value)) {
                    return std::nullopt;
                }
                value = std::clamp(value, 0.0, 2.2);
                return static_cast<int>(std::lround(value * 100.0));
            } catch (...) {
                return std::nullopt;
            }
        });

        s_sh_intensity_ = std::make_unique<DMSlider>("Light Intensity", 0, 255, shading_light_.intensity);
        s_sh_radius_    = std::make_unique<DMSlider>("Radius (px)", 0, 2000, shading_light_.radius);
        s_sh_x_radius_  = std::make_unique<DMSlider>("X Orbit Radius (px)", 0, 2000, shading_light_.x_radius);
        s_sh_y_radius_  = std::make_unique<DMSlider>("Y Orbit Radius (px)", 0, 2000, shading_light_.y_radius);
        s_sh_apex_bias_ = std::make_unique<DMSlider>("Apex Velocity Bias", 0, 100, shading_light_.apex_speed_bias);
        s_sh_offset_x_  = std::make_unique<DMSlider>("X Offset (px)", -2000, 2000, shading_light_.offset_x);
        s_sh_offset_y_  = std::make_unique<DMSlider>("Y Offset (px)", -2000, 2000, shading_light_.offset_y);
        s_sh_falloff_   = std::make_unique<DMSlider>("Falloff (%)", 0, 100, shading_light_.fall_off);
        s_sh_factor_    = std::make_unique<DMSlider>("Factor", 1, 200, shading_factor_);
    }

    void layout_custom_content(int /*screen_w*/, int /*screen_h*/) const override {
        int x = rect_.x + DMSpacing::panel_padding();
        int y = rect_.y + DMSpacing::panel_padding() + DMButton::height() + DMSpacing::header_gap();
        int maxw = rect_.w - 2 * DMSpacing::panel_padding();

        auto place = [&](auto& widget, int h) {
            if (!widget) return;
            widget->set_rect(SDL_Rect{x, y - scroll_, maxw, h});
            y += h + DMSpacing::item_gap();
};

        if (c_is_shaded_) {
            place(c_is_shaded_, DMCheckbox::height());
        }

        if (c_is_shaded_ && c_is_shaded_->value()) {
            int shade_start = y;
            place(s_base_shadow_height_, DMSlider::height());
            if (shading_label_) {
                int lbl_w = shading_label_->rect().w;
                int lbl_x = rect_.x + DMSpacing::panel_padding() + (maxw - lbl_w) / 2;
                shading_label_->set_rect(SDL_Rect{lbl_x, y - scroll_, lbl_w, DMButton::height()});
                y += DMButton::height() + DMSpacing::item_gap();
            }
            place(s_sh_intensity_, DMSlider::height());
            place(s_sh_radius_,    DMSlider::height());
            place(s_sh_x_radius_,  DMSlider::height());
            place(s_sh_y_radius_,  DMSlider::height());
            place(s_sh_apex_bias_, DMSlider::height());
            place(s_sh_offset_x_,  DMSlider::height());
            place(s_sh_offset_y_,  DMSlider::height());
            place(s_sh_falloff_,   DMSlider::height());
            place(s_sh_factor_,    DMSlider::height());
            shading_rect_ = SDL_Rect{x - 4, shade_start - scroll_ - 4, maxw + 8, (y - shade_start) + 8};
        } else {
            shading_rect_ = SDL_Rect{0, 0, 0, 0};
        }

        content_height_ = std::max(0, y - (rect_.y + DMSpacing::panel_padding() + DMButton::height() + DMSpacing::header_gap()));
    }

    bool handle_event(const SDL_Event& e) override {
        bool used = DockableCollapsible::handle_event(e);
        if (!info_ || !expanded_) return used;
        bool changed = false;
        bool reset_scaling_profile = false;
        const bool shading_was_enabled = info_ && info_->is_shaded;

        if (c_is_shaded_ && c_is_shaded_->handle_event(e)) {
            changed = true;
            reset_scaling_profile = true;
        }

        if (c_is_shaded_ && c_is_shaded_->value()) {
            if (s_base_shadow_height_ && s_base_shadow_height_->handle_event(e)) {
                base_shadow_height_value_ = s_base_shadow_height_->value();
            }
            if (s_sh_intensity_ && s_sh_intensity_->handle_event(e)) { shading_light_.intensity = s_sh_intensity_->value(); changed = true; reset_scaling_profile = true; }
            if (s_sh_radius_    && s_sh_radius_->handle_event(e))    { shading_light_.radius = s_sh_radius_->value(); changed = true; reset_scaling_profile = true; }
            if (s_sh_x_radius_  && s_sh_x_radius_->handle_event(e))  { shading_light_.x_radius = s_sh_x_radius_->value(); changed = true; reset_scaling_profile = true; }
            if (s_sh_y_radius_  && s_sh_y_radius_->handle_event(e))  { shading_light_.y_radius = s_sh_y_radius_->value(); changed = true; reset_scaling_profile = true; }
            if (s_sh_apex_bias_ && s_sh_apex_bias_->handle_event(e)) { shading_light_.apex_speed_bias = s_sh_apex_bias_->value(); changed = true; reset_scaling_profile = true; }
            if (s_sh_offset_x_  && s_sh_offset_x_->handle_event(e))  { shading_light_.offset_x = s_sh_offset_x_->value(); changed = true; reset_scaling_profile = true; }
            if (s_sh_offset_y_  && s_sh_offset_y_->handle_event(e))  { shading_light_.offset_y = s_sh_offset_y_->value(); changed = true; reset_scaling_profile = true; }
            if (s_sh_falloff_   && s_sh_falloff_->handle_event(e))   { shading_light_.fall_off = s_sh_falloff_->value(); changed = true; reset_scaling_profile = true; }
            if (s_sh_factor_    && s_sh_factor_->handle_event(e)) {
                int new_factor = std::clamp(s_sh_factor_->value(), 1, 200);
                if (new_factor != shading_factor_) {
                    const double prev = std::max(1, shading_factor_);
                    const double ratio = static_cast<double>(new_factor) / prev;
                    auto scale_clamped = [&](int value, int min_v, int max_v) {
                        double scaled = std::round(static_cast<double>(value) * ratio);
                        return static_cast<int>(std::clamp(scaled, static_cast<double>(min_v), static_cast<double>(max_v)));
};
                    shading_light_.x_radius = scale_clamped(shading_light_.x_radius, 0, 2000);
                    shading_light_.y_radius = scale_clamped(shading_light_.y_radius, 0, 2000);
                    shading_light_.offset_x = scale_clamped(shading_light_.offset_x, -2000, 2000);
                    shading_light_.offset_y = scale_clamped(shading_light_.offset_y, -2000, 2000);
                    if (s_sh_x_radius_) s_sh_x_radius_->set_value(shading_light_.x_radius);
                    if (s_sh_y_radius_) s_sh_y_radius_->set_value(shading_light_.y_radius);
                    if (s_sh_offset_x_) s_sh_offset_x_->set_value(shading_light_.offset_x);
                    if (s_sh_offset_y_) s_sh_offset_y_->set_value(shading_light_.offset_y);
                }
                shading_factor_ = new_factor;
                changed = true;
                reset_scaling_profile = true;
            }
        }

        const bool shading_now_enabled = c_is_shaded_ && c_is_shaded_->value();
        const bool shading_removed = shading_was_enabled && !shading_now_enabled;

        if (changed) {
            commit_to_info();
            if (info_) {
                if (ui_ && reset_scaling_profile) {
                    ui_->notify_light_sources_modified(shading_removed);
                }
                (void)info_->update_info_json();
                if (ui_) {
                    SDL_Renderer* renderer = ui_->get_last_renderer();
                    if (renderer) {
                        LightingLoader::generate_textures(*info_, renderer);
                    }
                }
            }
        }

        return used || changed;
    }

    void render_content(SDL_Renderer* r) const override {
        if (c_is_shaded_) c_is_shaded_->render(r);
        if (c_is_shaded_ && c_is_shaded_->value()) {
            if (s_base_shadow_height_) s_base_shadow_height_->render(r);
            if (shading_label_) shading_label_->render(r);
            if (s_sh_intensity_) s_sh_intensity_->render(r);
            if (s_sh_radius_)    s_sh_radius_->render(r);
            if (s_sh_x_radius_)  s_sh_x_radius_->render(r);
            if (s_sh_y_radius_)  s_sh_y_radius_->render(r);
            if (s_sh_apex_bias_) s_sh_apex_bias_->render(r);
            if (s_sh_offset_x_)  s_sh_offset_x_->render(r);
            if (s_sh_offset_y_)  s_sh_offset_y_->render(r);
            if (s_sh_falloff_)   s_sh_falloff_->render(r);
            if (s_sh_factor_)    s_sh_factor_->render(r);
            SDL_Color bc = DMStyles::Border();
            dm_draw::DrawRoundedOutline(
                r,
                shading_rect_,
                DMStyles::CornerRadius(),
                1,
                bc);
        }
    }

    bool shading_enabled() const { return c_is_shaded_ && c_is_shaded_->value(); }
    bool shading_source_enabled() const {
        if (!shading_enabled()) {
            return false;
        }
        return shading_light_.radius > 0 || shading_light_.x_radius > 0 || shading_light_.y_radius > 0;
    }
    const LightSource& shading_light() const { return shading_light_; }

    double base_shadow_height_factor() const {
        return static_cast<double>(base_shadow_height_value_) / 100.0;
    }

private:
    void commit_to_info() {
        if (!info_) return;
        std::vector<LightSource> lights = info_->light_sources;
        info_->set_lighting(c_is_shaded_ ? c_is_shaded_->value() : false, shading_light_, shading_factor_, lights);
    }

    static constexpr int default_base_shadow_height_value_ = 100;

    LightSource shading_light_{};
    int shading_factor_ = 100;
    int base_shadow_height_value_ = default_base_shadow_height_value_;
    std::unique_ptr<DMSlider> s_base_shadow_height_;
    std::unique_ptr<DMButton> shading_label_;
    mutable SDL_Rect shading_rect_{0, 0, 0, 0};
    std::unique_ptr<DMCheckbox> c_is_shaded_;
    std::unique_ptr<DMSlider> s_sh_intensity_;
    std::unique_ptr<DMSlider> s_sh_radius_;
    std::unique_ptr<DMSlider> s_sh_x_radius_;
    std::unique_ptr<DMSlider> s_sh_y_radius_;
    std::unique_ptr<DMSlider> s_sh_apex_bias_;
    std::unique_ptr<DMSlider> s_sh_offset_x_;
    std::unique_ptr<DMSlider> s_sh_offset_y_;
    std::unique_ptr<DMSlider> s_sh_falloff_;
    std::unique_ptr<DMSlider> s_sh_factor_;

    AssetInfoUI* ui_ = nullptr;
    const AssetInfo* last_info_ = nullptr;

protected:
    std::string_view lock_settings_namespace() const override { return "asset_info"; }
    std::string_view lock_settings_id() const override { return "shading"; }
};

