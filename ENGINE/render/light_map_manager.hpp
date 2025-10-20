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
    using UseShadowData = world::Chunk::UseShadowData;

    struct ChunkSnapshot {
        int      index               = -1;
        SDL_Rect world_rect{0, 0, 0, 0};
        bool     active              = false;
        bool     dirty               = false;
        float    base_brightness     = 0.0f;
        float    combined_brightness = 0.0f;
        float    static_min          = 0.0f;
        float    static_max          = 0.0f;
        float    static_average      = 0.0f;
        bool     static_empty        = true;
        float    shadow_opacity_min  = 0.0f;
        float    shadow_opacity_max  = 0.0f;
        float    brightness_strength = 1.0f;
        float                 opacity_strength    = 1.0f;
        float                 scale_strength      = 1.0f;
        int                   offset_x            = 0;
        int                   offset_y            = 0;
        world::Chunk::UseShadowData shadow{};
    };

    explicit LightMapManager(Assets* assets);

    void begin_frame();

    const LightMap* light_map() const;
    std::vector<ChunkSnapshot> all_snapshots() const;
    std::vector<std::string>      assets_sampling_chunk(int index) const;
    std::optional<ChunkSnapshot> snapshot_for_chunk(int index) const;
    std::optional<UseShadowData>      get_shadow_data(SDL_FPoint world_or_screen_pos) const;
    std::optional<UseShadowData>      get_shadow_data_for_index(int index) const;

private:
    std::optional<int> find_chunk_index(SDL_FPoint world_or_screen_pos) const;
    std::optional<UseShadowData> shadow_data_for_chunk(const world::Chunk* chunk) const;

    Assets* assets_ = nullptr;
    float last_screen_light_opacity_ = -1.0f; // normalized [0,1]; forces update on first frame
};



