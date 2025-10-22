#include "MapShadowPanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/AssetsManager.hpp"
#include "dev_mode/shared/formatting.hpp"
#include "dev_mode/widgets.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"
#include "utils/input.hpp"

namespace map_shadow_panel_detail {

constexpr int kPrecisionScale = 100;

bool nearly_equal(float a, float b, float epsilon = 0.0005f) {
    return std::fabs(a - b) <= epsilon;
}

std::unique_ptr<DMSlider> make_scaled_slider(const std::string& label,
                                             float                min_value,
                                             float                max_value,
                                             float                current_value,
                                             int                  scale,
                                             int                  precision = 2) {
    const int min_scaled = static_cast<int>(std::floor(min_value * static_cast<float>(scale)));
    const int max_scaled = static_cast<int>(std::ceil(max_value * static_cast<float>(scale)));
    int       value      = static_cast<int>(std::round(current_value * static_cast<float>(scale)));
    value                = std::clamp(value, min_scaled, max_scaled);
    auto slider          = std::make_unique<DMSlider>(label, min_scaled, max_scaled, value);
    slider->set_defer_commit_until_unfocus(false);
    slider->set_value_formatter([scale, precision](int value,
                                                   std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) {
        const float scaled = static_cast<float>(value) / static_cast<float>(scale);
        return dev_mode::FormatSliderValue(scaled, precision, buffer);
    });
    slider->set_value_parser([scale](const std::string& text) -> std::optional<int> {
        try {
            float parsed = std::stof(text);
            return static_cast<int>(std::round(parsed * static_cast<float>(scale)));
        } catch (...) {
            return std::nullopt;
        }
    });
    return slider;
}

std::unique_ptr<DMSlider> make_integer_slider(const std::string& label,
                                              int                  min_value,
                                              int                  max_value,
                                              int                  current_value) {
    int value = std::clamp(current_value, min_value, max_value);
    auto slider = std::make_unique<DMSlider>(label, min_value, max_value, value);
    slider->set_defer_commit_until_unfocus(false);
    return slider;
}

float slider_value_scaled(const std::unique_ptr<DMSlider>& slider, float fallback, int scale) {
    if (!slider) {
        return fallback;
    }
    return static_cast<float>(slider->displayed_value()) / static_cast<float>(scale);
}

int slider_value_int(const std::unique_ptr<DMSlider>& slider, int fallback) {
    if (!slider) {
        return fallback;
    }
    return slider->displayed_value();
}

void set_slider_scaled(const std::unique_ptr<DMSlider>& slider, float value, int scale) {
    if (!slider) {
        return;
    }
    const int scaled = static_cast<int>(std::round(value * static_cast<float>(scale)));
    slider->set_value(scaled);
}

void set_slider_int(const std::unique_ptr<DMSlider>& slider, int value) {
    if (!slider) {
        return;
    }
    slider->set_value(value);
}

}  // namespace map_shadow_panel_detail

namespace mspd = map_shadow_panel_detail;

MapShadowPanel::MapShadowPanel(Assets* assets, int x, int y)
    : DockableCollapsible("Shading", true, x, y), assets_(assets) {
    set_expanded(true);
    set_slider_defaults();
    build_ui();
}

MapShadowPanel::~MapShadowPanel() = default;

