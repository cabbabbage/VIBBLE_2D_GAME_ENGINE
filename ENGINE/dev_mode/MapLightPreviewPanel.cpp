#include "MapLightPreviewPanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <cstdio>
#include <optional>
#include <sstream>

#include <SDL_ttf.h>
#include <nlohmann/json.hpp>

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/dev_ui_settings.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/shared/formatting.hpp"
#include "dev_mode/widgets.hpp"
#include "input.hpp"
#include "render/camera.hpp"
#include "world/chunk.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"
#include "utils/map_grid_settings.hpp"
#include "util/grid.hpp"
#include "world/chunk.hpp"

namespace {
constexpr std::string_view kReactiveSettingsKey = "dev_ui.lighting.map_panel.reactive";

int clamp_int(int value, int lo, int hi) {
    return std::max(lo, std::min(hi, value));
}

float clamp_float(float value, float lo, float hi) {
    return std::max(lo, std::min(hi, value));
}

std::string format_float(float value, int precision) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

std::unique_ptr<DMSlider> make_float_slider(const std::string& label,
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

SDL_Point preview_event_point(const SDL_Event& e) {
    if (e.type == SDL_MOUSEMOTION) {
        return SDL_Point{e.motion.x, e.motion.y};
    }
    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        return SDL_Point{e.button.x, e.button.y};
    }
    return SDL_Point{0, 0};
}

}  // namespace

class MapLightPreviewPanel::PreviewWidget : public Widget {
public:
    explicit PreviewWidget(MapLightPreviewPanel* owner) : owner_(owner) {}

    void set_rect(const SDL_Rect& r) override {
        rect_ = r;
        if (owner_) {
            rect_.h = owner_->preview_height_for_width(r.w);
            owner_->preview_widget_bounds_ = rect_;
        }
    }

    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int width) const override {
        if (!owner_) {
            return 320;
        }
        return owner_->preview_height_for_width(width);
    }

    bool handle_event(const SDL_Event& e) override {
        if (!owner_) {
            return false;
        }
        return owner_->handle_preview_event(e);
    }

    void render(SDL_Renderer* renderer) const override {
        if (owner_) {
            owner_->render_preview(renderer);
        }
    }

    bool wants_full_row() const override { return true; }

private:
    MapLightPreviewPanel* owner_ = nullptr;
    SDL_Rect              rect_{0, 0, 0, 0};
};

MapLightPreviewPanel::MapLightPreviewPanel(Assets* assets, int x, int y)
    : DockableCollapsible("Light Map", true, x, y), assets_(assets) {
    set_floating_content_width(360);
    set_visible_height(540);
    quadrant_note_text_ =
        "Note: Light map tiles update from static + dynamic samples and fade when inactive.";
    last_chunk_resolution_ = MapGridSettings::defaults().r_chunk;

    build_ui();
    rebuild_rows();
}

MapLightPreviewPanel::~MapLightPreviewPanel() = default;

void MapLightPreviewPanel::set_assets(Assets* assets) {
    assets_ = assets;
    if (assets_) {
        last_quadrant_size_px_ = clamp_int(assets_->virtual_light_map_quadrant_size(),
                                           LightMap::kMinQuadrantSizePx,
                                           LightMap::kMaxQuadrantSizePx);
        last_chunk_resolution_ = clamp_int(assets_->map_grid_chunk_resolution(), 0, vibble::grid::kMaxResolution);
        if (chunk_resolution_) {
            chunk_resolution_->set_value(last_chunk_resolution_);
        }
    }
    apply_virtual_light_map_quadrant_size(last_quadrant_size_px_, false, false);
    pending_light_map_regeneration_ = false;
    if (regenerate_button_) {
        regenerate_button_->set_text("Regenerate");
    }
}

void MapLightPreviewPanel::set_map_info(nlohmann::json* map_info, SaveCallback on_save) {
    map_info_ = map_info;
    on_save_  = std::move(on_save);
    if (map_info_ && map_info_->is_object()) {
        ensure_map_grid_settings(*map_info_);
    }
    if (!reactive_settings_initialized_) {
        last_applied_settings_ = load_reactive_settings_from_dev_settings();
        set_reactive_sliders(last_applied_settings_);
        reactive_settings_initialized_ = true;
    }
    if (assets_) {
        last_quadrant_size_px_ = clamp_int(assets_->virtual_light_map_quadrant_size(),
                                           LightMap::kMinQuadrantSizePx,
                                           LightMap::kMaxQuadrantSizePx);
    }
    apply_virtual_light_map_quadrant_size(last_quadrant_size_px_, false, false);
    apply_immediate_settings();
    sync_ui_from_json();
    pending_light_map_regeneration_ = false;
    if (regenerate_button_) {
        regenerate_button_->set_text("Regenerate");
    }
}

void MapLightPreviewPanel::set_reactive_settings(render_pipeline::shading::ReactiveShadowSettings* settings) {
    reactive_settings_shared_ = settings;
    if (settings) {
        last_applied_settings_ = render_pipeline::shading::sanitize_reactive_shadow_settings(*settings);
        set_reactive_sliders(last_applied_settings_);
        apply_immediate_settings();
        forced_settings_snapshot_ = last_applied_settings_;
    }
}

void MapLightPreviewPanel::update(const Input& input, int screen_w, int screen_h) {
    DockableCollapsible::update(input, screen_w, screen_h);
    if (!is_visible()) {
        return;
    }
    if (chunk_resolution_) {
        const int chunk_value = chunk_resolution_->value();
        if (chunk_value != last_chunk_resolution_) {
            last_chunk_resolution_ = chunk_value;
            handle_chunk_resolution_changed();
        }
    }
    if (needs_sync_to_json_) {
        sync_json_from_ui();
    }
}

bool MapLightPreviewPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) {
        return false;
    }

    const bool pointer_event = (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP ||
                                e.type == SDL_MOUSEMOTION);

    bool handled = DockableCollapsible::handle_event(e);
    if (!handled) {
        return false;
    }

    if (pointer_event) {
        SDL_Point point = preview_event_point(e);
        if (SDL_PointInRect(&point, &preview_widget_bounds_)) {
            return true;
        }
    }

    needs_sync_to_json_ = true;
    return true;
}

