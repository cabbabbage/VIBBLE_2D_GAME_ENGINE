#pragma once

#include <SDL.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"
#include "world/chunk.hpp"

class Assets;
class LightMap;

namespace world {
struct Chunk;
}

class LightMapManager {
public:
    using ShadowParameters   = world::Chunk::ChunkShadowParameters;
    using LightingChunk      = world::Chunk::LightingChunk;

    struct ChunkSnapshot {
        int      index                 = -1;
        SDL_Rect world_rect{0, 0, 0, 0};
        bool     active                = false;
        bool     needs_update          = false;
        bool     has_runtime_sample    = false;
        float    brightness            = 1.0f;
        float    runtime_sample        = 1.0f;
        float    static_component      = 1.0f;
        float    dynamic_component     = 1.0f;
        world::Chunk::ChunkShadowParameters shadow{};
    };

    explicit LightMapManager(Assets* assets);

    void begin_frame();

    const LightMap* light_map() const;
    float current_map_light_opacity() const { return last_map_light_opacity_; }
    std::vector<ChunkSnapshot> all_snapshots() const;
    std::vector<std::string>      assets_sampling_chunk(int index) const;
    std::optional<ChunkSnapshot> snapshot_for_chunk(int index) const;
    std::optional<ShadowParameters>      get_shadow_data(SDL_FPoint world_or_screen_pos) const;
    std::optional<ShadowParameters>      get_shadow_data_for_index(int index) const;

private:
    std::vector<const LightingChunk*> collect_active_lighting_chunks() const;
    std::optional<int> find_chunk_index(SDL_FPoint world_or_screen_pos) const;
    std::optional<ShadowParameters> shadow_data_for_chunk(const LightingChunk* chunk) const;

    Assets* assets_ = nullptr;
    float last_map_light_opacity_ = -1.0f; // normalized [0,1]; forces update on first frame
};



