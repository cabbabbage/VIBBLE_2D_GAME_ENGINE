#include "MapShadowPanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string_view>
#include <SDL_ttf.h>

#include "MapLightPanel.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/dev_ui_settings.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "utils/input.hpp"
#include "render/light_map.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"

using nlohmann::json;

namespace {

constexpr std::string_view kReactiveSettingsKeyPrefix = "dev_ui.lighting.map_panel.reactive";

std::string reactive_settings_key(std::string_view suffix) {
    std::string key(kReactiveSettingsKeyPrefix);
    if (!suffix.empty()) {
        key.push_back('.');
        key.append(suffix);
    }
    return key;
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
    slider->set_value(static_cast<int>(std::round(value * static_cast<float>(scale))));
}

}  // namespace

class MapShadowPanel::WarningLabel : public Widget {
public:
    WarningLabel() = default;

    void set_text(std::string text) { text_ = std::move(text); }
    const std::string& text() const { return text_; }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int w) const override {
        if (text_.empty()) {
            return 0;
        }
        const DMLabelStyle& style = DMStyles::Label();
        TTF_Font* font = style.open_font();
        if (!font) {
            return style.font_size;
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended_Wrapped(font, text_.c_str(), color_, std::max(10, w));
        int height = surface ? surface->h : style.font_size;
        if (surface) {
            SDL_FreeSurface(surface);
        }
        TTF_CloseFont(font);
        return height + DMSpacing::small_gap();
    }

    bool handle_event(const SDL_Event&) override { return false; }

    void render(SDL_Renderer* r) const override {
        if (text_.empty() || !r) {
            return;
        }
        const DMLabelStyle& style = DMStyles::Label();
        TTF_Font* font = style.open_font();
        if (!font) {
            return;
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended_Wrapped(font, text_.c_str(), color_, std::max(10, rect_.w));
        if (surface) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surface);
            if (tex) {
                SDL_Rect dst{rect_.x, rect_.y, surface->w, surface->h};
                SDL_RenderCopy(r, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surface);
        }
        TTF_CloseFont(font);
    }

    bool wants_full_row() const override { return true; }

    void set_color(SDL_Color color) { color_ = color; }

private:
    SDL_Rect rect_{0, 0, 0, 0};
    std::string text_;
    SDL_Color color_{255, 120, 120, 255};
};

MapShadowPanel::MapShadowPanel(MapLightPanel* light_panel, Assets* assets, int x, int y)
    : DockableCollapsible("Shadows", true, x, y)
    , light_panel_(light_panel)
    , assets_(assets) {
    set_expanded(true);
    build_ui();
    update_save_status(true);
}

MapShadowPanel::~MapShadowPanel() = default;

void MapShadowPanel::set_map_info(nlohmann::json* map_info, SaveCallback on_save) {
    map_info_ = map_info;
    on_save_ = std::move(on_save);
    reactive_settings_initialized_ = false;
    sync_ui_from_json();
}

void MapShadowPanel::set_reactive_settings(render_pipeline::shading::ReactiveShadowSettings* settings) {
    reactive_settings_shared_ = settings;
    if (reactive_settings_shared_ && reactive_settings_initialized_) {
        *reactive_settings_shared_ = render_pipeline::shading::sanitize_reactive_shadow_settings(last_applied_settings_);
    }
}

void MapShadowPanel::open() {
    set_visible(true);
    set_expanded(true);
}

void MapShadowPanel::close() { set_visible(false); }

void MapShadowPanel::toggle() {
    if (is_visible()) {
        close();
    } else {
        open();
    }
}

bool MapShadowPanel::is_visible() const { return visible_; }

void MapShadowPanel::update(const Input& input, int screen_w, int screen_h) {
    if (!visible_) return;

    if (screen_w > 0) {
        screen_width_px_ = screen_w;
    }
    if (screen_h > 0) {
        screen_height_px_ = screen_h;
    }

    DockableCollapsible::update(input, screen_w, screen_h);
    apply_immediate_settings();
}

bool MapShadowPanel::handle_event(const SDL_Event& e) {
    if (!visible_) return false;

    bool used = DockableCollapsible::handle_event(e);
    if (used) {
        needs_sync_to_json_ = true;
    }
    if (needs_sync_to_json_) {
        sync_json_from_ui();
    }
    return used;
}

void MapShadowPanel::render(SDL_Renderer* r) const {
    if (!visible_) return;
    DockableCollapsible::render(r);
}

bool MapShadowPanel::is_point_inside(int x, int y) const {
    return DockableCollapsible::is_point_inside(x, y);
}

void MapShadowPanel::render_content(SDL_Renderer* r) const {
    DockableCollapsible::render_content(r);
    render_light_map_preview(r);
}

