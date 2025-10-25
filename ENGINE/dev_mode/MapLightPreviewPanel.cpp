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

enum class PreviewStage : int {
    Mask = 0,
};

constexpr int kPreviewStageCount = 1;

constexpr std::array<const char*, kPreviewStageCount> kPreviewStageLabels{
    "Mask",
};

PreviewStage normalize_stage(int stage_index) {
    if (kPreviewStageCount <= 0) {
        return PreviewStage::Mask;
    }
    int normalized = stage_index % kPreviewStageCount;
    if (normalized < 0) {
        normalized += kPreviewStageCount;
    }
    return static_cast<PreviewStage>(normalized);
}

SDL_Texture* texture_for_stage(const world::Chunk* chunk, PreviewStage stage) {
    if (!chunk) {
        return nullptr;
    }
    switch (stage) {
        case PreviewStage::Mask:
            return nullptr;
    }
    return nullptr;
}

bool stage_requires_blend(PreviewStage stage) {
    (void)stage;
    return false;
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
    chunk_note_text_ =
        "Note: Light map tiles update from static + dynamic samples and fade when inactive.";
    last_chunk_resolution_ = MapGridSettings::defaults().r_chunk;

    build_ui();
    rebuild_rows();
}

MapLightPreviewPanel::~MapLightPreviewPanel() = default;