void MapLightPreviewPanel::render(SDL_Renderer* renderer) const {
    if (!is_visible()) {
        return;
    }
    DockableCollapsible::render(renderer);
}

bool MapLightPreviewPanel::is_point_inside(int x, int y) const {
    if (DockableCollapsible::is_point_inside(x, y)) {
        return true;
    }
    SDL_Point point{x, y};
    return SDL_PointInRect(&point, &preview_rect_);
}

void MapLightPreviewPanel::render_content(SDL_Renderer* renderer) const {
    DockableCollapsible::render_content(renderer);
}

void MapLightPreviewPanel::layout_custom_content(int, int) const {}

const LightMap* MapLightPreviewPanel::current_light_map() const {
    if (const LightMapManager* manager = light_map_manager()) {
        return manager->light_map();
    }
    return nullptr;
}

const LightMapManager* MapLightPreviewPanel::light_map_manager() const {
    return assets_ ? assets_->light_map_manager() : nullptr;
}

const LightMapManager::QuadrantSnapshot* MapLightPreviewPanel::snapshot_for_quadrant(int index) const {
    if (index < 0) {
        return nullptr;
    }
    const std::size_t idx = static_cast<std::size_t>(index);
    if (idx >= quadrant_snapshot_valid_.size() || idx >= quadrant_snapshots_.size()) {
        return nullptr;
    }
    if (!quadrant_snapshot_valid_[idx]) {
        return nullptr;
    }
    return &quadrant_snapshots_[idx];
}

std::optional<SDL_Point> MapLightPreviewPanel::player_screen_position() const {
    if (!assets_) {
        return std::nullopt;
    }
    camera& view = assets_->getView();
    SDL_Point center = view.get_screen_center();
    return center;
}

std::vector<std::string> MapLightPreviewPanel::assets_in_quadrant(int quadrant) const {
    if (const LightMapManager* manager = light_map_manager()) {
        return manager->assets_sampling_quadrant(quadrant);
    }
    return {};
}

int MapLightPreviewPanel::quadrant_index_from_point(int x, int y) const {
    SDL_Point point{x, y};
    if (!SDL_PointInRect(&point, &preview_grid_rect_)) {
        return -1;
    }
    for (std::size_t i = 0; i < quadrant_preview_rects_.size(); ++i) {
        if (SDL_PointInRect(&point, &quadrant_preview_rects_[i])) {
            return static_cast<int>(i);
        }
    }
    const LightMap* map = current_light_map();
    if (!map || preview_grid_rect_.w <= 0 || preview_grid_rect_.h <= 0) {
        return -1;
    }
    const int grid_w = map->quadrant_columns();
    const int grid_h = map->quadrant_rows();
    if (grid_w <= 0 || grid_h <= 0) {
        return -1;
    }

    const float norm_x = static_cast<float>(x - preview_grid_rect_.x) /
                         static_cast<float>(preview_grid_rect_.w);
    const float norm_y = static_cast<float>(y - preview_grid_rect_.y) /
                         static_cast<float>(preview_grid_rect_.h);

    const int gx = clamp_int(static_cast<int>(std::floor(norm_x * static_cast<float>(grid_w))),
                             0,
                             grid_w - 1);
    const int gy = clamp_int(static_cast<int>(std::floor(norm_y * static_cast<float>(grid_h))),
                             0,
                             grid_h - 1);
    return gy * grid_w + gx;
}

bool MapLightPreviewPanel::handle_preview_event(const SDL_Event& e) {
    if (!is_visible()) {
        return false;
    }
    if (e.type != SDL_MOUSEBUTTONDOWN && e.type != SDL_MOUSEMOTION) {
        return false;
    }

    SDL_Point point = preview_event_point(e);
    if (!SDL_PointInRect(&point, &preview_widget_bounds_)) {
        return false;
    }

    const int quadrant = quadrant_index_from_point(point.x, point.y);
    if (quadrant < 0) {
        return false;
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        selected_quadrant_ = quadrant;
        return true;
    }

    if (e.type == SDL_MOUSEMOTION) {
        selected_quadrant_ = quadrant;
    }

    return false;
}

int MapLightPreviewPanel::preview_height_for_width(int width) const {
    const int available_width = std::max(40, width);

    const LightMapManager* manager = light_map_manager();
    const LightMap*        map     = manager ? manager->light_map() : nullptr;
    if (!map && assets_) {
        map = assets_->light_map();
    }

    float aspect = 1.0f;
    if (map && map->screen_width() > 0) {
        aspect = static_cast<float>(std::max(1, map->screen_height())) /
                 static_cast<float>(std::max(1, map->screen_width()));
    } else if (screen_width_px_ > 0 && screen_height_px_ > 0) {
        aspect = static_cast<float>(screen_height_px_) /
                 static_cast<float>(std::max(1, screen_width_px_));
    }

    int grid_width_px = std::max(40, std::min(available_width, 320));
    int grid_height_px = std::max(40, static_cast<int>(std::lround(static_cast<double>(grid_width_px) * aspect)));

    int detail_gap   = DMSpacing::item_gap();
    int detail_width = available_width - grid_width_px - detail_gap;
    bool detail_below = (detail_width < 160);

    const int lines = estimated_detail_line_count();
    const int line_height = DMStyles::Label().font_size + DMSpacing::small_gap();
    const int detail_height = (lines > 0) ? std::max(0, lines * line_height) : 0;

    if (detail_below) {
        if (detail_height > 0) {
            return grid_height_px + detail_gap + detail_height;
        }
        return grid_height_px;
    }

    if (detail_height > 0) {
        return std::max(grid_height_px, detail_height);
    }
    return grid_height_px;
}

