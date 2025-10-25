#include "lighting/ChunkLightingPreloader.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "lighting/PreloadInputs.hpp"
#include "persistence/LightingCache.hpp"
#include "utils/log.hpp"
#include "world/chunk.hpp"

#include <string>

namespace lighting {

ChunkLightingPreloader::ChunkLightingPreloader(SDL_Renderer* renderer,
                                               Assets* assets,
                                               LightingCache* cache)
    : renderer_(renderer)
    , assets_(assets)
    , cache_(cache)
    , mask_pass_(renderer)
    , base_pass_(renderer)
    , minmax_pass_(renderer) {}

void ChunkLightingPreloader::setRenderer(SDL_Renderer* renderer) {
    renderer_ = renderer;
    mask_pass_.setRenderer(renderer);
    base_pass_.setRenderer(renderer);
    minmax_pass_.setRenderer(renderer);
}

void ChunkLightingPreloader::setAssets(Assets* assets) { assets_ = assets; }

void ChunkLightingPreloader::setCache(LightingCache* cache) { cache_ = cache; }

SDL_Texture* ChunkLightingPreloader::cloneMaskTexture(SDL_Texture* source,
                                                      const PreloadInputs& inputs) {
    if (!renderer_ || !source) {
        return nullptr;
    }

    const SDL_Point size = inputs.targetSize();
    SDL_Texture* clone = SDL_CreateTexture(renderer_,
                                           inputs.runtimePixelFormat(),
                                           SDL_TEXTUREACCESS_TARGET,
                                           size.x,
                                           size.y);
    if (!clone) {
        vibble::log::warn(std::string{"[Lighting] Failed to clone mask texture: "} + SDL_GetError());
        return nullptr;
    }
    const SDL_BlendMode runtime_blend = inputs.runtimeLightBlendMode();
    if (SDL_SetTextureBlendMode(clone, runtime_blend) != 0) {
        vibble::log::warn(std::string{"[Lighting] Failed to configure cloned mask blend mode: "} + SDL_GetError());
        // Leave texture usable even if blend configuration fails.
    }

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer_);
    if (SDL_SetRenderTarget(renderer_, clone) != 0) {
        vibble::log::warn(std::string{"[Lighting] Failed to bind clone mask target: "} + SDL_GetError());
        SDL_DestroyTexture(clone);
        return nullptr;
    }

    SDL_RenderClear(renderer_);
    SDL_Rect rect{0, 0, size.x, size.y};
    if (SDL_RenderCopy(renderer_, source, nullptr, &rect) != 0) {
        vibble::log::warn(std::string{"[Lighting] Failed to copy mask texture: "} + SDL_GetError());
    }

    SDL_SetRenderTarget(renderer_, previous_target);
    return clone;
}

bool ChunkLightingPreloader::processChunk(world::Chunk& chunk,
                                          PreviewViewport& mask_preview,
                                          PreviewViewport& base_preview,
                                          PreviewViewport& minmax_preview) {
    (void)mask_preview;
    (void)base_preview;
    (void)minmax_preview;
    if (!renderer_ || !assets_) {
        return false;
    }
    chunk.releaseLightingArtifacts();
    return true;
}

bool ChunkLightingPreloader::preloadChunk(world::Chunk& chunk) {
    PreviewViewport mask_preview(renderer_);
    PreviewViewport base_preview(renderer_);
    PreviewViewport minmax_preview(renderer_);
    return processChunk(chunk, mask_preview, base_preview, minmax_preview);
}

} // namespace lighting