void MapLightPreviewPanel::set_assets(Assets* assets) {
    assets_ = assets;
    if (assets_) {
        last_chunk_resolution_ = clamp_int(assets_->map_grid_chunk_resolution(), 0, vibble::grid::kMaxResolution);
        if (chunk_resolution_) {
            chunk_resolution_->set_value(last_chunk_resolution_);
        }
    }
    force_shading_refresh_if_needed(true);
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
    apply_immediate_settings();
    sync_ui_from_json();
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
        const int chunk_index_value = chunk_resolution_->value();
        if (chunk_index_value != last_chunk_resolution_) {
            last_chunk_resolution_ = chunk_index_value;
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

const LightMapManager::ChunkSnapshot* MapLightPreviewPanel::snapshot_for_chunk(int index) const {
    if (index < 0) {
        return nullptr;
    }
    const std::size_t idx = static_cast<std::size_t>(index);
    if (idx >= chunk_snapshot_valid_.size() || idx >= chunk_snapshots_.size()) {
        return nullptr;
    }
    if (!chunk_snapshot_valid_[idx]) {
        return nullptr;
    }
    return &chunk_snapshots_[idx];
}

std::optional<SDL_Point> MapLightPreviewPanel::player_screen_position() const {
    if (!assets_) {
        return std::nullopt;
    }
    camera& view = assets_->getView();
    SDL_Point center = view.get_screen_center();
    return center;
}

std::vector<std::string> MapLightPreviewPanel::assets_in_chunk(int chunk_index) const {
    if (const LightMapManager* manager = light_map_manager()) {
        return manager->assets_sampling_chunk(chunk_index);
    }
    return {};
}

int MapLightPreviewPanel::chunk_index_from_point(int x, int y) const {
    SDL_Point point{x, y};
    if (!SDL_PointInRect(&point, &preview_grid_rect_)) {
        return -1;
    }
    for (std::size_t i = 0; i < chunk_preview_rects_.size(); ++i) {
        if (SDL_PointInRect(&point, &chunk_preview_rects_[i])) {
            return static_cast<int>(i);
        }
    }
    const LightMap* map = current_light_map();
    if (!map || preview_grid_rect_.w <= 0 || preview_grid_rect_.h <= 0) {
        return -1;
    }
    const int grid_w = map->chunk_columns();
    const int grid_h = map->chunk_rows();
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

    const int chunk_index = chunk_index_from_point(point.x, point.y);
    if (chunk_index < 0) {
        return false;
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        selected_chunk_ = chunk_index;
        return true;
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) {
        selected_chunk_ = chunk_index;
        cycle_preview_stage(1);
        return true;
    }

    if (e.type == SDL_MOUSEMOTION) {
        selected_chunk_ = chunk_index;
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

void MapLightPreviewPanel::cycle_preview_stage(int delta) {
    if (delta == 0) {
        return;
    }
    if (kPreviewStageCount <= 0) {
        preview_stage_index_ = 0;
        return;
    }
    preview_stage_index_ += delta;
    if (preview_stage_index_ >= kPreviewStageCount) {
        preview_stage_index_ %= kPreviewStageCount;
    }
    while (preview_stage_index_ < 0) {
        preview_stage_index_ += kPreviewStageCount;
    }
}

int MapLightPreviewPanel::estimated_detail_line_count() const {
    int count = 0;

    const LightMapManager* manager = light_map_manager();
    const LightMap*        map     = manager ? manager->light_map() : nullptr;
    if (!map && assets_) {
        map = assets_->light_map();
    }

    const int total_chunks = map ? map->chunk_count() : 0;
    const int detail_chunk =
        (selected_chunk_ >= 0 && selected_chunk_ < total_chunks) ? selected_chunk_ : -1;

    if (detail_chunk >= 0) {
        count += 1;  // Tile header
        count += 1;  // Stage line

        // Assume full snapshot metrics when available.
        count += 7;

        if (map) {
            count += 2;  // Grid resolution + padding
        }

        count += 1;  // Blank line before assets
        count += 1;  // Assets Sampling header

        int asset_lines = 1;
        if (manager) {
            const auto assets = manager->assets_sampling_chunk(detail_chunk);
            asset_lines = static_cast<int>(assets.empty() ? 1 : assets.size());
        }
        count += asset_lines;
    } else {
        count += 1;  // No Chunk selected
    }

    if (!chunk_note_text_.empty()) {
        count += 1;  // Blank line before note
        count += count_lines(chunk_note_text_);
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
    chunk_preview_rects_.clear();
    chunk_snapshots_.clear();
    chunk_snapshot_valid_.clear();

    if (!renderer || !is_visible()) {
        return;
    }

    const LightMapManager* manager = light_map_manager();
    const LightMap*        map     = manager ? manager->light_map() : nullptr;
    if (!manager || !map || map->screen_width() <= 0 || map->screen_height() <= 0) {
        return;
    }

    const int grid_w = map->chunk_columns();
    const int grid_h = map->chunk_rows();
    if (grid_w <= 0 || grid_h <= 0) {
        return;
    }

    const int total_chunks = map->chunk_count();
    chunk_preview_rects_.assign(static_cast<std::size_t>(total_chunks), SDL_Rect{0, 0, 0, 0});
    chunk_snapshots_.assign(static_cast<std::size_t>(total_chunks), LightMapManager::ChunkSnapshot{});
    chunk_snapshot_valid_.assign(static_cast<std::size_t>(total_chunks), false);

    const std::vector<LightMapManager::ChunkSnapshot> snapshots = manager->all_snapshots();
    for (const auto& snapshot : snapshots) {
        if (snapshot.index >= 0 && snapshot.index < total_chunks) {
            const std::size_t idx = static_cast<std::size_t>(snapshot.index);
            chunk_snapshots_[idx]      = snapshot;
            chunk_snapshot_valid_[idx] = true;
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

    const int detail_available_height = detail_below
                                            ? std::max(0, preview_widget_bounds_.h - grid_height_px - detail_gap)
                                            : grid_height_px;

    PreviewStage stage = normalize_stage(preview_stage_index_);
    std::string stage_label_text{kPreviewStageLabels[static_cast<int>(stage)]};

    const int detail_chunk =
        (selected_chunk_ >= 0 && selected_chunk_ < total_chunks) ? selected_chunk_ : -1;
    const world::Chunk* detail_chunk_ptr = (detail_chunk >= 0) ? map->chunk_at(detail_chunk) : nullptr;

    SDL_Texture* stage_texture = texture_for_stage(detail_chunk_ptr, stage);
    bool         stage_available = (stage_texture != nullptr);

    std::string stage_header_text = std::string("Stage: ") + stage_label_text;
    if (!stage_available) {
        stage_header_text.append(" (no preview)");
    }

    preview_viewport_.setRenderer(renderer);
    preview_viewport_.setLabel(stage_label_text);
    preview_viewport_.setTarget(stage_texture);
    preview_viewport_.setPresentBlendMode(SDL_BLENDMODE_BLEND);
    preview_viewport_.enablePresentBlend(stage_requires_blend(stage));

    const int stage_padding = DMSpacing::small_gap();
    const int available_stage_width = std::max(0, detail_width - stage_padding * 2);
    const int available_stage_height = std::max(0, detail_available_height - stage_padding * 2);
    const int stage_label_height = (detail_font) ? TTF_FontHeight(detail_font.get()) : 0;
    const int texture_w = stage_texture ? preview_viewport_.width() : 0;
    const int texture_h = stage_texture ? preview_viewport_.height() : 0;

    int stage_texture_width  = 0;
    int stage_texture_height = 0;
    if (stage_texture && texture_w > 0 && texture_h > 0 && available_stage_width > 0 && available_stage_height > stage_label_height) {
        int available_for_texture = available_stage_height - stage_label_height;
        if (stage_label_height > 0) {
            available_for_texture = std::max(0, available_for_texture - stage_padding);
        }
        if (available_for_texture > 0) {
            const double aspect = static_cast<double>(texture_h) / static_cast<double>(texture_w);
            stage_texture_width = available_stage_width;
            stage_texture_height = static_cast<int>(std::lround(static_cast<double>(stage_texture_width) * aspect));
            if (stage_texture_height > available_for_texture) {
                stage_texture_height = available_for_texture;
                stage_texture_width = static_cast<int>(std::lround(static_cast<double>(stage_texture_height) / aspect));
            }
            if (stage_texture_width > available_stage_width) {
                stage_texture_width = available_stage_width;
            }
            if (stage_texture_height <= 0 || stage_texture_width <= 0) {
                stage_texture_width  = 0;
                stage_texture_height = 0;
            }
        }
    }

    int stage_block_height = 0;
    if (detail_chunk_ptr && (stage_label_height > 0 || stage_texture_height > 0)) {
        stage_block_height = stage_padding * 2 + stage_label_height;
        if (stage_texture_width > 0 && stage_texture_height > 0) {
            stage_block_height += stage_padding + stage_texture_height;
        }
        stage_block_height = std::min(stage_block_height, detail_available_height);
    }

    if (stage_block_height > 0 && stage_texture_width > 0 && stage_texture_height > 0) {
        int block_inner_height = stage_block_height - stage_padding * 2 - stage_label_height;
        if (stage_label_height > 0) {
            block_inner_height = std::max(0, block_inner_height - stage_padding);
        }
        int adjusted_height = std::min(stage_texture_height, std::max(0, block_inner_height));
        if (adjusted_height <= 0) {
            stage_texture_width  = 0;
            stage_texture_height = 0;
        } else if (adjusted_height != stage_texture_height && texture_w > 0 && texture_h > 0) {
            const double aspect = static_cast<double>(texture_h) / static_cast<double>(texture_w);
            stage_texture_width = std::min(available_stage_width,
                                           static_cast<int>(std::lround(static_cast<double>(adjusted_height) /
                                                                        aspect)));
            stage_texture_height = adjusted_height;
        } else {
            stage_texture_height = adjusted_height;
        }
    }

    if (stage_block_height == 0) {
        stage_texture_width  = 0;
        stage_texture_height = 0;
    }

    SDL_Rect stage_panel_rect{detail_rect.x, detail_rect.y, detail_width, stage_block_height};
    SDL_Rect stage_label_rect{detail_rect.x + stage_padding,
                              detail_rect.y + stage_padding,
                              std::max(0, detail_width - stage_padding * 2),
                              stage_label_height};
    if (stage_label_rect.h > std::max(0, stage_panel_rect.h - stage_padding * 2)) {
        stage_label_rect.h = std::max(0, stage_panel_rect.h - stage_padding * 2);
    }

    SDL_Rect stage_texture_rect{detail_rect.x + stage_padding,
                                stage_label_rect.y + stage_label_rect.h + (stage_label_rect.h > 0 ? stage_padding : 0),
                                stage_texture_width,
                                stage_texture_height};
    if (stage_texture_rect.w > 0) {
        int centered_x = detail_rect.x + stage_padding +
                         std::max(0, (available_stage_width - stage_texture_rect.w) / 2);
        stage_texture_rect.x = centered_x;
    }

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
                                      const LightMapManager::ChunkSnapshot* snap) {
        if (!snap || !tile_font || rect.w <= 10 || rect.h <= 14) {
            return;
        }
        std::ostringstream stream;
        stream << (snap->active ? "A" : "-") << ' ' << (snap->needs_update ? "U" : "-") << '\n';
        stream << "B:" << format_float(snap->brightness, 2) << '\n';
        stream << "S:" << format_float(snap->static_component, 2) << '\n';
        stream << "D:" << format_float(snap->dynamic_component, 2) << '\n';
        if (snap->has_runtime_sample) {
            stream << "R:" << format_float(snap->runtime_sample, 2);
        } else {
            stream << "R:--";
        }
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

    const SDL_Color update_color{200, 120, 40, 255};
    const SDL_Color outline_color{249, 115, 22, 255};

    for (int gy = 0; gy < grid_h; ++gy) {
        for (int gx = 0; gx < grid_w; ++gx) {
            const int index = gy * grid_w + gx;
            SDL_Rect cell_rect = fallback_cell_rect(gx, gy);

            if (static_cast<std::size_t>(index) < chunk_preview_rects_.size()) {
                chunk_preview_rects_[static_cast<std::size_t>(index)] = cell_rect;
            }

            const LightMapManager::ChunkSnapshot* snap = snapshot_for_chunk(index);
            float brightness = snap ? snap->brightness : 0.0f;
            if (!snap) {
                if (const world::Chunk* chunk = map->chunk_at(index)) {
                    SDL_Rect world_rect = chunk->world_bounds;
                    const float cx = static_cast<float>(world_rect.x) + static_cast<float>(world_rect.w) * 0.5f;
                    const float cy = static_cast<float>(world_rect.y) + static_cast<float>(world_rect.h) * 0.5f;
                    const LightMap::SampledBrightness sample =
                        map->sample_lighting(static_cast<int>(std::round(cx)),
                                             static_cast<int>(std::round(cy)));
                    brightness = sample.blended;
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
                if (snap->needs_update) {
                    draw_indicator(cell_rect, update_color, true);
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

    if (detail_chunk >= 0 &&
        static_cast<std::size_t>(detail_chunk) < chunk_preview_rects_.size()) {
        const SDL_Rect& selected_rect =
            chunk_preview_rects_[static_cast<std::size_t>(detail_chunk)];
        if (selected_rect.w > 0 && selected_rect.h > 0) {
            const SDL_Color accent = DMStyles::HighlightColor();
            SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, accent.a);
            SDL_RenderDrawRect(renderer, &selected_rect);
        }
    }

    std::vector<std::string> detail_lines;
    if (detail_chunk >= 0) {
        const int gx = detail_chunk % grid_w;
        const int gy = detail_chunk / grid_w;
        detail_lines.push_back("Chunk(" + std::to_string(gx) + ", " + std::to_string(gy) + ") #" +
                               std::to_string(detail_chunk));

        float current_brightness = 0.0f;
        float current_static     = 0.0f;
        float current_dynamic    = 0.0f;
        bool  has_current        = false;
        if (const auto* snap = snapshot_for_chunk(detail_chunk)) {
            current_brightness = std::clamp(snap->brightness, 0.0f, 1.0f);
            current_static     = std::clamp(snap->static_component, 0.0f, 1.0f);
            current_dynamic    = std::clamp(snap->dynamic_component, 0.0f, 1.0f);
            has_current        = true;
        } else if (const world::Chunk* chunk = map->chunk_at(detail_chunk)) {
            SDL_Rect world_rect = chunk->world_bounds;
            if (world_rect.w > 0 && world_rect.h > 0) {
                const float cx = static_cast<float>(world_rect.x) + static_cast<float>(world_rect.w) * 0.5f;
                const float cy = static_cast<float>(world_rect.y) + static_cast<float>(world_rect.h) * 0.5f;
                const LightMap::SampledBrightness sample =
                    map->sample_lighting(static_cast<int>(std::round(cx)),
                                         static_cast<int>(std::round(cy)));
                current_brightness = std::clamp(sample.blended, 0.0f, 1.0f);
                current_static     = std::clamp(sample.static_component, 0.0f, 1.0f);
                current_dynamic    = std::clamp(sample.dynamic_component, 0.0f, 1.0f);
                has_current        = true;
            }
        }
        if (!has_current) {
            detail_lines.push_back("Current Brightness: --");
            detail_lines.push_back("Static Component: --");
            detail_lines.push_back("Dynamic Component: --");
        } else {
            detail_lines.push_back("Current Brightness: " + format_float(current_brightness, 3));
            detail_lines.push_back("Static Component: " + format_float(current_static, 3));
            detail_lines.push_back("Dynamic Component: " + format_float(current_dynamic, 3));
        }

        if (const auto* snap = snapshot_for_chunk(detail_chunk)) {
            detail_lines.push_back(std::string("Needs Update: ") + (snap->needs_update ? "yes" : "no"));
            if (snap->has_runtime_sample) {
                detail_lines.push_back("Runtime Sample: " + format_float(snap->runtime_sample, 3));
            } else {
                detail_lines.push_back("Runtime Sample: --");
            }
        } else {
            detail_lines.push_back("Needs Update: --");
            detail_lines.push_back("Runtime Sample: --");
        }

        std::string stage_line = std::string("Preview Stage: ") + stage_label_text;
        if (!stage_available) {
            stage_line.append(" (no preview)");
        }
        stage_line.append(" | Right-click to cycle");
        detail_lines.push_back(std::move(stage_line));

        if (const auto* snap = snapshot_for_chunk(detail_chunk)) {
            detail_lines.push_back(std::string("Active: ") + (snap->active ? "yes" : "no"));
            detail_lines.push_back("Shadow Opacity: " + format_float(snap->shadow.opacity, 3));
            detail_lines.push_back("Shadow Scale: " + format_float(snap->shadow.scale, 3));
            detail_lines.push_back("Shadow Offset X%: " + format_float(snap->shadow.offset_x_percent, 3));
            detail_lines.push_back("Shadow Offset Y%: " + format_float(snap->shadow.offset_y_percent, 3));
            detail_lines.push_back("Shadow Parallax%: " +
                                   format_float(snap->shadow.parallax_intensity_percent, 3));
        }

        detail_lines.push_back("");
        detail_lines.push_back("Assets Sampling:");

        auto assets = assets_in_chunk(detail_chunk);
        if (assets.empty()) {
            detail_lines.push_back("  (none)");
        } else {
            for (const auto& name : assets) {
                detail_lines.push_back("  - " + name);
            }
        }
    } else {
        detail_lines.push_back("No Chunk selected.");
    }

    if (!chunk_note_text_.empty()) {
        detail_lines.push_back("");
        detail_lines.push_back(chunk_note_text_);
    }

    std::string detail_text;
    for (std::size_t i = 0; i < detail_lines.size(); ++i) {
        if (i > 0) {
            detail_text.push_back('\n');
        }
        detail_text.append(detail_lines[i]);
    }

    const SDL_Color panel_bg{20, 20, 20, 180};

    const int text_gap = (!detail_text.empty() && stage_block_height > 0) ? DMSpacing::item_gap() : 0;
    SDL_Rect detail_text_rect{detail_rect.x,
                              detail_rect.y + stage_block_height + text_gap,
                              detail_width,
                              std::max(0, detail_available_height - stage_block_height - text_gap)};

    if (stage_block_height > 0) {
        SDL_SetRenderDrawColor(renderer, panel_bg.r, panel_bg.g, panel_bg.b, panel_bg.a);
        SDL_RenderFillRect(renderer, &stage_panel_rect);
        SDL_SetRenderDrawColor(renderer, outline_color.r, outline_color.g, outline_color.b, outline_color.a);
        SDL_RenderDrawRect(renderer, &stage_panel_rect);

        if (!stage_header_text.empty() && detail_font && stage_label_rect.h > 0) {
            SDL_Surface* label_surface =
                TTF_RenderUTF8_Blended(detail_font.get(), stage_header_text.c_str(), text_color);
            if (label_surface) {
                SDL_Texture* label_texture = SDL_CreateTextureFromSurface(renderer, label_surface);
                if (label_texture) {
                    SDL_Rect label_dst{stage_label_rect.x,
                                       stage_label_rect.y,
                                       label_surface->w,
                                       label_surface->h};
                    label_dst.w = std::min(label_dst.w, stage_label_rect.w);
                    label_dst.h = std::min(label_dst.h, stage_label_rect.h);
                    SDL_RenderCopy(renderer, label_texture, nullptr, &label_dst);
                    SDL_DestroyTexture(label_texture);
                }
                SDL_FreeSurface(label_surface);
            }
        }

        if (stage_texture && stage_texture_rect.w > 0 && stage_texture_rect.h > 0) {
            preview_viewport_.present(stage_texture_rect);
            SDL_SetRenderDrawColor(renderer, outline_color.r, outline_color.g, outline_color.b, outline_color.a);
            SDL_RenderDrawRect(renderer, &stage_texture_rect);
        }
    }

    int detail_text_height = 0;
    int detail_text_drawn  = 0;
    if (!detail_text.empty() && detail_width > 0 && detail_font && detail_text_rect.h > 0) {
        SDL_Surface* surface =
            TTF_RenderUTF8_Blended_Wrapped(detail_font.get(), detail_text.c_str(), text_color,
                                           std::max(10, detail_width));
        if (surface) {
            detail_text_height = surface->h;
            SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
            if (texture) {
                SDL_Rect dst{detail_text_rect.x, detail_text_rect.y, surface->w, surface->h};
                dst.w = std::min(dst.w, detail_text_rect.w);
                dst.h = std::min(dst.h, detail_text_rect.h);
                SDL_Rect bg_rect = detail_text_rect;
                bg_rect.h = dst.h;
                SDL_SetRenderDrawColor(renderer, panel_bg.r, panel_bg.g, panel_bg.b, panel_bg.a);
                SDL_RenderFillRect(renderer, &bg_rect);
                SDL_RenderCopy(renderer, texture, nullptr, &dst);
                SDL_DestroyTexture(texture);
                detail_text_drawn = dst.h;
            }
            SDL_FreeSurface(surface);
        }
    }

    int detail_drawn_height = stage_block_height;
    if (detail_text_drawn > 0) {
        detail_drawn_height += text_gap + detail_text_drawn;
    }

    // Clamp final heights to the widget bounds to avoid drawing over controls
    const int max_preview_h = std::max(0, preview_widget_bounds_.h);
    if (detail_below) {
        const int remaining = detail_available_height;
        detail_rect.h = std::min(detail_drawn_height, remaining);
        preview_rect_.h = std::min(max_preview_h, grid_height_px + detail_gap + detail_rect.h);
    } else {
        detail_rect.h = grid_height_px;
        int detail_panel_height = detail_drawn_height;
        if (detail_panel_height == 0 && detail_text_height > 0) {
            detail_panel_height = std::min(detail_text_height, detail_text_rect.h);
        }
        preview_rect_.h = std::min(max_preview_h, std::max(grid_height_px, detail_panel_height));
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
        last_applied_settings_ = render_pipeline::shading::reactive_shadow_settings_from_json(*it, last_applied_settings_);
        last_applied_settings_ = render_pipeline::shading::sanitize_reactive_shadow_settings(last_applied_settings_);
        set_reactive_sliders(last_applied_settings_);
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
    settings.opacity_sensitivity_percent = static_cast<float>(
        load_number(make_setting_key("opacity_sensitivity_percent"), settings.opacity_sensitivity_percent));
    settings.virtual_light_map.min_scale_percent = static_cast<int>(std::lround(load_number(
        make_setting_key("virtual_light_map.min_scale_percent"),
        static_cast<double>(settings.virtual_light_map.min_scale_percent))));
    settings.virtual_light_map.max_scale_percent = static_cast<int>(std::lround(load_number(
        make_setting_key("virtual_light_map.max_scale_percent"),
        static_cast<double>(settings.virtual_light_map.max_scale_percent))));
    settings.frame_blend_falloff_frames = static_cast<int>(std::lround(load_number(
        make_setting_key("frame_blend_falloff_frames"),
        static_cast<double>(settings.frame_blend_falloff_frames))));
    settings.virtual_light_map.map_light_dir_offset_strength = static_cast<float>(load_number(
        make_setting_key("virtual_light_map.map_light_dir_offset_strength"),
        settings.virtual_light_map.map_light_dir_offset_strength));
    settings.virtual_light_map.parallax_percent = static_cast<float>(load_number(
        make_setting_key("virtual_light_map.parallax_percent"), settings.virtual_light_map.parallax_percent));
    settings.virtual_light_map.search_radius = static_cast<int>(
        std::lround(load_number(make_setting_key("virtual_light_map.search_radius"),
                                 static_cast<double>(settings.virtual_light_map.search_radius))));
    return render_pipeline::shading::sanitize_reactive_shadow_settings(settings);
}

void MapLightPreviewPanel::persist_reactive_settings_to_dev_settings(const render_pipeline::shading::ReactiveShadowSettings& settings) const {
    using devmode::ui_settings::save_number;
    save_number(make_setting_key("virtual_light_map.horizontal_falloff"), settings.virtual_light_map.horizontal_falloff);
    save_number(make_setting_key("virtual_light_map.vertical_falloff"), settings.virtual_light_map.vertical_falloff);
    save_number(make_setting_key("virtual_light_map.max_offset_x"), settings.virtual_light_map.max_offset_x);
    save_number(make_setting_key("virtual_light_map.max_offset_y"), settings.virtual_light_map.max_offset_y);
    save_number(make_setting_key("opacity_sensitivity_percent"), settings.opacity_sensitivity_percent);
    save_number(make_setting_key("virtual_light_map.min_scale_percent"), settings.virtual_light_map.min_scale_percent);
    save_number(make_setting_key("virtual_light_map.max_scale_percent"), settings.virtual_light_map.max_scale_percent);
    save_number(make_setting_key("frame_blend_falloff_frames"), settings.frame_blend_falloff_frames);
    save_number(make_setting_key("virtual_light_map.map_light_dir_offset_strength"),
                settings.virtual_light_map.map_light_dir_offset_strength);
    save_number(make_setting_key("virtual_light_map.parallax_percent"), settings.virtual_light_map.parallax_percent);
    save_number(make_setting_key("virtual_light_map.search_radius"), settings.virtual_light_map.search_radius);
}

void MapLightPreviewPanel::write_reactive_settings_to_json(const render_pipeline::shading::ReactiveShadowSettings& settings) {
    if (!map_info_) {
        return;
    }
    nlohmann::json& json = (*map_info_)["reactive_shadows"];
    render_pipeline::shading::assign_reactive_shadow_settings(json, settings);
}

nlohmann::json& MapLightPreviewPanel::ensure_reactive_settings_json() {
    if (!map_info_) {
        static nlohmann::json dummy = nlohmann::json::object();
        return dummy;
    }
    return (*map_info_)["reactive_shadows"];
}

void MapLightPreviewPanel::force_shading_refresh_if_needed(bool force_refresh) {
    const bool settings_changed = forced_settings_snapshot_ != last_applied_settings_;
    const bool should_refresh   = force_refresh || settings_changed;
    if (!assets_ || !should_refresh) {
        return;
    }
    forced_settings_snapshot_ = last_applied_settings_;
    assets_->force_shaded_assets_rerender();
}

void MapLightPreviewPanel::handle_chunk_resolution_changed() {
    needs_sync_to_json_ = true;
}








