#include "MapShadowPanel.hpp"

#include "MapLightPanel.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/shared/formatting.hpp"
#include "dev_mode/dev_ui_settings.hpp"
#include "input.hpp"
#include "render/camera.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace {
constexpr std::string_view kReactiveSettingsKey = "dev_ui.lighting.map_panel.reactive";

float clamp_float(float value, float lo, float hi) {
    return std::max(lo, std::min(hi, value));
}

std::unique_ptr<DMSlider> make_float_slider(const std::string& label,
                                            float min_value,
                                            float max_value,
                                            float current,
                                            int scale) {
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

float slider_value_scaled(const std::unique_ptr<DMSlider>& slider, float fallback, int scale) {
    if (!slider) {
        return fallback;
    }
    return static_cast<float>(slider->displayed_value()) / static_cast<float>(scale);
}

void set_slider_scaled(const std::unique_ptr<DMSlider>& slider, float value, int scale) {
    if (!slider) {
        return;
    }
    const int scaled = static_cast<int>(std::round(value * static_cast<float>(scale)));
    slider->set_value(scaled);
}

std::string make_setting_key(std::string_view suffix) {
    std::string key{kReactiveSettingsKey};
    key.push_back('.');
    key.append(suffix);
    return key;
}
}  // namespace

MapShadowPanel::MapShadowPanel(MapLightPanel* light_panel, Assets* assets, int x, int y)
    : DockableCollapsible("Virtual Light Map Shadows (Quadrant-based)", true, x, y),
      light_panel_(light_panel),
      assets_(assets) {
    set_floating_content_width(360);
    set_visible_height(540);
    if (assets_) {
        last_quadrant_count_ = clamp_int(assets_->virtual_light_map_quadrants(),
                                         VirtualLightMap::kMinGridSize,
                                         VirtualLightMap::kMaxGridSize);
        forced_quadrant_snapshot_ = last_quadrant_count_;
    }
    forced_settings_snapshot_ = last_applied_settings_;
    build_ui();
    rebuild_rows();
}

MapShadowPanel::~MapShadowPanel() = default;

void MapShadowPanel::set_map_info(nlohmann::json* map_info, SaveCallback on_save) {
    map_info_ = map_info;
    on_save_ = std::move(on_save);
    if (!reactive_settings_initialized_) {
        last_applied_settings_ = load_reactive_settings_from_dev_settings();
        set_reactive_sliders(last_applied_settings_);
        reactive_settings_initialized_ = true;
    }
    if (assets_) {
        last_quadrant_count_ = clamp_int(assets_->virtual_light_map_quadrants(),
                                         VirtualLightMap::kMinGridSize,
                                         VirtualLightMap::kMaxGridSize);
    }
    if (quadrant_count_) {
        quadrant_count_->set_value(last_quadrant_count_);
    }
    apply_virtual_light_map_quadrants(last_quadrant_count_, false);
    apply_immediate_settings();
    sync_ui_from_json();
}

void MapShadowPanel::set_reactive_settings(render_pipeline::shading::ReactiveShadowSettings* settings) {
    reactive_settings_shared_ = settings;
    if (settings) {
        last_applied_settings_ = render_pipeline::shading::sanitize_reactive_shadow_settings(*settings);
        set_reactive_sliders(last_applied_settings_);
        apply_immediate_settings();
        forced_settings_snapshot_ = last_applied_settings_;
    }
}

void MapShadowPanel::open() {
    DockableCollapsible::open();
    sync_ui_from_json();
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

    if (DockableCollapsible::handle_event(e)) {
        needs_sync_to_json_ = true;
        return true;
    }

    return false;
}

void MapShadowPanel::render(SDL_Renderer* renderer) const {
    if (!is_visible()) {
        return;
    }
    DockableCollapsible::render(renderer);
}

bool MapShadowPanel::is_point_inside(int x, int y) const { return DockableCollapsible::is_point_inside(x, y); }

void MapShadowPanel::render_content(SDL_Renderer* renderer) const {
    DockableCollapsible::render_content(renderer);
}

