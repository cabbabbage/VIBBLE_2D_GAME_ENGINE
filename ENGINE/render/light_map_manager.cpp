#include "light_map_manager.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <cstdint>
#include <climits>

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "render/global_light_source.hpp"
#include "render/light_map.hpp"
#include "world/chunk.hpp"
#include "world/grid.hpp"

LightMapManager::LightMapManager(Assets* assets) : assets_(assets) {}

namespace {
using render_pipeline::shading::ReactiveShadowSettings;

// Simple linear interpolation helper
template <typename T>
T lerp(T a, T b, float t) {
    return static_cast<T>(a + (b - a) * t);
}

// Local helper: compute average transparency of a chunk mask.
static float average_transparency(SDL_Renderer* renderer, SDL_Texture* texture) {
    if (!renderer || !texture) return 0.0f;
    int tex_w = 0, tex_h = 0;
    if (SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h) != 0 || tex_w <= 0 || tex_h <= 0) {
        return 0.0f;
    }
    const std::size_t count = static_cast<std::size_t>(tex_w) * static_cast<std::size_t>(tex_h);
    std::vector<std::uint32_t> pixels(count);
    if (pixels.empty()) return 0.0f;
    SDL_Texture* prev = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, texture);
    const int pitch = tex_w * static_cast<int>(sizeof(std::uint32_t));
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA8888, pixels.data(), pitch) != 0) {
        SDL_SetRenderTarget(renderer, prev);
        return 0.0f;
    }
    SDL_SetRenderTarget(renderer, prev);
    std::unique_ptr<SDL_PixelFormat, decltype(&SDL_FreeFormat)> fmt(SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888), &SDL_FreeFormat);
    if (!fmt) return 0.0f;
    double accum = 0.0;
    for (std::uint32_t p : pixels) {
        Uint8 a = 255;
        SDL_GetRGBA(p, fmt.get(), nullptr, nullptr, nullptr, &a);
        accum += 1.0 - static_cast<double>(a) / 255.0;
    }
    if (count == 0) return 0.0f;
    const double avg = accum / static_cast<double>(count);
    return static_cast<float>(std::clamp(avg, 0.0, 1.0));
}

// Dummy calculators for UseShadowData. To be implemented precisely later.
static float compute_shadow_opacity(float current_strength) {
    return std::clamp(current_strength, 0.0f, 1.0f);
}
static float compute_shadow_scale(float /*current_strength*/) {
    return 1.0f;
}
static std::pair<float, float> compute_shadow_offsets_percent(float /*current_strength*/) {
    return {0.0f, 0.0f};
}
static float compute_parallax_intensity_percent(float /*current_strength*/) {
    return 0.0f;
}

} // namespace

