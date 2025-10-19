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

void LightMap::ensure_chunk_rebaked(SDL_Renderer* renderer, world::Chunk& chunk) const {
    if (!renderer) {
        return;
    }

    if (!chunk.lighting_dirty && chunk.static_light_map) {
        return;
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
    if (SDL_SetRenderTarget(renderer, texture) == 0) {
        const float brightness      = std::max(0.0f, chunk.base_brightness);
        const Uint8 brightness_byte = clamp_alpha(brightness);
        SDL_SetRenderDrawColor(renderer, brightness_byte, brightness_byte, brightness_byte, brightness_byte);
        SDL_RenderClear(renderer);
        SDL_SetRenderTarget(renderer, previous_target);
    } else {
        SDL_DestroyTexture(texture);
        texture = nullptr;
        SDL_SetRenderTarget(renderer, previous_target);
        return;
    }

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
    for (world::Chunk* chunk : chunks) {
        if (!chunk) {
            continue;
        }
        ensure_chunk_rebaked(renderer, *chunk);
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