void MapShadowPanel::set_slider_defaults() {
    horizontal_falloff_   = mspd::make_scaled_slider("Horizontal Falloff", 0.0f, 10.0f, 1.0f, mspd::kPrecisionScale);
    vertical_falloff_     = mspd::make_scaled_slider("Vertical Falloff", 0.0f, 10.0f, 1.0f, mspd::kPrecisionScale);
    max_offset_x_         = mspd::make_scaled_slider("Max Offset X", 0.0f, 500.0f, 0.0f, 10, 1);
    max_offset_y_         = mspd::make_scaled_slider("Max Offset Y", 0.0f, 500.0f, 0.0f, 10, 1);
    shadow_scale_         = mspd::make_scaled_slider("Shadow Scale", 0.0f, 10.0f, 1.0f, mspd::kPrecisionScale);
    size_scale_factor_    = mspd::make_scaled_slider("Size Scale Factor", 0.0f, 10.0f, 1.0f, mspd::kPrecisionScale);
    search_radius_        = mspd::make_integer_slider("Search Radius", 0, 64, 2);
    opacity_strength_     = mspd::make_scaled_slider("Opacity Strength", 0.0f, 10.0f, 1.0f, mspd::kPrecisionScale);
    parallax_strength_    = mspd::make_scaled_slider("Parallax Strength", 0.0f, 10.0f, 1.0f, mspd::kPrecisionScale);
    scale_strength_       = mspd::make_scaled_slider("Scale Strength", 0.0f, 10.0f, 1.0f, mspd::kPrecisionScale);
    static_weight_        = mspd::make_scaled_slider("Static Sample Weight", 0.0f, 10.0f, 0.8f, mspd::kPrecisionScale);
    dynamic_weight_       = mspd::make_scaled_slider("Dynamic Sample Weight", 0.0f, 10.0f, 1.0f, mspd::kPrecisionScale);

    mask_expansion_ratio_  = mspd::make_scaled_slider("Mask Expansion Ratio", 0.0f, 4.0f, 0.8f, mspd::kPrecisionScale);
    mask_blur_scale_       = mspd::make_scaled_slider("Mask Blur Scale", 0.0f, 8.0f, 1.0f, mspd::kPrecisionScale);
    mask_falloff_start_    = mspd::make_scaled_slider("Mask Falloff Start", 0.0f, 0.99f, 0.0f, mspd::kPrecisionScale);
    mask_falloff_exponent_ = mspd::make_scaled_slider("Mask Falloff Exponent", 0.01f, 20.0f, 1.05f, mspd::kPrecisionScale);
    mask_alpha_multiplier_ = mspd::make_scaled_slider("Mask Alpha Multiplier", 0.0f, 4.0f, 1.0f, mspd::kPrecisionScale);
}

void MapShadowPanel::build_ui() {
    rebuild_rows();
}

void MapShadowPanel::rebuild_rows() {
    widget_wrappers_.clear();
    Rows rows;

    auto add_slider_row = [&](std::unique_ptr<DMSlider>& slider) {
        if (!slider) {
            return;
        }
        auto widget = std::make_unique<SliderWidget>(slider.get());
        rows.push_back({widget.get()});
        widget_wrappers_.push_back(std::move(widget));
    };

    add_slider_row(horizontal_falloff_);
    add_slider_row(vertical_falloff_);
    add_slider_row(max_offset_x_);
    add_slider_row(max_offset_y_);
    add_slider_row(shadow_scale_);
    add_slider_row(size_scale_factor_);
    add_slider_row(search_radius_);
    add_slider_row(opacity_strength_);
    add_slider_row(parallax_strength_);
    add_slider_row(scale_strength_);
    add_slider_row(static_weight_);
    add_slider_row(dynamic_weight_);

    add_slider_row(mask_expansion_ratio_);
    add_slider_row(mask_blur_scale_);
    add_slider_row(mask_falloff_start_);
    add_slider_row(mask_falloff_exponent_);
    add_slider_row(mask_alpha_multiplier_);

    set_rows(rows);
}

void MapShadowPanel::set_map_info(nlohmann::json* map_info, SaveCallback on_save) {
    map_info_ = map_info;
    on_save_  = std::move(on_save);
    sync_ui_from_json();
}

void MapShadowPanel::set_reactive_settings(render_pipeline::shading::ReactiveShadowSettings* settings) {
    reactive_settings_shared_ = settings;
    if (settings) {
        last_applied_settings_   = render_pipeline::shading::sanitize_reactive_shadow_settings(*settings);
        forced_settings_snapshot_ = last_applied_settings_;
        set_reactive_sliders(last_applied_settings_);
        apply_immediate_settings(true);
    }
}

void MapShadowPanel::open() {
    set_visible(true);
    set_expanded(true);
    setLocked(false);
}