void MapShadowPanel::layout_custom_content(int screen_w, int screen_h) const {
    DockableCollapsible::layout_custom_content(screen_w, screen_h);

    preview_rect_ = SDL_Rect{0, 0, 0, 0};
    preview_grid_rect_ = SDL_Rect{0, 0, 0, 0};

    if (!expanded_ || body_viewport_h_ <= 0) {
        return;
    }

    const int gap = DMSpacing::item_gap();
    preview_rect_ = SDL_Rect{
        body_viewport_.x + body_viewport_.w + gap,
        body_viewport_.y,
        kPreviewWidth,
        body_viewport_h_
    };

    if (preview_rect_.w <= 0 || preview_rect_.h <= 0) {
        preview_rect_ = SDL_Rect{0, 0, 0, 0};
        return;
    }

    const int preview_right = preview_rect_.x + preview_rect_.w;
    const int desired_width = preview_right + padding_ - rect_.x;
    if (desired_width > rect_.w) {
        rect_.w = desired_width;
    }

    preview_grid_rect_ = preview_rect_;
    preview_grid_rect_.x += kPreviewPadding;
    preview_grid_rect_.y += kPreviewPadding;
    preview_grid_rect_.w = std::max(0, preview_grid_rect_.w - 2 * kPreviewPadding);
    preview_grid_rect_.h = std::max(0, preview_grid_rect_.h - 2 * kPreviewPadding);

    if (!show_header_) {
        return;
    }

    const bool lock_visible = lock_rect_.w > 0 && lock_rect_.h > 0;
    const bool close_visible = close_rect_.w > 0 && close_rect_.h > 0;
    const int button_width = DMButton::height();

    header_rect_.x = rect_.x + padding_;
    int next_x = rect_.x + rect_.w - padding_;

    if (close_visible) {
        next_x -= button_width;
        close_rect_ = SDL_Rect{ next_x, rect_.y + padding_, button_width, button_width };
    }

    if (lock_visible) {
        next_x -= button_width;
        lock_rect_ = SDL_Rect{ next_x, rect_.y + padding_, button_width, button_width };
    }

    header_rect_.w = std::max(0, next_x - header_rect_.x);
    if (header_btn_) header_btn_->set_rect(header_rect_);
    if (close_visible && close_btn_) close_btn_->set_rect(close_rect_);
    if (lock_visible && lock_btn_) lock_btn_->set_rect(lock_rect_);

    if (header_rect_.w > 0) {
        int grip_w = std::max(32, std::min(80, std::max(1, header_rect_.w) / 3));
        grip_w = std::min(grip_w, header_rect_.w);
        handle_rect_ = SDL_Rect{ header_rect_.x, header_rect_.y, grip_w, header_rect_.h };
    } else {
        handle_rect_ = SDL_Rect{ header_rect_.x, header_rect_.y, 0, header_rect_.h };
    }
}

