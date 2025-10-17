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
}  // namespace

MapLightPreviewPanel::MapLightPreviewPanel(Assets* assets, int x, int y)
    : DockableCollapsible("Virtual Light Map Preview", true, x, y), assets_(assets) {
    set_floating_content_width(360);
    set_visible_height(540);
    set_scroll_enabled(false);
    quadrant_note_text_ =
        "Note: Quadrant preview values are shared—tuning a cell updates all virtual light map cells.";
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

const VirtualLightMap* MapLightPreviewPanel::current_virtual_light_map() const {
    return assets_ ? assets_->virtual_light_map() : nullptr;
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
    std::vector<std::string> names;
    const VirtualLightMap* map = current_virtual_light_map();
    if (!assets_ || !map || quadrant < 0 || quadrant >= map->quadrant_count()) {
        return names;
    }

    const auto& active_assets = assets_->getActive();
    camera& cam = assets_->getView();

    for (Asset* asset : active_assets) {
        if (!asset) {
            continue;
        }
        SDL_Point world_pos{asset->pos.x, asset->pos.y};
        SDL_Point screen_pos = cam.map_to_screen(world_pos);
        int asset_quadrant = map->quadrant_for_point(static_cast<float>(screen_pos.x),
                                                     static_cast<float>(screen_pos.y));
        if (asset_quadrant == quadrant && asset->info) {
            names.push_back(asset->info->name);
        }
    }

    return names;
}

int MapLightPreviewPanel::quadrant_index_from_point(int x, int y) const {
    SDL_Point point{x, y};
    if (!SDL_PointInRect(&point, &preview_grid_rect_)) {
        return -1;
    }
    const VirtualLightMap* map = current_virtual_light_map();
    if (!map || preview_grid_rect_.w <= 0 || preview_grid_rect_.h <= 0) {
        return -1;
    }
    const int grid_w = map->grid_width();
    const int grid_h = map->grid_height();
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

    if (!renderer || !is_visible()) {
        return;
    }

    const VirtualLightMap* map = current_virtual_light_map();
    if (!map || map->screen_width <= 0 || map->screen_height <= 0) {
        return;
    }

    const int grid_w = map->grid_width();
    const int grid_h = map->grid_height();
    if (grid_w <= 0 || grid_h <= 0) {
        return;
    }

    quadrant_preview_rects_.assign(static_cast<std::size_t>(map->quadrant_count()), SDL_Rect{0, 0, 0, 0});

    const int padding = padding_;
    const int preview_x = rect_.x + padding;
    const int preview_y = rect_.y + header_rect_.h + padding - scroll_;
    const int available_width = std::max(40, rect_.w - padding * 2);

    const float aspect = (map->screen_width > 0)
                             ? static_cast<float>(map->screen_height) /
                                   static_cast<float>(map->screen_width)
                             : 1.0f;

    int grid_width_px = std::max(40, std::min(available_width, 320));
    int grid_height_px = static_cast<int>(std::lround(static_cast<double>(grid_width_px) *
                                                      static_cast<double>(aspect)));
    grid_height_px = std::max(grid_height_px, 40);

    int detail_gap = DMSpacing::item_gap();
    int detail_width = available_width - grid_width_px - detail_gap;
    bool detail_below = (detail_width < 160);

    SDL_Rect detail_rect{0, 0, 0, 0};
    if (detail_below) {
        detail_width = available_width;
        detail_gap = DMSpacing::small_gap();
        detail_rect = SDL_Rect{preview_x, preview_y + grid_height_px + detail_gap, detail_width, 0};
    } else {
        detail_rect = SDL_Rect{preview_x + grid_width_px + detail_gap, preview_y, detail_width, grid_height_px};
    }

    preview_grid_rect_ = SDL_Rect{preview_x, preview_y, grid_width_px, grid_height_px};
    preview_rect_ = SDL_Rect{preview_x, preview_y, available_width, grid_height_px};

    screen_width_px_ = map->screen_width;
    screen_height_px_ = map->screen_height;

    SDL_Color bg{30, 30, 30, 255};
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &preview_grid_rect_);

    auto cell_left = [&](int gx) {
        return preview_grid_rect_.x + static_cast<int>(std::lround(static_cast<double>(gx) *
                                                                   preview_grid_rect_.w /
                                                                   static_cast<double>(grid_w)));
    };
    auto cell_top = [&](int gy) {
        return preview_grid_rect_.y + static_cast<int>(std::lround(static_cast<double>(gy) *
                                                                   preview_grid_rect_.h /
                                                                   static_cast<double>(grid_h)));
    };

    for (int gy = 0; gy < grid_h; ++gy) {
        for (int gx = 0; gx < grid_w; ++gx) {
            const int left = cell_left(gx);
            const int right = cell_left(gx + 1);
            const int top = cell_top(gy);
            const int bottom = cell_top(gy + 1);

            SDL_Rect cell_rect{left,
                               top,
                               std::max(1, right - left),
                               std::max(1, bottom - top)};

            const std::size_t index = map->index_of(gx, gy);
            if (index < quadrant_preview_rects_.size()) {
                quadrant_preview_rects_[index] = cell_rect;
            }

            const VirtualLightMap::ShadowCell& cell = map->cell(gx, gy);
            const Uint8 brightness = static_cast<Uint8>(
                std::clamp(cell.brightness, 0.0f, 1.0f) * 255.0f);
            SDL_SetRenderDrawColor(renderer, brightness, brightness, brightness, 255);
            SDL_RenderFillRect(renderer, &cell_rect);
        }
    }

    SDL_Color grid_line{70, 70, 70, 255};
    SDL_SetRenderDrawColor(renderer, grid_line.r, grid_line.g, grid_line.b, grid_line.a);
    SDL_RenderDrawRect(renderer, &preview_grid_rect_);
    for (int gx = 1; gx < grid_w; ++gx) {
        const int x = cell_left(gx);
        SDL_RenderDrawLine(renderer, x, preview_grid_rect_.y, x,
                           preview_grid_rect_.y + preview_grid_rect_.h);
    }
    for (int gy = 1; gy < grid_h; ++gy) {
        const int y = cell_top(gy);
        SDL_RenderDrawLine(renderer, preview_grid_rect_.x, y,
                           preview_grid_rect_.x + preview_grid_rect_.w, y);
    }

    const int detail_quadrant = (selected_quadrant_ >= 0 &&
                                 selected_quadrant_ < map->quadrant_count())
                                    ? selected_quadrant_
                                    : -1;
    if (detail_quadrant >= 0 && detail_quadrant < static_cast<int>(quadrant_preview_rects_.size())) {
        const SDL_Rect& selected_rect = quadrant_preview_rects_[static_cast<std::size_t>(detail_quadrant)];
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
        const VirtualLightMap::ShadowCell& cell = map->cell_for_index(detail_quadrant);

        auto format_value = [](float value, int precision = 3) {
            std::ostringstream stream;
            stream << std::fixed << std::setprecision(precision) << value;
            return stream.str();
        };

        detail_lines.push_back("Cell [" + std::to_string(gx) + ", " + std::to_string(gy) + "] #" +
                               std::to_string(detail_quadrant));
        detail_lines.push_back("Brightness: " + format_value(cell.brightness));
        detail_lines.push_back("Opacity: " + format_value(cell.opacity));
        detail_lines.push_back("Offset X: " + format_value(cell.offset_x, 2));
        detail_lines.push_back("Offset Y: " + format_value(cell.offset_y, 2));
        detail_lines.push_back("Scale: " + format_value(cell.scale));
        detail_lines.push_back("");
        detail_lines.push_back("Assets:");

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
    if (!detail_text.empty() && detail_width > 0) {
        const DMLabelStyle& style = DMStyles::Label();
        std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)> font(style.open_font(), &TTF_CloseFont);
        if (font) {
            SDL_Surface* surface = TTF_RenderUTF8_Blended_Wrapped(font.get(), detail_text.c_str(), style.color,
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
    }

    if (detail_below) {
        detail_rect.h = detail_text_height;
        preview_rect_.h = grid_height_px + detail_gap + detail_text_height;
    } else {
        detail_rect.h = grid_height_px;
        preview_rect_.h = std::max(grid_height_px, detail_text_height);
    }
}

