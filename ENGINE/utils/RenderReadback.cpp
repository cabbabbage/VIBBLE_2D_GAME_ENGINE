#include "utils/RenderReadback.hpp"

#include "utils/log.hpp"

#include <new>
#include <string>

namespace lighting {

MeasureResult readRgba(SDL_Renderer* renderer, SDL_Texture* texture) {
    MeasureResult result{};
    if (!renderer || !texture) {
        result.status = MeasureResult::Status::InvalidInput;
        return result;
    }

    Uint32 format = 0;
    int    access = 0;
    int    width  = 0;
    int    height = 0;
    if (SDL_QueryTexture(texture, &format, &access, &width, &height) != 0 || width <= 0 || height <= 0) {
        result.status = MeasureResult::Status::InvalidInput;
        return result;
    }

    const int pitch = width * 4;
    try {
        result.pixels.resize(static_cast<std::size_t>(pitch) * static_cast<std::size_t>(height));
    } catch (const std::bad_alloc&) {
        vibble::log::warn("[Lighting] Unable to allocate readback buffer");
        result.status = MeasureResult::Status::AllocationFailed;
        return result;
    }

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
    const bool   changed_target  = (previous_target != texture);
    if (changed_target) {
        if (SDL_SetRenderTarget(renderer, texture) != 0) {
            vibble::log::warn(std::string{"[Lighting] Failed to bind texture for readback: "} + SDL_GetError());
            result.status = MeasureResult::Status::ReadbackFailed;
            result.pixels.clear();
            return result;
        }
    }

    if (SDL_RenderReadPixels(renderer,
                              nullptr,
                              SDL_PIXELFORMAT_RGBA32,
                              result.pixels.data(),
                              pitch) != 0) {
        vibble::log::warn(std::string{"[Lighting] SDL_RenderReadPixels failed: "} + SDL_GetError());
        result.status = MeasureResult::Status::ReadbackFailed;
        result.pixels.clear();
    } else {
        result.status = MeasureResult::Status::Success;
        result.width  = width;
        result.height = height;
    }

    if (changed_target) {
        SDL_SetRenderTarget(renderer, previous_target);
    }

    return result;
}

} // namespace lighting

