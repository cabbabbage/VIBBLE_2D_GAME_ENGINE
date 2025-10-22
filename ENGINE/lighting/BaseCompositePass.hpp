#pragma once

#include <SDL.h>

#include "lighting/PreloadInputs.hpp"

#include <string>
#include <vector>

class PreviewViewport;

namespace lighting {

class BaseCompositePass {
public:
    explicit BaseCompositePass(SDL_Renderer* renderer = nullptr);
    ~BaseCompositePass();

    void          setRenderer(SDL_Renderer* renderer);
    SDL_Renderer* renderer() const { return renderer_; }

    SDL_Texture* drawBase(PreloadInputs& inputs,
                          PreviewViewport& preview,
                          const std::string& label = "Base");

    SDL_Texture* getCurrentTarget() const { return current_target_; }

private:
    SDL_Texture* ensureTarget(const PreloadInputs& inputs);
    void         compositeDraws(const std::vector<PreloadInputs::TextureDraw>& draws);

private:
    SDL_Renderer* renderer_       = nullptr;
    SDL_Texture*  current_target_ = nullptr;
};

} // namespace lighting