int MapLightPreviewPanel::estimated_detail_line_count() const {
    int count = 0;

    const LightMapManager* manager = light_map_manager();
    const LightMap*        map     = manager ? manager->light_map() : nullptr;
    if (!map && assets_) {
        map = assets_->light_map();
    }

    const int total_quadrants = map ? map->quadrant_count() : 0;
    const int detail_quadrant =
        (selected_quadrant_ >= 0 && selected_quadrant_ < total_quadrants) ? selected_quadrant_ : -1;

    if (detail_quadrant >= 0) {
        count += 1;  // Tile header

        // Assume full snapshot metrics when available.
        count += 7;

        if (map) {
            count += 2;  // Grid resolution + padding
        }

        count += 1;  // Blank line before assets
        count += 1;  // Assets Sampling header

        int asset_lines = 1;
        if (manager) {
            const auto assets = manager->assets_sampling_quadrant(detail_quadrant);
            asset_lines = static_cast<int>(assets.empty() ? 1 : assets.size());
        }
        count += asset_lines;
    } else {
        count += 1;  // No quadrant selected
    }

    if (!quadrant_note_text_.empty()) {
        count += 1;  // Blank line before note
        count += count_lines(quadrant_note_text_);
    }

    return count;
}

int MapLightPreviewPanel::count_lines(std::string_view text) {
    if (text.empty()) {
        return 0;
    }
    int lines = 1;
    for (char c : text) {
        if (c == '\n') {
            ++lines;
        }
    }
    return lines;
}

