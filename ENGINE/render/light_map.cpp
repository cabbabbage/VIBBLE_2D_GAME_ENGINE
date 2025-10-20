#include "light_map.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "world/chunk.hpp"
#include "world/grid.hpp"

namespace {
Uint8 clamp_alpha(float value) {
    return static_cast<Uint8>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

SDL_Rect world_rect_from_screen(const camera& cam, const SDL_Rect& screen_rect) {
    SDL_Point top_left    = cam.screen_to_map({screen_rect.x, screen_rect.y});
    SDL_Point bottom_right = cam.screen_to_map({screen_rect.x + screen_rect.w, screen_rect.y + screen_rect.h});
    SDL_Rect result{};
    result.x = std::min(top_left.x, bottom_right.x);
    result.y = std::min(top_left.y, bottom_right.y);
    result.w = std::abs(bottom_right.x - top_left.x);
    result.h = std::abs(bottom_right.y - top_left.y);
    return result;
}

bool intersects(const SDL_Rect& a, const SDL_Rect& b) {
    return SDL_HasIntersection(&a, &b) == SDL_TRUE;
}

} // namespace

LightMap::LightMap(Assets* assets,
                   int screen_width,
                   int screen_height,
                   std::unique_ptr<PrecomputedLightMap> precomputed_map)
    : assets_(assets)
    , screen_width_(screen_width)
    , screen_height_(screen_height)
    , pending_precomputed_map_(std::move(precomputed_map)) {}

LightMap::~LightMap() = default;

void LightMap::apply_precomputed_light_map(SDL_Renderer* renderer) {
    if (!pending_precomputed_map_ || precomputed_applied_) {
        return;
    }

    // The legacy quadrant data does not map directly onto chunks yet. For the initial
    // implementation we simply acknowledge the precomputed map and mark existing chunk
    // textures dirty so they will be rebuilt on demand during the next update.
    if (assets_) {
        world::Grid& grid = assets_->world_grid();
        for (world::Chunk* chunk : grid.active_chunks()) {
            if (chunk) {
                chunk->lighting_dirty = true;
            }
        }
    }

    pending_precomputed_map_.reset();
    precomputed_applied_ = true;
}

void LightMap::destroy_chunk_texture(world::Chunk& chunk) const {
    if (chunk.static_light_map) {
        SDL_DestroyTexture(chunk.static_light_map);
        chunk.static_light_map = nullptr;
    }
}

bool LightMap::begin_full_world_mask(SDL_Renderer* renderer) const {
    if (!assets_ || !renderer) {
        return false;
    }
    if (batch_active_) {
        return batch_full_mask_ != nullptr;
    }

    // Compute union bounds in world space for all active chunks
    const auto& chunks = active_chunks();
    bool have_bounds = false;
    int min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    for (const world::Chunk* c : chunks) {
        if (!c) continue;
        const SDL_Rect& wb = c->world_bounds;
        if (!have_bounds) {
            min_x = wb.x;
            min_y = wb.y;
            max_x = wb.x + wb.w;
            max_y = wb.y + wb.h;
            have_bounds = true;
        } else {
            min_x = std::min(min_x, wb.x);
            min_y = std::min(min_y, wb.y);
            max_x = std::max(max_x, wb.x + wb.w);
            max_y = std::max(max_y, wb.y + wb.h);
        }
    }
    if (!have_bounds) {
        return false;
    }

    const int full_w = std::max(1, max_x - min_x);
    const int full_h = std::max(1, max_y - min_y);
    SDL_Texture* full = SDL_CreateTexture(renderer,
                                         SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET,
                                         full_w,
                                         full_h);
    if (!full) {
        return false;
    }
    SDL_SetTextureBlendMode(full, SDL_BLENDMODE_BLEND);

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, full) != 0) {
        SDL_DestroyTexture(full);
        SDL_SetRenderTarget(renderer, previous_target);
        return false;
    }

    // Start fully black and fully opaque: darkness mask in world units
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

