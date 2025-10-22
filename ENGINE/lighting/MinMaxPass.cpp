#include "lighting/MinMaxPass.hpp"

#include "dev_mode/PreviewViewport.hpp"
#include "lighting/BaseCompositePass.hpp"
#include "lighting/PreloadInputs.hpp"
#include "utils/Luminance.hpp"
#include "utils/RenderReadback.hpp"
#include "utils/log.hpp"
#include "world/chunk.hpp"

#include <string>

namespace lighting {

MinMaxPass::MinMaxPass(SDL_Renderer* renderer) : renderer_(renderer) {}

void MinMaxPass::setRenderer(SDL_Renderer* renderer) { renderer_ = renderer; }

bool MinMaxPass::applyMask(SDL_Texture* mask, SDL_Texture* target, const PreloadInputs& inputs) {
    if (!renderer_ || !mask || !target) {
        return false;
    }

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer_);
    if (SDL_SetRenderTarget(renderer_, target) != 0) {
        vibble::log::warn(std::string{"[Lighting] Failed to bind base for min evaluation: "} + SDL_GetError());
        return false;
    }

    SDL_BlendMode saved_mode = SDL_BLENDMODE_NONE;
    SDL_GetTextureBlendMode(mask, &saved_mode);
    SDL_SetTextureBlendMode(mask, SDL_BLENDMODE_MOD);

    SDL_Rect rect{0, 0, inputs.targetSize().x, inputs.targetSize().y};
    if (SDL_RenderCopy(renderer_, mask, nullptr, &rect) != 0) {
        vibble::log::warn(std::string{"[Lighting] Failed to render static mask onto base: "} + SDL_GetError());
    }

    SDL_SetTextureBlendMode(mask, saved_mode);
    SDL_SetRenderTarget(renderer_, previous_target);
    return true;
}

bool MinMaxPass::evaluate(PreloadInputs& inputs,
                          BaseCompositePass& base_pass,
                          SDL_Texture* mask,
                          PreviewViewport& preview,
                          world::Chunk& chunk) {
    if (!renderer_) {
        return false;
    }

    SDL_Texture* base_texture = base_pass.getCurrentTarget();
    if (!base_texture) {
        base_texture = base_pass.drawBase(inputs, preview, "Base");
    }
    if (!base_texture) {
        return false;
    }

    if (mask) {
        applyMask(mask, base_texture, inputs);
    }

    preview.setRenderer(renderer_);
    preview.setLabel("Min");
    preview.setTarget(base_texture);
    if (preview.begin()) {
        SDL_Rect rect{0, 0, inputs.targetSize().x, inputs.targetSize().y};
        preview.clear(0, 0, 0, 0);
        preview.present(rect);
        preview.end();
    }

    MeasureResult min_result = readRgba(renderer_, base_texture);
    if (min_result.success()) {
        chunk.lighting.min_static_avg_strength = computeAverageLuminance(min_result);
    } else if (min_result.status == MeasureResult::Status::AllocationFailed ||
               min_result.status == MeasureResult::Status::ReadbackFailed) {
        chunk.needs_retry = true;
    }

    SDL_Texture* refreshed_base = base_pass.drawBase(inputs, preview, "Max");
    if (!refreshed_base) {
        return false;
    }

    MeasureResult max_result = readRgba(renderer_, refreshed_base);
    if (max_result.success()) {
        chunk.lighting.max_static_avg_strength = computeAverageLuminance(max_result);
    } else if (max_result.status == MeasureResult::Status::AllocationFailed ||
               max_result.status == MeasureResult::Status::ReadbackFailed) {
        chunk.needs_retry = true;
    }

    return true;
}

} // namespace lighting