void MapLightPreviewPanel::render_preview(SDL_Renderer* renderer) const {
    preview_rect_       = SDL_Rect{0, 0, 0, 0};
    preview_grid_rect_  = SDL_Rect{0, 0, 0, 0};
    quadrant_preview_rects_.clear();
    quadrant_snapshots_.clear();
    quadrant_snapshot_valid_.clear();

    if (!renderer || !is_visible()) {
        return;
    }

    const LightMapManager* manager = light_map_manager();
    const LightMap*        map     = manager ? manager->light_map() : nullptr;
    if (!manager || !map || map->screen_width() <= 0 || map->screen_height() <= 0) {
        return;
    }

    const int grid_w = map->quadrant_columns();
    const int grid_h = map->quadrant_rows();
    if (grid_w <= 0 || grid_h <= 0) {
        return;
    }

    const int total_quadrants = map->quadrant_count();
    quadrant_preview_rects_.assign(static_cast<std::size_t>(total_quadrants), SDL_Rect{0, 0, 0, 0});
    quadrant_snapshots_.assign(static_cast<std::size_t>(total_quadrants), LightMapManager::QuadrantSnapshot{});
    quadrant_snapshot_valid_.assign(static_cast<std::size_t>(total_quadrants), false);

    const std::vector<LightMapManager::QuadrantSnapshot> snapshots = manager->all_snapshots();
    for (const auto& snapshot : snapshots) {
        if (snapshot.index >= 0 && snapshot.index < total_quadrants) {
            const std::size_t idx = static_cast<std::size_t>(snapshot.index);
            quadrant_snapshots_[idx]      = snapshot;
            quadrant_snapshot_valid_[idx] = true;
        }
    }

    const int available_width = std::max(40, preview_widget_bounds_.w);
    const int preview_x       = preview_widget_bounds_.x;
    const int preview_y       = preview_widget_bounds_.y;

    const int map_screen_width  = map->screen_width();
    const int map_screen_height = map->screen_height();
    const float aspect = (map_screen_width > 0)
                             ? static_cast<float>(map_screen_height) / static_cast<float>(map_screen_width)
                             : 1.0f;

    int grid_width_px = std::max(40, std::min(available_width, 320));
    int grid_height_px = static_cast<int>(
        std::lround(static_cast<double>(grid_width_px) * static_cast<double>(aspect)));
    grid_height_px = std::max(grid_height_px, 40);

    int  detail_gap   = DMSpacing::item_gap();
    int  detail_width = available_width - grid_width_px - detail_gap;
    bool detail_below = (detail_width < 160);

    SDL_Rect detail_rect{0, 0, 0, 0};
    if (detail_below) {
        detail_width = available_width;
        detail_gap   = DMSpacing::small_gap();
        detail_rect  = SDL_Rect{preview_x, preview_y + grid_height_px + detail_gap, detail_width, 0};
    } else {
        detail_rect = SDL_Rect{preview_x + grid_width_px + detail_gap, preview_y, detail_width, grid_height_px};
    }

    preview_grid_rect_ = SDL_Rect{preview_x, preview_y, grid_width_px, grid_height_px};
    preview_rect_      = SDL_Rect{preview_x, preview_y, available_width, grid_height_px};

    screen_width_px_  = map->screen_width();
    screen_height_px_ = map->screen_height();

    // Clip all preview rendering to the preview widget bounds to prevent overlap
    SDL_Rect prev_clip;
    SDL_RenderGetClipRect(renderer, &prev_clip);
    SDL_RenderSetClipRect(renderer, &preview_widget_bounds_);

    SDL_Color bg{30, 30, 30, 255};
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &preview_grid_rect_);

    const DMLabelStyle& label_style = DMStyles::Label();
    std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)> detail_font(label_style.open_font(), &TTF_CloseFont);
    std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)> tile_font(
        TTF_OpenFont(label_style.font_path.c_str(), std::max(8, label_style.font_size - 4)), &TTF_CloseFont);
    SDL_Color text_color = label_style.color;

    auto fallback_cell_rect = [&](int gx, int gy) -> SDL_Rect {
        const int left   = preview_grid_rect_.x + static_cast<int>(
            std::lround(static_cast<double>(gx) * preview_grid_rect_.w / static_cast<double>(grid_w)));
        const int right  = preview_grid_rect_.x + static_cast<int>(
            std::lround(static_cast<double>(gx + 1) * preview_grid_rect_.w / static_cast<double>(grid_w)));
        const int top    = preview_grid_rect_.y + static_cast<int>(
            std::lround(static_cast<double>(gy) * preview_grid_rect_.h / static_cast<double>(grid_h)));
        const int bottom = preview_grid_rect_.y + static_cast<int>(
            std::lround(static_cast<double>(gy + 1) * preview_grid_rect_.h / static_cast<double>(grid_h)));
        return SDL_Rect{left, top, std::max(1, right - left), std::max(1, bottom - top)};
    };

    auto rect_from_world = [&](const SDL_Rect& world_rect) -> SDL_Rect {
        if (map_screen_width <= 0 || map_screen_height <= 0) {
            return SDL_Rect{0, 0, 0, 0};
        }
        const float left_ratio =
            std::clamp(static_cast<float>(world_rect.x) / static_cast<float>(map_screen_width), 0.0f, 1.0f);
        const float right_ratio =
            std::clamp(static_cast<float>(world_rect.x + world_rect.w) / static_cast<float>(map_screen_width), 0.0f, 1.0f);
        const float top_ratio =
            std::clamp(static_cast<float>(world_rect.y) / static_cast<float>(map_screen_height), 0.0f, 1.0f);
        const float bottom_ratio =
            std::clamp(static_cast<float>(world_rect.y + world_rect.h) / static_cast<float>(map_screen_height), 0.0f, 1.0f);

        const int left = preview_grid_rect_.x + static_cast<int>(std::lround(left_ratio * preview_grid_rect_.w));
        const int right = preview_grid_rect_.x + static_cast<int>(std::lround(right_ratio * preview_grid_rect_.w));
        const int top = preview_grid_rect_.y + static_cast<int>(std::lround(top_ratio * preview_grid_rect_.h));
        const int bottom = preview_grid_rect_.y + static_cast<int>(std::lround(bottom_ratio * preview_grid_rect_.h));
        return SDL_Rect{left, top, std::max(1, right - left), std::max(1, bottom - top)};
    };

    auto draw_indicator = [&](const SDL_Rect& rect, const SDL_Color& color, bool top_right) {
        if (rect.w <= 4 || rect.h <= 4) {
            return;
        }
        const int size = std::max(4, std::min(rect.w, rect.h) / 6);
        SDL_Rect indicator{rect.x + 2, rect.y + 2, size, size};
        if (top_right) {
            indicator.x = rect.x + rect.w - size - 2;
        }
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_RenderFillRect(renderer, &indicator);
    };

    auto render_tile_annotation = [&](const SDL_Rect& rect,
                                      const LightMapManager::QuadrantSnapshot* snap) {
        if (!snap || !tile_font || rect.w <= 10 || rect.h <= 14) {
            return;
        }
        std::ostringstream stream;
        stream << (snap->active ? "A" : "-") << ' ' << (snap->dirty ? "D" : "-") << '\n';
        if (snap->static_empty) {
            stream << "S:--" << '\n';
        } else {
            stream << "S:" << format_float(snap->static_min, 2) << '/' << format_float(snap->static_average, 2)
                   << '/' << format_float(snap->static_max, 2) << '\n';
        }
        stream << "C:" << format_float(snap->combined_brightness, 2);
        const std::string text = stream.str();

        SDL_Surface* surface =
            TTF_RenderUTF8_Blended_Wrapped(tile_font.get(), text.c_str(), text_color, std::max(8, rect.w - 6));
        if (!surface) {
            return;
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (texture) {
            SDL_Rect dst{rect.x + 4, rect.y + 4, surface->w, surface->h};
            dst.w = std::min(dst.w, rect.w - 8);
            dst.h = std::min(dst.h, rect.h - 8);

            SDL_Rect bg_rect = dst;
            bg_rect.x -= 2;
            bg_rect.y -= 2;
            bg_rect.w += 4;
            bg_rect.h += 4;
            bg_rect.x = std::max(bg_rect.x, rect.x + 2);
            bg_rect.y = std::max(bg_rect.y, rect.y + 2);
            bg_rect.w = std::min(bg_rect.w, rect.w - 4);
            bg_rect.h = std::min(bg_rect.h, rect.h - 4);

            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
            SDL_RenderFillRect(renderer, &bg_rect);
            SDL_RenderCopy(renderer, texture, nullptr, &dst);
            SDL_DestroyTexture(texture);
        }
        SDL_FreeSurface(surface);
    };

    auto draw_light_debug_overlay = [&](const SDL_Rect& grid_rect) {
        if (!assets_) {
            return;
        }

        struct LightDebugSample {
            SDL_Color color{};
            Uint8     intensity = 0;
        };

        constexpr int kMaxOverlayLights = 12;

        std::vector<LightDebugSample> samples;
        samples.reserve(kMaxOverlayLights);

        const auto& moving_assets = assets_->getActiveMovingLightAssets();
        for (Asset* asset : moving_assets) {
            if (!asset || !asset->info) {
                continue;
            }
            for (const auto& light : asset->info->light_sources) {
                LightDebugSample sample{};
                sample.color.r = static_cast<Uint8>(std::clamp(static_cast<int>(light.color.r), 0, 255));
                sample.color.g = static_cast<Uint8>(std::clamp(static_cast<int>(light.color.g), 0, 255));
                sample.color.b = static_cast<Uint8>(std::clamp(static_cast<int>(light.color.b), 0, 255));
                sample.color.a = 255;
                sample.intensity = static_cast<Uint8>(std::clamp(light.intensity, 0, 255));
                samples.push_back(sample);
                if (samples.size() >= kMaxOverlayLights) {
                    break;
                }
            }
            if (samples.size() >= kMaxOverlayLights) {
                break;
            }
        }

        if (samples.empty()) {
            return;
        }

        const int padding      = 6;
        const int spacing      = 4;
        const int swatch_size  = 14;
        const int bar_width    = 3;
        const int cell_width   = swatch_size + bar_width + spacing;
        const int available_w  = std::max(0, grid_rect.w - padding * 2);
        if (available_w <= 0) {
            return;
        }

        const int max_per_row = std::max(1, (available_w + spacing) / cell_width);
        const int columns     = std::min(max_per_row, static_cast<int>(samples.size()));
        const int rows        = std::max(1, (static_cast<int>(samples.size()) + max_per_row - 1) / max_per_row);
        const int overlay_w   = columns * cell_width - spacing;
        const int overlay_h   = rows * (swatch_size + spacing) - spacing;
        if (overlay_w <= 0 || overlay_h <= 0 || overlay_h + padding * 2 > grid_rect.h) {
            return;
        }

        SDL_Rect overlay_rect{grid_rect.x + grid_rect.w - overlay_w - padding,
                              grid_rect.y + padding,
                              overlay_w,
                              overlay_h};

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
        SDL_RenderFillRect(renderer, &overlay_rect);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 80);
        SDL_RenderDrawRect(renderer, &overlay_rect);

        for (std::size_t i = 0; i < samples.size(); ++i) {
            const int row = static_cast<int>(i) / max_per_row;
            const int col = static_cast<int>(i) % max_per_row;
            SDL_Rect swatch_rect{overlay_rect.x + col * cell_width,
                                 overlay_rect.y + row * (swatch_size + spacing),
                                 swatch_size,
                                 swatch_size};

            const float intensity_scale = static_cast<float>(samples[i].intensity) / 255.0f;
            const Uint8 draw_r = static_cast<Uint8>(std::lround(static_cast<float>(samples[i].color.r) * intensity_scale));
            const Uint8 draw_g = static_cast<Uint8>(std::lround(static_cast<float>(samples[i].color.g) * intensity_scale));
            const Uint8 draw_b = static_cast<Uint8>(std::lround(static_cast<float>(samples[i].color.b) * intensity_scale));

            SDL_SetRenderDrawColor(renderer, draw_r, draw_g, draw_b, 255);
            SDL_RenderFillRect(renderer, &swatch_rect);
            SDL_SetRenderDrawColor(renderer, samples[i].color.r, samples[i].color.g, samples[i].color.b, 255);
            SDL_RenderDrawRect(renderer, &swatch_rect);

            SDL_Rect bar_rect{swatch_rect.x + swatch_rect.w,
                              swatch_rect.y,
                              bar_width,
                              swatch_rect.h};
            SDL_SetRenderDrawColor(renderer, 30, 30, 30, 220);
            SDL_RenderFillRect(renderer, &bar_rect);

            const int bar_fill_height = static_cast<int>(std::round(intensity_scale * static_cast<float>(bar_rect.h)));
            if (bar_fill_height > 0) {
                SDL_Rect fill_rect{bar_rect.x,
                                   bar_rect.y + bar_rect.h - bar_fill_height,
                                   bar_rect.w,
                                   bar_fill_height};
                SDL_SetRenderDrawColor(renderer,
                                       samples[i].color.r,
                                       samples[i].color.g,
                                       samples[i].color.b,
                                       220);
                SDL_RenderFillRect(renderer, &fill_rect);
            }
        }
    };

    const SDL_Color dirty_color{200, 120, 40, 255};
    const SDL_Color outline_color{70, 70, 70, 255};

    for (int gy = 0; gy < grid_h; ++gy) {
        for (int gx = 0; gx < grid_w; ++gx) {
            const int index = gy * grid_w + gx;
            SDL_Rect cell_rect = fallback_cell_rect(gx, gy);

            if (const auto* snap = snapshot_for_quadrant(index)) {
                SDL_Rect world_rect = rect_from_world(snap->world_rect);
                if (world_rect.w > 0 && world_rect.h > 0) {
                    cell_rect = world_rect;
                }
            } else if (const world::Chunk* chunk = map->quadrant(index)) {
                SDL_Rect world_rect = rect_from_world(chunk->world_bounds);
                if (world_rect.w > 0 && world_rect.h > 0) {
                    cell_rect = world_rect;
                }
            }

            if (static_cast<std::size_t>(index) < quadrant_preview_rects_.size()) {
                quadrant_preview_rects_[static_cast<std::size_t>(index)] = cell_rect;
            }

            const LightMapManager::QuadrantSnapshot* snap = snapshot_for_quadrant(index);
            float brightness = snap ? snap->combined_brightness : 0.0f;
            if (!snap) {
                if (const world::Chunk* chunk = map->quadrant(index)) {
                    SDL_Rect world_rect = chunk->world_bounds;
                    const float cx = static_cast<float>(world_rect.x) + static_cast<float>(world_rect.w) * 0.5f;
                    const float cy = static_cast<float>(world_rect.y) + static_cast<float>(world_rect.h) * 0.5f;
                    brightness = map->sample_brightness(static_cast<int>(std::round(cx)),
                                                        static_cast<int>(std::round(cy)));
                }
            }
            brightness = std::clamp(brightness, 0.0f, 1.0f);
            const Uint8 brightness_u8 = static_cast<Uint8>(brightness * 255.0f);
            SDL_SetRenderDrawColor(renderer, brightness_u8, brightness_u8, brightness_u8, 255);
            SDL_RenderFillRect(renderer, &cell_rect);

            if (snap) {
                if (snap->active) {
                    const SDL_Color highlight = DMStyles::HighlightColor();
                    draw_indicator(cell_rect, highlight, false);
                }
                if (snap->dirty) {
                    draw_indicator(cell_rect, dirty_color, true);
                }
                render_tile_annotation(cell_rect, snap);
            }

            SDL_SetRenderDrawColor(renderer, outline_color.r, outline_color.g, outline_color.b, outline_color.a);
            SDL_RenderDrawRect(renderer, &cell_rect);
        }
    }

    draw_light_debug_overlay(preview_grid_rect_);

    SDL_SetRenderDrawColor(renderer, outline_color.r, outline_color.g, outline_color.b, outline_color.a);
    SDL_RenderDrawRect(renderer, &preview_grid_rect_);

    const int detail_quadrant =
        (selected_quadrant_ >= 0 && selected_quadrant_ < total_quadrants) ? selected_quadrant_ : -1;
    if (detail_quadrant >= 0 &&
        static_cast<std::size_t>(detail_quadrant) < quadrant_preview_rects_.size()) {
        const SDL_Rect& selected_rect =
            quadrant_preview_rects_[static_cast<std::size_t>(detail_quadrant)];
        if (selected_rect.w > 0 && selected_rect.h > 0) {
            const SDL_Color accent = DMStyles::HighlightColor();
            SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, accent.a);
            SDL_RenderDrawRect(renderer, &selected_rect);
        }
    }

    std::vector<std::string> detail_lines;
    if (detail_quadrant >= 0) {
        const int gx = detail_quadrant % grid_w;
        const int gy = detail_quadrant / grid_w;
        detail_lines.push_back("Tile [" + std::to_string(gx) + ", " + std::to_string(gy) + "] #" +
                               std::to_string(detail_quadrant));

        if (const auto* snap = snapshot_for_quadrant(detail_quadrant)) {
            detail_lines.push_back(std::string("Active: ") + (snap->active ? "yes" : "no") +
                                   " | Dirty: " + (snap->dirty ? "yes" : "no"));
            detail_lines.push_back("Base Brightness: " + format_float(snap->base_brightness, 3));
            detail_lines.push_back("Combined Brightness: " + format_float(snap->combined_brightness, 3));
            if (snap->static_empty) {
                detail_lines.push_back("Static Grid: (empty)");
            } else {
                detail_lines.push_back("Static Min: " + format_float(snap->static_min, 3));
                detail_lines.push_back("Static Avg: " + format_float(snap->static_average, 3));
                detail_lines.push_back("Static Max: " + format_float(snap->static_max, 3));
            }
            detail_lines.push_back("Dynamic Lighting: disabled");
            detail_lines.push_back("Shadow Strength: " + format_float(snap->shadow_opacity_min, 3) + " - " +
                                   format_float(snap->shadow_opacity_max, 3));
        }

        if (const world::Chunk* chunk = map->quadrant(detail_quadrant)) {
            detail_lines.push_back("Chunk Bounds: " + std::to_string(chunk->world_bounds.x) + ", " +
                                   std::to_string(chunk->world_bounds.y) + " " +
                                   std::to_string(chunk->world_bounds.w) + "x" +
                                   std::to_string(chunk->world_bounds.h));
        }

        detail_lines.push_back("");
        detail_lines.push_back("Assets Sampling:");

        auto assets = assets_in_quadrant(detail_quadrant);
        if (assets.empty()) {
            detail_lines.push_back("  (none)");
        } else {
            for (const auto& name : assets) {
                detail_lines.push_back("  - " + name);
            }
        }
    } else {
        detail_lines.push_back("No quadrant selected.");
    }

    if (!quadrant_note_text_.empty()) {
        detail_lines.push_back("");
        detail_lines.push_back(quadrant_note_text_);
    }

    std::string detail_text;
    for (std::size_t i = 0; i < detail_lines.size(); ++i) {
        if (i > 0) {
            detail_text.push_back('\n');
        }
        detail_text.append(detail_lines[i]);
    }

    int detail_text_height = 0;
    if (!detail_text.empty() && detail_width > 0 && detail_font) {
        SDL_Surface* surface =
            TTF_RenderUTF8_Blended_Wrapped(detail_font.get(), detail_text.c_str(), text_color,
                                           std::max(10, detail_width));
        if (surface) {
            SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
            if (texture) {
                SDL_Rect dst{detail_rect.x, detail_rect.y, surface->w, surface->h};
                if (!detail_below) {
                    dst.w = std::min(dst.w, detail_rect.w);
                    dst.h = std::min(dst.h, detail_rect.h);
                }
                SDL_Color panel_bg{20, 20, 20, 180};
                SDL_Rect bg_rect = detail_rect;
                bg_rect.w = detail_width;
                bg_rect.h = surface->h;
                SDL_SetRenderDrawColor(renderer, panel_bg.r, panel_bg.g, panel_bg.b, panel_bg.a);
                SDL_RenderFillRect(renderer, &bg_rect);

                SDL_RenderCopy(renderer, texture, nullptr, &dst);
                SDL_DestroyTexture(texture);
            }
            detail_text_height = surface->h;
            SDL_FreeSurface(surface);
        }
    }

    // Clamp final heights to the widget bounds to avoid drawing over controls
    const int max_preview_h = std::max(0, preview_widget_bounds_.h);
    if (detail_below) {
        const int remaining = std::max(0, max_preview_h - grid_height_px - detail_gap);
        detail_rect.h = std::min(detail_text_height, remaining);
        preview_rect_.h = std::min(max_preview_h, grid_height_px + detail_gap + detail_rect.h);
    } else {
        detail_rect.h = grid_height_px;
        preview_rect_.h = std::min(max_preview_h, std::max(grid_height_px, detail_text_height));
    }

    // Restore previous clip rectangle
    SDL_RenderSetClipRect(renderer, &prev_clip);
}