void MapShadowPanel::layout_custom_content(int, int) const {}

void MapShadowPanel::update_save_status(bool) const {}

void MapShadowPanel::build_ui() {
    horizontal_falloff_ = make_float_slider("Horizontal Falloff", 0.0f, 10.0f,
                                            last_applied_settings_.virtual_light_map.horizontal_falloff, 100);
    vertical_falloff_   = make_float_slider("Vertical Falloff", 0.0f, 10.0f,
                                          last_applied_settings_.virtual_light_map.vertical_falloff, 100);
    max_offset_x_       = make_float_slider("Max Offset X", 0.0f, 500.0f,
                                            last_applied_settings_.virtual_light_map.max_offset_x, 100);
    max_offset_y_       = make_float_slider("Max Offset Y", 0.0f, 500.0f,
                                            last_applied_settings_.virtual_light_map.max_offset_y, 100);
    shadow_scale_       = make_float_slider("Shadow Scale", 0.0f, 10.0f,
                                            last_applied_settings_.virtual_light_map.shadow_scale, 100);
    size_scale_factor_  = make_float_slider("Size Scale Factor", 0.0f, 10.0f,
                                            last_applied_settings_.virtual_light_map.size_scale_factor, 100);
    map_light_factor_   = make_float_slider("Map Light Factor", 0.0f, 1.0f,
                                            last_applied_settings_.virtual_light_map.map_light_factor, 100);

    const int clamped_quadrants = clamp_int(last_quadrant_count_, VirtualLightMap::kMinGridSize,
                                            VirtualLightMap::kMaxGridSize);
    quadrant_count_ = std::make_unique<DMSlider>(
        "Quadrant Resolution", VirtualLightMap::kMinGridSize, VirtualLightMap::kMaxGridSize, clamped_quadrants);
    if (quadrant_count_) {
        quadrant_count_->set_defer_commit_until_unfocus(false);
    }
}

void MapShadowPanel::rebuild_rows() {
    widget_wrappers_.clear();
    widget_wrappers_.reserve(16);

    auto add_widget = [this](std::unique_ptr<Widget> widget) -> Widget* {
        Widget* raw = widget.get();
        widget_wrappers_.push_back(std::move(widget));
        return raw;
    };

    Rows rows;

    if (quadrant_count_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(quadrant_count_.get())) });
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
    if (shadow_scale_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(shadow_scale_.get())) });
    }
    if (size_scale_factor_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(size_scale_factor_.get())) });
    }
    if (map_light_factor_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(map_light_factor_.get())) });
    }

    set_rows(rows);
}

void MapShadowPanel::sync_ui_from_json() {
    if (!map_info_) {
        return;
    }
    auto it = map_info_->find("reactive_shadows");
    if (it != map_info_->end() && it->is_object()) {
        if (auto quad_it = it->find("virtual_light_map_quadrants");
            quad_it != it->end() && quad_it->is_number_integer()) {
            last_quadrant_count_ = clamp_int(quad_it->get<int>(),
                                             VirtualLightMap::kMinGridSize,
                                             VirtualLightMap::kMaxGridSize);
        }
        last_applied_settings_ = render_pipeline::shading::reactive_shadow_settings_from_json(*it, last_applied_settings_);
        last_applied_settings_ = render_pipeline::shading::sanitize_reactive_shadow_settings(last_applied_settings_);
        set_reactive_sliders(last_applied_settings_);
        apply_virtual_light_map_quadrants(last_quadrant_count_, false);
        apply_immediate_settings();
    }
    needs_sync_to_json_ = false;
}

void MapShadowPanel::sync_json_from_ui() {
    if (!map_info_) {
        return;
    }
    render_pipeline::shading::ReactiveShadowSettings settings = current_settings_from_ui();
    int quadrants = last_quadrant_count_;
    if (quadrant_count_) {
        quadrants = clamp_int(quadrant_count_->value(),
                               VirtualLightMap::kMinGridSize,
                               VirtualLightMap::kMaxGridSize);
    }
    apply_virtual_light_map_quadrants(quadrants, true);
    write_reactive_settings_to_json(settings);
    last_applied_settings_ = settings;
    apply_immediate_settings();
    needs_sync_to_json_ = false;
}

