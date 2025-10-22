#pragma once

#include <SDL.h>

class PreviewViewport;

namespace world {
class Chunk;
} // namespace world

namespace lighting {

class BaseCompositePass;
class PreloadInputs;

class MinMaxPass {
public:
    explicit MinMaxPass(SDL_Renderer* renderer = nullptr);

    void          setRenderer(SDL_Renderer* renderer);
    SDL_Renderer* renderer() const { return renderer_; }

    bool evaluate(PreloadInputs& inputs,
                  BaseCompositePass& base_pass,
                  SDL_Texture* mask,
                  PreviewViewport& preview,
                  world::Chunk& chunk);

private:
    bool applyMask(SDL_Texture* mask, SDL_Texture* target, const PreloadInputs& inputs);

private:
    SDL_Renderer* renderer_ = nullptr;
};

} // namespace lighting