void MapShadowPanel::render_light_map_preview(SDL_Renderer* renderer) const {
    if (!renderer || preview_rect_.w <= 0 || preview_rect_.h <= 0) {
        return;
    }

    SDL_Rect prev_clip;
    SDL_RenderGetClipRect(renderer, &prev_clip);
#if SDL_VERSION_ATLEAST(2,0,4)
    const SDL_bool was_clipping = SDL_RenderIsClipEnabled(renderer);
#else
    const SDL_bool was_clipping = (prev_clip.w != 0 || prev_clip.h != 0) ? SDL_TRUE : SDL_FALSE;
#endif
    SDL_RenderSetClipRect(renderer, &preview_rect_);

    const SDL_Color panel_bg = DMStyles::PanelBG();
    auto shade = [](int base, int delta) {
        return static_cast<Uint8>(std::clamp(base + delta, 0, 255));
    };
    SDL_Color preview_bg{ shade(panel_bg.r, 6), shade(panel_bg.g, 6), shade(panel_bg.b, 6), panel_bg.a };
    SDL_Color grid_bg{ shade(panel_bg.r, -18), shade(panel_bg.g, -18), shade(panel_bg.b, -18), panel_bg.a };
    const SDL_Color border = DMStyles::Border();
    const SDL_Color highlight = DMStyles::HighlightColor();

    SDL_SetRenderDrawColor(renderer, preview_bg.r, preview_bg.g, preview_bg.b, preview_bg.a);
    SDL_RenderFillRect(renderer, &preview_rect_);

    if (preview_grid_rect_.w > 0 && preview_grid_rect_.h > 0) {
        SDL_SetRenderDrawColor(renderer, grid_bg.r, grid_bg.g, grid_bg.b, grid_bg.a);
        SDL_RenderFillRect(renderer, &preview_grid_rect_);
    }

    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &preview_rect_);

    const VirtualLightMap* map = current_virtual_light_map();
    std::array<int, VirtualLightMap::kGridWidth + 1> column_edges{};
    std::array<int, VirtualLightMap::kGridHeight + 1> row_edges{};
    if (preview_grid_rect_.w > 0 && preview_grid_rect_.h > 0) {
        for (int x = 0; x <= VirtualLightMap::kGridWidth; ++x) {
            float t = static_cast<float>(x) / static_cast<float>(VirtualLightMap::kGridWidth);
            column_edges[x] = preview_grid_rect_.x + static_cast<int>(std::lround(t * static_cast<float>(preview_grid_rect_.w)));
        }
        for (int y = 0; y <= VirtualLightMap::kGridHeight; ++y) {
            float t = static_cast<float>(y) / static_cast<float>(VirtualLightMap::kGridHeight);
            row_edges[y] = preview_grid_rect_.y + static_cast<int>(std::lround(t * static_cast<float>(preview_grid_rect_.h)));
        }
    }

    if (map && preview_grid_rect_.w > 0 && preview_grid_rect_.h > 0) {
        for (int y = 0; y < VirtualLightMap::kGridHeight; ++y) {
            for (int x = 0; x < VirtualLightMap::kGridWidth; ++x) {
                float value = std::clamp(map->at(x, y), 0.0f, 1.0f);
                Uint8 intensity = static_cast<Uint8>(std::lround(value * 255.0f));
                SDL_SetRenderDrawColor(renderer, intensity, intensity, intensity, 255);
                SDL_Rect cell_rect{
                    column_edges[x],
                    row_edges[y],
                    std::max(1, column_edges[x + 1] - column_edges[x]),
                    std::max(1, row_edges[y + 1] - row_edges[y])
                };
                SDL_RenderFillRect(renderer, &cell_rect);
            }
        }
    }

    int player_cell_x = -1;
    int player_cell_y = -1;
    if (screen_width_px_ > 0 && screen_height_px_ > 0) {
        if (auto player_screen = player_screen_position()) {
            int px = std::clamp(player_screen->x, 0, std::max(0, screen_width_px_ - 1));
            int py = std::clamp(player_screen->y, 0, std::max(0, screen_height_px_ - 1));
            player_cell_x = std::clamp((px * VirtualLightMap::kGridWidth) / std::max(1, screen_width_px_), 0, VirtualLightMap::kGridWidth - 1);
            player_cell_y = std::clamp((py * VirtualLightMap::kGridHeight) / std::max(1, screen_height_px_), 0, VirtualLightMap::kGridHeight - 1);
        }
    }

    if (player_cell_x >= 0 && player_cell_y >= 0 && preview_grid_rect_.w > 0 && preview_grid_rect_.h > 0) {
        SDL_Rect player_rect{
            column_edges[player_cell_x],
            row_edges[player_cell_y],
            std::max(1, column_edges[player_cell_x + 1] - column_edges[player_cell_x]),
            std::max(1, row_edges[player_cell_y + 1] - row_edges[player_cell_y])
        };
        SDL_SetRenderDrawColor(renderer, highlight.r, highlight.g, highlight.b, 255);
        SDL_RenderDrawRect(renderer, &player_rect);
    }

    int quadrant_count = current_quadrant_count();
    int player_quadrant = -1;
    float start_angle = 0.0f;
    float end_angle = 0.0f;
    if (player_cell_x >= 0 && player_cell_y >= 0 && quadrant_count > 0) {
        constexpr float kTwoPi = 6.28318530717958647692f;
        const float step = kTwoPi / static_cast<float>(quadrant_count);
        float player_fx = static_cast<float>(player_cell_x) + 0.5f;
        float player_fy = static_cast<float>(player_cell_y) + 0.5f;
        float center_fx = static_cast<float>(VirtualLightMap::kGridWidth) * 0.5f;
        float center_fy = static_cast<float>(VirtualLightMap::kGridHeight) * 0.5f;
        float dy = center_fy - player_fy;
        float dx = player_fx - center_fx;
        float angle = std::atan2(dy, dx);
        if (angle < 0.0f) {
            angle += kTwoPi;
        }
        player_quadrant = static_cast<int>(std::floor(angle / step));
        player_quadrant = std::clamp(player_quadrant, 0, quadrant_count - 1);
        start_angle = player_quadrant * step;
        end_angle = start_angle + step;

        if (preview_grid_rect_.w > 0 && preview_grid_rect_.h > 0) {
            SDL_FPoint center_point{
                static_cast<float>(preview_grid_rect_.x) + static_cast<float>(preview_grid_rect_.w) * 0.5f,
                static_cast<float>(preview_grid_rect_.y) + static_cast<float>(preview_grid_rect_.h) * 0.5f
            };
            float radius = std::sqrt(
                std::pow(static_cast<float>(preview_grid_rect_.w) * 0.5f, 2.0f) +
                std::pow(static_cast<float>(preview_grid_rect_.h) * 0.5f, 2.0f));

            auto draw_boundary = [&](float angle_radians) {
                float dir_x = std::cos(angle_radians);
                float dir_y = -std::sin(angle_radians);
                int x2 = static_cast<int>(std::lround(center_point.x + dir_x * radius));
                int y2 = static_cast<int>(std::lround(center_point.y + dir_y * radius));
                SDL_RenderDrawLine(renderer,
                                   static_cast<int>(std::lround(center_point.x)),
                                   static_cast<int>(std::lround(center_point.y)),
                                   x2,
                                   y2);
            };

            SDL_SetRenderDrawColor(renderer, highlight.r, highlight.g, highlight.b, 255);
            draw_boundary(start_angle);
            draw_boundary(end_angle);

            SDL_SetRenderDrawColor(renderer, highlight.r, highlight.g, highlight.b, 90);
            const int fill_steps = 20;
            for (int i = 0; i <= fill_steps; ++i) {
                float angle_radians = start_angle + (end_angle - start_angle) * (static_cast<float>(i) / static_cast<float>(fill_steps));
                float dir_x = std::cos(angle_radians);
                float dir_y = -std::sin(angle_radians);
                int x2 = static_cast<int>(std::lround(center_point.x + dir_x * radius));
                int y2 = static_cast<int>(std::lround(center_point.y + dir_y * radius));
                SDL_RenderDrawLine(renderer,
                                   static_cast<int>(std::lround(center_point.x)),
                                   static_cast<int>(std::lround(center_point.y)),
                                   x2,
                                   y2);
            }
        }
    }

    if (preview_grid_rect_.w > 0 && preview_grid_rect_.h > 0) {
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &preview_grid_rect_);
    }

    auto draw_label = [&](const std::string& text, int y) {
        const DMLabelStyle& label_style = DMStyles::Label();
        TTF_Font* font = label_style.open_font();
        if (!font) {
            return;
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), label_style.color);
        if (surface) {
            SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
            if (texture) {
                SDL_Rect dst{
                    preview_rect_.x + kPreviewPadding,
                    y,
                    surface->w,
                    surface->h
                };
                const int max_x = preview_rect_.x + preview_rect_.w - kPreviewPadding;
                if (dst.x + dst.w > max_x) {
                    dst.x = std::max(preview_rect_.x + kPreviewPadding, max_x - dst.w);
                }
                SDL_RenderCopy(renderer, texture, nullptr, &dst);
                SDL_DestroyTexture(texture);
            }
            SDL_FreeSurface(surface);
        }
        TTF_CloseFont(font);
    };

    if (!map) {
        draw_label("Light map unavailable", preview_rect_.y + kPreviewPadding);
    } else if (player_cell_x < 0 || player_cell_y < 0) {
        draw_label("Player position unavailable", preview_rect_.y + kPreviewPadding);
    } else {
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "Quadrant %d of %d", player_quadrant + 1, std::max(1, quadrant_count));
        draw_label(buffer, preview_rect_.y + kPreviewPadding);
    }

    if (was_clipping == SDL_TRUE) {
        SDL_RenderSetClipRect(renderer, &prev_clip);
    } else {
        SDL_RenderSetClipRect(renderer, nullptr);
    }
}

const VirtualLightMap* MapShadowPanel::current_virtual_light_map() const {
    return assets_ ? assets_->virtual_light_map() : nullptr;
}