void MapShadowPanel::close() {
    set_visible(false);
}

void MapShadowPanel::toggle() {
    if (is_visible()) {
        close();
    } else {
        open();
    }
}

bool MapShadowPanel::is_visible() const {
    return DockableCollapsible::is_visible();
}

void MapShadowPanel::update(const Input& input, int screen_w, int screen_h) {
    DockableCollapsible::update(input, screen_w, screen_h);
    sync_shared_reactive_settings();
}

bool MapShadowPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) {
        return false;
    }

    bool used = DockableCollapsible::handle_event(e);
    if (!used) {
        return false;
    }

    bool changed = sync_json_from_ui();
    return used || changed;
}

void MapShadowPanel::render(SDL_Renderer* renderer) const {
    if (!is_visible()) {
        return;
    }
    DockableCollapsible::render(renderer);
}

bool MapShadowPanel::is_point_inside(int x, int y) const {
    return DockableCollapsible::is_point_inside(x, y);
}

void MapShadowPanel::render_content(SDL_Renderer* renderer) const {
    DockableCollapsible::render_content(renderer);
}

void MapShadowPanel::sync_shared_reactive_settings() {
    if (!reactive_settings_shared_) {
        return;
    }

    auto sanitized = render_pipeline::shading::sanitize_reactive_shadow_settings(*reactive_settings_shared_);
    if (sanitized == last_applied_settings_) {
        return;
    }

    last_applied_settings_ = sanitized;
    set_reactive_sliders(last_applied_settings_);
    write_reactive_settings_to_json(last_applied_settings_);
    apply_immediate_settings(true);
}

void MapShadowPanel::sync_ui_from_json() {
    if (!map_info_ || !map_info_->is_object()) {
        return;
    }

    if (map_info_->contains("reactive_shadows")) {
        const nlohmann::json& json = (*map_info_)["reactive_shadows"];
        last_applied_settings_     = render_pipeline::shading::reactive_shadow_settings_from_json(
            json, last_applied_settings_);
        last_applied_settings_     = render_pipeline::shading::sanitize_reactive_shadow_settings(last_applied_settings_);
        forced_settings_snapshot_  = last_applied_settings_;
        set_reactive_sliders(last_applied_settings_);
        apply_immediate_settings(true);
    }

    ShadowMaskSettings mask_settings = last_shadow_mask_settings_;
    if (map_info_->contains("shadow_mask_settings") && (*map_info_)["shadow_mask_settings"].is_object()) {
        const nlohmann::json& json = (*map_info_)["shadow_mask_settings"];
        mask_settings.expansion_ratio  = json.value("expansion_ratio", mask_settings.expansion_ratio);
        mask_settings.blur_scale       = json.value("blur_scale", mask_settings.blur_scale);
        mask_settings.falloff_start    = json.value("falloff_start", mask_settings.falloff_start);
        mask_settings.falloff_exponent = json.value("falloff_exponent", mask_settings.falloff_exponent);
        mask_settings.alpha_multiplier = json.value("alpha_multiplier", mask_settings.alpha_multiplier);
        mask_settings                  = SanitizeShadowMaskSettings(mask_settings);
    } else {
        mask_settings = SanitizeShadowMaskSettings(mask_settings);
    }
    last_shadow_mask_settings_ = mask_settings;
    set_shadow_mask_sliders(mask_settings);
}