#if SDL_VERSION_ATLEAST(2, 0, 6)
    const SDL_BlendMode erase_alpha_blend = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ZERO,                // srcRGB factor -> ignored (0)
        SDL_BLENDFACTOR_ONE,                 // dstRGB factor -> keep dest color
        SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO,                // srcA factor   -> 0
        SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, // dstA factor   -> dstA * (1 - srcA)
        SDL_BLENDOPERATION_ADD);
#else
    const SDL_BlendMode erase_alpha_blend = SDL_BLENDMODE_ADD;
#endif

    // Stamp static light sources in world-space
    const auto& static_lights = assets_->getActiveStaticLightAssets();
    for (const Asset* asset : static_lights) {
        if (!asset || !asset->info) continue;
        if (asset->info->light_sources.empty()) continue;

        for (const auto& light : asset->info->light_sources) {
            SDL_Texture* tex = light.texture;
            if (!tex) continue;

            int src_w = light.cached_w > 0 ? light.cached_w : 0;
            int src_h = light.cached_h > 0 ? light.cached_h : 0;
            if (src_w <= 0 || src_h <= 0) {
                SDL_QueryTexture(tex, nullptr, nullptr, &src_w, &src_h);
            }
            if (src_w <= 0 || src_h <= 0) continue;

            // The light texture already represents the authored radius. Avoid applying any
            // additional asset scaling so the light map matches the configured size.
            const int draw_w = std::max(1, src_w);
            const int draw_h = std::max(1, src_h);

            SDL_Point world_center{ asset->pos.x + light.offset_x, asset->pos.y + light.offset_y };
            SDL_Rect  world_dst{ world_center.x - draw_w / 2,
                                 world_center.y - draw_h / 2,
                                 draw_w,
                                 draw_h };

            // Convert to local coordinates of the full texture
            SDL_Rect local_dst = world_dst;
            local_dst.x -= min_x;
            local_dst.y -= min_y;

            // Save and apply blend state
            Uint8 save_r = 255, save_g = 255, save_b = 255, save_a = 255;
            SDL_BlendMode save_bm = SDL_BLENDMODE_BLEND;
            SDL_GetTextureColorMod(tex, &save_r, &save_g, &save_b);
            SDL_GetTextureAlphaMod(tex, &save_a);
            SDL_GetTextureBlendMode(tex, &save_bm);

            SDL_SetTextureBlendMode(tex, erase_alpha_blend);
            SDL_SetTextureColorMod(tex, 255, 255, 255);
            SDL_SetTextureAlphaMod(tex, 255);
            SDL_RenderCopy(renderer, tex, nullptr, &local_dst);

            SDL_SetTextureBlendMode(tex, save_bm);
            SDL_SetTextureColorMod(tex, save_r, save_g, save_b);
            SDL_SetTextureAlphaMod(tex, save_a);
        }
    }

    SDL_SetRenderTarget(renderer, previous_target);
    batch_full_mask_   = full;
    batch_full_bounds_ = SDL_Rect{ min_x, min_y, full_w, full_h };
    batch_active_      = true;
    return true;
}

void LightMap::end_full_world_mask(SDL_Renderer* renderer) const {
    (void)renderer;
    if (!batch_active_) return;
    if (batch_full_mask_) {
        SDL_DestroyTexture(batch_full_mask_);
        batch_full_mask_ = nullptr;
    }
    batch_full_bounds_ = SDL_Rect{0, 0, 0, 0};
    batch_active_ = false;
}

