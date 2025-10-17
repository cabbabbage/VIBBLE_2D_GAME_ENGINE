#include "MapShadowPanel.hpp"

#include "MapLightPanel.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/shared/formatting.hpp"
#include "dev_mode/widgets.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"

#include <array>
#include <cmath>
#include <optional>
#include <nlohmann/json.hpp>

namespace {

constexpr int kStrengthSliderScale = 100;

std::unique_ptr<DMSlider> make_strength_slider(const std::string& label, float value) {
    const int scaled = static_cast<int>(std::round(value * static_cast<float>(kStrengthSliderScale)));
    auto       slider = std::make_unique<DMSlider>(label,
                                            0,
                                            10 * kStrengthSliderScale,
                                            scaled);
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

float slider_value(const std::unique_ptr<DMSlider>& slider, float fallback) {
    if (!slider) {
        return fallback;
    }
    return static_cast<float>(slider->displayed_value()) / static_cast<float>(kStrengthSliderScale);
}

}  // namespace

MapShadowPanel::MapShadowPanel(MapLightPanel* light_panel, Assets* assets, int x, int y)
    : DockableCollapsible("Light Map Shadows", true, x, y),
      light_panel_(light_panel),
      assets_(assets) {
    set_floating_content_width(320);
    set_visible_height(220);
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
        if (opacity_strength_) {
            opacity_strength_->set_value(static_cast<int>(std::round(last_settings_.opacity_strength *
                                                                     static_cast<float>(kStrengthSliderScale))));
        }
        if (parallax_strength_) {
            parallax_strength_->set_value(static_cast<int>(std::round(last_settings_.parallax_strength *
                                                                       static_cast<float>(kStrengthSliderScale))));
        }
        if (scale_strength_) {
            scale_strength_->set_value(static_cast<int>(std::round(last_settings_.scale_strength *
                                                                   static_cast<float>(kStrengthSliderScale))));
        }
        apply_settings_to_shared();
    }
}

void MapShadowPanel::open() { DockableCollapsible::open(); }

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
    scale_strength_    = make_strength_slider("Scale Factor", last_settings_.scale_strength);
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

    set_rows(rows);
}

void MapShadowPanel::sync_ui_from_json() {
    if (!map_info_) {
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
    if (opacity_strength_) {
        opacity_strength_->set_value(static_cast<int>(std::round(last_settings_.opacity_strength *
                                                                 static_cast<float>(kStrengthSliderScale))));
    }
    if (parallax_strength_) {
        parallax_strength_->set_value(static_cast<int>(std::round(last_settings_.parallax_strength *
                                                                   static_cast<float>(kStrengthSliderScale))));
    }
    if (scale_strength_) {
        scale_strength_->set_value(static_cast<int>(std::round(last_settings_.scale_strength *
                                                               static_cast<float>(kStrengthSliderScale))));
    }
    apply_settings_to_shared();
    needs_sync_to_json_ = false;
}

void MapShadowPanel::sync_json_from_ui() {
    if (!map_info_) {
        needs_sync_to_json_ = false;
        return;
    }
    last_settings_.opacity_strength  = slider_value(opacity_strength_, last_settings_.opacity_strength);
    last_settings_.parallax_strength = slider_value(parallax_strength_, last_settings_.parallax_strength);
    last_settings_.scale_strength    = slider_value(scale_strength_, last_settings_.scale_strength);
    last_settings_ = render_pipeline::shading::sanitize_reactive_shadow_settings(last_settings_);

    nlohmann::json& json = (*map_info_)["reactive_shadows"];
    json["opacity_strength"]  = last_settings_.opacity_strength;
    json["parallax_strength"] = last_settings_.parallax_strength;
    json["scale_strength"]    = last_settings_.scale_strength;

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
            assets_->force_shaded_assets_rerender();
        }
    }
    last_settings_ = sanitized;
}

void MapShadowPanel::render_content(SDL_Renderer* renderer) const {
    DockableCollapsible::render_content(renderer);
}

void MapShadowPanel::layout_custom_content(int, int) const {}
