#include "MapShadowPanel.hpp"

#include "MapLightPanel.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/shared/formatting.hpp"
#include "dev_mode/widgets.hpp"
#include "render/light_map.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <optional>
#include <nlohmann/json.hpp>

namespace {

constexpr int kStrengthSliderScale = 100;

std::unique_ptr<DMSlider> make_shadow_float_slider(const std::string& label,
                                                   float                min_value,
                                                   float                max_value,
                                                   float                current,
                                                   int                  scale) {
    const int min_i = static_cast<int>(std::round(min_value * static_cast<float>(scale)));
    const int max_i = static_cast<int>(std::round(max_value * static_cast<float>(scale)));
    const int cur_i = static_cast<int>(std::round(current * static_cast<float>(scale)));
    auto       slider = std::make_unique<DMSlider>(label, min_i, max_i, cur_i);
    slider->set_defer_commit_until_unfocus(false);
    slider->set_value_formatter([scale](int value, std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) -> std::string_view {
        const float scaled = static_cast<float>(value) / static_cast<float>(scale);
        return dev_mode::FormatSliderValue(scaled, 2, buffer);
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

float shadow_slider_value_scaled(const std::unique_ptr<DMSlider>& slider, float fallback, int scale) {
    if (!slider) {
        return fallback;
    }
    return static_cast<float>(slider->displayed_value()) / static_cast<float>(scale);
}

void shadow_set_slider_scaled(const std::unique_ptr<DMSlider>& slider, float value, int scale) {
    if (!slider) return;
    const int scaled = static_cast<int>(std::round(value * static_cast<float>(scale)));
    slider->set_value(scaled);
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
        shadow_set_slider_scaled(horizontal_falloff_, last_settings_.virtual_light_map.horizontal_falloff, 100);
        shadow_set_slider_scaled(vertical_falloff_,   last_settings_.virtual_light_map.vertical_falloff,   100);
        shadow_set_slider_scaled(max_offset_x_,       last_settings_.virtual_light_map.max_offset_x,       100);
        shadow_set_slider_scaled(max_offset_y_,       last_settings_.virtual_light_map.max_offset_y,       100);
        shadow_set_slider_scaled(map_light_factor_,   last_settings_.virtual_light_map.map_light_factor,   100);
        if (search_radius_) search_radius_->set_value(last_settings_.virtual_light_map.search_radius);
        int init_quad = assets_ ? assets_->virtual_light_map_quadrant_size() : last_quadrant_size_px_;
        if (init_quad <= 0) init_quad = LightMap::kDefaultQuadrantSizePx;
        init_quad = std::clamp(init_quad, LightMap::kMinQuadrantSizePx, LightMap::kMaxQuadrantSizePx);
        last_quadrant_size_px_ = init_quad;
        if (quadrant_size_px_) quadrant_size_px_->set_value(last_quadrant_size_px_);
        apply_settings_to_shared();
    }
}

void MapShadowPanel::open() {
    DockableCollapsible::open();
    // Ensure controls are enabled/unlocked when opening in Map Mode
    setLocked(false);
    // Allow immediate interaction after opening (avoid initial click suppression)
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
    scale_strength_    = make_strength_slider("Scale Factor", last_settings_.scale_strength);

    // Virtual light map controls moved from preview panel
    horizontal_falloff_ = make_shadow_float_slider("Horizontal Falloff", 0.0f, 10.0f,
                                            last_settings_.virtual_light_map.horizontal_falloff, 100);
    vertical_falloff_   = make_shadow_float_slider("Vertical Falloff", 0.0f, 10.0f,
                                            last_settings_.virtual_light_map.vertical_falloff, 100);
    max_offset_x_       = make_shadow_float_slider("Max Offset X", 0.0f, 500.0f,
                                            last_settings_.virtual_light_map.max_offset_x, 100);
    max_offset_y_       = make_shadow_float_slider("Max Offset Y", 0.0f, 500.0f,
                                            last_settings_.virtual_light_map.max_offset_y, 100);
    map_light_factor_   = make_shadow_float_slider("Map Light Factor", 0.0f, 1.0f,
                                            last_settings_.virtual_light_map.map_light_factor, 100);
    search_radius_      = std::make_unique<DMSlider>("Search Radius", 0, 64,
                                                     last_settings_.virtual_light_map.search_radius);
    if (search_radius_) search_radius_->set_defer_commit_until_unfocus(false);

    // Quadrant Size no longer exposed in UI, keep regenerate to force refresh if needed
    int init_quad_size = last_quadrant_size_px_;
    if (init_quad_size <= 0) {
        init_quad_size = assets_ ? assets_->virtual_light_map_quadrant_size() : LightMap::kDefaultQuadrantSizePx;
    }
    init_quad_size = std::clamp(init_quad_size, LightMap::kMinQuadrantSizePx, LightMap::kMaxQuadrantSizePx);
    last_quadrant_size_px_ = init_quad_size;
    regenerate_button_ = std::make_unique<DMButton>("Regenerate", &DMStyles::AccentButton(), 160, DMButton::height());
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
    // Prioritize key virtual light map controls near the top
    if (search_radius_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(search_radius_.get())) });
    }
    if (map_light_factor_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(map_light_factor_.get())) });
    }

    if (opacity_strength_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(opacity_strength_.get())) });
    }
    if (parallax_strength_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(parallax_strength_.get())) });
    }
    if (scale_strength_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(scale_strength_.get())) });
    }

    // Virtual Light Map block
    if (regenerate_button_) {
        rows.push_back({ add_widget(std::make_unique<ButtonWidget>(regenerate_button_.get(), [this]() {
            this->apply_virtual_light_map_quadrant_size(this->last_quadrant_size_px_, true);
        })) });
    }
    if (horizontal_falloff_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(horizontal_falloff_.get())) });
    }
    if (vertical_falloff_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(vertical_falloff_.get())) });
    }
    if (max_offset_x_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(max_offset_x_.get())) });
    }
    if (max_offset_y_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(max_offset_y_.get())) });
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
    // Update sliders from JSON
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
    shadow_set_slider_scaled(horizontal_falloff_, last_settings_.virtual_light_map.horizontal_falloff, 100);
    shadow_set_slider_scaled(vertical_falloff_,   last_settings_.virtual_light_map.vertical_falloff,   100);
    shadow_set_slider_scaled(max_offset_x_,       last_settings_.virtual_light_map.max_offset_x,       100);
    shadow_set_slider_scaled(max_offset_y_,       last_settings_.virtual_light_map.max_offset_y,       100);
    shadow_set_slider_scaled(map_light_factor_,   last_settings_.virtual_light_map.map_light_factor,   100);
    if (search_radius_) search_radius_->set_value(last_settings_.virtual_light_map.search_radius);

    // Quadrant size from JSON or assets
    int desired_size = last_quadrant_size_px_ > 0 ? last_quadrant_size_px_ : (assets_ ? assets_->virtual_light_map_quadrant_size() : LightMap::kDefaultQuadrantSizePx);
    if (auto size_it = it->find("virtual_light_map_quadrant_size"); size_it != it->end() && size_it->is_number_integer()) {
        desired_size = size_it->get<int>();
    }
    desired_size = std::clamp(desired_size, LightMap::kMinQuadrantSizePx, LightMap::kMaxQuadrantSizePx);
    last_quadrant_size_px_ = desired_size;
    if (quadrant_size_px_) quadrant_size_px_->set_value(last_quadrant_size_px_);
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
    // Virtual light map
    last_settings_.virtual_light_map.horizontal_falloff = shadow_slider_value_scaled(horizontal_falloff_, last_settings_.virtual_light_map.horizontal_falloff, 100);
    last_settings_.virtual_light_map.vertical_falloff   = shadow_slider_value_scaled(vertical_falloff_,   last_settings_.virtual_light_map.vertical_falloff,   100);
    last_settings_.virtual_light_map.max_offset_x       = shadow_slider_value_scaled(max_offset_x_,       last_settings_.virtual_light_map.max_offset_x,       100);
    last_settings_.virtual_light_map.max_offset_y       = shadow_slider_value_scaled(max_offset_y_,       last_settings_.virtual_light_map.max_offset_y,       100);
    last_settings_.virtual_light_map.map_light_factor   = shadow_slider_value_scaled(map_light_factor_,   last_settings_.virtual_light_map.map_light_factor,   100);
    if (search_radius_) last_settings_.virtual_light_map.search_radius = search_radius_->displayed_value();
    // Quadrant size: only mark pending unless applied via button
    int desired_size = last_quadrant_size_px_;
    if (quadrant_size_px_) {
        desired_size = std::clamp(quadrant_size_px_->value(), LightMap::kMinQuadrantSizePx, LightMap::kMaxQuadrantSizePx);
    }
    if (desired_size != last_quadrant_size_px_) {
        last_quadrant_size_px_ = desired_size;
        request_light_map_regeneration();
    }
    last_settings_ = render_pipeline::shading::sanitize_reactive_shadow_settings(last_settings_);

    nlohmann::json& json = (*map_info_)["reactive_shadows"];
    render_pipeline::shading::assign_reactive_shadow_settings(json, last_settings_);
    json["virtual_light_map_quadrant_size"] = last_quadrant_size_px_;

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
            // Refresh both the light map (sampling depends on settings) and shaded textures
            assets_->force_virtual_light_map_refresh();
            assets_->force_shaded_assets_rerender();
        }
    }
    last_settings_ = sanitized;
}

