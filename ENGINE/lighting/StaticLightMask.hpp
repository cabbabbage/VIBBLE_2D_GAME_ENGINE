#pragma once

#include <SDL.h>

class PreviewViewport;

namespace world {
class Chunk;
} // namespace world

namespace lighting {

class PreloadInputs;

class StaticLightMask {
public:
    explicit StaticLightMask(SDL_Renderer* renderer = nullptr);
    ~StaticLightMask();

    void          setRenderer(SDL_Renderer* renderer);
    SDL_Renderer* renderer() const { return renderer_; }

    SDL_Texture* buildMask(const world::Chunk& chunk,
                           const PreloadInputs& inputs,
                           PreviewViewport& preview);

private:
    SDL_Texture* ensureTarget(const PreloadInputs& inputs);

private:
    SDL_Renderer* renderer_       = nullptr;
    SDL_Texture*  current_target_ = nullptr;
};

} // namespace lighting

