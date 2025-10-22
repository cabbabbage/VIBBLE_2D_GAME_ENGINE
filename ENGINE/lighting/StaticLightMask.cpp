#include "lighting/StaticLightMask.hpp"

#include "dev_mode/PreviewViewport.hpp"
#include "lighting/PreloadInputs.hpp"
#include "utils/log.hpp"
#include "world/chunk.hpp"

#include <algorithm>
#include <string>

namespace lighting {

StaticLightMask::StaticLightMask(SDL_Renderer* renderer) : renderer_(renderer) {}

StaticLightMask::~StaticLightMask() {
    if (current_target_) {
        SDL_DestroyTexture(current_target_);
        current_target_ = nullptr;
    }
}

void StaticLightMask::setRenderer(SDL_Renderer* renderer) { renderer_ = renderer; }

SDL_Texture* StaticLightMask::ensureTarget(const PreloadInputs& inputs) {
    if (!renderer_) {
        return nullptr;
    }

    const SDL_Point size = inputs.targetSize();
    Uint32          fmt  = inputs.runtimePixelFormat();

    int existing_w = 0;
    int existing_h = 0;
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
            vibble::log::warn(std::string{"[Lighting] Failed to allocate static mask: "} + SDL_GetError());
            return nullptr;
        }
        SDL_SetTextureBlendMode(current_target_, SDL_BLENDMODE_BLEND);
    }

    return current_target_;
}

SDL_Texture* StaticLightMask::buildMask(const world::Chunk& chunk,
                                        const PreloadInputs& inputs,
                                        PreviewViewport& preview) {
    SDL_Texture* target = ensureTarget(inputs);
    if (!renderer_ || !target) {
        return nullptr;
    }

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer_);
    if (SDL_SetRenderTarget(renderer_, target) != 0) {
        vibble::log::warn(std::string{"[Lighting] Failed to set mask render target: "} + SDL_GetError());
        return nullptr;
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

    const auto& draws = inputs.staticLightDraws();
    for (const auto& draw : draws) {
        if (!draw.texture) {
            continue;
        }
        Uint8 save_r = 255, save_g = 255, save_b = 255, save_a = 255;
        SDL_BlendMode saved_mode = SDL_BLENDMODE_NONE;
        SDL_GetTextureColorMod(draw.texture, &save_r, &save_g, &save_b);
        SDL_GetTextureAlphaMod(draw.texture, &save_a);
        SDL_GetTextureBlendMode(draw.texture, &saved_mode);

        SDL_SetTextureColorMod(draw.texture, 255, 255, 255);
        SDL_SetTextureAlphaMod(draw.texture, 255);
        SDL_SetTextureBlendMode(draw.texture, draw.blend);

        if (SDL_RenderCopy(renderer_, draw.texture, &draw.src, &draw.dst) != 0) {
            vibble::log::warn(std::string{"[Lighting] Failed to stamp static light: "} + SDL_GetError());
        }

        SDL_SetTextureColorMod(draw.texture, save_r, save_g, save_b);
        SDL_SetTextureAlphaMod(draw.texture, save_a);
        SDL_SetTextureBlendMode(draw.texture, saved_mode);
    }

    SDL_SetRenderTarget(renderer_, previous_target);

    preview.setRenderer(renderer_);
    preview.setLabel("Mask build");
    preview.setTarget(target);
    if (preview.begin()) {
        SDL_Rect rect{0, 0, inputs.targetSize().x, inputs.targetSize().y};
        preview.clear(0, 0, 0, 255);
        preview.present(rect);
        preview.end();
    }

    (void)chunk;
    return target;
}

} // namespace lighting