std::optional<SDL_Point> MapShadowPanel::player_screen_position() const {
    if (!assets_ || !assets_->player) {
        return std::nullopt;
    }
    return assets_->getView().map_to_screen(assets_->player->pos);
}

int MapShadowPanel::current_quadrant_count() const {
    if (quadrant_count_) {
        return clamp_int(quadrant_count_->displayed_value(), 1, 24);
    }
    return std::max(1, last_applied_settings_.virtual_light_map.quadrant_count);
}

void MapShadowPanel::update_save_status(bool success) const {
    if (!warning_label_) {
        return;
    }
    const std::string failure_message = "Failed to save map lighting changes. Check logs.";
    if (success) {
        if (!persistence_warning_text_.empty()) {
            persistence_warning_text_.clear();
            warning_label_->set_text({});
            const_cast<MapShadowPanel*>(this)->layout();
        }
        return;
    }
    if (persistence_warning_text_ != failure_message) {
        persistence_warning_text_ = failure_message;
        warning_label_->set_text(persistence_warning_text_);
        const_cast<MapShadowPanel*>(this)->layout();
    }
}

void MapShadowPanel::build_ui() {
    quadrant_count_ = std::make_unique<DMSlider>("Quadrants", 1, 24, last_applied_settings_.virtual_light_map.quadrant_count);
    if (quadrant_count_) quadrant_count_->set_defer_commit_until_unfocus(true);

    auto make_float_slider = [&](const std::string& label,
                                 float min,
                                 float max,
                                 float initial,
                                 int scale,
                                 int decimals = 2) {
        int min_i = static_cast<int>(std::round(min * static_cast<float>(scale)));
        int max_i = static_cast<int>(std::round(max * static_cast<float>(scale)));
        int init_i = static_cast<int>(std::round(initial * static_cast<float>(scale)));
        auto slider = std::make_unique<DMSlider>(label, min_i, max_i, init_i);
        slider->set_value_formatter([scale, decimals](int value, auto& buffer) -> std::string_view {
            const float actual = static_cast<float>(value) / static_cast<float>(scale);
            const char* fmt = (decimals == 3) ? "%.3f" : "%.2f";
            std::snprintf(buffer.data(), buffer.size(), fmt, actual);
            return std::string_view(buffer.data());
        });
        slider->set_value_parser([scale, min, max](const std::string& text) -> std::optional<int> {
            try {
                float parsed = std::stof(text);
                parsed = std::clamp(parsed, min, max);
                return static_cast<int>(std::round(parsed * static_cast<float>(scale)));
            } catch (...) {
                return std::nullopt;
            }
        });
        slider->set_defer_commit_until_unfocus(true);
        return slider;
    };

    quadrant_distance_falloff_ = make_float_slider("Distance Falloff", 0.05f, 10.0f,
        last_applied_settings_.virtual_light_map.distance_strength_falloff, 100, 2);
    quadrant_directional_strength_ = make_float_slider("Directional Strength", 0.0f, 4.0f,
        last_applied_settings_.virtual_light_map.directional_strength, 100, 2);

    reactive_offsets_enabled_ = std::make_unique<DMCheckbox>("Enable Offsets", last_applied_settings_.directionality.enable_offsets);
    reactive_opacity_enabled_ = std::make_unique<DMCheckbox>("Enable Opacity", last_applied_settings_.response.enable_opacity);
    reactive_temporal_enabled_ = std::make_unique<DMCheckbox>("Enable Temporal", last_applied_settings_.stability.enable_temporal_smoothing);

    reactive_kernel_radius_       = std::make_unique<DMSlider>("Kernel Radius", 1, 16, last_applied_settings_.sampling.kernel_radius);
    reactive_outer_ring_weight_   = make_float_slider("Outer Ring Weight", 0.0f, 3.0f, last_applied_settings_.sampling.outer_ring_weight, 100);
    reactive_diagonal_weight_     = make_float_slider("Diagonal Weight", 0.0f, 2.0f, last_applied_settings_.sampling.diagonal_weight, 100);
    reactive_gradient_sensitivity_= make_float_slider("Gradient Sensitivity", 0.0f, 2.0f, last_applied_settings_.directionality.gradient_sensitivity, 100);
    reactive_offset_strength_     = make_float_slider("Offset Strength", 0.0f, 2.0f, last_applied_settings_.directionality.offset_strength, 100);
    reactive_max_offset_ratio_    = make_float_slider("Max Offset Ratio", 0.0f, 1.0f, last_applied_settings_.directionality.max_offset_ratio, 100);
    reactive_front_weight_        = make_float_slider("Front Weight", 0.0f, 5.0f, last_applied_settings_.directionality.front_weight, 100);
    reactive_side_weight_         = make_float_slider("Side Weight", 0.0f, 5.0f, last_applied_settings_.directionality.side_weight, 100);
    reactive_back_weight_         = make_float_slider("Back Weight", 0.0f, 5.0f, last_applied_settings_.directionality.back_weight, 100);
    reactive_scale_factor_        = make_float_slider("Scale Factor", 0.1f, 4.0f, last_applied_settings_.output.scale_factor, 100);
    reactive_map_line_weight_     = make_float_slider("map_light_weight", 0.0f, 5.0f, last_applied_settings_.output.map_line_weight, 100);
    reactive_parallax_strength_   = make_float_slider("Parallax Strength", 0.0f, 5.0f, last_applied_settings_.output.parallax_strength, 100);
    reactive_opacity_strength_    = make_float_slider("Opacity Strength", 0.0f, 3.0f, last_applied_settings_.response.opacity_strength, 100);
    reactive_min_opacity_         = make_float_slider("Min Opacity", 0.0f, 1.0f, last_applied_settings_.response.min_opacity, 100);
    reactive_max_opacity_         = make_float_slider("Max Opacity", 0.0f, 1.0f, last_applied_settings_.response.max_opacity, 100);
    reactive_temporal_smoothing_  = make_float_slider("Temporal Smoothing", 0.0f, 0.999f, last_applied_settings_.stability.temporal_smoothing, 1000, 3);
    reactive_front_opacity_boost_ = make_float_slider("Front Opacity Boost", 0.0f, 10.0f, last_applied_settings_.response.front_opacity_boost, 100);
    reactive_similarity_threshold_= make_float_slider("Reuse Similarity Threshold", 0.0f, 1.0f, last_applied_settings_.stability.reuse_similarity_threshold, 1000, 3);

    if (reactive_kernel_radius_) reactive_kernel_radius_->set_defer_commit_until_unfocus(true);
    if (reactive_scale_factor_) reactive_scale_factor_->set_defer_commit_until_unfocus(true);
    if (reactive_map_line_weight_) reactive_map_line_weight_->set_defer_commit_until_unfocus(true);
    if (reactive_parallax_strength_) reactive_parallax_strength_->set_defer_commit_until_unfocus(true);

    opacity_section_btn_  = std::make_unique<DMButton>("", &DMStyles::HeaderButton(), 220, DMButton::height());
    placement_section_btn_= std::make_unique<DMButton>("", &DMStyles::HeaderButton(), 220, DMButton::height());
    scale_section_btn_    = std::make_unique<DMButton>("", &DMStyles::HeaderButton(), 220, DMButton::height());

    rebuild_rows();
}