void MapShadowPanel::apply_immediate_settings() {
    bool settings_changed = false;
    if (reactive_settings_shared_) {
        auto sanitized = render_pipeline::shading::sanitize_reactive_shadow_settings(last_applied_settings_);
        if (*reactive_settings_shared_ != sanitized) {
            *reactive_settings_shared_ = sanitized;
            settings_changed = true;
        }
        last_applied_settings_ = sanitized;
    }
    persist_reactive_settings_to_dev_settings(last_applied_settings_);
    force_shading_refresh_if_needed(settings_changed);
}

render_pipeline::shading::ReactiveShadowSettings MapShadowPanel::current_settings_from_ui() const {
    render_pipeline::shading::ReactiveShadowSettings settings = last_applied_settings_;
    settings.virtual_light_map.map_light_factor = slider_value_scaled(map_light_factor_, settings.virtual_light_map.map_light_factor, 100);
    settings.virtual_light_map.horizontal_falloff = slider_value_scaled(horizontal_falloff_, settings.virtual_light_map.horizontal_falloff, 100);
    settings.virtual_light_map.vertical_falloff = slider_value_scaled(vertical_falloff_, settings.virtual_light_map.vertical_falloff, 100);
    settings.virtual_light_map.max_offset_x = slider_value_scaled(max_offset_x_, settings.virtual_light_map.max_offset_x, 100);
    settings.virtual_light_map.max_offset_y = slider_value_scaled(max_offset_y_, settings.virtual_light_map.max_offset_y, 100);
    settings.virtual_light_map.shadow_scale = slider_value_scaled(shadow_scale_, settings.virtual_light_map.shadow_scale, 100);
    settings.virtual_light_map.size_scale_factor = slider_value_scaled(size_scale_factor_, settings.virtual_light_map.size_scale_factor, 100);
    return render_pipeline::shading::sanitize_reactive_shadow_settings(settings);
}

void MapShadowPanel::set_reactive_sliders(const render_pipeline::shading::ReactiveShadowSettings& settings) {
    set_slider_scaled(map_light_factor_, settings.virtual_light_map.map_light_factor, 100);
    set_slider_scaled(horizontal_falloff_, settings.virtual_light_map.horizontal_falloff, 100);
    set_slider_scaled(vertical_falloff_, settings.virtual_light_map.vertical_falloff, 100);
    set_slider_scaled(max_offset_x_, settings.virtual_light_map.max_offset_x, 100);
    set_slider_scaled(max_offset_y_, settings.virtual_light_map.max_offset_y, 100);
    set_slider_scaled(shadow_scale_, settings.virtual_light_map.shadow_scale, 100);
    set_slider_scaled(size_scale_factor_, settings.virtual_light_map.size_scale_factor, 100);
    if (quadrant_count_) {
        quadrant_count_->set_value(last_quadrant_count_);
    }
}

render_pipeline::shading::ReactiveShadowSettings MapShadowPanel::load_reactive_settings_from_dev_settings() {
    using devmode::ui_settings::load_number;
    render_pipeline::shading::ReactiveShadowSettings settings = render_pipeline::shading::sanitize_reactive_shadow_settings({});
    settings.virtual_light_map.map_light_factor = static_cast<float>(
        load_number(make_setting_key("virtual_light_map.map_light_factor"), settings.virtual_light_map.map_light_factor));
    settings.virtual_light_map.horizontal_falloff = static_cast<float>(
        load_number(make_setting_key("virtual_light_map.horizontal_falloff"), settings.virtual_light_map.horizontal_falloff));
    settings.virtual_light_map.vertical_falloff = static_cast<float>(
        load_number(make_setting_key("virtual_light_map.vertical_falloff"), settings.virtual_light_map.vertical_falloff));
    settings.virtual_light_map.max_offset_x = static_cast<float>(
        load_number(make_setting_key("virtual_light_map.max_offset_x"), settings.virtual_light_map.max_offset_x));
    settings.virtual_light_map.max_offset_y = static_cast<float>(
        load_number(make_setting_key("virtual_light_map.max_offset_y"), settings.virtual_light_map.max_offset_y));
    settings.virtual_light_map.shadow_scale = static_cast<float>(
        load_number(make_setting_key("virtual_light_map.shadow_scale"), settings.virtual_light_map.shadow_scale));
    settings.virtual_light_map.size_scale_factor = static_cast<float>(
        load_number(make_setting_key("virtual_light_map.size_scale_factor"), settings.virtual_light_map.size_scale_factor));
    int stored_quadrants = static_cast<int>(
        load_number(make_setting_key("virtual_light_map.quadrants"), last_quadrant_count_));
    last_quadrant_count_ = clamp_int(stored_quadrants, VirtualLightMap::kMinGridSize, VirtualLightMap::kMaxGridSize);
    return render_pipeline::shading::sanitize_reactive_shadow_settings(settings);
}

