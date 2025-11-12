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

namespace {
LightMapManager::ChunkSnapshot::ShadowInfo BuildShadowInfo(const world::Chunk* chunk) {
    LightMapManager::ChunkSnapshot::ShadowInfo info{};
    (void)chunk;
    // TODO(#reactive-shadows): Populate once runtime shadow data is exposed.
    return info;
}
}

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

    const auto chunks = collect_active_chunks();
    snapshots.reserve(chunks.size());
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const world::Chunk* chunk = chunks[i];
        if (!chunk) {
            continue;
        }
        ChunkSnapshot snap;
        snap.index              = static_cast<int>(i);
        snap.world_rect         = chunk->world_bounds;
        snap.active             = chunk->lighting.current_strength < 0.999f;
        snap.needs_update       = chunk->lighting.needs_update;
        snap.has_runtime_sample = chunk->lighting.has_runtime_average;
        snap.runtime_sample     = chunk->lighting.runtime_average_strength;
        snap.brightness         = chunk->lighting.current_strength;
        snap.static_component   = chunk->lighting.static_strength;
        snap.dynamic_component  = chunk->lighting.dynamic_strength;
        snap.has_runtime_color  = chunk->lighting.has_runtime_average;
        snap.runtime_color      = chunk->lighting.runtime_average_color;
        snap.shadow             = BuildShadowInfo(chunk);
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
    const auto chunks = collect_active_chunks();
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
    snap.active             = chunk->lighting.current_strength < 0.999f;
    snap.needs_update       = chunk->lighting.needs_update;
    snap.has_runtime_sample = chunk->lighting.has_runtime_average;
    snap.runtime_sample     = chunk->lighting.runtime_average_strength;
    snap.brightness         = chunk->lighting.current_strength;
    snap.static_component   = chunk->lighting.static_strength;
    snap.dynamic_component  = chunk->lighting.dynamic_strength;
    snap.has_runtime_color  = chunk->lighting.has_runtime_average;
    snap.runtime_color      = chunk->lighting.runtime_average_color;
    snap.shadow             = BuildShadowInfo(chunk);
    return snap;
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
        SDL_FPoint mapped = cam.screen_to_map({search_point.x, search_point.y});
        search_point      = SDL_Point{static_cast<int>(std::lround(mapped.x)), static_cast<int>(std::lround(mapped.y))};
        chunk             = map->chunk_from_world(search_point);
    }
    if (!chunk) {
        return std::nullopt;
    }

    const auto chunks = collect_active_chunks();
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        if (chunks[i] == chunk) {
            return static_cast<int>(i);
        }
    }
    return std::nullopt;
}

std::vector<const world::Chunk*> LightMapManager::collect_active_chunks() const {
    std::vector<const world::Chunk*> result;
    const LightMap* map = light_map();
    if (!map) {
        return result;
    }
    const auto& chunks = map->active_chunks();
    for (const world::Chunk* chunk : chunks) {
        result.push_back(chunk);
    }
    return result;
}
