#include "grid_tile_renderer.hpp"

#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "tiling/grid_tile.hpp"
#include "world/chunk.hpp"
#include "world/grid.hpp"

namespace {

SDL_Point chunk_center(const world::Chunk& chunk) {
    return SDL_Point{
        chunk.world_bounds.x + chunk.world_bounds.w / 2,
        chunk.world_bounds.y + chunk.world_bounds.h / 2
    };
}

}  // namespace

void GridTileRenderer::render(SDL_Renderer* renderer) {
    if (!renderer || !assets_) {
        return;
    }
    render(renderer, assets_->getView(), assets_->world_grid());
}

void GridTileRenderer::render(SDL_Renderer* renderer, const camera& cam, const world::Grid& grid) {
    if (!renderer) {
        return;
    }

    const auto& chunks = grid.active_chunks();
    if (chunks.empty()) {
        return;
    }

    for (const world::Chunk* chunk : chunks) {
        if (!chunk || chunk->tiles.empty()) {
            continue;
        }

        const float chunk_parallax = grid.parallax_offset(chunk_center(*chunk));
        const SDL_Rect chunk_bounds = chunk->world_bounds;

        for (const auto& tile : chunk->tiles) {
            if (!tile.texture || tile.world_rect.w <= 0 || tile.world_rect.h <= 0) {
                continue;
            }

            SDL_Rect tile_bounds = tile.world_rect;
            SDL_Rect overlap{};
            if (!SDL_IntersectRect(&chunk_bounds, &tile_bounds, &overlap) || overlap.w <= 0 || overlap.h <= 0) {
                continue;
            }

            SDL_Point overlap_tl{overlap.x, overlap.y};
            SDL_Point overlap_br{overlap.x + overlap.w, overlap.y + overlap.h};
            SDL_FPoint screen_tl = cam.map_to_screen(overlap_tl);
            SDL_FPoint screen_br = cam.map_to_screen(overlap_br);

            SDL_FRect dst{
                screen_tl.x + chunk_parallax,
                screen_tl.y,
                screen_br.x - screen_tl.x,
                screen_br.y - screen_tl.y
            };

            if (dst.w <= 0.0f || dst.h <= 0.0f) {
                continue;
            }

            SDL_Rect src{
                overlap.x - tile.world_rect.x,
                overlap.y - tile.world_rect.y,
                overlap.w,
                overlap.h
            };

            SDL_RenderCopyF(renderer, tile.texture, &src, &dst);
        }
    }
}
