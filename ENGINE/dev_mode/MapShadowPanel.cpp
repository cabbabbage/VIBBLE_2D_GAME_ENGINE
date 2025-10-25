#include "MapShadowPanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <string_view>

#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"
#include "dev_mode/dev_ui_settings.hpp"
#include "dev_mode/shared/formatting.hpp"
#include "utils/input.hpp"

namespace {
constexpr int kDefaultPanelWidth = DockableCollapsible::kDefaultFloatingContentWidth;

constexpr std::string_view kRenderShadowsSettingKey = "dev_ui.lighting.shadow_panel.render_shadows";

std::unique_ptr<DMSlider> make_scaled_slider(const std::string& label,
                                             float min_value,
                                             float max_value,
                                             float current,
                                             int scale,
                                             int precision) {
    const int min_i = static_cast<int>(std::round(min_value * static_cast<float>(scale)));
    const int max_i = static_cast<int>(std::round(max_value * static_cast<float>(scale)));
    int value_i      = static_cast<int>(std::round(current * static_cast<float>(scale)));
    value_i          = std::clamp(value_i, std::min(min_i, max_i), std::max(min_i, max_i));

    auto slider = std::make_unique<DMSlider>(label, std::min(min_i, max_i), std::max(min_i, max_i), value_i);
    slider->set_defer_commit_until_unfocus(false);
    slider->set_value_formatter([scale, precision](int value,
                                                   std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) -> std::string_view {
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

std::string_view format_lut_entry_label(int index,
                                        const render_pipeline::shading::ReactiveShadowSettings& settings,
                                        std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) {
    const auto& entries = settings.response_lut.entries;
    if (entries.empty()) {
        const int written = std::snprintf(buffer.data(), buffer.size(), "#%d", index);
        if (written <= 0) {
            return {};
        }
        return std::string_view(buffer.data(), static_cast<std::size_t>(std::min<std::size_t>(buffer.size(), written)));
    }
    const int clamped = std::clamp(index, 0, static_cast<int>(entries.size()) - 1);
    const auto brightness = entries[static_cast<std::size_t>(clamped)].brightness;
    const int written = std::snprintf(buffer.data(), buffer.size(), "#%d (%.3f)", clamped, brightness);
    if (written <= 0) {
        return {};
    }
    return std::string_view(buffer.data(), static_cast<std::size_t>(std::min<std::size_t>(buffer.size(), written)));
}

int clamp_entry_index(int index, const render_pipeline::shading::ReactiveShadowSettings& settings) {
    if (settings.response_lut.entries.empty()) {
        return 0;
    }
    return std::clamp(index, 0, static_cast<int>(settings.response_lut.entries.size()) - 1);
}

}  // namespace

MapShadowPanel::MapShadowPanel(Assets* assets, int x, int y)
    : DockableCollapsible("Reactive Shadows", true, x, y), assets_(assets) {
    current_settings_      = render_pipeline::shading::sanitize_reactive_shadow_settings({});
    last_applied_settings_ = current_settings_;
    set_floating_content_width(kDefaultPanelWidth);
    build_ui();
    sync_ui_from_settings(current_settings_);
}

MapShadowPanel::~MapShadowPanel() = default;

void MapShadowPanel::set_map_info(nlohmann::json* map_info, SaveCallback on_save) {
    map_info_ = map_info;
    on_save_  = std::move(on_save);
    current_settings_ = load_settings();
    last_applied_settings_ = current_settings_;
    rebuild_ui();
    apply_settings(current_settings_, false);
    initialized_ = true;
}

void MapShadowPanel::set_reactive_settings(std::function<ReactiveShadowSettings*()> accessor) {
    reactive_settings_accessor_ = std::move(accessor);

    ReactiveShadowSettings* settings = reactive_settings_accessor_ ? reactive_settings_accessor_() : nullptr;
    if (!settings) {
        return;
    }

    if (!initialized_) {
        current_settings_      = render_pipeline::shading::sanitize_reactive_shadow_settings(*settings);
        last_applied_settings_ = current_settings_;
        rebuild_ui();
        initialized_ = true;
    } else {
        apply_settings(current_settings_, false);
    }
}

void MapShadowPanel::clear_reactive_settings() {
    reactive_settings_accessor_ = {};
}

void MapShadowPanel::open() {
    DockableCollapsible::open();
}

void MapShadowPanel::close() {
    DockableCollapsible::close();
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

    if (pending_save_ && on_save_) {
        if (on_save_()) {
            pending_save_ = false;
        }
    }

    if (!is_visible()) {
        return;
    }

    if (lut_index_slider_) {
        const int desired = clamp_entry_index(lut_index_slider_->displayed_value(), current_settings_);
        if (desired != selected_entry_index_) {
            selected_entry_index_ = desired;
            sync_ui_from_settings(current_settings_);
        }
    }

    if (applying_ui_) {
        return;
    }

    ReactiveShadowSettings proposed = settings_from_ui();
    LutEntry target_entry{};
    if (!proposed.response_lut.entries.empty()) {
        const int safe_index = clamp_entry_index(selected_entry_index_, proposed);
        target_entry         = proposed.response_lut.entries[static_cast<std::size_t>(safe_index)];
    }

    ReactiveShadowSettings sanitized = render_pipeline::shading::sanitize_reactive_shadow_settings(proposed);
    if (!sanitized.response_lut.entries.empty()) {
        const int new_index = find_entry_index(sanitized.response_lut.entries, target_entry);
        selected_entry_index_ = clamp_entry_index(new_index, sanitized);
    } else {
        selected_entry_index_ = 0;
    }

    if (sanitized != current_settings_) {
        current_settings_ = sanitized;
        sync_ui_from_settings(current_settings_);
    }

    if (sanitized != last_applied_settings_) {
        apply_settings(sanitized, true);
    }
}

bool MapShadowPanel::handle_event(const SDL_Event& e) {
    bool handled = DockableCollapsible::handle_event(e);

    if (render_shadows_checkbox_) {
        bool desired = render_shadows_checkbox_->value();
        if (desired != render_shadows_enabled_) {
            render_shadows_enabled_ = desired;
            devmode::ui_settings::save_bool(kRenderShadowsSettingKey, render_shadows_enabled_);
        }
    }

    return handled;
}

void MapShadowPanel::render(SDL_Renderer* renderer) const {
    DockableCollapsible::render(renderer);
}

bool MapShadowPanel::is_point_inside(int x, int y) const {
    return DockableCollapsible::is_point_inside(x, y);
}

void MapShadowPanel::render_content(SDL_Renderer* renderer) const {
    DockableCollapsible::render_content(renderer);
}

void MapShadowPanel::build_ui() {
    widget_wrappers_.clear();
    Rows rows;

    render_shadows_enabled_ = devmode::ui_settings::load_bool(kRenderShadowsSettingKey, true);
    render_shadows_checkbox_ = std::make_unique<DMCheckbox>("Render Shadows", render_shadows_enabled_);
    if (render_shadows_checkbox_) {
        auto widget = std::make_unique<CheckboxWidget>(render_shadows_checkbox_.get());
        rows.push_back({widget.get()});
        widget_wrappers_.push_back(std::move(widget));
    }

    auto add_slider_row = [&](std::unique_ptr<DMSlider>& slider) {
        if (!slider) {
            return;
        }
        auto widget = std::make_unique<SliderWidget>(slider.get());
        rows.push_back({widget.get()});
        widget_wrappers_.push_back(std::move(widget));
    };

    horizontal_falloff_ = make_scaled_slider("Horizontal Falloff", 0.0f, 10.0f,
                                             current_settings_.virtual_light_map.horizontal_falloff, 100, 2);
    vertical_falloff_ = make_scaled_slider("Vertical Falloff", 0.0f, 10.0f,
                                           current_settings_.virtual_light_map.vertical_falloff, 100, 2);
    max_offset_x_ = make_scaled_slider("Max Offset X", 0.0f, 500.0f,
                                       current_settings_.virtual_light_map.max_offset_x, 100, 2);
    max_offset_y_ = make_scaled_slider("Max Offset Y", 0.0f, 500.0f,
                                       current_settings_.virtual_light_map.max_offset_y, 100, 2);
    shadow_scale_ = make_scaled_slider("Shadow Scale", 0.0f, 10.0f,
                                       current_settings_.virtual_light_map.shadow_scale, 100, 2);

    const int min_scale_value =
        std::clamp(current_settings_.virtual_light_map.min_scale_percent, 50, 200);
    const int max_scale_value =
        std::clamp(current_settings_.virtual_light_map.max_scale_percent, 50, 200);
    min_scale_percent_ = std::make_unique<DMSlider>("Min Scale %", 50, 200, min_scale_value);
    max_scale_percent_ = std::make_unique<DMSlider>("Max Scale %", 50, 200, max_scale_value);
    if (min_scale_percent_) {
        min_scale_percent_->set_defer_commit_until_unfocus(false);
    }
    if (max_scale_percent_) {
        max_scale_percent_->set_defer_commit_until_unfocus(false);
    }

    map_light_dir_strength_ = make_scaled_slider("Directional Offset Strength", 0.0f, 1.0f,
                                                 current_settings_.virtual_light_map.map_light_dir_offset_strength,
                                                 100,
                                                 2);
    parallax_percent_ = make_scaled_slider("Parallax %", 0.0f, 100.0f,
                                          current_settings_.virtual_light_map.parallax_percent,
                                          100,
                                          1);

    const int search_radius_value = std::clamp(current_settings_.virtual_light_map.search_radius, 0, 128);
    search_radius_ = std::make_unique<DMSlider>("Search Radius", 0, 128, search_radius_value);
    search_radius_->set_defer_commit_until_unfocus(false);

    static_weight_ = make_scaled_slider("Static Weight", 0.0f, 10.0f,
                                        current_settings_.sampling_weights.static_weight, 100, 2);

    const int entry_count = static_cast<int>(current_settings_.response_lut.entries.size());
    const int max_index   = entry_count > 0 ? entry_count - 1 : 0;
    selected_entry_index_ = std::clamp(selected_entry_index_, 0, std::max(0, max_index));

    lut_index_slider_ = std::make_unique<DMSlider>("Response Entry", 0, std::max(0, max_index), selected_entry_index_);
    lut_index_slider_->set_defer_commit_until_unfocus(false);
    lut_index_slider_->set_value_formatter([this](int value,
                                                  std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) -> std::string_view {
        return format_lut_entry_label(value, this->current_settings_, buffer);
    });
    lut_index_slider_->set_value_parser([](const std::string& text) -> std::optional<int> {
        try {
            int parsed = std::stoi(text);
            return parsed;
        } catch (...) {
            return std::nullopt;
        }
    });

    lut_brightness_ = make_scaled_slider("Brightness", 0.0f, 1.0f,
                                         entry_count > 0 ? current_settings_.response_lut.entries[static_cast<std::size_t>(selected_entry_index_)].brightness : 0.0f,
                                         1000, 3);
    lut_opacity_ = make_scaled_slider("Opacity", 0.0f, 10.0f,
                                      entry_count > 0 ? current_settings_.response_lut.entries[static_cast<std::size_t>(selected_entry_index_)].opacity : 1.0f,
                                      100, 2);
    lut_offset_ = make_scaled_slider("Offset", -1000.0f, 1000.0f,
                                     entry_count > 0 ? current_settings_.response_lut.entries[static_cast<std::size_t>(selected_entry_index_)].offset : 0.0f,
                                     100, 2);
    lut_scale_ = make_scaled_slider("Scale", 0.0f, 10.0f,
                                    entry_count > 0 ? current_settings_.response_lut.entries[static_cast<std::size_t>(selected_entry_index_)].scale : 1.0f,
                                    100, 2);

    add_slider_row(horizontal_falloff_);
    add_slider_row(vertical_falloff_);
    add_slider_row(max_offset_x_);
    add_slider_row(max_offset_y_);
    add_slider_row(shadow_scale_);
    add_slider_row(min_scale_percent_);
    add_slider_row(max_scale_percent_);
    add_slider_row(map_light_dir_strength_);
    add_slider_row(parallax_percent_);
    add_slider_row(search_radius_);

    add_slider_row(static_weight_);

    add_slider_row(lut_index_slider_);
    add_slider_row(lut_brightness_);
    add_slider_row(lut_opacity_);
    add_slider_row(lut_offset_);
    add_slider_row(lut_scale_);

    set_rows(rows);
}

void MapShadowPanel::rebuild_ui() {
    build_ui();
    sync_ui_from_settings(current_settings_);
}

void MapShadowPanel::sync_ui_from_settings(const ReactiveShadowSettings& settings) {
    applying_ui_ = true;

    if (horizontal_falloff_) horizontal_falloff_->set_value(static_cast<int>(std::round(settings.virtual_light_map.horizontal_falloff * 100.0f)));
    if (vertical_falloff_) vertical_falloff_->set_value(static_cast<int>(std::round(settings.virtual_light_map.vertical_falloff * 100.0f)));
    if (max_offset_x_) max_offset_x_->set_value(static_cast<int>(std::round(settings.virtual_light_map.max_offset_x * 100.0f)));
    if (max_offset_y_) max_offset_y_->set_value(static_cast<int>(std::round(settings.virtual_light_map.max_offset_y * 100.0f)));
    if (shadow_scale_) shadow_scale_->set_value(static_cast<int>(std::round(settings.virtual_light_map.shadow_scale * 100.0f)));
    if (min_scale_percent_)
        min_scale_percent_->set_value(std::clamp(settings.virtual_light_map.min_scale_percent, 50, 200));
    if (max_scale_percent_)
        max_scale_percent_->set_value(std::clamp(settings.virtual_light_map.max_scale_percent, 50, 200));
    if (map_light_dir_strength_)
        map_light_dir_strength_->set_value(static_cast<int>(std::round(settings.virtual_light_map.map_light_dir_offset_strength * 100.0f)));
    if (parallax_percent_)
        parallax_percent_->set_value(static_cast<int>(std::round(settings.virtual_light_map.parallax_percent * 100.0f)));
    if (search_radius_) search_radius_->set_value(std::clamp(settings.virtual_light_map.search_radius, 0, 128));

    if (static_weight_) static_weight_->set_value(static_cast<int>(std::round(settings.sampling_weights.static_weight * 100.0f)));

    if (lut_index_slider_) {
        const int entry_count = static_cast<int>(settings.response_lut.entries.size());
        const int max_index   = entry_count > 0 ? entry_count - 1 : 0;
        selected_entry_index_ = std::clamp(selected_entry_index_, 0, std::max(0, max_index));
        lut_index_slider_->set_value(selected_entry_index_);
    }

    if (!settings.response_lut.entries.empty()) {
        const int safe_index = clamp_entry_index(selected_entry_index_, settings);
        const auto& entry = settings.response_lut.entries[static_cast<std::size_t>(safe_index)];
        if (lut_brightness_) lut_brightness_->set_value(static_cast<int>(std::round(entry.brightness * 1000.0f)));
        if (lut_opacity_) lut_opacity_->set_value(static_cast<int>(std::round(entry.opacity * 100.0f)));
        if (lut_offset_) lut_offset_->set_value(static_cast<int>(std::round(entry.offset * 100.0f)));
        if (lut_scale_) lut_scale_->set_value(static_cast<int>(std::round(entry.scale * 100.0f)));
    }

    applying_ui_ = false;
}

MapShadowPanel::ReactiveShadowSettings MapShadowPanel::settings_from_ui() {
    ReactiveShadowSettings settings = current_settings_;

    settings.virtual_light_map.horizontal_falloff = read_scaled_slider(horizontal_falloff_, 100, settings.virtual_light_map.horizontal_falloff);
    settings.virtual_light_map.vertical_falloff   = read_scaled_slider(vertical_falloff_, 100, settings.virtual_light_map.vertical_falloff);
    settings.virtual_light_map.max_offset_x       = read_scaled_slider(max_offset_x_, 100, settings.virtual_light_map.max_offset_x);
    settings.virtual_light_map.max_offset_y       = read_scaled_slider(max_offset_y_, 100, settings.virtual_light_map.max_offset_y);
    settings.virtual_light_map.shadow_scale       = read_scaled_slider(shadow_scale_, 100, settings.virtual_light_map.shadow_scale);
    if (min_scale_percent_) {
        settings.virtual_light_map.min_scale_percent =
            std::clamp(min_scale_percent_->displayed_value(), 50, 200);
    }
    if (max_scale_percent_) {
        settings.virtual_light_map.max_scale_percent =
            std::clamp(max_scale_percent_->displayed_value(), 50, 200);
    }
    settings.virtual_light_map.map_light_dir_offset_strength =
        read_scaled_slider(map_light_dir_strength_, 100, settings.virtual_light_map.map_light_dir_offset_strength);
    settings.virtual_light_map.parallax_percent =
        read_scaled_slider(parallax_percent_, 100, settings.virtual_light_map.parallax_percent);
    if (search_radius_) {
        settings.virtual_light_map.search_radius = std::clamp(search_radius_->displayed_value(), 0, 128);
    }

    settings.sampling_weights.static_weight  = read_scaled_slider(static_weight_, 100, settings.sampling_weights.static_weight);

    if (!settings.response_lut.entries.empty()) {
        const int safe_index = clamp_entry_index(lut_index_slider_ ? lut_index_slider_->displayed_value() : selected_entry_index_, settings);
        selected_entry_index_ = safe_index;
        auto& entry = settings.response_lut.entries[static_cast<std::size_t>(safe_index)];
        entry.brightness = read_scaled_slider(lut_brightness_, 1000, entry.brightness);
        entry.opacity    = read_scaled_slider(lut_opacity_, 100, entry.opacity);
        entry.offset     = read_scaled_slider(lut_offset_, 100, entry.offset);
        entry.scale      = read_scaled_slider(lut_scale_, 100, entry.scale);
    }

    return settings;
}

MapShadowPanel::ReactiveShadowSettings MapShadowPanel::load_settings() const {
    using namespace render_pipeline::shading;
    const ReactiveShadowSettings fallback = sanitize_reactive_shadow_settings({});

    if (map_info_ && map_info_->is_object()) {
        auto it = map_info_->find("reactive_shadows");
        if (it != map_info_->end() && it->is_object()) {
            try {
                return sanitize_reactive_shadow_settings(reactive_shadow_settings_from_json(*it, fallback));
            } catch (...) {
            }
        }
    }

    if (reactive_settings_accessor_) {
        if (ReactiveShadowSettings* shared = reactive_settings_accessor_()) {
            return sanitize_reactive_shadow_settings(*shared);
        }
    }

    return fallback;
}

void MapShadowPanel::apply_settings(const ReactiveShadowSettings& settings, bool persist) {
    last_applied_settings_ = settings;

    ReactiveShadowSettings* shared = reactive_settings_accessor_ ? reactive_settings_accessor_() : nullptr;
    if (!shared) {
        return;
    }

    *shared = settings;

    if (map_info_) {
        if (nlohmann::json* json = ensure_reactive_shadow_json()) {
            render_pipeline::shading::assign_reactive_shadow_settings(*json, settings);
            if (persist) {
                request_save();
            }
        }
    }
}

void MapShadowPanel::request_save() {
    pending_save_ = true;
}

nlohmann::json* MapShadowPanel::ensure_reactive_shadow_json() {
    if (!map_info_ || !map_info_->is_object()) {
        return nullptr;
    }
    nlohmann::json& json = *map_info_;
    if (!json.contains("reactive_shadows") || !json["reactive_shadows"].is_object()) {
        json["reactive_shadows"] = nlohmann::json::object();
    }
    return &json["reactive_shadows"];
}

int MapShadowPanel::find_entry_index(const std::vector<LutEntry>& entries, const LutEntry& target) {
    if (entries.empty()) {
        return 0;
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        const bool matches = std::abs(entry.brightness - target.brightness) < 1e-4f &&
                             std::abs(entry.opacity - target.opacity) < 1e-3f &&
                             std::abs(entry.offset - target.offset) < 1e-3f &&
                             std::abs(entry.scale - target.scale) < 1e-3f;
        if (matches) {
            return static_cast<int>(i);
        }
    }

    float best_diff = std::numeric_limits<float>::max();
    int best_index  = 0;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const float diff = std::abs(entries[i].brightness - target.brightness);
        if (diff < best_diff) {
            best_diff = diff;
            best_index = static_cast<int>(i);
        }
    }
    return best_index;
}

float MapShadowPanel::read_scaled_slider(const std::unique_ptr<DMSlider>& slider, int scale, float fallback) {
    if (!slider) {
        return fallback;
    }
    const int value = slider->displayed_value();
    return static_cast<float>(value) / static_cast<float>(scale);
}