void MapLightPreviewPanel::rebuild_rows() {
    widget_wrappers_.clear();
    widget_wrappers_.reserve(16);

    auto add_widget = [this](std::unique_ptr<Widget> widget) -> Widget* {
        Widget* raw = widget.get();
        widget_wrappers_.push_back(std::move(widget));
        return raw;
    };

    Rows rows;

    if (chunk_resolution_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(chunk_resolution_.get())) });
    }

    // Preview-only: do not attach any other sliders or buttons here.
    rows.push_back({ add_widget(std::make_unique<PreviewWidget>(this)) });

    set_rows(rows);
}

void MapLightPreviewPanel::build_ui() {
    chunk_resolution_ = std::make_unique<DMSlider>("Chunk Resolution (2^r px)",
                                                  0,
                                                  vibble::grid::kMaxResolution,
                                                  last_chunk_resolution_);
    if (chunk_resolution_) {
        chunk_resolution_->set_defer_commit_until_unfocus(false);
        chunk_resolution_->set_value_formatter([](int value,
                                                  std::array<char, dev_mode::kSliderFormatBufferSize>& buffer)
                                                   -> std::string_view {
            const int clamped = clamp_int(value, 0, vibble::grid::kMaxResolution);
            const int size_px = 1 << clamped;
            std::snprintf(buffer.data(), buffer.size(), "r=%d (%d px)", clamped, size_px);
            return buffer.data();
        });
    }
}