bool LightMap::rebuild_chunk_from_batch(SDL_Renderer* renderer, world::Chunk& chunk) const {
    if (!batch_active_ || !batch_full_mask_ || !renderer) {
        return false;
    }

    // Create/replace chunk texture
    destroy_chunk_texture(chunk);
    const int width  = std::max(1, chunk.world_bounds.w);
    const int height = std::max(1, chunk.world_bounds.h);
    SDL_Texture* texture = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_TARGET,
                                             width,
                                             height);
    if (!texture) {
        return false;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, texture) != 0) {
        SDL_DestroyTexture(texture);
        SDL_SetRenderTarget(renderer, previous_target);
        return false;
    }

    // Clear to fully opaque black
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    // Copy raw pixels from the full mask
    SDL_BlendMode full_prev_bm = SDL_BLENDMODE_BLEND;
    SDL_GetTextureBlendMode(batch_full_mask_, &full_prev_bm);
    SDL_SetTextureBlendMode(batch_full_mask_, SDL_BLENDMODE_NONE);

    SDL_Rect src{ chunk.world_bounds.x - batch_full_bounds_.x,
                  chunk.world_bounds.y - batch_full_bounds_.y,
                  chunk.world_bounds.w,
                  chunk.world_bounds.h };
    SDL_Rect dst{ 0, 0, width, height };
    SDL_RenderCopy(renderer, batch_full_mask_, &src, &dst);

    SDL_SetTextureBlendMode(batch_full_mask_, full_prev_bm);

    SDL_SetRenderTarget(renderer, previous_target);
    chunk.static_light_map = texture;
    chunk.lighting_dirty   = false;
    return true;
}

void LightMap::ensure_chunk_rebaked(SDL_Renderer* renderer, world::Chunk& chunk) const {
    if (!renderer) {
        return;
    }

    if (!chunk.lighting_dirty && chunk.static_light_map) {
        return;
    }

    // If batch mask is available, rebuild from it
    if (batch_active_ && batch_full_mask_) {
        if (rebuild_chunk_from_batch(renderer, chunk)) {
            return;
        }
        // If batch failed for any reason, fall back to per-chunk stamping below
    }

    destroy_chunk_texture(chunk);

    const int width  = std::max(1, chunk.world_bounds.w);
    const int height = std::max(1, chunk.world_bounds.h);

    SDL_Texture* texture = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_TARGET,
                                             width,
                                             height);
    if (!texture) {
        return;
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, texture) != 0) {
        SDL_DestroyTexture(texture);
        SDL_SetRenderTarget(renderer, previous_target);
        return;
    }

    // Start fully black and fully opaque: this is our darkness mask.
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

#if SDL_VERSION_ATLEAST(2, 0, 6)
    // Custom blend to "carve" transparency: keep destination color, reduce destination alpha by src alpha.
    const SDL_BlendMode erase_alpha_blend = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ZERO,                // srcRGB factor -> ignored (0)
        SDL_BLENDFACTOR_ONE,                 // dstRGB factor -> keep dest color
        SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO,                // srcA factor   -> 0
        SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, // dstA factor   -> dstA * (1 - srcA)
        SDL_BLENDOPERATION_ADD);
#else
    const SDL_BlendMode erase_alpha_blend = SDL_BLENDMODE_ADD; // Fallback (won't carve alpha properly)
#endif

    // Stamp all static light sources that intersect this chunk into the alpha channel.
    if (assets_) {
        const auto& static_lights = assets_->getActiveStaticLightAssets();
        for (const Asset* asset : static_lights) {
            if (!asset || !asset->info) {
                continue;
            }
            if (asset->info->light_sources.empty()) {
                continue;
            }

            for (const auto& light : asset->info->light_sources) {
                SDL_Texture* tex = light.texture;
                if (!tex) {
                    continue;
                }

                int src_w = light.cached_w > 0 ? light.cached_w : 0;
                int src_h = light.cached_h > 0 ? light.cached_h : 0;
                if (src_w <= 0 || src_h <= 0) {
                    SDL_QueryTexture(tex, nullptr, nullptr, &src_w, &src_h);
                }
                if (src_w <= 0 || src_h <= 0) {
                    continue;
                }

                // The baked light texture already matches the desired size. Do not reapply
                // asset scaling so the static light map uses the original radius.
                const int draw_w = std::max(1, src_w);
                const int draw_h = std::max(1, src_h);

                SDL_Point world_center{ asset->pos.x + light.offset_x, asset->pos.y + light.offset_y };
                SDL_Rect  world_dst{ world_center.x - draw_w / 2,
                                     world_center.y - draw_h / 2,
                                     draw_w,
                                     draw_h };

                SDL_Rect intersection{};
                if (!SDL_IntersectRect(&world_dst, &chunk.world_bounds, &intersection)) {
                    continue;
                }

                // Save texture state
                Uint8 save_r = 255, save_g = 255, save_b = 255, save_a = 255;
                SDL_BlendMode save_bm = SDL_BLENDMODE_BLEND;
                SDL_GetTextureColorMod(tex, &save_r, &save_g, &save_b);
                SDL_GetTextureAlphaMod(tex, &save_a);
                SDL_GetTextureBlendMode(tex, &save_bm);

                // Apply alpha-erasing blend and draw in chunk-local space
                SDL_SetTextureBlendMode(tex, erase_alpha_blend);
                SDL_SetTextureColorMod(tex, 255, 255, 255);
                SDL_SetTextureAlphaMod(tex, 255);

                SDL_Rect local_dst = world_dst;
                local_dst.x -= chunk.world_bounds.x;
                local_dst.y -= chunk.world_bounds.y;
                SDL_RenderCopy(renderer, tex, nullptr, &local_dst);

                // Restore texture state
                SDL_SetTextureBlendMode(tex, save_bm);
                SDL_SetTextureColorMod(tex, save_r, save_g, save_b);
                SDL_SetTextureAlphaMod(tex, save_a);
            }
        }
    }

    SDL_SetRenderTarget(renderer, previous_target);

    chunk.static_light_map = texture;
    chunk.lighting_dirty   = false;
}