bool MapShadowPanel::sync_json_from_ui() {
    if (!map_info_ || !map_info_->is_object()) {
        return false;
    }

    bool changed = false;

    render_pipeline::shading::ReactiveShadowSettings current = current_settings_from_ui();
    current = render_pipeline::shading::sanitize_reactive_shadow_settings(current);
    if (current != last_applied_settings_) {
        last_applied_settings_ = current;
        write_reactive_settings_to_json(last_applied_settings_);
        apply_immediate_settings();
        changed = true;
    }

    ShadowMaskSettings mask = current_shadow_mask_from_ui();
    mask                    = SanitizeShadowMaskSettings(mask);

    auto mask_differs = [&]() {
        return !mspd::nearly_equal(mask.expansion_ratio, last_shadow_mask_settings_.expansion_ratio) ||
               !mspd::nearly_equal(mask.blur_scale, last_shadow_mask_settings_.blur_scale) ||
               !mspd::nearly_equal(mask.falloff_start, last_shadow_mask_settings_.falloff_start) ||
               !mspd::nearly_equal(mask.falloff_exponent, last_shadow_mask_settings_.falloff_exponent) ||
               !mspd::nearly_equal(mask.alpha_multiplier, last_shadow_mask_settings_.alpha_multiplier);
    };

    if (mask_differs()) {
        last_shadow_mask_settings_ = mask;
        write_shadow_mask_settings_to_json(last_shadow_mask_settings_);
        apply_immediate_settings(true);
        changed = true;
    }

    return changed;
}

render_pipeline::shading::ReactiveShadowSettings MapShadowPanel::current_settings_from_ui() const {
    auto settings = last_applied_settings_;
    settings.virtual_light_map.horizontal_falloff =
        mspd::slider_value_scaled(horizontal_falloff_, settings.virtual_light_map.horizontal_falloff, mspd::kPrecisionScale);
    settings.virtual_light_map.vertical_falloff =
        mspd::slider_value_scaled(vertical_falloff_, settings.virtual_light_map.vertical_falloff, mspd::kPrecisionScale);
    settings.virtual_light_map.max_offset_x = mspd::slider_value_scaled(max_offset_x_, settings.virtual_light_map.max_offset_x, 10);
    settings.virtual_light_map.max_offset_y = mspd::slider_value_scaled(max_offset_y_, settings.virtual_light_map.max_offset_y, 10);
    settings.virtual_light_map.shadow_scale =
        mspd::slider_value_scaled(shadow_scale_, settings.virtual_light_map.shadow_scale, mspd::kPrecisionScale);
    settings.virtual_light_map.size_scale_factor =
        mspd::slider_value_scaled(size_scale_factor_, settings.virtual_light_map.size_scale_factor, mspd::kPrecisionScale);
    settings.virtual_light_map.search_radius = mspd::slider_value_int(search_radius_, settings.virtual_light_map.search_radius);
    settings.opacity_strength = mspd::slider_value_scaled(opacity_strength_, settings.opacity_strength, mspd::kPrecisionScale);
    settings.parallax_strength = mspd::slider_value_scaled(parallax_strength_, settings.parallax_strength, mspd::kPrecisionScale);
    settings.scale_strength    = mspd::slider_value_scaled(scale_strength_, settings.scale_strength, mspd::kPrecisionScale);
    settings.sampling_weights.static_weight =
        mspd::slider_value_scaled(static_weight_, settings.sampling_weights.static_weight, mspd::kPrecisionScale);
    settings.sampling_weights.dynamic_weight =
        mspd::slider_value_scaled(dynamic_weight_, settings.sampling_weights.dynamic_weight, mspd::kPrecisionScale);
    return render_pipeline::shading::sanitize_reactive_shadow_settings(settings);
}

ShadowMaskSettings MapShadowPanel::current_shadow_mask_from_ui() const {
    ShadowMaskSettings settings = last_shadow_mask_settings_;
    settings.expansion_ratio    = mspd::slider_value_scaled(mask_expansion_ratio_, settings.expansion_ratio, mspd::kPrecisionScale);
    settings.blur_scale         = mspd::slider_value_scaled(mask_blur_scale_, settings.blur_scale, mspd::kPrecisionScale);
    settings.falloff_start      = mspd::slider_value_scaled(mask_falloff_start_, settings.falloff_start, mspd::kPrecisionScale);
    settings.falloff_exponent   = mspd::slider_value_scaled(mask_falloff_exponent_, settings.falloff_exponent, mspd::kPrecisionScale);
    settings.alpha_multiplier   = mspd::slider_value_scaled(mask_alpha_multiplier_, settings.alpha_multiplier, mspd::kPrecisionScale);
    return settings;
}

