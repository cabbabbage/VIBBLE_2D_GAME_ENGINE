#include "world/chunk.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "render/warped_screen_grid.hpp"
#include "render/render.hpp"
#include "world/world_grid.hpp"

namespace chunk_detail {

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float blend_light_components(float static_strength, float dynamic_strength, float static_weight, float dynamic_weight) {
    const float sw = std::max(0.0f, static_weight);
    const float dw = std::max(0.0f, dynamic_weight);
    const float total = sw + dw;
    if (total <= 1e-6f) {
        return clamp01(static_strength);
    }
    const float blended = (clamp01(static_strength) * sw + clamp01(dynamic_strength) * dw) / total;
    return clamp01(blended);
}

}

namespace world {

Chunk::Chunk(int in_i, int in_j, int r, SDL_Rect bounds)
    : i(in_i)
    , j(in_j)
    , r_chunk(r)
    , world_bounds(bounds) {}

Chunk::~Chunk() {
    releaseLightingArtifacts();
    releaseTileTextures();
}

void Chunk::releaseLightingArtifacts() {
    lighting = LightingState{};
    lighting.static_strength          = 1.0f;
    lighting.dynamic_strength         = 1.0f;
    lighting.current_strength         = 1.0f;
    lighting.runtime_average_strength = 1.0f;
}

void Chunk::releaseTileTextures() {
    for (auto& t : tiles) {
        if (t.texture) {
            SDL_DestroyTexture(t.texture);
            t.texture = nullptr;
        }
    }
    tiles.clear();
}

}

LightMap::LightMap(Assets* assets, int screen_width, int screen_height)
    : assets_(assets)
    , screen_width_(screen_width)
    , screen_height_(screen_height) {}

LightMap::~LightMap() = default;

void LightMap::rebuild(SDL_Renderer*) {
    std::scoped_lock lock(mutex_);
    if (!assets_) {
        return;
    }
    world::WorldGrid& grid = assets_->world_grid();
    for (const auto& chunk : grid.chunks().storage()) {
        if (chunk) {
            chunk->releaseLightingArtifacts();
        }
    }
}

void LightMap::update(SDL_Renderer*, std::uint32_t) {
    std::scoped_lock lock(mutex_);
    if (!assets_) {
        return;
    }

    world::WorldGrid& grid = assets_->world_grid();

    const auto weights = resolve_sampling_weights(0.0f, 1.0f);

    const float map_alpha = 1.0f;

    for (world::Chunk* chunk : grid.active_chunks()) {
        if (!chunk) {
            continue;
        }
        auto& lighting = chunk->lighting;
        if (lighting.has_runtime_average) {
            lighting.dynamic_strength         = chunk_detail::clamp01(lighting.runtime_average_strength);
            lighting.runtime_average_strength = lighting.dynamic_strength;
            lighting.has_runtime_average      = false;
        }
        lighting.static_strength  = chunk_detail::clamp01(lighting.static_strength);
        lighting.dynamic_strength = chunk_detail::clamp01(lighting.dynamic_strength) * map_alpha;
        lighting.current_strength = chunk_detail::blend_light_components(lighting.static_strength, lighting.dynamic_strength, weights.first, weights.second);
        lighting.needs_update = false;
    }
}

LightMap::SampledBrightness LightMap::sample_lighting(int world_x,
                                                      int world_y,
                                                      float static_weight,
                                                      float dynamic_weight) const {
    std::scoped_lock lock(mutex_);
    SampledBrightness result{};
    const auto weights = resolve_sampling_weights(static_weight, dynamic_weight);

    world::Chunk* chunk = ensure_chunk_from_world(SDL_Point{world_x, world_y});
    if (!chunk) {
        result.blended = chunk_detail::blend_light_components(result.static_component, result.dynamic_component, weights.first, weights.second);
        return result;
    }

    const auto& lighting = chunk->lighting;
    result.static_component  = chunk_detail::clamp01(lighting.static_strength);
    result.dynamic_component = chunk_detail::clamp01(lighting.dynamic_strength);
    result.has_color         = lighting.runtime_average_color.a > 0 && lighting.dynamic_strength < 1.0f;
    result.color             = lighting.runtime_average_color;
    result.blended           = chunk_detail::blend_light_components(result.static_component, result.dynamic_component, weights.first, weights.second);
    return result;
}