void LightMap::rebuild(SDL_Renderer* renderer) {
    precomputed_applied_ = false;
    if (!assets_) {
        return;
    }

    world::Grid& grid = assets_->world_grid();
    for (world::Chunk* chunk : grid.active_chunks()) {
        if (chunk) {
            chunk->lighting_dirty = true;
        }
    }

    apply_precomputed_light_map(renderer);
}

void LightMap::update(SDL_Renderer* renderer, std::uint32_t /*delta_ms*/) {
    apply_precomputed_light_map(renderer);

    const auto& chunks = active_chunks();

    // Decide if we should build a single world-space mask this frame.
    bool any_dirty = false;
    for (const world::Chunk* c : chunks) {
        if (c && (c->lighting_dirty || !c->static_light_map)) {
            any_dirty = true;
            break;
        }
    }

    if (any_dirty) {
        begin_full_world_mask(renderer);
    }

    for (world::Chunk* chunk : chunks) {
        if (!chunk) {
            continue;
        }
        ensure_chunk_rebaked(renderer, *chunk);
    }

    if (any_dirty) {
        end_full_world_mask(renderer);
    }
}

float LightMap::sample_brightness(int world_x,
                                  int world_y,
                                  float static_weight,
                                  float dynamic_weight) const {
    (void)dynamic_weight;
    const world::Chunk* chunk = chunk_from_world(SDL_Point{world_x, world_y});
    if (!chunk) {
        return 1.0f;
    }
    const float weight = std::clamp(static_weight, 0.0f, 1.0f);
    return std::clamp(chunk->base_brightness * weight, 0.0f, 1.0f);
}

float LightMap::sample_brightness_bilinear(float world_x,
                                           float world_y,
                                           float static_weight,
                                           float dynamic_weight) const {
    return sample_brightness(static_cast<int>(std::lround(world_x)),
                             static_cast<int>(std::lround(world_y)),
                             static_weight,
                             dynamic_weight);
}

void LightMap::render_visible_quadrants(SDL_Renderer* renderer, const SDL_Rect& view_rect) const {
    render_visible_quadrants(renderer, view_rect, 1.0f);
}

