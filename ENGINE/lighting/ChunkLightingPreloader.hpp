#pragma once

#include <SDL.h>

#include <cstddef>
#include <string>
#include <vector>

#include "dev_mode/PreviewViewport.hpp"
#include "lighting/BaseCompositePass.hpp"
#include "lighting/MinMaxPass.hpp"
#include "lighting/StaticLightMask.hpp"
#include "lighting/chunk_lighting_state_utils.hpp"
#include "utils/loading_status_notifier.hpp"

class Assets;
class PreloadInputs;

namespace world {
class Chunk;
} // namespace world

namespace lighting {

class LightingCache;

class ChunkLightingPreloader {
public:
    ChunkLightingPreloader(SDL_Renderer* renderer,
                           Assets* assets,
                           LightingCache* cache = nullptr);

    void setRenderer(SDL_Renderer* renderer);
    void setAssets(Assets* assets);
    void setCache(LightingCache* cache);

    bool preloadChunk(world::Chunk& chunk);
    void preloadChunks(const std::vector<world::Chunk*>& chunks);

private:
    bool processChunk(world::Chunk& chunk,
                      PreviewViewport& mask_preview,
                      PreviewViewport& base_preview,
                      PreviewViewport& minmax_preview);
    SDL_Texture* cloneMaskTexture(SDL_Texture* source, const PreloadInputs& inputs);

private:
    SDL_Renderer* renderer_ = nullptr;
    Assets*       assets_   = nullptr;
    LightingCache* cache_   = nullptr;

    StaticLightMask   mask_pass_;
    BaseCompositePass base_pass_;
    MinMaxPass        minmax_pass_;
};

} // namespace lighting

inline void lighting::ChunkLightingPreloader::preloadChunks(const std::vector<world::Chunk*>& chunks) {
    if (chunks.empty()) {
        return;
    }

    PreviewViewport mask_preview(renderer_);
    PreviewViewport base_preview(renderer_);
    PreviewViewport minmax_preview(renderer_);

    const std::size_t total = chunks.size();
    std::size_t       index = 0;

    for (world::Chunk* chunk : chunks) {
        ++index;
        if (!chunk) {
            continue;
        }

        if (lighting::chunk_ready_for_static_preload(*chunk)) {
            continue;
        }

        std::string status = "Lighting chunk " + std::to_string(index) + "/" + std::to_string(total);
        loading_status::notify(status);

        if (!processChunk(*chunk, mask_preview, base_preview, minmax_preview)) {
            lighting::reset_chunk_retry_flags(*chunk);
        }
    }
}

