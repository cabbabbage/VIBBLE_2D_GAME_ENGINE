#include "MapShadowPanel.hpp"

#include "core/AssetsManager.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/shared/formatting.hpp"
#include "dev_mode/widgets.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include <nlohmann/json.hpp>

namespace {

constexpr int kStrengthSliderScale = 100;
constexpr int kFloatSliderScale    = 100;

std::unique_ptr<DMSlider> make_strength_slider(const std::string& label, float value) {
    const int scaled = static_cast<int>(std::round(value * static_cast<float>(kStrengthSliderScale)));
    auto       slider = std::make_unique<DMSlider>(label, 0, 10 * kStrengthSliderScale, scaled);
    slider->set_defer_commit_until_unfocus(false);
    slider->set_value_formatter([](int v, std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) -> std::string_view {
        const float scaled_value = static_cast<float>(v) / static_cast<float>(kStrengthSliderScale);
        return dev_mode::FormatSliderValue(scaled_value, 2, buffer);
    });
    slider->set_value_parser([](const std::string& text) -> std::optional<int> {
        try {
            float parsed = std::stof(text);
            return static_cast<int>(std::round(parsed * static_cast<float>(kStrengthSliderScale)));
        } catch (...) {
            return std::nullopt;
        }
    });
    return slider;
}

std::unique_ptr<DMSlider> make_float_slider(const std::string& label,
                                            float                min_value,
                                            float                max_value,
                                            float                current,
                                            int                  scale = kFloatSliderScale,
                                            int                  decimals = 2) {
    const int min_i = static_cast<int>(std::round(min_value * static_cast<float>(scale)));
    const int max_i = static_cast<int>(std::round(max_value * static_cast<float>(scale)));
    const int cur_i = static_cast<int>(std::round(current * static_cast<float>(scale)));
    auto       slider = std::make_unique<DMSlider>(label, min_i, max_i, cur_i);
    slider->set_defer_commit_until_unfocus(false);
    slider->set_value_formatter([scale, decimals](int value,
                                                 std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) -> std::string_view {
        const float scaled = static_cast<float>(value) / static_cast<float>(scale);
        return dev_mode::FormatSliderValue(scaled, decimals, buffer);
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

float slider_value_strength(const std::unique_ptr<DMSlider>& slider, float fallback) {
    if (!slider) {
        return fallback;
    }
    return static_cast<float>(slider->displayed_value()) / static_cast<float>(kStrengthSliderScale);
}

float shadow_slider_value_scaled(const std::unique_ptr<DMSlider>& slider,
                                 float                         fallback,
                                 int                           scale = kFloatSliderScale) {
    if (!slider) {
        return fallback;
    }
    return static_cast<float>(slider->displayed_value()) / static_cast<float>(scale);
}

void set_slider_strength(const std::unique_ptr<DMSlider>& slider, float value) {
    if (!slider) {
        return;
    }
    const int scaled = static_cast<int>(std::round(value * static_cast<float>(kStrengthSliderScale)));
    slider->set_value(scaled);
}

void set_shadow_slider_scaled(const std::unique_ptr<DMSlider>& slider,
                              float                          value,
                              int                            scale = kFloatSliderScale) {
    if (!slider) {
        return;
    }
    const int scaled = static_cast<int>(std::round(value * static_cast<float>(scale)));
    slider->set_value(scaled);
}

}  // namespace

MapShadowPanel::MapShadowPanel(Assets* assets, int x, int y)
    : DockableCollapsible("Light Map Shadows", true, x, y), assets_(assets) {
    set_floating_content_width(340);
    set_visible_height(280);
    build_ui();
    rebuild_rows();
}

MapShadowPanel::~MapShadowPanel() = default;

void MapShadowPanel::set_map_info(nlohmann::json* map_info, SaveCallback on_save) {
    map_info_ = map_info;
    on_save_  = std::move(on_save);
    sync_ui_from_json();
}

void MapShadowPanel::set_reactive_settings(render_pipeline::shading::ReactiveShadowSettings* settings) {
    reactive_settings_shared_ = settings;
    if (settings) {
        last_settings_ = render_pipeline::shading::sanitize_reactive_shadow_settings(*settings);
        apply_settings_to_sliders(last_settings_);
        apply_settings_to_shared();
    }
}

void MapShadowPanel::open() {
    DockableCollapsible::open();
    setLocked(false);
    force_pointer_ready();
}

void MapShadowPanel::close() { DockableCollapsible::close(); }

void MapShadowPanel::toggle() {
    if (is_visible()) {
        close();
    } else {
        open();
    }
}

bool MapShadowPanel::is_visible() const { return DockableCollapsible::is_visible(); }

void MapShadowPanel::update(const Input& input, int screen_w, int screen_h) {
    DockableCollapsible::update(input, screen_w, screen_h);
    if (!is_visible()) {
        return;
    }
    if (needs_sync_to_json_) {
        sync_json_from_ui();
    }
}

bool MapShadowPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) {
        return false;
    }
    bool handled = DockableCollapsible::handle_event(e);
    if (handled) {
        needs_sync_to_json_ = true;
    }
    return handled;
}

void MapShadowPanel::render(SDL_Renderer* renderer) const {
    if (!is_visible()) {
        return;
    }
    DockableCollapsible::render(renderer);
}

bool MapShadowPanel::is_point_inside(int x, int y) const { return DockableCollapsible::is_point_inside(x, y); }

void MapShadowPanel::build_ui() {
    opacity_strength_  = make_strength_slider("Opacity Strength", last_settings_.opacity_strength);
    parallax_strength_ = make_strength_slider("Parallax Strength", last_settings_.parallax_strength);
    scale_strength_    = make_strength_slider("Scale Strength", last_settings_.scale_strength);
    shadow_scale_      = make_strength_slider("Shadow Scale", last_settings_.virtual_light_map.shadow_scale);

    horizontal_falloff_ = make_float_slider("Horizontal Falloff", 0.0f, 10.0f,
                                            last_settings_.virtual_light_map.horizontal_falloff);
    vertical_falloff_   = make_float_slider("Vertical Falloff", 0.0f, 10.0f,
                                            last_settings_.virtual_light_map.vertical_falloff);
    size_scale_factor_  = make_float_slider("Size Scale Factor", 0.0f, 10.0f,
                                            last_settings_.virtual_light_map.size_scale_factor);

    search_radius_ = std::make_unique<DMSlider>("Search Radius", 0, 64,
                                                last_settings_.virtual_light_map.search_radius);
    search_radius_->set_defer_commit_until_unfocus(false);
}

void MapShadowPanel::rebuild_rows() {
    widget_wrappers_.clear();
    widget_wrappers_.reserve(8);

    auto add_widget = [this](std::unique_ptr<Widget> widget) -> Widget* {
        Widget* raw = widget.get();
        widget_wrappers_.push_back(std::move(widget));
        return raw;
    };

    Rows rows;
    if (opacity_strength_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(opacity_strength_.get())) });
    }
    if (parallax_strength_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(parallax_strength_.get())) });
    }
    if (scale_strength_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(scale_strength_.get())) });
    }
    if (shadow_scale_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(shadow_scale_.get())) });
    }
    if (size_scale_factor_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(size_scale_factor_.get())) });
    }
    if (horizontal_falloff_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(horizontal_falloff_.get())) });
    }
    if (vertical_falloff_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(vertical_falloff_.get())) });
    }
    if (search_radius_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(search_radius_.get())) });
    }

    set_rows(rows);
}

