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
#include "dev_mode/font_cache.hpp"
#include "dev_mode/shared/formatting.hpp"
#include "utils/input.hpp"

namespace {
constexpr int kDefaultPanelWidth = DockableCollapsible::kDefaultFloatingContentWidth;

class SliderHelpLabel : public Widget {
public:
    explicit SliderHelpLabel(std::string text) : text_(std::move(text)) {}

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int  height_for_width(int) const override {
        const DMLabelStyle& style = DMStyles::Label();
        int                 lines = 1;
        for (char c : text_) {
            if (c == '\n') {
                ++lines;
            }
        }
        lines = std::max(1, lines);
        return lines * (style.font_size + DMSpacing::small_gap());
    }
    bool handle_event(const SDL_Event&) override { return false; }
    void render(SDL_Renderer* renderer) const override {
        if (!renderer) {
            return;
        }
        const DMLabelStyle& style = DMStyles::Label();
        const int           line_height = style.font_size + DMSpacing::small_gap();
        int                 y = rect_.y;
        std::string         current;
        auto flush_line = [&](const std::string& line) {
            if (!line.empty()) {
                DrawLabelText(renderer, line, rect_.x, y, style);
            }
            y += line_height;
        };
        for (char c : text_) {
            if (c == '\n') {
                flush_line(current);
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        flush_line(current);
    }
    bool wants_full_row() const override { return true; }

private:
    std::string text_;
    SDL_Rect    rect_{0, 0, 0, 0};
};

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
    widget_wrappers_.reserve(16);
    Rows rows;

    const auto& vsettings = current_settings_.virtual_light_map;

    horizontal_falloff_ = make_scaled_slider("Horizontal Falloff", 0.0f, 10.0f, vsettings.horizontal_falloff, 100, 2);
    vertical_falloff_ = make_scaled_slider("Vertical Falloff", 0.0f, 10.0f, vsettings.vertical_falloff, 100, 2);
    max_offset_x_ = make_scaled_slider("Max Offset X (px)", 0.0f, 500.0f, vsettings.max_offset_x, 100, 2);
    max_offset_y_ = make_scaled_slider("Max Offset Y (px)", 0.0f, 500.0f, vsettings.max_offset_y, 100, 2);
    const float initial_sensitivity =
        std::clamp(current_settings_.opacity_sensitivity_percent, 0.0f, 100.0f);
    opacity_sensitivity_percent_ = std::make_unique<DMSlider>("Opacity Sensitivity %", 0, 100, static_cast<int>(std::lround(initial_sensitivity)));
    if (opacity_sensitivity_percent_) {
        opacity_sensitivity_percent_->set_defer_commit_until_unfocus(false);
    }

    const int blur_frames_value = std::clamp(current_settings_.frame_blend_falloff_frames, 0, 200);
    frame_blend_falloff_frames_ =
        std::make_unique<DMSlider>("Motion Blur Frames", 0, 200, blur_frames_value);
    if (frame_blend_falloff_frames_) {
        frame_blend_falloff_frames_->set_defer_commit_until_unfocus(false);
        frame_blend_falloff_frames_->set_value_formatter([](int value,
                                                             std::array<char, dev_mode::kSliderFormatBufferSize>& buffer)
                                                             -> std::string_view {
            const int clamped = std::clamp(value, 0, 999);
            const int written = std::snprintf(buffer.data(), buffer.size(), "%d frames", clamped);
            if (written <= 0) {
                return {};
            }
            return std::string_view(buffer.data(), static_cast<std::size_t>(written));
        });
        frame_blend_falloff_frames_->set_value_parser([](const std::string& text) -> std::optional<int> {
            try {
                return std::stoi(text);
            } catch (...) {
                return std::nullopt;
            }
        });
    }

    map_light_dir_strength_ = make_scaled_slider("Directional Offset Strength", 0.0f, 1.0f, vsettings.map_light_dir_offset_strength, 100, 2);

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

    const int grid_subdivide_value = std::clamp(std::max(1, vsettings.grid_subdivide), 0, 8);
    grid_subdivide_ = std::make_unique<DMSlider>("Grid Subdivide", 0, 8, grid_subdivide_value);
    if (grid_subdivide_) {
        grid_subdivide_->set_defer_commit_until_unfocus(false);
        grid_subdivide_->set_value_formatter([](int value,
                                                std::array<char, dev_mode::kSliderFormatBufferSize>& buffer)
                                                -> std::string_view {
            const int clamped = std::clamp(value, 0, 8);
            const int written = std::snprintf(buffer.data(), buffer.size(), "%dx", clamped == 0 ? 1 : clamped);
            if (written <= 0) {
                return {};
            }
            return std::string_view(buffer.data(), static_cast<std::size_t>(written));
        });
        grid_subdivide_->set_value_parser([](const std::string& text) -> std::optional<int> {
            try {
                return std::stoi(text);
            } catch (...) {
                return std::nullopt;
            }
        });
    }

    const int light_grid_subdivide_value =
        std::clamp(std::max(1, vsettings.light_grid_subdivide), 0, 8);
    light_grid_subdivide_ =
        std::make_unique<DMSlider>("Light Grid Subdivide", 0, 8, light_grid_subdivide_value);
    if (light_grid_subdivide_) {
        light_grid_subdivide_->set_defer_commit_until_unfocus(false);
        light_grid_subdivide_->set_value_formatter([](int value,
                                                      std::array<char, dev_mode::kSliderFormatBufferSize>& buffer)
                                                      -> std::string_view {
            const int clamped = std::clamp(value, 0, 8);
            const int written = std::snprintf(buffer.data(), buffer.size(), "%dx", clamped == 0 ? 1 : clamped);
            if (written <= 0) {
                return {};
            }
            return std::string_view(buffer.data(), static_cast<std::size_t>(written));
        });
        light_grid_subdivide_->set_value_parser([](const std::string& text) -> std::optional<int> {
            try {
                return std::stoi(text);
            } catch (...) {
                return std::nullopt;
            }
        });
    }

    auto add_slider_with_help = [&](std::unique_ptr<DMSlider>& slider, const std::string& help_text) {
        if (!slider) {
            return;
        }
        auto slider_widget = std::make_unique<SliderWidget>(slider.get());
        auto help_widget   = std::make_unique<SliderHelpLabel>(help_text);
        rows.push_back({slider_widget.get()});
        rows.push_back({help_widget.get()});
        widget_wrappers_.push_back(std::move(slider_widget));
        widget_wrappers_.push_back(std::move(help_widget));
};

    add_slider_with_help(horizontal_falloff_, "Lower: tighter horizontal fade. Higher: spreads effect wider.");
    add_slider_with_help(vertical_falloff_, "Lower: tighter vertical fade. Higher: spreads effect taller.");
    add_slider_with_help(max_offset_x_, "Lower: limits sideways shift. Higher: allows more horizontal movement.");
    add_slider_with_help(max_offset_y_, "Lower: keeps shadows close vertically. Higher: lets them stretch farther up/down.");
    add_slider_with_help(opacity_sensitivity_percent_, "Lower: react to local brightness. Higher: follow scene-wide light levels.");
    add_slider_with_help(frame_blend_falloff_frames_, "Lower: changes respond instantly. Higher: smooth changes across more frames.");
    add_slider_with_help(map_light_dir_strength_, "Lower: ignore directional light push. Higher: follow main light direction strongly.");
    add_slider_with_help(search_radius_, "Lower: sample nearby cells. Higher: gather lighting from a wider area.");
    add_slider_with_help(grid_subdivide_,
                         "Lower: fewer grid cells. Higher: subdivide the virtual light map grid for smoother detail.");
    add_slider_with_help(light_grid_subdivide_,
                         "Lower: fewer light cells. Higher: subdivide grid for smoother falloff.");

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
    if (opacity_sensitivity_percent_)
        opacity_sensitivity_percent_->set_value( static_cast<int>(std::lround(std::clamp(settings.opacity_sensitivity_percent, 0.0f, 100.0f))));
    if (frame_blend_falloff_frames_)
        frame_blend_falloff_frames_->set_value(std::clamp(settings.frame_blend_falloff_frames, 0, 200));
    if (map_light_dir_strength_)
        map_light_dir_strength_->set_value(static_cast<int>(std::round(settings.virtual_light_map.map_light_dir_offset_strength * 100.0f)));
    if (search_radius_) search_radius_->set_value(std::clamp(settings.virtual_light_map.search_radius, 0, 128));
    if (grid_subdivide_)
        grid_subdivide_->set_value(std::clamp(std::max(1, settings.virtual_light_map.grid_subdivide), 0, 8));
    if (light_grid_subdivide_)
        light_grid_subdivide_->set_value(std::clamp(std::max(1, settings.virtual_light_map.light_grid_subdivide), 0, 8));
    applying_ui_ = false;
}

MapShadowPanel::ReactiveShadowSettings MapShadowPanel::settings_from_ui() {
    ReactiveShadowSettings settings = current_settings_;

    settings.virtual_light_map.horizontal_falloff = read_scaled_slider(horizontal_falloff_, 100, settings.virtual_light_map.horizontal_falloff);
    settings.virtual_light_map.vertical_falloff   = read_scaled_slider(vertical_falloff_, 100, settings.virtual_light_map.vertical_falloff);
    settings.virtual_light_map.max_offset_x       = read_scaled_slider(max_offset_x_, 100, settings.virtual_light_map.max_offset_x);
    settings.virtual_light_map.max_offset_y       = read_scaled_slider(max_offset_y_, 100, settings.virtual_light_map.max_offset_y);
    if (opacity_sensitivity_percent_) {
        settings.opacity_sensitivity_percent =
            static_cast<float>(std::clamp(opacity_sensitivity_percent_->displayed_value(), 0, 100));
    }
    if (frame_blend_falloff_frames_) {
        settings.frame_blend_falloff_frames =
            std::clamp(frame_blend_falloff_frames_->displayed_value(), 0, 200);
    }
    settings.virtual_light_map.map_light_dir_offset_strength =
        read_scaled_slider(map_light_dir_strength_, 100, settings.virtual_light_map.map_light_dir_offset_strength);
    if (search_radius_) {
        settings.virtual_light_map.search_radius = std::clamp(search_radius_->displayed_value(), 0, 128);
    }
    if (grid_subdivide_) {
        int subdivide = std::clamp(grid_subdivide_->displayed_value(), 0, 8);
        settings.virtual_light_map.grid_subdivide = subdivide;
    }
    if (light_grid_subdivide_) {
        int subdivide = std::clamp(light_grid_subdivide_->displayed_value(), 0, 8);
        if (subdivide == 0) {
            subdivide = 1;
        }
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
    last_applied_settings_ = settings;

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

    if (shared_changed && assets_) {
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

