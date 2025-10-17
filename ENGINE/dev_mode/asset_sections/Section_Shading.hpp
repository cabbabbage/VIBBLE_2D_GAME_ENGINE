#pragma once

#include "../DockableCollapsible.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "asset/asset_info.hpp"
#include "dev_mode/asset_info_sections.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/shared/formatting.hpp"

class AssetInfoUI;

class Section_Shading : public DockableCollapsible {
public:
    Section_Shading() : DockableCollapsible("Shading", false) {}
    void set_ui(AssetInfoUI* ui) { ui_ = ui; }
    ~Section_Shading() override = default;

    void build() override {
        c_is_shaded_.reset();
        s_parallax_amount_.reset();
        s_screen_brightness_.reset();
        s_opacity_multiplier_.reset();

        if (!info_) {
            return;
        }

        c_is_shaded_ = std::make_unique<DMCheckbox>("Has Shading", info_->is_shaded);

        assign_slider_values_from_asset();

        s_parallax_amount_ = std::make_unique<DMSlider>(
            "Parallax Amount", parallax_min_, parallax_max_, parallax_value_);
        configure_ratio_slider(*s_parallax_amount_, slider_scale_);

        s_screen_brightness_ = std::make_unique<DMSlider>(
            "Screen Brightness Multiplier", brightness_min_, brightness_max_, brightness_value_);
        configure_ratio_slider(*s_screen_brightness_, slider_scale_);

        s_opacity_multiplier_ = std::make_unique<DMSlider>(
            "Opacity Multiplier", opacity_min_, opacity_max_, opacity_value_);
        configure_ratio_slider(*s_opacity_multiplier_, slider_scale_);

        assign_slider_values_from_asset();
    }

    void layout_custom_content(int /*screen_w*/, int /*screen_h*/) const override {
        const int padding = DMSpacing::panel_padding();
        const int header_gap = DMSpacing::header_gap();
        const int base_x = rect_.x + padding;
        const int base_y = rect_.y + padding + DMButton::height() + header_gap;
        const int control_w = rect_.w - 2 * padding;
        int control_y = base_y;
        const int gap = DMSpacing::item_gap();

        auto place = [&](auto& widget, int h) {
            if (!widget) {
                return;
            }
            widget->set_rect(SDL_Rect{ base_x, control_y - scroll_, control_w, h });
            control_y += h + gap;
        };

        if (c_is_shaded_) {
            place(c_is_shaded_, DMCheckbox::height());
        }

        if (c_is_shaded_ && c_is_shaded_->value()) {
            place(s_parallax_amount_, DMSlider::height());
            place(s_screen_brightness_, DMSlider::height());
            place(s_opacity_multiplier_, DMSlider::height());
        }

        content_height_ = std::max(0, control_y - base_y);
    }

    bool handle_event(const SDL_Event& e) override {
        bool used = DockableCollapsible::handle_event(e);
        if (!info_ || !expanded_) {
            return used;
        }

        bool shading_toggled = false;
        bool slider_changed = false;

        if (c_is_shaded_ && c_is_shaded_->handle_event(e)) {
            used = true;
            shading_toggled = true;
        }

        if (c_is_shaded_ && c_is_shaded_->value()) {
            auto handle_slider = [&](std::unique_ptr<DMSlider>& slider,
                                     int& stored_value,
                                     int min_v,
                                     int max_v) {
                if (!slider) {
                    return;
                }
                const int previous = stored_value;
                const bool slider_used = slider->handle_event(e);
                const int committed = std::clamp(slider->value(), min_v, max_v);
                if (committed != previous) {
                    stored_value = committed;
                    slider_changed = true;
                    used = true;
                } else if (slider_used) {
                    used = true;
                }
            };

            handle_slider(s_parallax_amount_, parallax_value_, parallax_min_, parallax_max_);
            handle_slider(s_screen_brightness_, brightness_value_, brightness_min_, brightness_max_);
            handle_slider(s_opacity_multiplier_, opacity_value_, opacity_min_, opacity_max_);
        }

        if (shading_toggled) {
            const bool new_state = c_is_shaded_ ? c_is_shaded_->value() : false;
            if (info_->is_shaded != new_state) {
                info_->set_shading_enabled(new_state);
                (void)info_->commit_manifest();
                if (ui_) {
                    ui_->notify_light_sources_modified(false);
                }
            }
        }

        if (slider_changed && info_) {
            apply_slider_values_to_asset();
            (void)info_->commit_manifest();
        }

        return used || shading_toggled || slider_changed;
    }

