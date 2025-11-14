#include "grid_tile_renderer.hpp"

#include <algorithm>
#include <cmath>

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "tiling/grid_tile.hpp"
#include "world/chunk.hpp"
#include "world/grid.hpp"

namespace grid_tile_renderer_detail {
constexpr float kParallaxEqualityEpsilon = 0.001f;

inline bool nearly_equal(float a, float b, float eps) {
    return std::fabs(a - b) <= eps;
}

}  // namespace grid_tile_renderer_detail

void GridTileRenderer::render(SDL_Renderer* renderer) {
    if (!renderer || !assets_) return;
    render(renderer, assets_->getView(), assets_->world_grid());
}

void GridTileRenderer::render(SDL_Renderer* renderer, const camera& cam, const world::Grid& grid) {
    if (!renderer) return;

    const auto& chunks = grid.active_chunks();
    if (chunks.empty()) return;

    // Depth-cue effects are intentionally not applied to tiles.

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

            const float offset_tl = grid.parallax_offset(world_tl);
            const float offset_tr = grid.parallax_offset(world_tr);
            const float offset_br = grid.parallax_offset(world_br);
            const float offset_bl = grid.parallax_offset(world_bl);

            const bool uniform_parallax =
                grid_tile_renderer_detail::nearly_equal(offset_tl, offset_tr, grid_tile_renderer_detail::kParallaxEqualityEpsilon) &&
                grid_tile_renderer_detail::nearly_equal(offset_tl, offset_br, grid_tile_renderer_detail::kParallaxEqualityEpsilon) &&
                grid_tile_renderer_detail::nearly_equal(offset_tl, offset_bl, grid_tile_renderer_detail::kParallaxEqualityEpsilon);

            if (uniform_parallax) {
                SDL_FPoint screen_tl = cam.map_to_screen(world_tl);
                SDL_FPoint screen_br = cam.map_to_screen(world_br);

                const float width  = screen_br.x - screen_tl.x;
                const float height = screen_br.y - screen_tl.y;
                if (width <= 0.0f || height <= 0.0f) {
                    continue;
                }

                SDL_FRect dest{
                    grid.parallax_adjusted_screen_x(world_tl, screen_tl.x),
                    screen_tl.y,
                    width,
                    height
                };

                SDL_RenderCopyF(renderer, tile.texture, nullptr, &dest);
                continue;
            }

            SDL_FPoint screen_tl = cam.map_to_screen(world_tl);
            SDL_FPoint screen_tr = cam.map_to_screen(world_tr);
            SDL_FPoint screen_br = cam.map_to_screen(world_br);
            SDL_FPoint screen_bl = cam.map_to_screen(world_bl);

            screen_tl.x = grid.parallax_adjusted_screen_x(world_tl, screen_tl.x);
            screen_tr.x = grid.parallax_adjusted_screen_x(world_tr, screen_tr.x);
            screen_br.x = grid.parallax_adjusted_screen_x(world_br, screen_br.x);
            screen_bl.x = grid.parallax_adjusted_screen_x(world_bl, screen_bl.x);

            const float tex_w = static_cast<float>(tile.world_rect.w);
            const float tex_h = static_cast<float>(tile.world_rect.h);
            if (tex_w <= 0.0f || tex_h <= 0.0f) {
                continue;
            }
            const float padding_x = 0.5f / tex_w;
            const float padding_y = 0.5f / tex_h;

            const float tx0 = padding_x;
            const float ty0 = padding_y;
            const float tx1 = 1.0f - padding_x;
            const float ty1 = 1.0f - padding_y;

            SDL_Vertex vertices[4]{};
            vertices[0].position = SDL_FPoint{ screen_tl.x, screen_tl.y };
            vertices[1].position = SDL_FPoint{ screen_tr.x, screen_tr.y };
            vertices[2].position = SDL_FPoint{ screen_br.x, screen_br.y };
            vertices[3].position = SDL_FPoint{ screen_bl.x, screen_bl.y };
            vertices[0].color = vertices[1].color = vertices[2].color = vertices[3].color = white;
            vertices[0].tex_coord = SDL_FPoint{ tx0, ty0 };
            vertices[1].tex_coord = SDL_FPoint{ tx1, ty0 };
            vertices[2].tex_coord = SDL_FPoint{ tx1, ty1 };
            vertices[3].tex_coord = SDL_FPoint{ tx0, ty1 };

            SDL_RenderGeometry(renderer, tile.texture, vertices, 4, indices, 6);
        }
    }
}
