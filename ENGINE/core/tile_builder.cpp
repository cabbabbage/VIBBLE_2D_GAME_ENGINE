#include "tile_builder.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include <SDL.h>

#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "tiling/grid_tile.hpp"
#include "utils/map_grid_settings.hpp"
#include "world/chunk.hpp"
#include "world/world_grid.hpp"
#include "utils/grid.hpp"

namespace {

struct ChunkTileAsset {
    Asset*       asset        = nullptr;
    SDL_Rect     sprite_world{0, 0, 0, 0};
    SDL_Texture* texture      = nullptr;
    int          texture_w    = 0;
    int          texture_h    = 0;
    bool         flipped      = false;
};

static int floor_div_int(int value, int step) {
    if (step == 0) {
        return 0;
    }
    const int quotient  = value / step;
    const int remainder = value % step;
    if (remainder == 0) {
        return quotient;
    }
    if ((remainder < 0) != (step < 0)) {
        return quotient - 1;
    }
    return quotient;
}

static std::uint64_t chunk_key(int i, int j) {
    const auto hi = static_cast<std::uint32_t>(i);
    const auto lo = static_cast<std::uint32_t>(j);
    return (static_cast<std::uint64_t>(hi) << 32) | static_cast<std::uint64_t>(lo);
}

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

static std::optional<SDL_Rect> compute_sprite_world_rect(const Asset* asset) {
    if (!asset || !asset->info) {
        return std::nullopt;
    }

    const int base_w = std::max(1, asset->info->original_canvas_width);
    const int base_h = std::max(1, asset->info->original_canvas_height);
    double scale     = 1.0;
    if (std::isfinite(asset->info->scale_factor) && asset->info->scale_factor > 0.0f) {
        scale = static_cast<double>(asset->info->scale_factor);
    }

    const int scaled_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(base_w) * scale)));
    const int scaled_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(base_h) * scale)));

    SDL_Rect rect{};
    rect.x = asset->pos.x - (scaled_w / 2);
    rect.y = asset->pos.y - scaled_h;
    rect.w = scaled_w;
    rect.h = scaled_h;
    return rect.w > 0 && rect.h > 0 ? std::optional<SDL_Rect>(rect) : std::nullopt;
}

static SDL_Rect compute_source_rect(const ChunkTileAsset& ctx, const SDL_Rect& sprite_overlap) {
    SDL_Rect invalid{0, 0, 0, 0};
    if (!ctx.texture || ctx.texture_w <= 0 || ctx.texture_h <= 0 || sprite_overlap.w <= 0 || sprite_overlap.h <= 0 ||
        ctx.sprite_world.w <= 0 || ctx.sprite_world.h <= 0) {
        return invalid;
    }

    const double inv_w = 1.0 / static_cast<double>(ctx.sprite_world.w);
    const double inv_h = 1.0 / static_cast<double>(ctx.sprite_world.h);

    double start_u = static_cast<double>(sprite_overlap.x - ctx.sprite_world.x) * inv_w;
    double end_u   = static_cast<double>((sprite_overlap.x + sprite_overlap.w) - ctx.sprite_world.x) * inv_w;
    double start_v = static_cast<double>(sprite_overlap.y - ctx.sprite_world.y) * inv_h;
    double end_v   = static_cast<double>((sprite_overlap.y + sprite_overlap.h) - ctx.sprite_world.y) * inv_h;

    start_u = std::clamp(start_u, 0.0, 1.0);
    end_u   = std::clamp(end_u, 0.0, 1.0);
    start_v = std::clamp(start_v, 0.0, 1.0);
    end_v   = std::clamp(end_v, 0.0, 1.0);

    if (ctx.flipped) {
        double flipped_start = 1.0 - end_u;
        double flipped_end   = 1.0 - start_u;
        start_u = flipped_start;
        end_u   = flipped_end;
    }

    const double tex_start_x = start_u * static_cast<double>(ctx.texture_w);
    const double tex_end_x   = end_u * static_cast<double>(ctx.texture_w);
    const double tex_start_y = start_v * static_cast<double>(ctx.texture_h);
    const double tex_end_y   = end_v * static_cast<double>(ctx.texture_h);

    int sx  = static_cast<int>(std::floor(tex_start_x));
    int sy  = static_cast<int>(std::floor(tex_start_y));
    int sx2 = static_cast<int>(std::ceil(tex_end_x));
    int sy2 = static_cast<int>(std::ceil(tex_end_y));

    sx  = std::clamp(sx, 0, std::max(0, ctx.texture_w - 1));
    sy  = std::clamp(sy, 0, std::max(0, ctx.texture_h - 1));
    sx2 = std::min(std::max(sx + 1, sx2), ctx.texture_w);
    sy2 = std::min(std::max(sy + 1, sy2), ctx.texture_h);

    SDL_Rect src{};
    src.x = sx;
    src.y = sy;
    src.w = std::max(1, sx2 - sx);
    src.h = std::max(1, sy2 - sy);
    return src;
}

}

