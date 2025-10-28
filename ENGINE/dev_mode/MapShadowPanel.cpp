#include "MapShadowPanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include "core/AssetsManager.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"
#include "dev_mode/dm_styles.hpp"
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

}

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
    widget_wrappers_.reserve(24);
    Rows rows;

    const auto& vsettings = current_settings_.virtual_light_map;

    // Top: Subdivision ticker for number of light cells
    const int light_cells_value = std::clamp(std::max(1, vsettings.light_grid_subdivide), 1, 8);
    light_cells_subdivide_ = std::make_unique<DMNumericStepper>("Light Cells", 1, 8, light_cells_value);
    if (light_cells_subdivide_) {
        auto stepper_widget = std::make_unique<StepperWidget>(light_cells_subdivide_.get());
        stepper_widget->set_tooltip("Sets the number of light cells used for reactive shadowing.");
        rows.push_back({stepper_widget.get()});
        widget_wrappers_.push_back(std::move(stepper_widget));
    }

    // Section: Offset Settings (collapsible)
    if (!offset_section_header_btn_) {
        offset_section_header_btn_ = std::make_unique<DMButton>("Offset Settings", &DMStyles::HeaderButton(), 200, DMButton::height());
    }
    auto offset_header = std::make_unique<ButtonWidget>(offset_section_header_btn_.get(), [this]() {
        this->offset_section_expanded_ = !this->offset_section_expanded_;
        this->rebuild_ui();
    });
    rows.push_back({offset_header.get()});
    widget_wrappers_.push_back(std::move(offset_header));

    if (offset_section_expanded_) {
        enable_offset_chk_ = std::make_unique<DMCheckbox>("Enable Offset", vsettings.enable_offset);
        auto enable_offset_w = std::make_unique<CheckboxWidget>(enable_offset_chk_.get());
        rows.push_back({enable_offset_w.get()});
        widget_wrappers_.push_back(std::move(enable_offset_w));

        offset_horizontal_falloff_ = make_scaled_slider("Horizontal Falloff (offset)", 0.0f, 10.0f, vsettings.offset_horizontal_falloff, 100, 2);
        offset_vertical_falloff_   = make_scaled_slider("Vertical Falloff (offset)", 0.0f, 10.0f, vsettings.offset_vertical_falloff, 100, 2);
        max_offset_x_ = make_scaled_slider("Max Offset X (px)", 0.0f, 500.0f, vsettings.max_offset_x, 100, 2);
        max_offset_y_ = make_scaled_slider("Max Offset Y (px)", 0.0f, 500.0f, vsettings.max_offset_y, 100, 2);
        map_light_dir_strength_ = make_scaled_slider("Directional Offset Strength", 0.0f, 1.0f, vsettings.map_light_dir_offset_strength, 100, 2);

        const int offset_k = std::clamp(vsettings.offset_search_radius, 0, 128);
        offset_search_radius_step_ = std::make_unique<DMNumericStepper>("K Neighbors (offset)", 0, 128, offset_k);

        const int blur_frames_value = std::clamp(current_settings_.frame_blend_falloff_frames, 0, 200);
        frame_blend_falloff_frames_ = std::make_unique<DMSlider>("Motion Blur Frames", 0, 200, blur_frames_value);
        if (frame_blend_falloff_frames_) {
            frame_blend_falloff_frames_->set_defer_commit_until_unfocus(false);
            frame_blend_falloff_frames_->set_value_formatter([](int value,
                std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) -> std::string_view {
                const int clamped = std::clamp(value, 0, 999);
                const int written = std::snprintf(buffer.data(), buffer.size(), "%d frames", clamped);
                if (written <= 0) return {};
                return std::string_view(buffer.data(), static_cast<std::size_t>(written));
            });
            frame_blend_falloff_frames_->set_value_parser([](const std::string& text) -> std::optional<int> {
                try { return std::stoi(text); } catch (...) { return std::nullopt; }
            });
        }

        auto add_slider = [&](std::unique_ptr<DMSlider>& s, const std::string& tip) {
            if (!s) return;
            auto w = std::make_unique<SliderWidget>(s.get());
            w->set_tooltip(tip);
            rows.push_back({w.get()});
            widget_wrappers_.push_back(std::move(w));
        };
        auto add_stepper = [&](std::unique_ptr<DMNumericStepper>& s, const std::string& tip) {
            if (!s) return;
            auto w = std::make_unique<StepperWidget>(s.get());
            w->set_tooltip(tip);
            rows.push_back({w.get()});
            widget_wrappers_.push_back(std::move(w));
        };

        add_slider(offset_horizontal_falloff_, "Lower: tighter horizontal fade of offset. Higher: spreads horizontally.");
        add_slider(offset_vertical_falloff_,   "Lower: tighter vertical fade of offset. Higher: spreads vertically.");
        add_slider(max_offset_x_, "Lower: limits sideways shift. Higher: allows more horizontal movement.");
        add_slider(max_offset_y_, "Lower: keeps shadows close vertically. Higher: lets them stretch farther up/down.");
        add_slider(map_light_dir_strength_, "Lower: ignore directional light push. Higher: follow main light direction strongly.");
        add_stepper(offset_search_radius_step_, "K-neighbor search radius for offset sampling.");
        add_slider(frame_blend_falloff_frames_, "Lower: no blur. Higher: blend with more previous frames.");
    }

    // Section: Opacity Settings (collapsible)
    if (!opacity_section_header_btn_) {
        opacity_section_header_btn_ = std::make_unique<DMButton>("Opacity Settings", &DMStyles::HeaderButton(), 200, DMButton::height());
    }
    auto opacity_header = std::make_unique<ButtonWidget>(opacity_section_header_btn_.get(), [this]() {
        this->opacity_section_expanded_ = !this->opacity_section_expanded_;
        this->rebuild_ui();
    });
    rows.push_back({opacity_header.get()});
    widget_wrappers_.push_back(std::move(opacity_header));

    if (opacity_section_expanded_) {
        enable_opacity_chk_ = std::make_unique<DMCheckbox>("Enable Opacity", vsettings.enable_opacity);
        auto enable_opacity_w = std::make_unique<CheckboxWidget>(enable_opacity_chk_.get());
        rows.push_back({enable_opacity_w.get()});
        widget_wrappers_.push_back(std::move(enable_opacity_w));

        opacity_horizontal_falloff_ = make_scaled_slider("Horizontal Falloff (opacity)", 0.0f, 10.0f, vsettings.opacity_horizontal_falloff, 100, 2);
        opacity_vertical_falloff_   = make_scaled_slider("Vertical Falloff (opacity)", 0.0f, 10.0f, vsettings.opacity_vertical_falloff, 100, 2);

        const int opacity_k = std::clamp(vsettings.opacity_search_radius, 0, 128);
        opacity_search_radius_step_ = std::make_unique<DMNumericStepper>("K Neighbors (opacity)", 0, 128, opacity_k);

        min_opacity_ = make_scaled_slider("Min Opacity", 0.0f, 1.0f, vsettings.min_opacity, 100, 2);
        max_opacity_ = make_scaled_slider("Max Opacity", 0.0f, 1.0f, vsettings.max_opacity, 100, 2);

        const int boost_percent = static_cast<int>(std::lround(std::clamp(vsettings.opacity_boost, -1.0f, 1.0f) * 100.0f));
        opacity_boost_percent_ = std::make_unique<DMSlider>("Opacity Boost %", -100, 100, boost_percent);
        if (opacity_boost_percent_) {
            opacity_boost_percent_->set_defer_commit_until_unfocus(false);
            opacity_boost_percent_->set_value_formatter([](int value,
                std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) -> std::string_view {
                const int clamped = std::clamp(value, -100, 100);
                const int written = std::snprintf(buffer.data(), buffer.size(), "%d%%", clamped);
                if (written <= 0) return {};
                return std::string_view(buffer.data(), static_cast<std::size_t>(written));
            });
            opacity_boost_percent_->set_value_parser([](const std::string& text) -> std::optional<int> {
                try { return std::stoi(text); } catch (...) { return std::nullopt; }
            });
        }

        auto add_slider = [&](std::unique_ptr<DMSlider>& s, const std::string& tip) {
            if (!s) return;
            auto w = std::make_unique<SliderWidget>(s.get());
            w->set_tooltip(tip);
            rows.push_back({w.get()});
            widget_wrappers_.push_back(std::move(w));
        };
        auto add_stepper = [&](std::unique_ptr<DMNumericStepper>& s, const std::string& tip) {
            if (!s) return;
            auto w = std::make_unique<StepperWidget>(s.get());
            w->set_tooltip(tip);
            rows.push_back({w.get()});
            widget_wrappers_.push_back(std::move(w));
        };

        add_slider(opacity_horizontal_falloff_, "Lower: tighter horizontal fade of opacity.");
        add_slider(opacity_vertical_falloff_, "Lower: tighter vertical fade of opacity.");
        add_stepper(opacity_search_radius_step_, "K-neighbor search radius for opacity sampling.");
        add_slider(min_opacity_, "Minimum opacity clamp (0..100%).");
        add_slider(max_opacity_, "Maximum opacity clamp (0..100%).");
        add_slider(opacity_boost_percent_, "Boost or reduce opacity (-100%..100%).");
    }

    // Optional: grid subdivide slider left available for advanced control
    const int grid_subdivide_value = std::clamp(std::max(1, vsettings.grid_subdivide), 0, 8);
    grid_subdivide_ = std::make_unique<DMSlider>("Grid Subdivide", 0, 8, grid_subdivide_value);
    if (grid_subdivide_) {
        grid_subdivide_->set_defer_commit_until_unfocus(false);
        grid_subdivide_->set_value_formatter([](int value,
            std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) -> std::string_view {
            const int clamped = std::clamp(value, 0, 8);
            const int written = std::snprintf(buffer.data(), buffer.size(), "%dx", clamped == 0 ? 1 : clamped);
            if (written <= 0) { return {}; }
            return std::string_view(buffer.data(), static_cast<std::size_t>(written));
        });
        grid_subdivide_->set_value_parser([](const std::string& text) -> std::optional<int> {
            try { return std::stoi(text); } catch (...) { return std::nullopt; }
        });
        auto w = std::make_unique<SliderWidget>(grid_subdivide_.get());
        w->set_tooltip("Underlying virtual grid subdivision (advanced).");
        rows.push_back({w.get()});
        widget_wrappers_.push_back(std::move(w));
    }

    set_rows(rows);
}