void MapLightPreviewPanel::sync_ui_from_json() {
    if (!map_info_) {
        return;
    }
    auto it = map_info_->find("reactive_shadows");
    if (it != map_info_->end() && it->is_object()) {
        int desired_size = last_quadrant_size_px_;
        if (auto size_it = it->find("virtual_light_map_quadrant_size");
            size_it != it->end() && size_it->is_number_integer()) {
            desired_size = size_it->get<int>();
        } else if (auto quad_it = it->find("virtual_light_map_quadrants");
                   quad_it != it->end() && quad_it->is_number_integer()) {
            const int count = clamp_int(quad_it->get<int>(), LightMap::kMinQuadrantCount, LightMap::kMaxQuadrantCount);
            desired_size = last_quadrant_size_px_;
            const LightMap* map = current_light_map();
            if (!map && assets_) {
                map = assets_->light_map();
            }
            if (map) {
                const int approx_w = std::max(1, map->screen_width() / std::max(1, count));
                const int approx_h = std::max(1, map->screen_height() / std::max(1, count));
                desired_size       = std::max(approx_w, approx_h);
            }
        }
        last_quadrant_size_px_ = clamp_int(desired_size, LightMap::kMinQuadrantSizePx, LightMap::kMaxQuadrantSizePx);
        last_applied_settings_ = render_pipeline::shading::reactive_shadow_settings_from_json(*it, last_applied_settings_);
        last_applied_settings_ = render_pipeline::shading::sanitize_reactive_shadow_settings(last_applied_settings_);
        set_reactive_sliders(last_applied_settings_);
        apply_virtual_light_map_quadrant_size(last_quadrant_size_px_, false, false);
        apply_immediate_settings();
    }
    int chunk_value = last_chunk_resolution_;
    if (map_info_ && map_info_->is_object()) {
        auto grid_it = map_info_->find("map_grid_settings");
        if (grid_it != map_info_->end() && grid_it->is_object()) {
            MapGridSettings grid_settings = MapGridSettings::from_json(&(*grid_it));
            chunk_value = grid_settings.r_chunk;
        }
    }
    chunk_value = clamp_int(chunk_value, 0, vibble::grid::kMaxResolution);
    last_chunk_resolution_ = chunk_value;
    if (chunk_resolution_) {
        chunk_resolution_->set_value(chunk_value);
    }
    needs_sync_to_json_ = false;
}

