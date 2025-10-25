#include "MapShadowPanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string_view>

#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"
#include "dev_mode/shared/formatting.hpp"
#include "utils/input.hpp"

namespace {
constexpr int kDefaultPanelWidth = DockableCollapsible::kDefaultFloatingContentWidth;

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

    if (applying_ui_) {
        return;
    }

    ReactiveShadowSettings proposed = settings_from_ui();
    ReactiveShadowSettings sanitized = render_pipeline::shading::sanitize_reactive_shadow_settings(proposed);

    if (sanitized != current_settings_) {
        current_settings_ = sanitized;
        sync_ui_from_settings(current_settings_);
    }

    if (sanitized != last_applied_settings_) {
        apply_settings(sanitized, true);
    }
}

bool MapShadowPanel::handle_event(const SDL_Event& e) {
    return DockableCollapsible::handle_event(e);
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
    widget_wrappers_.reserve(16);
    Rows rows;

    const auto& vsettings = current_settings_.virtual_light_map;

    horizontal_falloff_ = make_scaled_slider("Horizontal Falloff",
                                             0.0f,
                                             10.0f,
                                             vsettings.horizontal_falloff,
                                             100,
                                             2);
    vertical_falloff_ = make_scaled_slider("Vertical Falloff",
                                           0.0f,
                                           10.0f,
                                           vsettings.vertical_falloff,
                                           100,
                                           2);
    max_offset_x_ = make_scaled_slider("Max Offset X (px)",
                                       0.0f,
                                       500.0f,
                                       vsettings.max_offset_x,
                                       100,
                                       2);
    max_offset_y_ = make_scaled_slider("Max Offset Y (px)",
                                       0.0f,
                                       500.0f,
                                       vsettings.max_offset_y,
                                       100,
                                       2);
    shadow_scale_ = make_scaled_slider("Base Shadow Scale",
                                       0.0f,
                                       10.0f,
                                       vsettings.shadow_scale,
                                       100,
                                       2);

    const int min_scale_value = std::clamp(vsettings.min_scale_percent, 50, 200);
    const int max_scale_value = std::clamp(vsettings.max_scale_percent, 50, 200);
    min_scale_percent_        = std::make_unique<DMSlider>("Min Scale %", 50, 200, min_scale_value);
    max_scale_percent_        = std::make_unique<DMSlider>("Max Scale %", 50, 200, max_scale_value);
    if (min_scale_percent_) {
        min_scale_percent_->set_defer_commit_until_unfocus(false);
        min_scale_percent_->set_value_formatter([](int value,
                                                   std::array<char, dev_mode::kSliderFormatBufferSize>& buffer)
                                                   -> std::string_view {
            const int clamped = std::clamp(value, 0, 999);
            const int written = std::snprintf(buffer.data(), buffer.size(), "%d%%", clamped);
            if (written <= 0) {
                return {};
            }
            return std::string_view(buffer.data(), static_cast<std::size_t>(written));
        });
        min_scale_percent_->set_value_parser([](const std::string& text) -> std::optional<int> {
            try {
                return std::stoi(text);
            } catch (...) {
                return std::nullopt;
            }
        });
    }
    if (max_scale_percent_) {
        max_scale_percent_->set_defer_commit_until_unfocus(false);
        max_scale_percent_->set_value_formatter([](int value,
                                                   std::array<char, dev_mode::kSliderFormatBufferSize>& buffer)
                                                   -> std::string_view {
            const int clamped = std::clamp(value, 0, 999);
            const int written = std::snprintf(buffer.data(), buffer.size(), "%d%%", clamped);
            if (written <= 0) {
                return {};
            }
            return std::string_view(buffer.data(), static_cast<std::size_t>(written));
        });
        max_scale_percent_->set_value_parser([](const std::string& text) -> std::optional<int> {
            try {
                return std::stoi(text);
            } catch (...) {
                return std::nullopt;
            }
        });
    }

    map_light_dir_strength_ = make_scaled_slider("Directional Offset Strength",
                                                 0.0f,
                                                 1.0f,
                                                 vsettings.map_light_dir_offset_strength,
                                                 100,
                                                 2);
    parallax_percent_ = make_scaled_slider("Parallax %",
                                          0.0f,
                                          100.0f,
                                          vsettings.parallax_percent,
                                          100,
                                          1);

    const int search_radius_value = std::clamp(vsettings.search_radius, 0, 128);
    search_radius_ = std::make_unique<DMSlider>("Search Radius (cells)", 0, 128, search_radius_value);
    if (search_radius_) {
        search_radius_->set_defer_commit_until_unfocus(false);
        search_radius_->set_value_formatter([](int value,
                                               std::array<char, dev_mode::kSliderFormatBufferSize>& buffer)
                                               -> std::string_view {
            const int clamped = std::clamp(value, 0, 999);
            const int written = std::snprintf(buffer.data(), buffer.size(), "%d cells", clamped);
            if (written <= 0) {
                return {};
            }
            return std::string_view(buffer.data(), static_cast<std::size_t>(written));
        });
        search_radius_->set_value_parser([](const std::string& text) -> std::optional<int> {
            try {
                return std::stoi(text);
            } catch (...) {
                return std::nullopt;
            }
        });
    }

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
    add_slider_row(min_scale_percent_);
    add_slider_row(max_scale_percent_);
    add_slider_row(map_light_dir_strength_);
    add_slider_row(parallax_percent_);
    add_slider_row(search_radius_);

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

float MapShadowPanel::read_scaled_slider(const std::unique_ptr<DMSlider>& slider, int scale, float fallback) {
    if (!slider) {
        return fallback;
    }
    const int value = slider->displayed_value();
    return static_cast<float>(value) / static_cast<float>(scale);
}

