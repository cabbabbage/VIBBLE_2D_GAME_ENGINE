#include "light_map_manager.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <string>
#include <cstdint>
#include <climits>

#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "render/global_light_source.hpp"
#include "world/chunk.hpp"
#include "world/grid.hpp"

LightMapManager::LightMapManager(Assets* assets) : assets_(assets) {}

void LightMapManager::begin_frame() {
    if (!assets_) {
        return;
    }
    if (const Global_Light_Source* gl = assets_->map_light_source()) {
        last_map_light_opacity_ =
            std::clamp(static_cast<float>(gl->get_current_color().a) / 255.0f, 0.0f, 1.0f);
    } else {
        last_map_light_opacity_ = 0.0f;
    }
}

const LightMap* LightMapManager::light_map() const {
    return assets_ ? assets_->light_map() : nullptr;
}

std::vector<LightMapManager::ChunkSnapshot> LightMapManager::all_snapshots() const {
    std::vector<ChunkSnapshot> snapshots;
    const LightMap*            map = light_map();
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
        ChunkSnapshot snap;
        snap.index              = static_cast<int>(i);
        snap.world_rect         = chunk->world_bounds;
        snap.active             = chunk->lighting.is_active;
        snap.needs_update       = chunk->lighting.needs_update;
        snap.occupied_by_moving = chunk->lighting.is_occupied_by_moving_source;
        snap.has_runtime_sample = chunk->lighting.has_runtime_average;
        snap.runtime_sample     = chunk->lighting.runtime_average_strength;
        snap.brightness         = chunk->lighting.current_strength;
        snap.shadow             = chunk->shadow;
        snapshots.push_back(snap);
    }
    return snapshots;
}

std::vector<std::string> LightMapManager::assets_sampling_chunk(int /*index*/) const {
    return {};
}

std::optional<LightMapManager::ChunkSnapshot> LightMapManager::snapshot_for_chunk(int index) const {
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
    ChunkSnapshot snap;
    snap.index              = index;
    snap.world_rect         = chunk->world_bounds;
    snap.active             = chunk->lighting.is_active;
    snap.needs_update       = chunk->lighting.needs_update;
    snap.occupied_by_moving = chunk->lighting.is_occupied_by_moving_source;
    snap.has_runtime_sample = chunk->lighting.has_runtime_average;
    snap.runtime_sample     = chunk->lighting.runtime_average_strength;
    snap.brightness         = chunk->lighting.current_strength;
    snap.shadow             = chunk->shadow;
    return snap;
}

std::optional<LightMapManager::ShadowParameters> LightMapManager::shadow_data_for_chunk(const world::Chunk* chunk) const {
    if (!chunk) {
        return std::nullopt;
    }
    return chunk->shadow;
}

std::optional<LightMapManager::ShadowParameters> LightMapManager::get_shadow_data_for_index(int index) const {
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

std::optional<int> LightMapManager::find_chunk_index(SDL_FPoint world_or_screen_pos) const {
    const LightMap* map = light_map();
    if (!map) {
        return std::nullopt;
    }

    SDL_Point world_point{static_cast<int>(std::lround(world_or_screen_pos.x)),
                          static_cast<int>(std::lround(world_or_screen_pos.y))};

    world::Chunk* chunk = map->chunk_from_world(world_point);
    if (!chunk && assets_) {
        const camera& cam = assets_->getView();
        SDL_Point     from_screen = cam.screen_to_map({world_point.x, world_point.y});
        chunk                     = map->chunk_from_world(from_screen);
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

std::optional<LightMapManager::ShadowParameters> LightMapManager::get_shadow_data(SDL_FPoint world_or_screen_pos) const {
    if (auto index = find_chunk_index(world_or_screen_pos)) {
        return get_shadow_data_for_index(*index);
    }
    return std::nullopt;
}
