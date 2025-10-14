#pragma once

#include <SDL.h>

#include "asset/Asset.hpp"
#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"
#include "render/global_light_source.hpp"

namespace render_pipeline::lighting::detail {

inline SDL_Texture* build_light_texture(SDL_Renderer* renderer,
                                        const Asset& asset,
                                        StageContext& context,
                                        bool behind,
                                        SDL_Texture* texture_override = nullptr) {
    if (!renderer) {
        return nullptr;
    }

    (void)behind;
    (void)asset;

    int width  = context.width;
    int height = context.height;
    if (width <= 0 || height <= 0) {
        if (SDL_Texture* base = context.base_texture) {
            SDL_QueryTexture(base, nullptr, nullptr, &width, &height);
            context.width  = width;
            context.height = height;
        }
    }

    if (width <= 0 || height <= 0) {
        return nullptr;
    }

    SDL_Texture* texture = texture_override;
    if (!texture) {
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
        if (!texture) {
            return nullptr;
        }
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    SDL_SetRenderTarget(renderer, prev_target);
    return texture;
}

}