void MapShadowPanel::update_section_header_labels() {
    auto label_for = [](const std::string& title, bool collapsed) {
        return std::string(collapsed ? "[\xE2\x86\x93] " : "[\xE2\x86\x91] ") + title;
    };
    if (opacity_section_btn_) {
        opacity_section_btn_->set_text(label_for("Opacity Settings", opacity_section_collapsed_));
    }
    if (placement_section_btn_) {
        placement_section_btn_->set_text(label_for("Placement & Offsets", placement_section_collapsed_));
    }
    if (scale_section_btn_) {
        scale_section_btn_->set_text(label_for("Scale", scale_section_collapsed_));
    }
}

void MapShadowPanel::rebuild_rows() {
    update_section_header_labels();

    widget_wrappers_.clear();
    widget_wrappers_.reserve(128);

    auto add_widget = [this](std::unique_ptr<Widget> w) -> Widget* {
        Widget* raw = w.get();
        widget_wrappers_.push_back(std::move(w));
        return raw;
    };

    Rows rows;

    auto warning_label = std::make_unique<WarningLabel>();
    warning_label_ = warning_label.get();
    warning_label_->set_color(SDL_Color{255, 120, 120, 255});
    if (!persistence_warning_text_.empty()) {
        warning_label_->set_text(persistence_warning_text_);
    }
    rows.push_back({ add_widget(std::move(warning_label)) });

    rows.push_back({
        add_widget(std::make_unique<SliderWidget>(quadrant_count_.get()))
    });
    rows.push_back({
        add_widget(std::make_unique<SliderWidget>(quadrant_distance_falloff_.get())),
        add_widget(std::make_unique<SliderWidget>(quadrant_directional_strength_.get()))
    });
    rows.push_back({
        add_widget(std::make_unique<CheckboxWidget>(reactive_offsets_enabled_.get())),
        add_widget(std::make_unique<CheckboxWidget>(reactive_temporal_enabled_.get()))
    });
    rows.push_back({ add_widget(std::make_unique<CheckboxWidget>(reactive_opacity_enabled_.get())) });
    rows.push_back({
        add_widget(std::make_unique<SliderWidget>(reactive_map_line_weight_.get())),
        add_widget(std::make_unique<SliderWidget>(reactive_parallax_strength_.get()))
    });

    rows.push_back({ add_widget(std::make_unique<ButtonWidget>(opacity_section_btn_.get(), [this]() { toggle_opacity_section(); })) });
    if (!opacity_section_collapsed_) {
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_opacity_strength_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_min_opacity_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_max_opacity_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_front_opacity_boost_.get()))
        });
    }

    rows.push_back({ add_widget(std::make_unique<ButtonWidget>(placement_section_btn_.get(), [this]() { toggle_placement_section(); })) });
    if (!placement_section_collapsed_) {
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_kernel_radius_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_outer_ring_weight_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_diagonal_weight_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_gradient_sensitivity_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_offset_strength_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_max_offset_ratio_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_front_weight_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_side_weight_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_back_weight_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_temporal_smoothing_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_similarity_threshold_.get()))
        });
    }

    rows.push_back({ add_widget(std::make_unique<ButtonWidget>(scale_section_btn_.get(), [this]() { toggle_scale_section(); })) });
    if (!scale_section_collapsed_) {
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_scale_factor_.get()))
        });
    }

    set_rows(rows);
}

void MapShadowPanel::toggle_opacity_section() {
    opacity_section_collapsed_ = !opacity_section_collapsed_;
    rebuild_rows();
}

void MapShadowPanel::toggle_placement_section() {
    placement_section_collapsed_ = !placement_section_collapsed_;
    rebuild_rows();
}

void MapShadowPanel::toggle_scale_section() {
    scale_section_collapsed_ = !scale_section_collapsed_;
    rebuild_rows();
}

void MapShadowPanel::sync_ui_from_json() {
    if (!light_panel_) {
        return;
    }

    json& reactive_json = ensure_reactive_settings_json();
    render_pipeline::shading::ReactiveShadowSettings settings =
        render_pipeline::shading::reactive_shadow_settings_from_json(
            reactive_json, load_reactive_settings_from_dev_settings());
    settings = render_pipeline::shading::sanitize_reactive_shadow_settings(settings);

    last_applied_settings_ = settings;

    if (quadrant_count_) quadrant_count_->set_value(settings.virtual_light_map.quadrant_count);
    set_slider_scaled(quadrant_distance_falloff_, settings.virtual_light_map.distance_strength_falloff, 100);
    set_slider_scaled(quadrant_directional_strength_, settings.virtual_light_map.directional_strength, 100);

    set_reactive_checkboxes(settings);
    set_reactive_sliders(settings);
    persist_reactive_settings_to_dev_settings(settings);
    reactive_settings_initialized_ = true;
    if (reactive_settings_shared_) {
        *reactive_settings_shared_ = settings;
    }

    needs_sync_to_json_ = false;
}

