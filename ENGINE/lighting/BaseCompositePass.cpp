#include "lighting/BaseCompositePass.hpp"

#include "dev_mode/PreviewViewport.hpp"
#include "lighting/PreloadInputs.hpp"
#include "utils/log.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace lighting {
namespace {

class LightingGuard {
public:
    explicit LightingGuard(PreloadInputs& inputs) : inputs_(inputs) { inputs_.disableScreenLightAndMovingLights(); }
    ~LightingGuard() { inputs_.restoreRuntimeLighting(); }

    LightingGuard(const LightingGuard&) = delete;
    LightingGuard& operator=(const LightingGuard&) = delete;

private:
    PreloadInputs& inputs_;
};

} // namespace

BaseCompositePass::BaseCompositePass(SDL_Renderer* renderer) : renderer_(renderer) {}

BaseCompositePass::~BaseCompositePass() {
    if (current_target_) {
        SDL_DestroyTexture(current_target_);
        current_target_ = nullptr;
    }
}

void BaseCompositePass::setRenderer(SDL_Renderer* renderer) { renderer_ = renderer; }

SDL_Texture* BaseCompositePass::ensureTarget(const PreloadInputs& inputs) {
    if (!renderer_) {
        return nullptr;
    }

    const SDL_Point size = inputs.targetSize();
    Uint32          fmt  = inputs.runtimePixelFormat();

    int    existing_w   = 0;
    int    existing_h   = 0;
    Uint32 existing_fmt = 0;
    if (current_target_) {
        SDL_QueryTexture(current_target_, &existing_fmt, nullptr, &existing_w, &existing_h);
    }

    if (!current_target_ || existing_w != size.x || existing_h != size.y || existing_fmt != fmt) {
        if (current_target_) {
            SDL_DestroyTexture(current_target_);
            current_target_ = nullptr;
        }
        current_target_ = SDL_CreateTexture(renderer_, fmt, SDL_TEXTUREACCESS_TARGET, size.x, size.y);
        if (!current_target_) {
            vibble::log::warn(std::string{"[Lighting] Failed to allocate base target: "} + SDL_GetError());
            return nullptr;
        }
        SDL_SetTextureBlendMode(current_target_, SDL_BLENDMODE_BLEND);
    }

    return current_target_;
}

void BaseCompositePass::compositeDraws(const std::vector<PreloadInputs::TextureDraw>& draws) {
    if (!renderer_) {
        return;
    }
    for (const auto& draw : draws) {
        if (!draw.texture) {
            continue;
        }
        SDL_BlendMode saved_mode = SDL_BLENDMODE_NONE;
        SDL_GetTextureBlendMode(draw.texture, &saved_mode);
        if (saved_mode != draw.blend) {
            SDL_SetTextureBlendMode(draw.texture, draw.blend);
        }
        if (SDL_RenderCopy(renderer_, draw.texture, &draw.src, &draw.dst) != 0) {
            vibble::log::warn(std::string{"[Lighting] Failed to composite texture: "} + SDL_GetError());
        }
        if (saved_mode != draw.blend) {
            SDL_SetTextureBlendMode(draw.texture, saved_mode);
        }
    }
}

SDL_Texture* BaseCompositePass::drawBase(PreloadInputs& inputs,
                                         PreviewViewport& preview,
                                         const std::string& label) {
    SDL_Texture* target = ensureTarget(inputs);
    if (!renderer_ || !target) {
        return nullptr;
    }

    LightingGuard guard(inputs);

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer_);
    if (SDL_SetRenderTarget(renderer_, target) != 0) {
        vibble::log::warn(std::string{"[Lighting] Failed to set base render target: "} + SDL_GetError());
        return nullptr;
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
    SDL_RenderClear(renderer_);

    compositeDraws(inputs.backgroundDraws());
    compositeDraws(inputs.tileDraws());
    compositeDraws(inputs.staticAssetDraws());
    compositeDraws(inputs.staticLightDraws());

    SDL_SetRenderTarget(renderer_, previous_target);

    preview.setRenderer(renderer_);
    preview.setLabel(label);
    preview.setTarget(target);
    if (preview.begin()) {
        SDL_Rect rect{0, 0, inputs.targetSize().x, inputs.targetSize().y};
        preview.clear(0, 0, 0, 0);
        preview.present(rect);
        preview.end();
    }

    return target;
}

} // namespace lighting