void LightMapManager::begin_frame() {
    if (!assets_) return;
    const LightMap* map = light_map();
    if (!map) return;

    const auto& chunks = map->active_chunks();
    if (chunks.empty()) return;

    // Normalize screen-light opacity to [0,1].
    float screen_light_opacity = 0.0f;
    if (const Global_Light_Source* gl = assets_->map_light_source()) {
        const int min_a = gl->min_opacity();
        const int max_a = gl->max_opacity();
        const int cur_a = std::clamp(static_cast<int>(gl->get_current_color().a), min_a, max_a);
        const int range = std::max(1, max_a - min_a);
        screen_light_opacity = std::clamp(static_cast<float>(cur_a - min_a) / static_cast<float>(range), 0.0f, 1.0f);
    }

    const bool screen_changed = (std::abs(screen_light_opacity - last_screen_light_opacity_) > 1e-4f);
    last_screen_light_opacity_ = screen_light_opacity;

    // Determine edge bounds in ij-space for active chunks.
    int min_i = INT32_MAX, max_i = INT32_MIN, min_j = INT32_MAX, max_j = INT32_MIN;
    for (const world::Chunk* c : chunks) {
        if (!c) continue;
        min_i = std::min(min_i, c->i); max_i = std::max(max_i, c->i);
        min_j = std::min(min_j, c->j); max_j = std::max(max_j, c->j);
    }

    // Build the update set: all active chunks + immediate neighbors of edge chunks.
    std::vector<world::Chunk*> update_set;
    update_set.reserve(chunks.size());
    const world::Grid& grid = assets_->world_grid();
    auto add_unique = [&](world::Chunk* c){ if (c && std::find(update_set.begin(), update_set.end(), c) == update_set.end()) update_set.push_back(c); };
    for (world::Chunk* c : chunks) { add_unique(c); }

    for (world::Chunk* c : chunks) {
        if (!c) continue;
        const bool is_edge = (c->i == min_i) || (c->i == max_i) || (c->j == min_j) || (c->j == max_j);
        if (!is_edge) continue;
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                if (di == 0 && dj == 0) continue;
                if (world::Chunk* n = grid.find_chunk_ij(c->i + di, c->j + dj)) {
                    add_unique(n);
                }
            }
        }
    }

    SDL_Renderer* renderer = assets_->renderer();
    const auto& moving = assets_->getActiveMovingLightAssets();

    for (world::Chunk* chunk : update_set) {
        if (!chunk) continue;
        chunk->light.is_active = true;
        if (screen_changed) {
            chunk->light.needs_update = true;
        }
        if (!chunk->light.needs_update) {
            // Another neighbor in this pass may have already updated it.
            continue;
        }

        // Determine if a moving light currently occupies this chunk (simple AABB test).
        bool occupied = false;
        for (Asset* a : moving) {
            if (!a) continue;
            const SDL_Point p{a->pos.x, a->pos.y};
            if (SDL_PointInRect(&p, &chunk->world_bounds)) { occupied = true; break; }
        }
        const bool was_occupied = chunk->light.is_occupied_by_moving_source;
        if (occupied != was_occupied) {
            chunk->light.needs_update = true;
        }
        chunk->light.is_occupied_by_moving_source = occupied;

        if (chunk->light.is_occupied_by_moving_source) {
            // Recompute brightness (background + static lights; no shadows). For now,
            // approximate using the static mask average transparency.
            if (chunk->static_light_map) {
                chunk->light.current_strength = average_transparency(renderer, chunk->static_light_map);
            } else {
                chunk->light.current_strength = chunk->base_brightness;
            }
        } else {
            chunk->light.current_strength = lerp(chunk->light.min_static_avg_strength,
                                                 chunk->light.max_static_avg_strength,
                                                 screen_light_opacity);
        }

        // Populate UseShadowData via dummy calculators.
        chunk->shadow.opacity  = compute_shadow_opacity(chunk->light.current_strength);
        chunk->shadow.scale    = compute_shadow_scale(chunk->light.current_strength);
        auto [oxp, oyp]        = compute_shadow_offsets_percent(chunk->light.current_strength);
        chunk->shadow.offset_x_percent = oxp;
        chunk->shadow.offset_y_percent = oyp;
        chunk->shadow.parallax_intensity_percent = compute_parallax_intensity_percent(chunk->light.current_strength);

        chunk->light.needs_update = false;
    }
}

const LightMap* LightMapManager::light_map() const {
    return assets_ ? assets_->light_map() : nullptr;
}

std::vector<LightMapManager::QuadrantSnapshot> LightMapManager::all_snapshots() const {
    std::vector<QuadrantSnapshot> snapshots;
    const LightMap* map = light_map();
    if (!map) {
        return snapshots;
    }

    const auto& chunks = map->active_chunks();
    snapshots.reserve(chunks.size());
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const world::Chunk* chunk = chunks[i];
        if (!chunk) {
            continue;
        }
        QuadrantSnapshot snap;
        snap.index               = static_cast<int>(i);
        snap.world_rect          = chunk->world_bounds;
        snap.active              = true;
        snap.dirty               = chunk->lighting_dirty;
        snap.base_brightness     = chunk->base_brightness;
        snap.combined_brightness = chunk->light.current_strength;
        snap.static_min          = chunk->light.min_static_avg_strength;
        snap.static_max          = chunk->light.max_static_avg_strength;
        snap.static_average      = chunk->light.current_strength;
        snap.static_empty        = false;
        snap.shadow_opacity_min  = chunk->shadow.opacity;
        snap.shadow_opacity_max  = chunk->shadow.opacity;
        snap.brightness_strength = chunk->brightness_strength;
        snap.opacity_strength    = chunk->opacity_strength;
        snap.scale_strength      = chunk->scale_strength;
        snap.offset_x            = chunk->offset_x;
        snap.offset_y            = chunk->offset_y;
        snap.shadow              = chunk->shadow;
        snapshots.push_back(snap);
    }
    return snapshots;
}