void MapShadowPanel::sync_json_from_ui() {
    if (!light_panel_) {
        return;
    }

    render_pipeline::shading::ReactiveShadowSettings settings = current_settings_from_ui();
    write_reactive_settings_to_json(settings);
    set_reactive_sliders(settings);
    set_reactive_checkboxes(settings);
    persist_reactive_settings_to_dev_settings(settings);
    needs_sync_to_json_ = false;
}

render_pipeline::shading::ReactiveShadowSettings MapShadowPanel::current_settings_from_ui() const {
    render_pipeline::shading::ReactiveShadowSettings settings = last_applied_settings_;

    if (quadrant_count_) {
        settings.virtual_light_map.quadrant_count = clamp_int(quadrant_count_->displayed_value(), 1, 24);
    }
    settings.virtual_light_map.distance_strength_falloff =
        slider_value_scaled(quadrant_distance_falloff_, settings.virtual_light_map.distance_strength_falloff, 100);
    settings.virtual_light_map.directional_strength =
        slider_value_scaled(quadrant_directional_strength_, settings.virtual_light_map.directional_strength, 100);

    if (reactive_kernel_radius_) {
        settings.sampling.kernel_radius = clamp_int(reactive_kernel_radius_->displayed_value(), 1, 16);
    }
    settings.sampling.outer_ring_weight = slider_value_scaled(reactive_outer_ring_weight_, settings.sampling.outer_ring_weight, 100);
    settings.sampling.diagonal_weight   = slider_value_scaled(reactive_diagonal_weight_, settings.sampling.diagonal_weight, 100);

    if (reactive_offsets_enabled_) {
        settings.directionality.enable_offsets = reactive_offsets_enabled_->value();
    }
    if (reactive_opacity_enabled_) {
        settings.response.enable_opacity = reactive_opacity_enabled_->value();
    }
    settings.directionality.gradient_sensitivity = slider_value_scaled(reactive_gradient_sensitivity_, settings.directionality.gradient_sensitivity, 100);
    settings.directionality.offset_strength      = slider_value_scaled(reactive_offset_strength_, settings.directionality.offset_strength, 100);
    settings.directionality.max_offset_ratio     = slider_value_scaled(reactive_max_offset_ratio_, settings.directionality.max_offset_ratio, 100);
    settings.directionality.front_weight         = slider_value_scaled(reactive_front_weight_, settings.directionality.front_weight, 100);
    settings.directionality.side_weight          = slider_value_scaled(reactive_side_weight_, settings.directionality.side_weight, 100);
    settings.directionality.back_weight          = slider_value_scaled(reactive_back_weight_, settings.directionality.back_weight, 100);

    settings.output.scale_factor      = slider_value_scaled(reactive_scale_factor_, settings.output.scale_factor, 100);
    settings.output.map_line_weight   = slider_value_scaled(reactive_map_line_weight_, settings.output.map_line_weight, 100);
    settings.output.parallax_strength = slider_value_scaled(reactive_parallax_strength_, settings.output.parallax_strength, 100);

    settings.response.opacity_strength    = slider_value_scaled(reactive_opacity_strength_, settings.response.opacity_strength, 100);
    settings.response.min_opacity         = slider_value_scaled(reactive_min_opacity_, settings.response.min_opacity, 100);
    settings.response.max_opacity         = slider_value_scaled(reactive_max_opacity_, settings.response.max_opacity, 100);
    settings.response.front_opacity_boost = slider_value_scaled(reactive_front_opacity_boost_, settings.response.front_opacity_boost, 100);

    if (reactive_temporal_enabled_) {
        settings.stability.enable_temporal_smoothing = reactive_temporal_enabled_->value();
    }
    settings.stability.temporal_smoothing = slider_value_scaled(reactive_temporal_smoothing_, settings.stability.temporal_smoothing, 1000);
    settings.stability.reuse_similarity_threshold = slider_value_scaled(
        reactive_similarity_threshold_, settings.stability.reuse_similarity_threshold, 1000);

    return render_pipeline::shading::sanitize_reactive_shadow_settings(settings);
}

void MapShadowPanel::set_reactive_sliders(const render_pipeline::shading::ReactiveShadowSettings& settings) {
    set_slider_scaled(quadrant_distance_falloff_, settings.virtual_light_map.distance_strength_falloff, 100);
    if (quadrant_count_) quadrant_count_->set_value(settings.virtual_light_map.quadrant_count);
    set_slider_scaled(quadrant_directional_strength_, settings.virtual_light_map.directional_strength, 100);

    if (reactive_kernel_radius_) reactive_kernel_radius_->set_value(settings.sampling.kernel_radius);
    set_slider_scaled(reactive_outer_ring_weight_, settings.sampling.outer_ring_weight, 100);
    set_slider_scaled(reactive_diagonal_weight_, settings.sampling.diagonal_weight, 100);
    set_slider_scaled(reactive_gradient_sensitivity_, settings.directionality.gradient_sensitivity, 100);
    set_slider_scaled(reactive_offset_strength_, settings.directionality.offset_strength, 100);
    set_slider_scaled(reactive_max_offset_ratio_, settings.directionality.max_offset_ratio, 100);
    set_slider_scaled(reactive_front_weight_, settings.directionality.front_weight, 100);
    set_slider_scaled(reactive_side_weight_, settings.directionality.side_weight, 100);
    set_slider_scaled(reactive_back_weight_, settings.directionality.back_weight, 100);
    set_slider_scaled(reactive_scale_factor_, settings.output.scale_factor, 100);
    set_slider_scaled(reactive_map_line_weight_, settings.output.map_line_weight, 100);
    set_slider_scaled(reactive_parallax_strength_, settings.output.parallax_strength, 100);
    set_slider_scaled(reactive_opacity_strength_, settings.response.opacity_strength, 100);
    set_slider_scaled(reactive_min_opacity_, settings.response.min_opacity, 100);
    set_slider_scaled(reactive_max_opacity_, settings.response.max_opacity, 100);
    set_slider_scaled(reactive_front_opacity_boost_, settings.response.front_opacity_boost, 100);
    set_slider_scaled(reactive_temporal_smoothing_, settings.stability.temporal_smoothing, 1000);
    set_slider_scaled(reactive_similarity_threshold_, settings.stability.reuse_similarity_threshold, 1000);
}

