#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include <SDL.h>

namespace render_pipeline {

struct ScaleSelection {
    int   index           = 0;
    float requested_scale = 1.0f;
    float stored_scale    = 1.0f;
    float remainder_scale = 1.0f;
};

struct ScalingLogic {
    static constexpr std::size_t kVariantCount = 10;
    static constexpr std::array<float, kVariantCount> kScaleSteps = {
        1.0f, 0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f
    };

    static inline float ComputeScale(int base_w, int base_h, int target_w, int target_h) {
        if (base_w <= 0 || base_h <= 0 || target_w <= 0 || target_h <= 0) {
            return 1.0f;
        }
        const float scale_w = static_cast<float>(target_w) / static_cast<float>(base_w);
        const float scale_h = static_cast<float>(target_h) / static_cast<float>(base_h);
        return (scale_w < scale_h) ? scale_w : scale_h;
    }

    static inline ScaleSelection Choose(float desired_scale) {
        ScaleSelection result{};
        if (!std::isfinite(desired_scale)) {
            desired_scale = 1.0f;
        }
        if (desired_scale <= 0.0f) {
            desired_scale = kScaleSteps.back();
        }

        result.requested_scale = desired_scale;

        float best_diff = std::numeric_limits<float>::max();
        float chosen_scale = kScaleSteps.front();
        int   chosen_index = 0;

        for (std::size_t i = 0; i < kVariantCount; ++i) {
            const float candidate = kScaleSteps[i];
            const float diff = std::fabs(candidate - desired_scale);
            if (diff < best_diff - 1e-4f) {
                best_diff    = diff;
                chosen_scale = candidate;
                chosen_index = static_cast<int>(i);
            } else if (std::fabs(diff - best_diff) <= 1e-4f && candidate > chosen_scale) {
                chosen_scale = candidate;
                chosen_index = static_cast<int>(i);
            }
        }

        result.index        = chosen_index;
        result.stored_scale = chosen_scale;
        result.remainder_scale = (chosen_scale > 0.0f) ? (desired_scale / chosen_scale) : 1.0f;
        return result;
    }
};

inline SDL_Texture* CreateScaledTexture(SDL_Renderer* renderer,
                                        SDL_Texture* source,
                                        int src_w,
                                        int src_h,
                                        float scale) {
    if (!renderer || !source || scale <= 0.0f) {
        return nullptr;
    }

    const int dst_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(src_w) * scale)));
    const int dst_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(src_h) * scale)));

    if (dst_w == src_w && dst_h == src_h) {
        return nullptr;
    }

    Uint32 format = SDL_PIXELFORMAT_RGBA8888;
    if (SDL_QueryTexture(source, &format, nullptr, nullptr, nullptr) != 0) {
        format = SDL_PIXELFORMAT_RGBA8888;
    }

    SDL_Texture* scaled = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_TARGET, dst_w, dst_h);
    if (!scaled) {
        return nullptr;
    }

    SDL_SetTextureBlendMode(scaled, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(scaled, SDL_ScaleModeBest);
#endif

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, scaled);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    SDL_Rect dst{0, 0, dst_w, dst_h};
    SDL_RenderCopy(renderer, source, nullptr, &dst);

    SDL_SetRenderTarget(renderer, previous_target);
    return scaled;
}

}  // namespace render_pipeline

