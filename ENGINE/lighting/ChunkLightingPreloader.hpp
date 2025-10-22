#pragma once

#include <SDL.h>

#include <vector>

#include "lighting/BaseCompositePass.hpp"
#include "lighting/MinMaxPass.hpp"
#include "lighting/StaticLightMask.hpp"

class Assets;
class PreviewViewport;

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