void MapShadowPanel::set_reactive_checkboxes(const render_pipeline::shading::ReactiveShadowSettings& settings) {
    if (reactive_offsets_enabled_) reactive_offsets_enabled_->set_value(settings.directionality.enable_offsets);
    if (reactive_opacity_enabled_) reactive_opacity_enabled_->set_value(settings.response.enable_opacity);
    if (reactive_temporal_enabled_) reactive_temporal_enabled_->set_value(settings.stability.enable_temporal_smoothing);
}

render_pipeline::shading::ReactiveShadowSettings MapShadowPanel::load_reactive_settings_from_dev_settings() const {
    using devmode::ui_settings::load_bool;
    using devmode::ui_settings::load_number;

    render_pipeline::shading::ReactiveShadowSettings settings =
        render_pipeline::shading::sanitize_reactive_shadow_settings({});

    settings.directionality.enable_offsets =
        load_bool(reactive_settings_key("directionality.enable_offsets"), settings.directionality.enable_offsets);
    settings.directionality.gradient_sensitivity = static_cast<float>(
        load_number(reactive_settings_key("directionality.gradient_sensitivity"), settings.directionality.gradient_sensitivity));
    settings.directionality.gradient_sensitivity = static_cast<float>(
        load_number(reactive_settings_key("directionality.gradient_deadzone"), settings.directionality.gradient_sensitivity));
    settings.directionality.offset_strength = static_cast<float>(
        load_number(reactive_settings_key("directionality.offset_strength"), settings.directionality.offset_strength));
    const float legacy_ratio_x = static_cast<float>(
        load_number(reactive_settings_key("directionality.offset_ratio_x"), settings.directionality.offset_strength));
    const float legacy_ratio_y = static_cast<float>(
        load_number(reactive_settings_key("directionality.offset_ratio_y"), settings.directionality.offset_strength));
    const float legacy_bias_x = static_cast<float>(
        load_number(reactive_settings_key("directionality.offset_x_bias"), 1.0));
    const float legacy_bias_y = static_cast<float>(
        load_number(reactive_settings_key("directionality.offset_y_bias"), 1.0));
    settings.directionality.offset_strength =
        std::max(settings.directionality.offset_strength, std::max(legacy_ratio_x * legacy_bias_x, legacy_ratio_y * legacy_bias_y));
    settings.directionality.max_offset_ratio = static_cast<float>(
        load_number(reactive_settings_key("directionality.max_offset_ratio"), settings.directionality.max_offset_ratio));
    const float legacy_max_ratio_x = static_cast<float>(
        load_number(reactive_settings_key("directionality.offset_max_ratio_x"), settings.directionality.max_offset_ratio));
    const float legacy_max_ratio_y = static_cast<float>(
        load_number(reactive_settings_key("directionality.offset_max_ratio_y"), settings.directionality.max_offset_ratio));
    settings.directionality.max_offset_ratio =
        std::max(settings.directionality.max_offset_ratio, std::max(legacy_max_ratio_x, legacy_max_ratio_y));
    settings.directionality.front_weight = static_cast<float>(
        load_number(reactive_settings_key("directionality.front_weight"), settings.directionality.front_weight));
    settings.directionality.side_weight = static_cast<float>(
        load_number(reactive_settings_key("directionality.side_weight"), settings.directionality.side_weight));
    settings.directionality.back_weight = static_cast<float>(
        load_number(reactive_settings_key("directionality.back_weight"), settings.directionality.back_weight));

    settings.output.scale_factor = static_cast<float>(
        load_number(reactive_settings_key("output.scale_factor"), settings.output.scale_factor));
    settings.output.map_line_weight = static_cast<float>(
        load_number(reactive_settings_key("output.map_line_weight"), settings.output.map_line_weight));
    settings.output.parallax_strength = static_cast<float>(
        load_number(reactive_settings_key("output.parallax_strength"), settings.output.parallax_strength));

    settings.response.enable_opacity =
        load_bool(reactive_settings_key("response.enable_opacity"), settings.response.enable_opacity);
    settings.response.opacity_strength = static_cast<float>(
        load_number(reactive_settings_key("response.opacity_strength"), settings.response.opacity_strength));
    settings.response.opacity_strength = static_cast<float>(
        load_number(reactive_settings_key("response.opacity_gamma"), settings.response.opacity_strength));
    settings.response.min_opacity = static_cast<float>(
        load_number(reactive_settings_key("response.min_opacity"), settings.response.min_opacity));
    settings.response.min_opacity = static_cast<float>(
        load_number(reactive_settings_key("response.absolute_opacity_min"), settings.response.min_opacity));
    settings.response.max_opacity = static_cast<float>(
        load_number(reactive_settings_key("response.max_opacity"), settings.response.max_opacity));
    settings.response.max_opacity = static_cast<float>(
        load_number(reactive_settings_key("response.absolute_opacity_max"), settings.response.max_opacity));
    settings.response.front_opacity_boost = static_cast<float>(
        load_number(reactive_settings_key("response.front_opacity_boost"), settings.response.front_opacity_boost));

    settings.stability.enable_temporal_smoothing =
        load_bool(reactive_settings_key("stability.enable_temporal_smoothing"), settings.stability.enable_temporal_smoothing);
    settings.stability.temporal_smoothing = static_cast<float>(
        load_number(reactive_settings_key("stability.temporal_smoothing"), settings.stability.temporal_smoothing));
    settings.stability.reuse_similarity_threshold = static_cast<float>(
        load_number(reactive_settings_key("stability.reuse_similarity_threshold"), settings.stability.reuse_similarity_threshold));

    settings.sampling.kernel_radius = clamp_int(static_cast<int>(std::round(
        load_number(reactive_settings_key("sampling.kernel_radius"), settings.sampling.kernel_radius))), 1, 16);
    settings.sampling.outer_ring_weight = static_cast<float>(
        load_number(reactive_settings_key("sampling.outer_ring_weight"), settings.sampling.outer_ring_weight));
    settings.sampling.diagonal_weight = static_cast<float>(
        load_number(reactive_settings_key("sampling.diagonal_weight"), settings.sampling.diagonal_weight));

    settings.virtual_light_map.quadrant_count = clamp_int(static_cast<int>(std::round(
        load_number(reactive_settings_key("virtual_light_map.quadrant_count"), settings.virtual_light_map.quadrant_count))), 1, 24);
    settings.virtual_light_map.distance_strength_falloff = static_cast<float>(
        load_number(reactive_settings_key("virtual_light_map.distance_strength_falloff"), settings.virtual_light_map.distance_strength_falloff));
    settings.virtual_light_map.directional_strength = static_cast<float>(
        load_number(reactive_settings_key("virtual_light_map.directional_strength"), settings.virtual_light_map.directional_strength));

    return render_pipeline::shading::sanitize_reactive_shadow_settings(settings);
}

