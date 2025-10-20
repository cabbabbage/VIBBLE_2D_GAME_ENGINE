#include "light_map_manager.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
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

struct LutSample {
    float opacity = 1.0f;
    float offset  = 0.0f;
    float scale   = 1.0f;
};

// Evaluate the response LUT for a given brightness in [0,1].
LutSample sample_lut(const ReactiveShadowSettings& cfg, float brightness) {
    LutSample out{};
    const auto& entries = cfg.response_lut.entries;
    if (entries.empty()) {
        return out;
    }
    const float b = std::clamp(brightness, 0.0f, 1.0f);
    // Find bounding entries
    std::size_t hi = 0;
    while (hi < entries.size() && entries[hi].brightness < b) {
        ++hi;
    }
    if (hi == 0) {
        out.opacity = entries.front().opacity;
        out.offset  = entries.front().offset;
        out.scale   = entries.front().scale;
        return out;
    }
    if (hi >= entries.size()) {
        out.opacity = entries.back().opacity;
        out.offset  = entries.back().offset;
        out.scale   = entries.back().scale;
        return out;
    }
    const auto& a = entries[hi - 1];
    const auto& c = entries[hi];
    const float t = (c.brightness - a.brightness) > 1e-6f
                        ? (b - a.brightness) / (c.brightness - a.brightness)
                        : 0.0f;
    out.opacity = lerp(a.opacity, c.opacity, t);
    out.offset  = lerp(a.offset,  c.offset,  t);
    out.scale   = lerp(a.scale,   c.scale,   t);
    return out;
}

// Compute a simple brightness gradient from neighboring chunks.
// Returns a pair (gx, gy) where larger magnitude implies steeper change towards brighter tiles.
std::pair<float, float> compute_brightness_gradient(const world::Chunk& center,
                                                    const world::Grid& grid,
                                                    int radius,
                                                    float falloff_x,
                                                    float falloff_y) {
    if (radius <= 0) {
        return {0.0f, 0.0f};
    }
    const float cb = std::clamp(center.base_brightness, 0.0f, 1.0f);
    float gx = 0.0f;
    float gy = 0.0f;
    for (int dj = -radius; dj <= radius; ++dj) {
        for (int di = -radius; di <= radius; ++di) {
            if (di == 0 && dj == 0) continue;
            const int ni = center.i + di;
            const int nj = center.j + dj;
            const world::Chunk* n = grid.find_chunk_ij(ni, nj);
            const float nb = n ? std::clamp(n->base_brightness, 0.0f, 1.0f) : 0.0f;
            const float db = nb - cb;
            const float dx = static_cast<float>(di);
            const float dy = static_cast<float>(dj);
            const float dist = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
            // Weight by inverse distance; apply anisotropic falloff.
            const float wx = falloff_x / dist;
            const float wy = falloff_y / dist;
            gx += db * (dx / dist) * wx;
            gy += db * (dy / dist) * wy;
        }
    }
    return {gx, gy};
}

} // namespace