void MapShadowPanel::rebuild_ui() {
    build_ui();
    sync_ui_from_settings(current_settings_);
}

void MapShadowPanel::sync_ui_from_settings(const ReactiveShadowSettings& settings) {
    applying_ui_ = true;

    if (light_cells_subdivide_) light_cells_subdivide_->set_value(std::clamp(std::max(1, settings.virtual_light_map.light_grid_subdivide), 1, 8));
    if (enable_offset_chk_) enable_offset_chk_->set_value(settings.virtual_light_map.enable_offset);
    if (enable_opacity_chk_) enable_opacity_chk_->set_value(settings.virtual_light_map.enable_opacity);
    if (offset_horizontal_falloff_) offset_horizontal_falloff_->set_value(static_cast<int>(std::round(settings.virtual_light_map.offset_horizontal_falloff * 100.0f)));
    if (offset_vertical_falloff_)   offset_vertical_falloff_->set_value(static_cast<int>(std::round(settings.virtual_light_map.offset_vertical_falloff * 100.0f)));
    if (opacity_horizontal_falloff_) opacity_horizontal_falloff_->set_value(static_cast<int>(std::round(settings.virtual_light_map.opacity_horizontal_falloff * 100.0f)));
    if (opacity_vertical_falloff_)   opacity_vertical_falloff_->set_value(static_cast<int>(std::round(settings.virtual_light_map.opacity_vertical_falloff * 100.0f)));
    if (max_offset_x_) max_offset_x_->set_value(static_cast<int>(std::round(settings.virtual_light_map.max_offset_x * 100.0f)));
    if (max_offset_y_) max_offset_y_->set_value(static_cast<int>(std::round(settings.virtual_light_map.max_offset_y * 100.0f)));
    if (frame_blend_falloff_frames_)
        frame_blend_falloff_frames_->set_value(std::clamp(settings.frame_blend_falloff_frames, 0, 200));
    if (map_light_dir_strength_)
        map_light_dir_strength_->set_value(static_cast<int>(std::round(settings.virtual_light_map.map_light_dir_offset_strength * 100.0f)));
    if (offset_search_radius_step_) offset_search_radius_step_->set_value(std::clamp(settings.virtual_light_map.offset_search_radius, 0, 128));
    if (opacity_search_radius_step_) opacity_search_radius_step_->set_value(std::clamp(settings.virtual_light_map.opacity_search_radius, 0, 128));
    if (min_opacity_) min_opacity_->set_value(static_cast<int>(std::round(std::clamp(settings.virtual_light_map.min_opacity, 0.0f, 1.0f) * 100.0f)));
    if (max_opacity_) max_opacity_->set_value(static_cast<int>(std::round(std::clamp(settings.virtual_light_map.max_opacity, 0.0f, 1.0f) * 100.0f)));
    if (opacity_boost_percent_) opacity_boost_percent_->set_value(static_cast<int>(std::lround(std::clamp(settings.virtual_light_map.opacity_boost, -1.0f, 1.0f) * 100.0f)));
    if (grid_subdivide_)
        grid_subdivide_->set_value(std::clamp(std::max(1, settings.virtual_light_map.grid_subdivide), 0, 8));
    applying_ui_ = false;
}