void MapShadowPanel::sync_ui_from_json() {
    if (!map_info_) {
        apply_settings_to_shared();
        needs_sync_to_json_ = false;
        return;
    }
    auto it = map_info_->find("reactive_shadows");
    if (it == map_info_->end() || !it->is_object()) {
        apply_settings_to_shared();
        needs_sync_to_json_ = false;
        return;
    }
    last_settings_ = render_pipeline::shading::reactive_shadow_settings_from_json(*it, last_settings_);
    last_settings_ = render_pipeline::shading::sanitize_reactive_shadow_settings(last_settings_);
    apply_settings_to_sliders(last_settings_);
    apply_settings_to_shared();
    needs_sync_to_json_ = false;
}

void MapShadowPanel::sync_json_from_ui() {
    last_settings_.opacity_strength  = slider_value_strength(opacity_strength_, last_settings_.opacity_strength);
    last_settings_.parallax_strength = slider_value_strength(parallax_strength_, last_settings_.parallax_strength);
    last_settings_.scale_strength    = slider_value_strength(scale_strength_, last_settings_.scale_strength);
    last_settings_.virtual_light_map.shadow_scale = slider_value_strength(shadow_scale_,
                                                                          last_settings_.virtual_light_map.shadow_scale);

    last_settings_.virtual_light_map.horizontal_falloff = shadow_slider_value_scaled(
        horizontal_falloff_, last_settings_.virtual_light_map.horizontal_falloff);
    last_settings_.virtual_light_map.vertical_falloff = shadow_slider_value_scaled(
        vertical_falloff_, last_settings_.virtual_light_map.vertical_falloff);
    last_settings_.virtual_light_map.size_scale_factor = shadow_slider_value_scaled(
        size_scale_factor_, last_settings_.virtual_light_map.size_scale_factor);
    if (search_radius_) {
        last_settings_.virtual_light_map.search_radius = search_radius_->displayed_value();
    }

    last_settings_ = render_pipeline::shading::sanitize_reactive_shadow_settings(last_settings_);

    if (map_info_) {
        nlohmann::json& json = (*map_info_)["reactive_shadows"];
        render_pipeline::shading::assign_reactive_shadow_settings(json, last_settings_);
    }

    apply_settings_to_shared();
    needs_sync_to_json_ = false;
    if (on_save_) {
        on_save_();
    }
}

void MapShadowPanel::apply_settings_to_shared() {
    if (!reactive_settings_shared_) {
        return;
    }
    auto sanitized = render_pipeline::shading::sanitize_reactive_shadow_settings(last_settings_);
    if (*reactive_settings_shared_ != sanitized) {
        *reactive_settings_shared_ = sanitized;
        if (assets_) {
            assets_->force_virtual_light_map_refresh();
            assets_->force_shaded_assets_rerender();
        }
    }
    last_settings_ = sanitized;
}

void MapShadowPanel::apply_settings_to_sliders(const render_pipeline::shading::ReactiveShadowSettings& settings) {
    set_slider_strength(opacity_strength_, settings.opacity_strength);
    set_slider_strength(parallax_strength_, settings.parallax_strength);
    set_slider_strength(scale_strength_, settings.scale_strength);
    set_slider_strength(shadow_scale_, settings.virtual_light_map.shadow_scale);
    set_shadow_slider_scaled(horizontal_falloff_, settings.virtual_light_map.horizontal_falloff);
    set_shadow_slider_scaled(vertical_falloff_, settings.virtual_light_map.vertical_falloff);
    set_shadow_slider_scaled(size_scale_factor_, settings.virtual_light_map.size_scale_factor);
    if (search_radius_) {
        search_radius_->set_value(settings.virtual_light_map.search_radius);
    }
}

void MapShadowPanel::render_content(SDL_Renderer* renderer) const {
    DockableCollapsible::render_content(renderer);
}

void MapShadowPanel::layout_custom_content(int, int) const {}