std::vector<std::string> LightMapManager::assets_sampling_quadrant(int /*index*/) const {
    return {};
}

std::optional<LightMapManager::QuadrantSnapshot> LightMapManager::snapshot_for_quadrant(int index) const {
    const LightMap* map = light_map();
    if (!map) {
        return std::nullopt;
    }
    const auto& chunks = map->active_chunks();
    if (index < 0 || static_cast<std::size_t>(index) >= chunks.size()) {
        return std::nullopt;
    }
    const world::Chunk* chunk = chunks[static_cast<std::size_t>(index)];
    if (!chunk) {
        return std::nullopt;
    }
    QuadrantSnapshot snap;
    snap.index               = index;
    snap.world_rect          = chunk->world_bounds;
    snap.active              = true;
    snap.dirty               = chunk->lighting_dirty;
    snap.base_brightness     = chunk->base_brightness;
    snap.combined_brightness = chunk->light.current_strength;
    snap.static_min          = chunk->light.min_static_avg_strength;
    snap.static_max          = chunk->light.max_static_avg_strength;
    snap.static_average      = chunk->light.current_strength;
    snap.static_empty        = false;
    snap.shadow_opacity_min  = chunk->shadow.opacity;
    snap.shadow_opacity_max  = chunk->shadow.opacity;
    snap.brightness_strength = chunk->brightness_strength;
    snap.opacity_strength    = chunk->opacity_strength;
    snap.scale_strength      = chunk->scale_strength;
    snap.offset_x            = chunk->offset_x;
    snap.offset_y            = chunk->offset_y;
    snap.shadow              = chunk->shadow;
    return snap;
}

std::optional<LightMapManager::UseShadowData> LightMapManager::shadow_data_for_chunk(const world::Chunk* chunk) const {
    if (!chunk) {
        return std::nullopt;
    }
    return chunk->shadow;
}

std::optional<LightMapManager::UseShadowData> LightMapManager::get_shadow_data_for_index(int index) const {
    const LightMap* map = light_map();
    if (!map) {
        return std::nullopt;
    }
    const auto& chunks = map->active_chunks();
    if (index < 0 || static_cast<std::size_t>(index) >= chunks.size()) {
        return std::nullopt;
    }
    return shadow_data_for_chunk(chunks[static_cast<std::size_t>(index)]);
}

std::optional<int> LightMapManager::find_quadrant_index(SDL_FPoint world_or_screen_pos) const {
    const LightMap* map = light_map();
    if (!map) {
        return std::nullopt;
    }

    SDL_Point world_point{static_cast<int>(std::lround(world_or_screen_pos.x)),
                          static_cast<int>(std::lround(world_or_screen_pos.y))};

    world::Chunk* chunk = map->chunk_from_world(world_point);
    if (!chunk && assets_) {
        const camera& cam = assets_->getView();
        SDL_Point from_screen = cam.screen_to_map({world_point.x, world_point.y});
        chunk = map->chunk_from_world(from_screen);
    }
    if (!chunk) {
        return std::nullopt;
    }

    const auto& chunks = map->active_chunks();
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        if (chunks[i] == chunk) {
            return static_cast<int>(i);
        }
    }
    return std::nullopt;
}

std::optional<LightMapManager::UseShadowData> LightMapManager::get_shadow_data(SDL_FPoint world_or_screen_pos) const {
    if (auto index = find_quadrant_index(world_or_screen_pos)) {
        return get_shadow_data_for_index(*index);
    }
    return std::nullopt;
}

