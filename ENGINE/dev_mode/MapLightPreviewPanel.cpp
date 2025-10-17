#include "MapLightPreviewPanel.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <SDL_ttf.h>

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/widgets.hpp"
#include "input.hpp"
#include "render/camera.hpp"
#include "render/light_map.hpp"

namespace {
int clamp_int(int value, int lo, int hi) {
    return std::max(lo, std::min(hi, value));
}

std::string format_float(float value, int precision) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}
}  // namespace

MapLightPreviewPanel::MapLightPreviewPanel(Assets* assets, int x, int y)
    : DockableCollapsible("Light Map Preview", true, x, y), assets_(assets) {
    set_floating_content_width(360);
    set_visible_height(540);
    set_scroll_enabled(false);
    quadrant_note_text_ =
        "Note: Light map tiles update from static + dynamic samples and fade when inactive.";
}

MapLightPreviewPanel::~MapLightPreviewPanel() = default;

void MapLightPreviewPanel::set_assets(Assets* assets) {
    assets_ = assets;
}

void MapLightPreviewPanel::update(const Input& input, int screen_w, int screen_h) {
    DockableCollapsible::update(input, screen_w, screen_h);
}

bool MapLightPreviewPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) {
        return false;
    }
    bool handled = DockableCollapsible::handle_event(e);
    if (handled) {
        return true;
    }

    if (e.type != SDL_MOUSEBUTTONDOWN && e.type != SDL_MOUSEMOTION) {
        return false;
    }

    SDL_Point point{0, 0};
    if (e.type == SDL_MOUSEBUTTONDOWN) {
        point = SDL_Point{e.button.x, e.button.y};
    } else {
        point = SDL_Point{e.motion.x, e.motion.y};
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

void MapLightPreviewPanel::render(SDL_Renderer* renderer) const {
    if (!is_visible()) {
        return;
    }
    DockableCollapsible::render(renderer);
    render_preview(renderer);
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


void MapLightPreviewPanel::render_preview(SDL_Renderer* renderer) const {
    preview_rect_ = SDL_Rect{0, 0, 0, 0};
    preview_grid_rect_ = SDL_Rect{0, 0, 0, 0};
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

    const int padding         = padding_;
    const int preview_x       = rect_.x + padding;
    const int preview_y       = rect_.y + header_rect_.h + padding - scroll_;
    const int available_width = std::max(40, rect_.w - padding * 2);

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
        stream << "S:" << format_float(snap->static_average, 2) << '\n';
        stream << "D:" << format_float(snap->dynamic_average, 2);
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
            } else if (const LightMapQuadrant* quadrant = map->quadrant(index)) {
                SDL_Rect world_rect = rect_from_world(quadrant->world_rect());
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
                if (const LightMapQuadrant* quadrant = map->quadrant(index)) {
                    SDL_Rect world_rect = quadrant->world_rect();
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
            detail_lines.push_back("Static Avg: " + format_float(snap->static_average, 3));
            detail_lines.push_back("Dynamic Avg: " + format_float(snap->dynamic_average, 3));
            detail_lines.push_back("Dynamic Range: " + format_float(snap->dynamic_min, 3) + " - " +
                                   format_float(snap->dynamic_max, 3));
            detail_lines.push_back("Shadow Strength: " + format_float(snap->shadow_opacity_min, 3) + " - " +
                                   format_float(snap->shadow_opacity_max, 3));
        }

        if (const LightMapQuadrant* quadrant = map->quadrant(detail_quadrant)) {
            detail_lines.push_back("Grid Resolution: " + std::to_string(quadrant->grid_width()) + "x" +
                                   std::to_string(quadrant->grid_height()));
            detail_lines.push_back("Padding: " + std::to_string(quadrant->padding()));
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

    if (detail_below) {
        detail_rect.h = detail_text_height;
        preview_rect_.h = grid_height_px + detail_gap + detail_text_height;
    } else {
        detail_rect.h = grid_height_px;
        preview_rect_.h = std::max(grid_height_px, detail_text_height);
    }
}