void MapShadowPanel::persist_reactive_settings_to_dev_settings(const render_pipeline::shading::ReactiveShadowSettings& settings) const {
    using devmode::ui_settings::save_bool;
    using devmode::ui_settings::save_number;

    save_bool(reactive_settings_key("directionality.enable_offsets"), settings.directionality.enable_offsets);
    save_number(reactive_settings_key("directionality.gradient_sensitivity"), settings.directionality.gradient_sensitivity);
    save_number(reactive_settings_key("directionality.offset_strength"), settings.directionality.offset_strength);
    save_number(reactive_settings_key("directionality.max_offset_ratio"), settings.directionality.max_offset_ratio);
    save_number(reactive_settings_key("directionality.front_weight"), settings.directionality.front_weight);
    save_number(reactive_settings_key("directionality.side_weight"), settings.directionality.side_weight);
    save_number(reactive_settings_key("directionality.back_weight"), settings.directionality.back_weight);

    save_number(reactive_settings_key("output.scale_factor"), settings.output.scale_factor);
    save_number(reactive_settings_key("output.map_line_weight"), settings.output.map_line_weight);
    save_number(reactive_settings_key("output.parallax_strength"), settings.output.parallax_strength);

    save_bool(reactive_settings_key("response.enable_opacity"), settings.response.enable_opacity);
    save_number(reactive_settings_key("response.opacity_strength"), settings.response.opacity_strength);
    save_number(reactive_settings_key("response.min_opacity"), settings.response.min_opacity);
    save_number(reactive_settings_key("response.max_opacity"), settings.response.max_opacity);
    save_number(reactive_settings_key("response.front_opacity_boost"), settings.response.front_opacity_boost);

    save_bool(reactive_settings_key("stability.enable_temporal_smoothing"), settings.stability.enable_temporal_smoothing);
    save_number(reactive_settings_key("stability.temporal_smoothing"), settings.stability.temporal_smoothing);
    save_number(reactive_settings_key("stability.reuse_similarity_threshold"), settings.stability.reuse_similarity_threshold);

    save_number(reactive_settings_key("sampling.kernel_radius"), static_cast<double>(settings.sampling.kernel_radius));
    save_number(reactive_settings_key("sampling.outer_ring_weight"), settings.sampling.outer_ring_weight);
    save_number(reactive_settings_key("sampling.diagonal_weight"), settings.sampling.diagonal_weight);

    save_number(reactive_settings_key("virtual_light_map.quadrant_count"), static_cast<double>(settings.virtual_light_map.quadrant_count));
    save_number(reactive_settings_key("virtual_light_map.distance_strength_falloff"), settings.virtual_light_map.distance_strength_falloff);
    save_number(reactive_settings_key("virtual_light_map.directional_strength"), settings.virtual_light_map.directional_strength);
}

void MapShadowPanel::write_reactive_settings_to_json(const render_pipeline::shading::ReactiveShadowSettings& settings) {
    json& reactive_json = ensure_reactive_settings_json();
    render_pipeline::shading::assign_reactive_shadow_settings(reactive_json, settings);
}

nlohmann::json& MapShadowPanel::ensure_reactive_settings_json() {
    json& light = light_panel_->mutable_light();
    if (!light.contains("reactive_shadows") || !light["reactive_shadows"].is_object()) {
        light["reactive_shadows"] = json::object();
    }
    return light["reactive_shadows"];
}

void MapShadowPanel::apply_immediate_settings() {
    if (!light_panel_) {
        return;
    }

    auto sanitized = render_pipeline::shading::sanitize_reactive_shadow_settings(current_settings_from_ui());
    if (sanitized == last_applied_settings_) {
        return;
    }

    write_reactive_settings_to_json(sanitized);
    set_reactive_sliders(sanitized);
    set_reactive_checkboxes(sanitized);
    persist_reactive_settings_to_dev_settings(sanitized);

    bool ok = light_panel_->commit_light_changes_external();
    update_save_status(ok);
    if (ok) {
        last_applied_settings_ = sanitized;
        reactive_settings_initialized_ = true;
        if (reactive_settings_shared_) {
            *reactive_settings_shared_ = sanitized;
        }
    }
}

int MapShadowPanel::clamp_int(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}