void MapShadowPanel::set_reactive_sliders(const render_pipeline::shading::ReactiveShadowSettings& settings) {
    mspd::set_slider_scaled(horizontal_falloff_, settings.virtual_light_map.horizontal_falloff, mspd::kPrecisionScale);
    mspd::set_slider_scaled(vertical_falloff_, settings.virtual_light_map.vertical_falloff, mspd::kPrecisionScale);
    mspd::set_slider_scaled(max_offset_x_, settings.virtual_light_map.max_offset_x, 10);
    mspd::set_slider_scaled(max_offset_y_, settings.virtual_light_map.max_offset_y, 10);
    mspd::set_slider_scaled(shadow_scale_, settings.virtual_light_map.shadow_scale, mspd::kPrecisionScale);
    mspd::set_slider_scaled(size_scale_factor_, settings.virtual_light_map.size_scale_factor, mspd::kPrecisionScale);
    mspd::set_slider_int(search_radius_, settings.virtual_light_map.search_radius);
    mspd::set_slider_scaled(opacity_strength_, settings.opacity_strength, mspd::kPrecisionScale);
    mspd::set_slider_scaled(parallax_strength_, settings.parallax_strength, mspd::kPrecisionScale);
    mspd::set_slider_scaled(scale_strength_, settings.scale_strength, mspd::kPrecisionScale);
    mspd::set_slider_scaled(static_weight_, settings.sampling_weights.static_weight, mspd::kPrecisionScale);
    mspd::set_slider_scaled(dynamic_weight_, settings.sampling_weights.dynamic_weight, mspd::kPrecisionScale);
}

void MapShadowPanel::set_shadow_mask_sliders(const ShadowMaskSettings& settings) {
    mspd::set_slider_scaled(mask_expansion_ratio_, settings.expansion_ratio, mspd::kPrecisionScale);
    mspd::set_slider_scaled(mask_blur_scale_, settings.blur_scale, mspd::kPrecisionScale);
    mspd::set_slider_scaled(mask_falloff_start_, settings.falloff_start, mspd::kPrecisionScale);
    mspd::set_slider_scaled(mask_falloff_exponent_, settings.falloff_exponent, mspd::kPrecisionScale);
    mspd::set_slider_scaled(mask_alpha_multiplier_, settings.alpha_multiplier, mspd::kPrecisionScale);
}

void MapShadowPanel::write_reactive_settings_to_json(
    const render_pipeline::shading::ReactiveShadowSettings& settings) {
    if (!map_info_ || !map_info_->is_object()) {
        return;
    }
    nlohmann::json& json = (*map_info_)["reactive_shadows"];
    render_pipeline::shading::assign_reactive_shadow_settings(json, settings);
}

void MapShadowPanel::write_shadow_mask_settings_to_json(const ShadowMaskSettings& settings) {
    if (!map_info_ || !map_info_->is_object()) {
        return;
    }
    nlohmann::json& json = (*map_info_)["shadow_mask_settings"];
    json                  = nlohmann::json::object();
    json["expansion_ratio"]  = settings.expansion_ratio;
    json["blur_scale"]       = settings.blur_scale;
    json["falloff_start"]    = settings.falloff_start;
    json["falloff_exponent"] = settings.falloff_exponent;
    json["alpha_multiplier"] = settings.alpha_multiplier;
}

void MapShadowPanel::apply_immediate_settings(bool force_refresh) {
    bool reactive_changed = false;
    if (reactive_settings_shared_) {
        auto sanitized = render_pipeline::shading::sanitize_reactive_shadow_settings(last_applied_settings_);
        if (*reactive_settings_shared_ != sanitized) {
            *reactive_settings_shared_ = sanitized;
            reactive_changed           = true;
        }
        last_applied_settings_ = sanitized;
    }

    const bool settings_changed = reactive_changed || force_refresh ||
                                  (forced_settings_snapshot_ != last_applied_settings_);
    if (settings_changed) {
        forced_settings_snapshot_ = last_applied_settings_;
        if (assets_) {
            assets_->force_shaded_assets_rerender();
        }
    }
}

*** End of File