void MapShadowPanel::render_content(SDL_Renderer* renderer) const {
    DockableCollapsible::render_content(renderer);
}

void MapShadowPanel::layout_custom_content(int, int) const {}

void MapShadowPanel::apply_virtual_light_map_quadrant_size(int size_px, bool apply_to_assets, bool mark_pending) {
    const int clamped = std::clamp(size_px, LightMap::kMinQuadrantSizePx, LightMap::kMaxQuadrantSizePx);
    const bool changed = (last_quadrant_size_px_ != clamped);
    last_quadrant_size_px_ = clamped;
    if (quadrant_size_px_) quadrant_size_px_->set_value(last_quadrant_size_px_);

    if (apply_to_assets) {
        pending_light_map_regeneration_ = false;
        if (regenerate_button_) regenerate_button_->set_text("Regenerate");
        if (assets_) {
            assets_->set_virtual_light_map_quadrant_size(last_quadrant_size_px_);
            assets_->force_virtual_light_map_refresh();
            assets_->force_shaded_assets_rerender();
        }
    } else if (changed && mark_pending) {
        request_light_map_regeneration();
    }
}

void MapShadowPanel::request_light_map_regeneration() {
    pending_light_map_regeneration_ = true;
    if (regenerate_button_) regenerate_button_->set_text("Regenerate*");
}