    void render_content(SDL_Renderer* r) const override {
        if (c_is_shaded_) {
            c_is_shaded_->render(r);
        }
        if (c_is_shaded_ && c_is_shaded_->value()) {
            if (s_parallax_amount_) {
                s_parallax_amount_->render(r);
            }
            if (s_screen_brightness_) {
                s_screen_brightness_->render(r);
            }
            if (s_opacity_multiplier_) {
                s_opacity_multiplier_->render(r);
            }
        }
    }

    bool shading_enabled() const { return c_is_shaded_ && c_is_shaded_->value(); }

private:
    static void configure_ratio_slider(DMSlider& slider, int scale) {
        slider.set_value_formatter([scale](int v, std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) {
            return dev_mode::FormatSliderValue(static_cast<double>(v) / static_cast<double>(scale), 2, buffer);
        });
        slider.set_value_parser([scale](const std::string& text) -> std::optional<int> {
            try {
                double parsed = std::stod(text);
                if (!std::isfinite(parsed)) {
                    return std::nullopt;
                }
                double scaled = parsed * static_cast<double>(scale);
                return static_cast<int>(std::lround(scaled));
            } catch (...) {
                return std::nullopt;
            }
        });
        slider.set_defer_commit_until_unfocus(true);
    }

    static int to_slider_value(float value, int min_v, int max_v) {
        if (!std::isfinite(value)) {
            value = 0.0f;
        }
        int scaled = static_cast<int>(std::lround(static_cast<double>(value) * slider_scale_));
        return std::clamp(scaled, min_v, max_v);
    }

    static float from_slider_value(int value) {
        return static_cast<float>(value) / static_cast<float>(slider_scale_);
    }

    void assign_slider_values_from_asset() {
        if (!info_) {
            return;
        }
        parallax_value_ = to_slider_value(info_->shading_parallax_amount, parallax_min_, parallax_max_);
        brightness_value_ = to_slider_value(
            info_->shading_screen_brightness_multiplier, brightness_min_, brightness_max_);
        opacity_value_ = to_slider_value(info_->shading_opacity_multiplier, opacity_min_, opacity_max_);

        if (s_parallax_amount_) {
            s_parallax_amount_->set_value(parallax_value_);
        }
        if (s_screen_brightness_) {
            s_screen_brightness_->set_value(brightness_value_);
        }
        if (s_opacity_multiplier_) {
            s_opacity_multiplier_->set_value(opacity_value_);
        }
    }

    void apply_slider_values_to_asset() {
        if (!info_) {
            return;
        }
        info_->set_shading_parallax_amount(from_slider_value(parallax_value_));
        info_->set_shading_screen_brightness_multiplier(from_slider_value(brightness_value_));
        info_->set_shading_opacity_multiplier(from_slider_value(opacity_value_));
        assign_slider_values_from_asset();
    }

    static constexpr int slider_scale_ = 100;
    static constexpr int parallax_min_ = 0;
    static constexpr int parallax_max_ = 400;
    static constexpr int brightness_min_ = 0;
    static constexpr int brightness_max_ = 400;
    static constexpr int opacity_min_ = 0;
    static constexpr int opacity_max_ = 400;

    int parallax_value_ = 0;
    int brightness_value_ = slider_scale_;
    int opacity_value_ = slider_scale_;

    std::unique_ptr<DMCheckbox> c_is_shaded_;
    std::unique_ptr<DMSlider> s_parallax_amount_;
    std::unique_ptr<DMSlider> s_screen_brightness_;
    std::unique_ptr<DMSlider> s_opacity_multiplier_;

    AssetInfoUI* ui_ = nullptr;
};