LightMap::SampledBrightness LightMap::sample_lighting_bilinear(float world_x,
                                                               float world_y,
                                                               float static_weight,
                                                               float dynamic_weight) const {
    const int x0 = static_cast<int>(std::floor(world_x));
    const int y0 = static_cast<int>(std::floor(world_y));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;

    const float tx = world_x - static_cast<float>(x0);
    const float ty = world_y - static_cast<float>(y0);

    const SampledBrightness s00 = sample_lighting(x0, y0, static_weight, dynamic_weight);
    const SampledBrightness s10 = sample_lighting(x1, y0, static_weight, dynamic_weight);
    const SampledBrightness s01 = sample_lighting(x0, y1, static_weight, dynamic_weight);
    const SampledBrightness s11 = sample_lighting(x1, y1, static_weight, dynamic_weight);

    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

    SampledBrightness blended{};
    blended.static_component = lerp(lerp(s00.static_component, s10.static_component, tx), lerp(s01.static_component, s11.static_component, tx), ty);
    blended.dynamic_component = lerp(lerp(s00.dynamic_component, s10.dynamic_component, tx), lerp(s01.dynamic_component, s11.dynamic_component, tx), ty);
    blended.blended = lerp(lerp(s00.blended, s10.blended, tx), lerp(s01.blended, s11.blended, tx), ty);
    blended.has_color = s00.has_color || s10.has_color || s01.has_color || s11.has_color;
    if (blended.has_color) {
        blended.color.r = static_cast<Uint8>(std::clamp(lerp(lerp(static_cast<float>(s00.color.r), static_cast<float>(s10.color.r), tx), lerp(static_cast<float>(s01.color.r), static_cast<float>(s11.color.r), tx), ty), 0.0f, 255.0f));
        blended.color.g = static_cast<Uint8>(std::clamp(lerp(lerp(static_cast<float>(s00.color.g), static_cast<float>(s10.color.g), tx), lerp(static_cast<float>(s01.color.g), static_cast<float>(s11.color.g), tx), ty), 0.0f, 255.0f));
        blended.color.b = static_cast<Uint8>(std::clamp(lerp(lerp(static_cast<float>(s00.color.b), static_cast<float>(s10.color.b), tx), lerp(static_cast<float>(s01.color.b), static_cast<float>(s11.color.b), tx), ty), 0.0f, 255.0f));
        blended.color.a = 255;
    }
    return blended;
}

float LightMap::sample_brightness(int world_x,
                                  int world_y,
                                  float static_weight,
                                  float dynamic_weight) const {
    return sample_lighting(world_x, world_y, static_weight, dynamic_weight).blended;
}

float LightMap::sample_brightness_bilinear(float world_x,
                                           float world_y,
                                           float static_weight,
                                           float dynamic_weight) const {
    return sample_lighting_bilinear(world_x, world_y, static_weight, dynamic_weight).blended;
}

void LightMap::render_visible_chunks(SDL_Renderer* renderer, const SDL_Rect& view_rect) const {
    static constexpr SDL_Color kDefaultColor{0, 0, 0, 255};
    render_visible_chunks(renderer, view_rect, 1.0f, kDefaultColor);
}

void LightMap::render_visible_chunks(SDL_Renderer* renderer,
                                     const SDL_Rect& view_rect,
                                     float           alpha_multiplier,
                                     const SDL_Color& color_mod) const {
    std::scoped_lock lock(mutex_);
    if (!renderer || !assets_) {
        return;
    }
    if (view_rect.w <= 0 || view_rect.h <= 0) {
        return;
    }

    const WarpedScreenGrid& cam = assets_->getView();
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    for (world::Chunk* chunk : active_chunks()) {
        if (!chunk) {
            continue;
        }

        SDL_FPoint top_left = cam.map_to_screen({chunk->world_bounds.x, chunk->world_bounds.y});
        SDL_FPoint bottom_right = cam.map_to_screen({chunk->world_bounds.x + chunk->world_bounds.w,
                                                     chunk->world_bounds.y + chunk->world_bounds.h});

        SDL_Rect dest{};
        dest.x = static_cast<int>(std::lround(std::min(top_left.x, bottom_right.x)));
        dest.y = static_cast<int>(std::lround(std::min(top_left.y, bottom_right.y)));
        dest.w = static_cast<int>(std::lround(std::abs(bottom_right.x - top_left.x)));
        dest.h = static_cast<int>(std::lround(std::abs(bottom_right.y - top_left.y)));

        if (dest.w <= 0 || dest.h <= 0) {
            continue;
        }
        if (SDL_HasIntersection(&dest, &view_rect) != SDL_TRUE) {
            continue;
        }

        const float brightness = chunk_detail::clamp01(chunk->lighting.current_strength);
        const float alpha = chunk_detail::clamp01(1.0f - brightness) * chunk_detail::clamp01(alpha_multiplier);
        const Uint8 shade = static_cast<Uint8>(std::lround(alpha * 255.0f));

        SDL_Color mod = color_mod;
        const auto apply_mod = [](Uint8 base, Uint8 tint) {
            const float base_f = static_cast<float>(base) / 255.0f;
            const float tint_f = static_cast<float>(tint) / 255.0f;
            return static_cast<Uint8>(std::lround(std::clamp(base_f * tint_f, 0.0f, 1.0f) * 255.0f));
};

        SDL_SetRenderDrawColor(renderer, apply_mod(255, mod.r), apply_mod(255, mod.g), apply_mod(255, mod.b), shade);
        SDL_RenderFillRect(renderer, &dest);
    }

    rendered_in_current_tick_ = true;
    last_render_tick_ = SDL_GetTicks();
}