namespace loader_tiles {

void build_grid_tiles(SDL_Renderer* renderer,
                      world::WorldGrid& grid,
                      const MapGridSettings& settings,
                      const std::vector<Asset*>& all_assets) {
    if (!renderer) return;

    const int step       = std::max(1, settings.spacing());
    const int chunk_step = 1 << std::clamp(grid.chunk_resolution(), 0, vibble::grid::kMaxResolution);
    if (chunk_step <= 0) {
        return;
    }
    const SDL_Point grid_origin = grid.origin();

    std::vector<ChunkTileAsset>                        asset_contexts;
    asset_contexts.reserve(all_assets.size());
    std::unordered_map<std::uint64_t, std::vector<const ChunkTileAsset*>> chunk_tilers;

    for (Asset* a : all_assets) {
        if (!a || !a->info || !a->info->tillable) continue;
        auto tiling = compute_tiling_for_asset(a, settings);
        if (!tiling || !tiling->is_valid()) continue;
        auto sprite_world = compute_sprite_world_rect(a);
        if (!sprite_world) continue;

        SDL_Texture* texture = a->get_current_frame();
        if (!texture) continue;

        int tex_w = 0;
        int tex_h = 0;
        if (SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h) != 0 || tex_w <= 0 || tex_h <= 0) {
            continue;
        }

        ChunkTileAsset ctx{};
        ctx.asset        = a;
        ctx.sprite_world = *sprite_world;
        ctx.texture      = texture;
        ctx.texture_w    = tex_w;
        ctx.texture_h    = tex_h;
        ctx.flipped      = a->flipped;

        asset_contexts.push_back(ctx);
        const ChunkTileAsset* stored_ctx = &asset_contexts.back();

        const int coverage_right  = tiling->coverage.x + tiling->coverage.w;
        const int coverage_bottom = tiling->coverage.y + tiling->coverage.h;
        const int chunk_i_min = floor_div_int(tiling->coverage.x - grid_origin.x, chunk_step);
        const int chunk_j_min = floor_div_int(tiling->coverage.y - grid_origin.y, chunk_step);
        const int chunk_i_max = floor_div_int((coverage_right - 1) - grid_origin.x, chunk_step);
        const int chunk_j_max = floor_div_int((coverage_bottom - 1) - grid_origin.y, chunk_step);

        for (int cj = chunk_j_min; cj <= chunk_j_max; ++cj) {
            for (int ci = chunk_i_min; ci <= chunk_i_max; ++ci) {
                grid.get_or_create_chunk_ij(ci, cj);
                chunk_tilers[chunk_key(ci, cj)].push_back(stored_ctx);
            }
        }
    }

    std::vector<world::Chunk*> chunks = grid.all_chunks();
    if (chunks.empty()) return;

    for (world::Chunk* chunk : chunks) {
        if (!chunk) continue;

        chunk->releaseTileTextures();

        const SDL_Rect bounds = chunk->world_bounds;
        if (bounds.w <= 0 || bounds.h <= 0) continue;

        const auto tiler_it = chunk_tilers.find(chunk_key(chunk->i, chunk->j));
        if (tiler_it == chunk_tilers.end() || tiler_it->second.empty()) {
            continue;
        }
        const auto& tilers = tiler_it->second;

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

                bool any = false;
                for (const ChunkTileAsset* ctx : tilers) {
                    if (!ctx) continue;
                    SDL_Rect sprite_inter{};
                    if (SDL_IntersectRect(&ctx->sprite_world, &tile_world, &sprite_inter) && sprite_inter.w > 0 &&
                        sprite_inter.h > 0) {
                        any = true;
                        break;
                    }
                }
                if (!any) continue;

                SDL_Texture* tile_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tile_world.w, tile_world.h);
                if (!tile_tex) continue;
                SDL_SetTextureBlendMode(tile_tex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(tile_tex, SDL_ScaleModeLinear);
                SDL_Texture* prev = SDL_GetRenderTarget(renderer);
                if (SDL_SetRenderTarget(renderer, tile_tex) != 0) {
                    SDL_DestroyTexture(tile_tex);
                    SDL_SetRenderTarget(renderer, prev);
                    continue;
                }

                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                SDL_RenderClear(renderer);

                for (const ChunkTileAsset* ctx : tilers) {
                    if (!ctx) continue;
                    SDL_Rect sprite_inter{};
                    if (!SDL_IntersectRect(&ctx->sprite_world, &tile_world, &sprite_inter) || sprite_inter.w <= 0 ||
                        sprite_inter.h <= 0) {
                        continue;
                    }

                    SDL_Rect dest{
                        sprite_inter.x - tile_world.x,
                        sprite_inter.y - tile_world.y,
                        sprite_inter.w,
                        sprite_inter.h
};
                    SDL_Rect src = compute_source_rect(*ctx, sprite_inter);
                    if (src.w <= 0 || src.h <= 0) {
                        continue;
                    }

                    SDL_RenderCopy(renderer, ctx->texture, &src, &dest);
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

}