void LightMapManager::begin_frame() {
    if (!assets_) {
        return;
    }
    const LightMap* map = light_map();
    if (!map) {
        return;
    }

    // Resolve settings; fall back to sanitized defaults.
    const ReactiveShadowSettings* live = assets_->reactive_shadow_settings();
    ReactiveShadowSettings cfg = live ? *live : render_pipeline::shading::sanitize_reactive_shadow_settings({});
    cfg = render_pipeline::shading::sanitize_reactive_shadow_settings(cfg);

    const auto& chunks = map->active_chunks();
    if (chunks.empty()) {
        return;
    }

    // For gradient sampling
    const world::Grid& grid = assets_->world_grid();

    for (world::Chunk* chunk : chunks) {
        if (!chunk) continue;
        // Sample response from LUT using this chunk's base brightness.
        const float b = std::clamp(chunk->base_brightness, 0.0f, 1.0f);
        const LutSample resp = sample_lut(cfg, b);

        // Compute gradient-based offset direction from neighbors.
        const int   radius = std::max(0, cfg.virtual_light_map.search_radius);
        const float fx     = std::max(0.0f, cfg.virtual_light_map.horizontal_falloff);
        const float fy     = std::max(0.0f, cfg.virtual_light_map.vertical_falloff);
        auto [gx, gy]      = compute_brightness_gradient(*chunk, grid, radius, fx, fy);

        // Normalize gradient to [-1,1] range to avoid extreme offsets.
        const float gmag = std::sqrt(gx * gx + gy * gy);
        float nx = 0.0f;
        float ny = 0.0f;
        if (gmag > 1e-4f) {
            nx = gx / gmag;
            ny = gy / gmag;
        }

        // Offset magnitude from LUT and chunk size factor.
        const float size_factor = std::max(0.0f, cfg.virtual_light_map.size_scale_factor);
        const float off_mag     = resp.offset;
        const float max_off_x   = std::max(0.0f, cfg.virtual_light_map.max_offset_x);
        const float max_off_y   = std::max(0.0f, cfg.virtual_light_map.max_offset_y);
        const float off_px_x    = nx * off_mag * fx * size_factor;
        const float off_px_y    = ny * off_mag * fy * size_factor;
        const float clamped_off_x = std::clamp(off_px_x, -max_off_x, max_off_x);
        const float clamped_off_y = std::clamp(off_px_y, -max_off_y, max_off_y);

        // Apply to chunk. Opacity/scale are multiplicative strengths applied in shading and
        // chunk rendering. Clamp opacity to [0, 1] because it drives texture transparency.
        const float combined_opacity = std::clamp(resp.opacity * cfg.opacity_strength, 0.0f, 1.0f);
        const float combined_scale   = std::max(0.0f, resp.scale * cfg.scale_strength);
        chunk->opacity_strength      = combined_opacity;
        chunk->scale_strength        = combined_scale;
        chunk->offset_x              = static_cast<int>(std::lround(clamped_off_x));
        chunk->offset_y              = static_cast<int>(std::lround(clamped_off_y));

        // Populate the per-chunk shadow data exposed to asset shading.
        chunk->shadow.opacity  = combined_opacity;
        chunk->shadow.scale    = std::max(0.0f, cfg.virtual_light_map.shadow_scale) * combined_scale;
        if (max_off_x > 1e-4f) {
            chunk->shadow.offset_x_percent = std::clamp((clamped_off_x / max_off_x) * 100.0f, -100.0f, 100.0f);
        } else {
            chunk->shadow.offset_x_percent = 0.0f;
        }
        if (max_off_y > 1e-4f) {
            chunk->shadow.offset_y_percent = std::clamp((clamped_off_y / max_off_y) * 100.0f, -100.0f, 100.0f);
        } else {
            chunk->shadow.offset_y_percent = 0.0f;
        }
        const float parallax_strength = std::clamp(cfg.parallax_strength, 0.0f, 10.0f);
        chunk->shadow.parallax_intensity_percent = parallax_strength * 10.0f;
        // Leave brightness_strength unchanged for now; combined in snapshot only.
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
        snap.combined_brightness = chunk->base_brightness * chunk->brightness_strength;
        snap.static_min          = chunk->base_brightness;
        snap.static_max          = chunk->base_brightness;
        snap.static_average      = chunk->base_brightness;
        snap.static_empty        = false;
        snap.shadow_opacity_min  = chunk->opacity_strength;
        snap.shadow_opacity_max  = chunk->opacity_strength;
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
    snap.combined_brightness = chunk->base_brightness * chunk->brightness_strength;
    snap.static_min          = chunk->base_brightness;
    snap.static_max          = chunk->base_brightness;
    snap.static_average      = chunk->base_brightness;
    snap.static_empty        = false;
    snap.shadow_opacity_min  = chunk->opacity_strength;
    snap.shadow_opacity_max  = chunk->opacity_strength;
    snap.brightness_strength = chunk->brightness_strength;
    snap.opacity_strength    = chunk->opacity_strength;
    snap.scale_strength      = chunk->scale_strength;
    snap.offset_x            = chunk->offset_x;
    snap.offset_y            = chunk->offset_y;
    snap.shadow              = chunk->shadow;
    return snap;
}

std::optional<LightMapManager::ShadowData> LightMapManager::shadow_data_for_chunk(const world::Chunk* chunk) const {
    if (!chunk) {
        return std::nullopt;
    }
    return chunk->shadow;
}

std::optional<LightMapManager::ShadowData> LightMapManager::get_shadow_data_for_index(int index) const {
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

std::optional<LightMapManager::ShadowData> LightMapManager::get_shadow_data(SDL_FPoint world_or_screen_pos) const {
    if (auto index = find_quadrant_index(world_or_screen_pos)) {
        return get_shadow_data_for_index(*index);
    }
    return std::nullopt;
}

