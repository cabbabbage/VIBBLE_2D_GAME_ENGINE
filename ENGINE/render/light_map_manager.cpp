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

    const auto lighting_chunks = collect_active_lighting_chunks();
    snapshots.reserve(lighting_chunks.size());
    for (std::size_t i = 0; i < lighting_chunks.size(); ++i) {
        const LightingChunk* chunk = lighting_chunks[i];
        if (!chunk) {
            continue;
        }
        ChunkSnapshot snap;
        snap.index              = static_cast<int>(i);
        snap.world_rect         = chunk->world_bounds;
        snap.active             = chunk->lighting.is_active;
        snap.needs_update       = chunk->lighting.needs_update;
        snap.has_runtime_sample = chunk->lighting.has_runtime_average;
        snap.runtime_sample     = chunk->lighting.runtime_average_strength;
        snap.brightness         = chunk->lighting.current_strength;
        snap.static_component   = chunk->lighting.static_strength;
        snap.dynamic_component  = chunk->lighting.dynamic_strength;
        snap.has_runtime_color  = chunk->lighting.has_runtime_average;
        snap.runtime_color      = chunk->lighting.runtime_average_color;
        snap.shadow             = chunk->shadow;
        snapshots.push_back(snap);
    }
    return snapshots;
}

std::vector<std::string> LightMapManager::assets_sampling_chunk(int ) const {
    return {};
}

std::optional<LightMapManager::ChunkSnapshot> LightMapManager::snapshot_for_chunk(int index) const {
    const LightMap* map = light_map();
    if (!map) {
        return std::nullopt;
    }
    const auto lighting_chunks = collect_active_lighting_chunks();
    if (index < 0 || static_cast<std::size_t>(index) >= lighting_chunks.size()) {
        return std::nullopt;
    }
    const LightingChunk* chunk = lighting_chunks[static_cast<std::size_t>(index)];
    if (!chunk) {
        return std::nullopt;
    }
    ChunkSnapshot snap;
    snap.index              = index;
    snap.world_rect         = chunk->world_bounds;
    snap.active             = chunk->lighting.is_active;
    snap.needs_update       = chunk->lighting.needs_update;
    snap.has_runtime_sample = chunk->lighting.has_runtime_average;
    snap.runtime_sample     = chunk->lighting.runtime_average_strength;
    snap.brightness         = chunk->lighting.current_strength;
    snap.static_component   = chunk->lighting.static_strength;
    snap.dynamic_component  = chunk->lighting.dynamic_strength;
    snap.has_runtime_color  = chunk->lighting.has_runtime_average;
    snap.runtime_color      = chunk->lighting.runtime_average_color;
    snap.shadow             = chunk->shadow;
    return snap;
}

std::optional<LightMapManager::ShadowParameters> LightMapManager::shadow_data_for_chunk(const LightingChunk* chunk) const {
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
    const auto lighting_chunks = collect_active_lighting_chunks();
    if (index < 0 || static_cast<std::size_t>(index) >= lighting_chunks.size()) {
        return std::nullopt;
    }
    return shadow_data_for_chunk(lighting_chunks[static_cast<std::size_t>(index)]);
}

std::optional<int> LightMapManager::find_chunk_index(SDL_FPoint world_or_screen_pos) const {
    const LightMap* map = light_map();
    if (!map) {
        return std::nullopt;
    }

    SDL_Point search_point{static_cast<int>(std::lround(world_or_screen_pos.x)),
                           static_cast<int>(std::lround(world_or_screen_pos.y))};

    world::Chunk* chunk = map->chunk_from_world(search_point);
    if (!chunk && assets_) {
        const camera& cam = assets_->getView();
        search_point      = cam.screen_to_map({search_point.x, search_point.y});
        chunk             = map->chunk_from_world(search_point);
    }
    if (!chunk) {
        return std::nullopt;
    }

    const LightingChunk* cell = chunk->lighting_chunk_from_world(search_point);
    if (!cell) {
        return std::nullopt;
    }

    const auto lighting_chunks = collect_active_lighting_chunks();
    for (std::size_t i = 0; i < lighting_chunks.size(); ++i) {
        if (lighting_chunks[i] == cell) {
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

std::vector<const LightMapManager::LightingChunk*> LightMapManager::collect_active_lighting_chunks() const {
    std::vector<const LightingChunk*> result;
    const LightMap* map = light_map();
    if (!map) {
        return result;
    }
    const auto& chunks = map->active_chunks();
    for (const world::Chunk* chunk : chunks) {
        if (!chunk) {
            continue;
        }
        const auto& lighting_chunks = chunk->lighting_chunks();
        for (const auto& cell : lighting_chunks) {
            result.push_back(&cell);
        }
    }
    return result;
}