void MapLightPreviewPanel::sync_json_from_ui() {
    if (!map_info_) {
        return;
    }
    MapGridSettings grid_settings = MapGridSettings::defaults();
    if (map_info_->contains("map_grid_settings") && (*map_info_)["map_grid_settings"].is_object()) {
        grid_settings = MapGridSettings::from_json(&(*map_info_)["map_grid_settings"]);
    }
    if (chunk_resolution_) {
        grid_settings.r_chunk = clamp_int(chunk_resolution_->value(), 0, vibble::grid::kMaxResolution);
    }
    grid_settings.clamp();
    last_chunk_resolution_ = grid_settings.r_chunk;
    if (chunk_resolution_ && chunk_resolution_->value() != grid_settings.r_chunk) {
        chunk_resolution_->set_value(grid_settings.r_chunk);
    }
    nlohmann::json& grid_section = (*map_info_)["map_grid_settings"];
    grid_settings.apply_to_json(grid_section);
    if (assets_) {
        assets_->apply_map_grid_settings(grid_settings);
    }
    render_pipeline::shading::ReactiveShadowSettings settings = current_settings_from_ui();
    int size_px = last_quadrant_size_px_;
    if (quadrant_size_px_) {
        size_px = clamp_int(quadrant_size_px_->value(),
                             LightMap::kMinQuadrantSizePx,
                             LightMap::kMaxQuadrantSizePx);
    }
    apply_virtual_light_map_quadrant_size(size_px, false);
    write_reactive_settings_to_json(settings);
    last_applied_settings_ = settings;
    apply_immediate_settings();
    needs_sync_to_json_ = false;
}