void MapShadowPanel::persist_reactive_settings_to_dev_settings(const render_pipeline::shading::ReactiveShadowSettings& settings) const {
    using devmode::ui_settings::save_number;
    save_number(make_setting_key("virtual_light_map.map_light_factor"), settings.virtual_light_map.map_light_factor);
    save_number(make_setting_key("virtual_light_map.horizontal_falloff"), settings.virtual_light_map.horizontal_falloff);
    save_number(make_setting_key("virtual_light_map.vertical_falloff"), settings.virtual_light_map.vertical_falloff);
    save_number(make_setting_key("virtual_light_map.max_offset_x"), settings.virtual_light_map.max_offset_x);
    save_number(make_setting_key("virtual_light_map.max_offset_y"), settings.virtual_light_map.max_offset_y);
    save_number(make_setting_key("virtual_light_map.shadow_scale"), settings.virtual_light_map.shadow_scale);
    save_number(make_setting_key("virtual_light_map.size_scale_factor"), settings.virtual_light_map.size_scale_factor);
    save_number(make_setting_key("virtual_light_map.quadrants"), last_quadrant_count_);
}

void MapShadowPanel::write_reactive_settings_to_json(const render_pipeline::shading::ReactiveShadowSettings& settings) {
    if (!map_info_) {
        return;
    }
    nlohmann::json& json = (*map_info_)["reactive_shadows"];
    render_pipeline::shading::assign_reactive_shadow_settings(json, settings);
    json["virtual_light_map_quadrants"] = last_quadrant_count_;
}

void MapShadowPanel::apply_virtual_light_map_quadrants(int quadrants, bool force_refresh) {
    const int clamped = clamp_int(quadrants, VirtualLightMap::kMinGridSize, VirtualLightMap::kMaxGridSize);
    if (last_quadrant_count_ == clamped && !force_refresh) {
        return;
    }
    last_quadrant_count_ = clamped;
    if (quadrant_count_) {
        quadrant_count_->set_value(last_quadrant_count_);
    }
    if (assets_) {
        assets_->set_virtual_light_map_quadrants(last_quadrant_count_);
    }
    persist_reactive_settings_to_dev_settings(last_applied_settings_);
    force_shading_refresh_if_needed(force_refresh);
}

void MapShadowPanel::force_shading_refresh_if_needed(bool force_refresh) {
    const bool settings_changed = forced_settings_snapshot_ != last_applied_settings_;
    const bool quadrants_changed = forced_quadrant_snapshot_ != last_quadrant_count_;
    if (!assets_ || (!force_refresh && !settings_changed && !quadrants_changed)) {
        return;
    }
    forced_settings_snapshot_ = last_applied_settings_;
    forced_quadrant_snapshot_ = last_quadrant_count_;
    assets_->force_virtual_light_map_refresh();
    assets_->force_shaded_assets_rerender();
}

nlohmann::json& MapShadowPanel::ensure_reactive_settings_json() {
    if (!map_info_) {
        static nlohmann::json dummy = nlohmann::json::object();
        return dummy;
    }
    return (*map_info_)["reactive_shadows"];
}

int MapShadowPanel::clamp_int(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}
