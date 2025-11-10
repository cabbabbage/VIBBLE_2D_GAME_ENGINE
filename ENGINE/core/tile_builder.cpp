#include "tile_builder.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>

#include <SDL.h>

#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "tiling/grid_tile.hpp"
#include "utils/map_grid_settings.hpp"
#include "world/chunk.hpp"
#include "world/grid.hpp"

namespace {

// Local copy of Assets::compute_tiling_for_asset logic to avoid coupling
// loader code to the runtime Assets manager.
static std::optional<Asset::TilingInfo> compute_tiling_for_asset(const Asset* asset,
                                                                 const MapGridSettings& grid_settings) {
    if (!asset || !asset->info || !asset->info->tillable) {
        return std::nullopt;
    }

    int step = grid_settings.spacing();
    if (step <= 0) {
        const int raw_w = std::max(1, asset->info->original_canvas_width);
        const int raw_h = std::max(1, asset->info->original_canvas_height);
        double scale = 1.0;
        if (std::isfinite(asset->info->scale_factor) && asset->info->scale_factor > 0.0f) {
            scale = static_cast<double>(asset->info->scale_factor);
        }
        step = std::max(1, static_cast<int>(std::lround(static_cast<double>(std::max(raw_w, raw_h)) * scale)));
    }
    step = std::max(1, step);

    const SDL_Point world_pos{ asset->pos.x, asset->pos.y };
    const int base_w = std::max(1, asset->info->original_canvas_width);
    const int base_h = std::max(1, asset->info->original_canvas_height);
    double scale = 1.0;
    if (std::isfinite(asset->info->scale_factor) && asset->info->scale_factor > 0.0f) {
        scale = static_cast<double>(asset->info->scale_factor);
    }
    const int scaled_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(base_w) * scale)));
    const int scaled_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(base_h) * scale)));

    const int left   = world_pos.x - (scaled_w / 2);
    const int top    = world_pos.y - scaled_h;
    const int right  = left + scaled_w;
    const int bottom = world_pos.y;

    auto align_down = [](int value, int step_) {
        if (step_ <= 0) return value;
        const double scaled = std::floor(static_cast<double>(value) / static_cast<double>(step_));
        return static_cast<int>(scaled * static_cast<double>(step_));
    };
    auto align_up = [](int value, int step_) {
        if (step_ <= 0) return value;
        const double scaled = std::ceil(static_cast<double>(value) / static_cast<double>(step_));
        return static_cast<int>(scaled * static_cast<double>(step_));
    };

    const int origin_x = align_down(left, step);
    const int origin_y = align_down(top, step);
    const int limit_x  = align_up(right, step);
    const int limit_y  = align_up(bottom, step);

    Asset::TilingInfo tiling{};
    tiling.enabled     = true;
    tiling.tile_size   = SDL_Point{ step, step };
    tiling.grid_origin = SDL_Point{ origin_x, origin_y };
    tiling.anchor      = SDL_Point{ align_down(world_pos.x, step) + step / 2,
                                    align_down(world_pos.y, step) + step / 2 };
    const int coverage_w = std::max(step, limit_x - origin_x);
    const int coverage_h = std::max(step, limit_y - origin_y);
    tiling.coverage = SDL_Rect{ origin_x, origin_y, coverage_w, coverage_h };
    return tiling.is_valid() ? std::optional<Asset::TilingInfo>(tiling) : std::nullopt;
}

} // namespace