void MapLightPreviewPanel::apply_immediate_settings() {
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

render_pipeline::shading::ReactiveShadowSettings MapLightPreviewPanel::current_settings_from_ui() const {
    render_pipeline::shading::ReactiveShadowSettings settings = last_applied_settings_;
    settings.virtual_light_map.horizontal_falloff = slider_value_scaled(horizontal_falloff_, settings.virtual_light_map.horizontal_falloff, 100);
    settings.virtual_light_map.vertical_falloff = slider_value_scaled(vertical_falloff_, settings.virtual_light_map.vertical_falloff, 100);
    settings.virtual_light_map.max_offset_x = slider_value_scaled(max_offset_x_, settings.virtual_light_map.max_offset_x, 100);
    settings.virtual_light_map.max_offset_y = slider_value_scaled(max_offset_y_, settings.virtual_light_map.max_offset_y, 100);
    if (search_radius_) {
        settings.virtual_light_map.search_radius = search_radius_->displayed_value();
    }
    return render_pipeline::shading::sanitize_reactive_shadow_settings(settings);
}

void MapLightPreviewPanel::set_reactive_sliders(const render_pipeline::shading::ReactiveShadowSettings& settings) {
    set_slider_scaled(horizontal_falloff_, settings.virtual_light_map.horizontal_falloff, 100);
    set_slider_scaled(vertical_falloff_, settings.virtual_light_map.vertical_falloff, 100);
    set_slider_scaled(max_offset_x_, settings.virtual_light_map.max_offset_x, 100);
    set_slider_scaled(max_offset_y_, settings.virtual_light_map.max_offset_y, 100);
    if (search_radius_) {
        search_radius_->set_value(settings.virtual_light_map.search_radius);
    }
    if (quadrant_size_px_) {
        quadrant_size_px_->set_value(last_quadrant_size_px_);
    }
}

render_pipeline::shading::ReactiveShadowSettings MapLightPreviewPanel::load_reactive_settings_from_dev_settings() {
    using devmode::ui_settings::load_number;
    render_pipeline::shading::ReactiveShadowSettings settings = render_pipeline::shading::sanitize_reactive_shadow_settings({});
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
    settings.virtual_light_map.search_radius = static_cast<int>(
        std::lround(load_number(make_setting_key("virtual_light_map.search_radius"),
                                 static_cast<double>(settings.virtual_light_map.search_radius))));
    int stored_size = static_cast<int>(
        std::lround(load_number(make_setting_key("virtual_light_map.quadrant_size"), last_quadrant_size_px_)));
    if (stored_size <= 0) {
        int stored_quadrants = static_cast<int>(
            load_number(make_setting_key("virtual_light_map.quadrants"), LightMap::kDefaultQuadrantCount));
        stored_quadrants = clamp_int(stored_quadrants, LightMap::kMinQuadrantCount, LightMap::kMaxQuadrantCount);
        if (const LightMap* map = current_light_map()) {
            const int approx_w = std::max(1, map->screen_width() / std::max(1, stored_quadrants));
            const int approx_h = std::max(1, map->screen_height() / std::max(1, stored_quadrants));
            stored_size        = std::max(approx_w, approx_h);
        }
    }
    last_quadrant_size_px_ = clamp_int(stored_size, LightMap::kMinQuadrantSizePx, LightMap::kMaxQuadrantSizePx);
    return render_pipeline::shading::sanitize_reactive_shadow_settings(settings);
}

void MapLightPreviewPanel::persist_reactive_settings_to_dev_settings(const render_pipeline::shading::ReactiveShadowSettings& settings) const {
    using devmode::ui_settings::save_number;
    save_number(make_setting_key("virtual_light_map.horizontal_falloff"), settings.virtual_light_map.horizontal_falloff);
    save_number(make_setting_key("virtual_light_map.vertical_falloff"), settings.virtual_light_map.vertical_falloff);
    save_number(make_setting_key("virtual_light_map.max_offset_x"), settings.virtual_light_map.max_offset_x);
    save_number(make_setting_key("virtual_light_map.max_offset_y"), settings.virtual_light_map.max_offset_y);
    save_number(make_setting_key("virtual_light_map.shadow_scale"), settings.virtual_light_map.shadow_scale);
    save_number(make_setting_key("virtual_light_map.size_scale_factor"), settings.virtual_light_map.size_scale_factor);
    save_number(make_setting_key("virtual_light_map.search_radius"), settings.virtual_light_map.search_radius);
    save_number(make_setting_key("virtual_light_map.quadrant_size"), last_quadrant_size_px_);
    int legacy_quadrants = LightMap::kDefaultQuadrantCount;
    if (assets_) {
        legacy_quadrants = std::max(LightMap::kMinQuadrantCount, assets_->virtual_light_map_quadrants());
    }
    save_number(make_setting_key("virtual_light_map.quadrants"), legacy_quadrants);
}

void MapLightPreviewPanel::write_reactive_settings_to_json(const render_pipeline::shading::ReactiveShadowSettings& settings) {
    if (!map_info_) {
        return;
    }
    nlohmann::json& json = (*map_info_)["reactive_shadows"];
    render_pipeline::shading::assign_reactive_shadow_settings(json, settings);
    json["virtual_light_map_quadrant_size"] = last_quadrant_size_px_;
    if (assets_) {
        json["virtual_light_map_quadrants"] = assets_->virtual_light_map_quadrants();
    }
}

nlohmann::json& MapLightPreviewPanel::ensure_reactive_settings_json() {
    if (!map_info_) {
        static nlohmann::json dummy = nlohmann::json::object();
        return dummy;
    }
    return (*map_info_)["reactive_shadows"];
}

void MapLightPreviewPanel::apply_virtual_light_map_quadrant_size(int size_px,
                                                                 bool apply_to_assets,
                                                                 bool mark_pending) {
    const int clamped = clamp_int(size_px, LightMap::kMinQuadrantSizePx, LightMap::kMaxQuadrantSizePx);
    const bool changed = (last_quadrant_size_px_ != clamped);
    last_quadrant_size_px_ = clamped;
    if (quadrant_size_px_) {
        quadrant_size_px_->set_value(last_quadrant_size_px_);
    }

    persist_reactive_settings_to_dev_settings(last_applied_settings_);

    if (apply_to_assets) {
        pending_light_map_regeneration_ = false;
        if (regenerate_button_) {
            regenerate_button_->set_text("Regenerate");
        }
        if (assets_) {
            assets_->set_virtual_light_map_quadrant_size(last_quadrant_size_px_);
        }
        forced_quadrant_size_snapshot_ = last_quadrant_size_px_;
        force_shading_refresh_if_needed(true);
    } else if (changed && mark_pending) {
        request_light_map_regeneration();
    }
}

void MapLightPreviewPanel::request_light_map_regeneration() {
    pending_light_map_regeneration_ = true;
    if (regenerate_button_) {
        regenerate_button_->set_text("Regenerate*");
    }
}

void MapLightPreviewPanel::force_shading_refresh_if_needed(bool force_refresh) {
    const bool settings_changed = forced_settings_snapshot_ != last_applied_settings_;
    const bool refresh_light_map = force_refresh || settings_changed;
    const bool refresh_shading   = force_refresh || settings_changed;
    if (!assets_ || (!refresh_light_map && !refresh_shading)) {
        return;
    }
    forced_settings_snapshot_ = last_applied_settings_;
    if (refresh_light_map) {
        forced_quadrant_size_snapshot_ = last_quadrant_size_px_;
        assets_->force_virtual_light_map_refresh();
    }
    if (refresh_shading || refresh_light_map) {
        assets_->force_shaded_assets_rerender();
    }
}

void MapLightPreviewPanel::handle_chunk_resolution_changed() {
    needs_sync_to_json_ = true;
}