void LightMap::render_visible_quadrants(SDL_Renderer* renderer,
                                        const SDL_Rect& view_rect,
                                        float alpha_multiplier) const {
    if (!renderer || !assets_) {
        return;
    }
    const camera& cam = assets_->getView();

    SDL_Rect world_view = world_rect_from_screen(cam, view_rect);
    const Uint8 alpha   = clamp_alpha(alpha_multiplier);

    for (world::Chunk* chunk : active_chunks()) {
        if (!chunk || !chunk->static_light_map) {
            continue;
        }
        if (!intersects(chunk->world_bounds, world_view)) {
            continue;
        }

        SDL_Point top_left     = cam.map_to_screen({chunk->world_bounds.x, chunk->world_bounds.y});
        SDL_Point bottom_right = cam.map_to_screen({chunk->world_bounds.x + chunk->world_bounds.w,
                                                    chunk->world_bounds.y + chunk->world_bounds.h});
        SDL_Rect dst{};
        dst.x = std::min(top_left.x, bottom_right.x);
        dst.y = std::min(top_left.y, bottom_right.y);
        dst.w = std::abs(bottom_right.x - top_left.x);
        dst.h = std::abs(bottom_right.y - top_left.y);
        if (dst.w <= 0 || dst.h <= 0) {
            continue;
        }
        SDL_SetTextureAlphaMod(chunk->static_light_map, alpha);
        SDL_RenderCopy(renderer, chunk->static_light_map, nullptr, &dst);
    }
}

void LightMap::render_visible_quadrants_debug(SDL_Renderer* renderer,
                                              const SDL_Rect& view_rect,
                                              float alpha_multiplier) const {
    render_visible_quadrants(renderer, view_rect, alpha_multiplier);
}

void LightMap::mark_region_dirty(const SDL_Rect& screen_rect) {
    if (!assets_) {
        return;
    }
    const camera& cam     = assets_->getView();
    SDL_Rect      world_r = world_rect_from_screen(cam, screen_rect);
    for (world::Chunk* chunk : active_chunks()) {
        if (chunk && intersects(chunk->world_bounds, world_r)) {
            chunk->lighting_dirty = true;
        }
    }
}

void LightMap::mark_asset_lights_dirty(const Asset* asset) {
    if (!asset) {
        return;
    }
    if (world::Chunk* chunk = chunk_from_world(asset->pos)) {
        chunk->lighting_dirty = true;
    }
}

void LightMap::mark_static_cache_dirty() {
    if (!assets_) {
        return;
    }
    for (world::Chunk* chunk : active_chunks()) {
        if (chunk) {
            chunk->lighting_dirty = true;
        }
    }
}

const std::vector<world::Chunk*>& LightMap::active_chunks() const {
    static const std::vector<world::Chunk*> kEmpty{};
    if (!assets_) {
        return kEmpty;
    }
    return assets_->world_grid().active_chunks();
}

world::Chunk* LightMap::chunk_from_world(SDL_Point world_px) const {
    if (!assets_) {
        return nullptr;
    }
    return assets_->world_grid().chunk_from_world(world_px);
}

int LightMap::quadrant_count() const {
    return static_cast<int>(active_chunks().size());
}

int LightMap::quadrant_columns() const {
    const int count = quadrant_count();
    if (count <= 0) {
        return 0;
    }
    const int columns = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
    return std::max(1, columns);
}

int LightMap::quadrant_rows() const {
    const int count = quadrant_count();
    if (count <= 0) {
        return 0;
    }
    const int columns = quadrant_columns();
    return std::max(1, (count + columns - 1) / columns);
}

const world::Chunk* LightMap::quadrant(int index) const {
    const auto& chunks = active_chunks();
    if (index < 0 || static_cast<std::size_t>(index) >= chunks.size()) {
        return nullptr;
    }
    return chunks[static_cast<std::size_t>(index)];
}

SDL_Rect LightMap::quadrant_bounds(int index) const {
    if (const world::Chunk* chunk = quadrant(index)) {
        return chunk->world_bounds;
    }
    return SDL_Rect{0, 0, 0, 0};
}

void LightMap::set_virtual_light_map_quadrants(int /*quadrants*/) {}

void LightMap::set_virtual_light_map_quadrant_size(int /*size_px*/) {}

void LightMap::set_cells_per_quadrant(int /*cells*/) {}

int LightMap::virtual_light_map_quadrant_size() const { return 0; }

int LightMap::virtual_light_map_quadrants() const { return static_cast<int>(active_chunks().size()); }

int LightMap::static_grid_resolution() const { return 0; }

int LightMap::padding_cells() const { return 0; }

