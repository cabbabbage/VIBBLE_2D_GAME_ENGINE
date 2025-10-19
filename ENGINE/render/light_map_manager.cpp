#include "light_map_manager.hpp"

#include <cmath>

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "render/light_map.hpp"
#include "world/chunk.hpp"
#include "world/grid.hpp"

LightMapManager::LightMapManager(Assets* assets) : assets_(assets) {}

void LightMapManager::begin_frame() {}

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
    return snap;
}

std::optional<LightMapManager::QuadrantParams> LightMapManager::params_for_chunk(const world::Chunk* chunk) const {
    if (!chunk) {
        return std::nullopt;
    }
    QuadrantParams params;
    params.opacity_q  = chunk->opacity_strength;
    params.scale_q    = chunk->scale_strength;
    params.offset_x_q = static_cast<float>(chunk->offset_x);
    params.offset_y_q = static_cast<float>(chunk->offset_y);
    return params;
}

std::optional<LightMapManager::QuadrantParams> LightMapManager::get_quadrant_params_for_index(int index) const {
    const LightMap* map = light_map();
    if (!map) {
        return std::nullopt;
    }
    const auto& chunks = map->active_chunks();
    if (index < 0 || static_cast<std::size_t>(index) >= chunks.size()) {
        return std::nullopt;
    }
    return params_for_chunk(chunks[static_cast<std::size_t>(index)]);
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

std::optional<LightMapManager::QuadrantParams> LightMapManager::get_quadrant_params(SDL_FPoint world_or_screen_pos) const {
    if (auto index = find_quadrant_index(world_or_screen_pos)) {
        return get_quadrant_params_for_index(*index);
    }
    return std::nullopt;
}