namespace loader_tiles {

void build_grid_tiles(SDL_Renderer* renderer,
                      world::Grid& grid,
                      const MapGridSettings& settings,
                      const std::vector<Asset*>& all_assets) {
    if (!renderer) return;

    // Discover all chunks and prepare a lookup of tileable assets per chunk
    std::vector<world::Chunk*> chunks = grid.all_chunks();
    if (chunks.empty()) return;

    const int step = std::max(1, settings.spacing());

    for (world::Chunk* chunk : chunks) {
        if (!chunk) continue;

        // Clear any preexisting tiles on the chunk
        chunk->releaseTileTextures();

        const SDL_Rect bounds = chunk->world_bounds;
        if (bounds.w <= 0 || bounds.h <= 0) continue;

        // Build a temporary list of tileable assets that intersect this chunk
        std::vector<Asset*> tilers;
        tilers.reserve(chunk->assets.size());
        for (Asset* a : chunk->assets) {
            if (!a || !a->info || !a->info->tillable) continue;
            auto tiling = compute_tiling_for_asset(a, settings);
            if (!tiling || !tiling->is_valid()) continue;
            SDL_Rect inter{};
            if (SDL_IntersectRect(&tiling->coverage, &bounds, &inter) && inter.w > 0 && inter.h > 0) {
                tilers.push_back(a);
            }
        }
        if (tilers.empty()) continue;

        // Determine grid-aligned tile extents across the chunk
        auto align_down = [](int value, int step_) {
            const double scaled = std::floor(static_cast<double>(value) / static_cast<double>(step_));
            return static_cast<int>(scaled * static_cast<double>(step_));
        };
        auto align_up = [](int value, int step_) {
            const double scaled = std::ceil(static_cast<double>(value) / static_cast<double>(step_));
            return static_cast<int>(scaled * static_cast<double>(step_));
        };
        const int x0 = align_down(bounds.x, step);
        const int y0 = align_down(bounds.y, step);
        const int x1 = align_up(bounds.x + bounds.w, step);
        const int y1 = align_up(bounds.y + bounds.h, step);

        for (int y = y0; y < y1; y += step) {
            for (int x = x0; x < x1; x += step) {
                SDL_Rect tile_world{ x, y, step, step };

                // Check if any tiler covers this tile
                bool any = false;
                for (Asset* a : tilers) {
                    auto tiling = compute_tiling_for_asset(a, settings);
                    if (!tiling) continue;
                    SDL_Rect inter{};
                    if (SDL_IntersectRect(&tiling->coverage, &tile_world, &inter) && inter.w > 0 && inter.h > 0) {
                        any = true;
                        break;
                    }
                }
                if (!any) continue;

                // Create tile texture target
                SDL_Texture* tile_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tile_world.w, tile_world.h);
                if (!tile_tex) continue;
                SDL_SetTextureBlendMode(tile_tex, SDL_BLENDMODE_BLEND);
                SDL_Texture* prev = SDL_GetRenderTarget(renderer);
                if (SDL_SetRenderTarget(renderer, tile_tex) != 0) {
                    SDL_DestroyTexture(tile_tex);
                    SDL_SetRenderTarget(renderer, prev);
                    continue;
                }

                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                SDL_RenderClear(renderer);

                // Composite all tilers in this tile
                for (Asset* a : tilers) {
                    auto tiling = compute_tiling_for_asset(a, settings);
                    if (!tiling || !tiling->is_valid()) continue;
                    SDL_Rect inter{};
                    if (!SDL_IntersectRect(&tiling->coverage, &tile_world, &inter) || inter.w <= 0 || inter.h <= 0) {
                        continue;
                    }
                    // Draw the tile pattern: scale source frame to the full tile cell
                    SDL_Texture* src = a->get_final_texture();
                    if (!src) src = a->get_current_frame();
                    if (!src) continue;

                    int src_w = 0, src_h = 0;
                    SDL_QueryTexture(src, nullptr, nullptr, &src_w, &src_h);
                    if (src_w <= 0 || src_h <= 0) continue;

                    SDL_Rect src_full{0, 0, src_w, src_h};
                    SDL_Rect dest{0, 0, tile_world.w, tile_world.h};
                    SDL_RenderCopy(renderer, src, &src_full, &dest);
                }

                SDL_SetRenderTarget(renderer, prev);

                GridTile tile{};
                tile.world_rect = tile_world;
                tile.texture    = tile_tex;
                chunk->tiles.push_back(tile);
            }
        }
    }
}

} // namespace loader_tiles

