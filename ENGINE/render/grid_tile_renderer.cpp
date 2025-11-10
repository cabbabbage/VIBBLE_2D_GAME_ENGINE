#include "grid_tile_renderer.hpp"

#include <algorithm>

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "tiling/grid_tile.hpp"
#include "world/chunk.hpp"
#include "world/grid.hpp"

void GridTileRenderer::render(SDL_Renderer* renderer) {
    if (!renderer || !assets_) return;
    render(renderer, assets_->getView(), assets_->world_grid());
}

void GridTileRenderer::render(SDL_Renderer* renderer, const camera& cam, const world::Grid& grid) {
    if (!renderer) return;

    const auto& chunks = grid.active_chunks();
    if (chunks.empty()) return;

    const SDL_Color white{255, 255, 255, 255};
    int indices[6] = {0, 1, 2, 0, 2, 3};

    for (const world::Chunk* chunk : chunks) {
        if (!chunk) continue;
        for (const auto& tile : chunk->tiles) {
            if (!tile.texture || tile.world_rect.w <= 0 || tile.world_rect.h <= 0) continue;

            SDL_Point world_tl{ tile.world_rect.x, tile.world_rect.y };
            SDL_Point world_tr{ tile.world_rect.x + tile.world_rect.w, tile.world_rect.y };
            SDL_Point world_br{ tile.world_rect.x + tile.world_rect.w, tile.world_rect.y + tile.world_rect.h };
            SDL_Point world_bl{ tile.world_rect.x, tile.world_rect.y + tile.world_rect.h };

            SDL_FPoint screen_tl = cam.map_to_screen(world_tl);
            SDL_FPoint screen_tr = cam.map_to_screen(world_tr);
            SDL_FPoint screen_br = cam.map_to_screen(world_br);
            SDL_FPoint screen_bl = cam.map_to_screen(world_bl);

            screen_tl.x = grid.parallax_adjusted_screen_x(world_tl, screen_tl.x);
            screen_tr.x = grid.parallax_adjusted_screen_x(world_tr, screen_tr.x);
            screen_br.x = grid.parallax_adjusted_screen_x(world_br, screen_br.x);
            screen_bl.x = grid.parallax_adjusted_screen_x(world_bl, screen_bl.x);

            const float tx1 = 1.0f;
            const float ty1 = 1.0f;

            SDL_Vertex vertices[4]{};
            vertices[0].position = SDL_FPoint{ screen_tl.x, screen_tl.y };
            vertices[1].position = SDL_FPoint{ screen_tr.x, screen_tr.y };
            vertices[2].position = SDL_FPoint{ screen_br.x, screen_br.y };
            vertices[3].position = SDL_FPoint{ screen_bl.x, screen_bl.y };
            vertices[0].color = vertices[1].color = vertices[2].color = vertices[3].color = white;
            vertices[0].tex_coord = SDL_FPoint{ 0.0f, 0.0f };
            vertices[1].tex_coord = SDL_FPoint{ tx1, 0.0f };
            vertices[2].tex_coord = SDL_FPoint{ tx1, ty1 };
            vertices[3].tex_coord = SDL_FPoint{ 0.0f, ty1 };

            SDL_RenderGeometry(renderer, tile.texture, vertices, 4, indices, 6);
        }
    }
}