void LightMap::subtract_runtime_shadow_from_texture(SDL_Renderer* renderer,
                                                    SDL_Texture*  target_texture,
                                                    const SDL_Rect& target_rect,
                                                    const SDL_Rect& screen_rect,
                                                    float           alpha_multiplier) const {
    if (!renderer || !target_texture) {
        return;
    }

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, target_texture);
    render_visible_chunks(renderer, screen_rect, alpha_multiplier, SDL_Color{255, 255, 255, 255});
    SDL_SetRenderTarget(renderer, previous_target);
}

void LightMap::render_chunk_preview(SDL_Renderer* renderer, const SDL_Rect& view_rect) const {
    render_visible_chunks(renderer, view_rect);
}

void LightMap::present_static_previews(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }
    SDL_Rect screen_rect{0, 0, screen_width_, screen_height_};
    render_visible_chunks(renderer, screen_rect);
    SDL_RenderPresent(renderer);
}

void LightMap::mark_region_dirty(const SDL_Rect&) {}

void LightMap::mark_asset_lights_dirty(const Asset*) {}

void LightMap::mark_static_cache_dirty() {}

const std::vector<world::Chunk*>& LightMap::active_chunks() const {
    static const std::vector<world::Chunk*> kEmpty;
    if (!assets_) {
        return kEmpty;
    }
    return assets_->world_grid().active_chunks();
}

world::Chunk* LightMap::ensure_chunk_from_world(SDL_Point world_px) const {
    if (!assets_) {
        return nullptr;
    }
    return assets_->world_grid().ensure_chunk_from_world(world_px);
}

world::Chunk* LightMap::chunk_from_world(SDL_Point world_px) const {
    if (!assets_) {
        return nullptr;
    }
    return assets_->world_grid().chunk_from_world(world_px);
}

int LightMap::chunk_count() const {
    if (!assets_) {
        return 0;
    }
    return static_cast<int>(assets_->world_grid().chunks().storage().size());
}

int LightMap::chunk_columns() const {
    if (!assets_) {
        return 0;
    }
    const auto& chunks = assets_->world_grid().chunks().storage();
    if (chunks.empty()) {
        return 0;
    }
    int min_i = chunks.front()->i;
    int max_i = chunks.front()->i;
    for (const auto& chunk : chunks) {
        if (!chunk) {
            continue;
        }
        min_i = std::min(min_i, chunk->i);
        max_i = std::max(max_i, chunk->i);
    }
    return (max_i - min_i) + 1;
}

int LightMap::chunk_rows() const {
    if (!assets_) {
        return 0;
    }
    const auto& chunks = assets_->world_grid().chunks().storage();
    if (chunks.empty()) {
        return 0;
    }
    int min_j = chunks.front()->j;
    int max_j = chunks.front()->j;
    for (const auto& chunk : chunks) {
        if (!chunk) {
            continue;
        }
        min_j = std::min(min_j, chunk->j);
        max_j = std::max(max_j, chunk->j);
    }
    return (max_j - min_j) + 1;
}

const world::Chunk* LightMap::chunk_at(int index) const {
    if (!assets_ || index < 0) {
        return nullptr;
    }
    const auto& chunks = assets_->world_grid().chunks().storage();
    if (index >= static_cast<int>(chunks.size())) {
        return nullptr;
    }
    return chunks[static_cast<std::size_t>(index)].get();
}

SDL_Rect LightMap::chunk_bounds(int index) const {
    if (const world::Chunk* chunk = chunk_at(index)) {
        return chunk->world_bounds;
    }
    return SDL_Rect{0, 0, 0, 0};
}

std::pair<float, float> LightMap::resolve_sampling_weights(float static_weight, float dynamic_weight) const {
    const float base_static  = 0.0f;
    const float base_dynamic = 1.0f;

    float effective_static  = static_weight;
    float effective_dynamic = dynamic_weight;

    if (std::abs(static_weight - base_static) <= 1e-4f) {
        effective_static = base_static;
    }
    if (std::abs(dynamic_weight - base_dynamic) <= 1e-4f) {
        effective_dynamic = base_dynamic;
    }

    return {std::max(0.0f, effective_static), std::max(0.0f, effective_dynamic)};
}