MapShadowPanel::ReactiveShadowSettings MapShadowPanel::settings_from_ui() {
    ReactiveShadowSettings settings = current_settings_;

    settings.virtual_light_map.offset_horizontal_falloff = read_scaled_slider(offset_horizontal_falloff_, 100, settings.virtual_light_map.offset_horizontal_falloff);
    settings.virtual_light_map.offset_vertical_falloff   = read_scaled_slider(offset_vertical_falloff_, 100, settings.virtual_light_map.offset_vertical_falloff);
    settings.virtual_light_map.opacity_horizontal_falloff = read_scaled_slider(opacity_horizontal_falloff_, 100, settings.virtual_light_map.opacity_horizontal_falloff);
    settings.virtual_light_map.opacity_vertical_falloff   = read_scaled_slider(opacity_vertical_falloff_, 100, settings.virtual_light_map.opacity_vertical_falloff);
    settings.virtual_light_map.max_offset_x       = read_scaled_slider(max_offset_x_, 100, settings.virtual_light_map.max_offset_x);
    settings.virtual_light_map.max_offset_y       = read_scaled_slider(max_offset_y_, 100, settings.virtual_light_map.max_offset_y);
    if (enable_offset_chk_) settings.virtual_light_map.enable_offset = enable_offset_chk_->value();
    if (enable_opacity_chk_) settings.virtual_light_map.enable_opacity = enable_opacity_chk_->value();
    if (frame_blend_falloff_frames_) {
        settings.frame_blend_falloff_frames =
            std::clamp(frame_blend_falloff_frames_->displayed_value(), 0, 200);
    }
    settings.virtual_light_map.map_light_dir_offset_strength =
        read_scaled_slider(map_light_dir_strength_, 100, settings.virtual_light_map.map_light_dir_offset_strength);
    if (offset_search_radius_step_) {
        settings.virtual_light_map.offset_search_radius = std::clamp(offset_search_radius_step_->value(), 0, 128);
    }
    if (opacity_search_radius_step_) {
        settings.virtual_light_map.opacity_search_radius = std::clamp(opacity_search_radius_step_->value(), 0, 128);
    }
    if (min_opacity_) settings.virtual_light_map.min_opacity = std::clamp(min_opacity_->displayed_value(), 0, 100) / 100.0f;
    if (max_opacity_) settings.virtual_light_map.max_opacity = std::clamp(max_opacity_->displayed_value(), 0, 100) / 100.0f;
    if (opacity_boost_percent_) settings.virtual_light_map.opacity_boost = std::clamp(opacity_boost_percent_->displayed_value(), -100, 100) / 100.0f;
    if (grid_subdivide_) {
        int subdivide = std::clamp(grid_subdivide_->displayed_value(), 0, 8);
        settings.virtual_light_map.grid_subdivide = subdivide;
    }
    if (light_cells_subdivide_) {
        int subdivide = std::clamp(light_cells_subdivide_->value(), 1, 8);
        settings.virtual_light_map.light_grid_subdivide = subdivide;
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
    const int previous_subdivide   = last_applied_settings_.virtual_light_map.grid_subdivide;
    const bool subdivisions_changed = settings.virtual_light_map.grid_subdivide != previous_subdivide;
    last_applied_settings_         = settings;

    bool forced_by_subdivide = false;
    if (assets_ && subdivisions_changed) {
        forced_by_subdivide = assets_->apply_lighting_grid_subdivide(settings.virtual_light_map.grid_subdivide);
    }

    ReactiveShadowSettings* shared = reactive_settings_accessor_ ? reactive_settings_accessor_() : nullptr;
    bool                      shared_changed = false;
    if (shared) {
        if (*shared != settings) {
            *shared = settings;
            shared_changed = true;
        } else {
            *shared = settings;
        }
    }

    if (shared_changed && assets_ && !forced_by_subdivide) {
        assets_->force_shaded_assets_rerender();
    }

    if (!shared) {
        return;
    }

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

