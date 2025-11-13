#include "grid_tile_renderer.hpp"

#include <algorithm>
#include <cmath>

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/depth_cue_settings.hpp"
#include "render/camera.hpp"
#include "render/depth_cue_utils.hpp"
#include "tiling/grid_tile.hpp"
#include "world/chunk.hpp"
#include "world/grid.hpp"

namespace {
using depth_cue::DepthSample;
using depth_cue::DepthSide;
using depth_cue::evaluate_depth_curve;
using depth_cue::kDepthCueDeadzonePx;
using depth_cue::nearly_equal;
using depth_cue::sample_signed_effect;

constexpr float kParallaxEqualityEpsilon = 0.001f;

}  // namespace

void GridTileRenderer::render(SDL_Renderer* renderer) {
    if (!renderer || !assets_) return;
    render(renderer, assets_->getView(), assets_->world_grid());
}

void GridTileRenderer::render(SDL_Renderer* renderer, const camera& cam, const world::Grid& grid) {
    if (!renderer) return;

    const auto& chunks = grid.active_chunks();
    if (chunks.empty()) return;

    const camera::RealismSettings& settings = cam.realism_settings();
    const bool depthcue_enabled = devmode::camera_prefs::load_depthcue_enabled();
    float center_y = static_cast<float>(cam.get_screen_center().y);
    SDL_FPoint center_world_f = cam.get_view_center_f();
    SDL_FPoint center_screen_f = cam.map_to_screen_f(center_world_f);
    if (std::isfinite(center_screen_f.y)) {
        center_y = center_screen_f.y;
    }
    const float fg_plane = std::clamp(settings.blur_foreground_screen_y, 0.0f, 1e6f);
    const float bg_plane = std::clamp(settings.blur_background_screen_y, 0.0f, 1e6f);
    const float fg_span  = std::max(0.0f, fg_plane - center_y);
    const float bg_span  = std::max(0.0f, center_y - bg_plane);
    auto depth_sample_for = [&](float screen_y) -> DepthSample {
        DepthSample sample;
        if (!std::isfinite(screen_y)) {
            return sample;
        }
        if (std::fabs(screen_y - center_y) <= kDepthCueDeadzonePx) {
            return sample;
        }
        if (screen_y > center_y) {
            sample.side = DepthSide::Foreground;
            sample.t = (screen_y >= fg_plane || fg_span <= 0.0f)
                ? 1.0f
                : std::clamp((screen_y - center_y) / fg_span, 0.0f, 1.0f);
        } else if (screen_y < center_y) {
            sample.side = DepthSide::Background;
            sample.t = (screen_y <= bg_plane || bg_span <= 0.0f)
                ? 1.0f
                : std::clamp((center_y - screen_y) / bg_span, 0.0f, 1.0f);
        }
        return sample;
    };
    auto brightness_value_for = [&](float screen_y) -> float {
        if (!depthcue_enabled) {
            return 0.0f;
        }
        const DepthSample sample = depth_sample_for(screen_y);
        return sample_signed_effect(
            sample,
            settings.foreground_brightness,
            settings.background_brightness,
            settings.brightness_falloff_method);
    };

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
                nearly_equal(offset_tl, offset_tr, kParallaxEqualityEpsilon) &&
                nearly_equal(offset_tl, offset_br, kParallaxEqualityEpsilon) &&
                nearly_equal(offset_tl, offset_bl, kParallaxEqualityEpsilon);

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

                const float mid_y    = dest.y + dest.h * 0.5f;
                const float brightness_pct = std::clamp(brightness_value_for(mid_y), -50.0f, 50.0f);
                const float frac = std::clamp(brightness_pct / 100.0f, -0.5f, 0.5f);
                const bool darken_active = frac < 0.0f;
                const bool lighten_active = frac > 0.0f;
                const Uint8 darken_mod = static_cast<Uint8>(std::clamp(std::lround(255.0f * (1.0f + std::min(0.0f, frac))), 0L, 255L));
                const Uint8 lighten_alpha = static_cast<Uint8>(std::clamp(std::lround(255.0f * std::max(0.0f, frac)), 0L, 255L));

                if (darken_active) {
                    SDL_SetTextureColorMod(tile.texture, darken_mod, darken_mod, darken_mod);
                }
                SDL_RenderCopyF(renderer, tile.texture, nullptr, &dest);
                // Optional brighten overlay
                if (lighten_active && lighten_alpha > 0) {
                    SDL_SetTextureBlendMode(tile.texture, SDL_BLENDMODE_ADD);
                    SDL_SetTextureAlphaMod(tile.texture, lighten_alpha);
                    SDL_SetTextureColorMod(tile.texture, 255, 255, 255);
                    SDL_RenderCopyF(renderer, tile.texture, nullptr, &dest);
                    SDL_SetTextureAlphaMod(tile.texture, 255);
                    SDL_SetTextureBlendMode(tile.texture, SDL_BLENDMODE_BLEND);
                }
                SDL_SetTextureColorMod(tile.texture, 255, 255, 255);
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

            // Apply depth cue brightness via texture color mod (darken) or additive overlay (lighten)
            const float mid_y2 = (screen_tl.y + screen_br.y) * 0.5f;
            const float brightness_pct2 = std::clamp(brightness_value_for(mid_y2), -50.0f, 50.0f);
            const float frac2 = std::clamp(brightness_pct2 / 100.0f, -0.5f, 0.5f);
            const bool darken_active2 = frac2 < 0.0f;
            const bool lighten_active2 = frac2 > 0.0f;
            const Uint8 darken_mod2 = static_cast<Uint8>(std::clamp(std::lround(255.0f * (1.0f + std::min(0.0f, frac2))), 0L, 255L));
            const Uint8 lighten_alpha2 = static_cast<Uint8>(std::clamp(std::lround(255.0f * std::max(0.0f, frac2)), 0L, 255L));

            if (darken_active2) {
                SDL_SetTextureColorMod(tile.texture, darken_mod2, darken_mod2, darken_mod2);
            }
            SDL_RenderGeometry(renderer, tile.texture, vertices, 4, indices, 6);
            if (lighten_active2 && lighten_alpha2 > 0) {
                SDL_SetTextureBlendMode(tile.texture, SDL_BLENDMODE_ADD);
                // reuse vertices; only alpha differs
                SDL_Vertex verts2[4] = { vertices[0], vertices[1], vertices[2], vertices[3] };
                for (int vi = 0; vi < 4; ++vi) { verts2[vi].color.a = lighten_alpha2; }
                SDL_RenderGeometry(renderer, tile.texture, verts2, 4, indices, 6);
                SDL_SetTextureBlendMode(tile.texture, SDL_BLENDMODE_BLEND);
            }
            SDL_SetTextureColorMod(tile.texture, 255, 255, 255);
        }
    }
}
