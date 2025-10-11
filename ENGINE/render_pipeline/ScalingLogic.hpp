#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>

#include <SDL.h>
#include <filesystem>

namespace render_pipeline {

struct ScaleSelection {
    int   index           = 0;
    float requested_scale = 1.0f;
    float stored_scale    = 1.0f;
    float remainder_scale = 1.0f;
};

struct ScalingLogic {
    static constexpr std::size_t kVariantCount = 20;
    static constexpr std::array<float, kVariantCount> kScaleSteps = {
        1.0f, 0.95f, 0.90f, 0.85f, 0.80f,
        0.75f, 0.70f, 0.65f, 0.60f, 0.55f,
        0.50f, 0.45f, 0.40f, 0.35f, 0.30f,
        0.25f, 0.20f, 0.15f, 0.10f, 0.05f
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

    static inline int ScalePercent(std::size_t index) {
        if (index >= kVariantCount) {
            return 0;
        }
        return static_cast<int>(std::lround(kScaleSteps[index] * 100.0f));
    }

    static inline std::string VariantFolder(const std::string& base, std::size_t index) {
        return std::filesystem::path(base)
            .append("scale_" + std::to_string(ScalePercent(index)))
            .string();
    }

    static inline std::array<int, kVariantCount> PercentSteps() {
        std::array<int, kVariantCount> percents{};
        for (std::size_t i = 0; i < kVariantCount; ++i) {
            percents[i] = ScalePercent(i);
        }
        return percents;
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

inline SDL_Surface* CreateScaledSurface(SDL_Surface* src, float scale) {
    if (!src || scale <= 0.0f) {
        return nullptr;
    }

    if (std::fabs(scale - 1.0f) <= 1e-4f) {
        SDL_Surface* copy = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_RGBA8888);
        if (!copy) {
            return nullptr;
        }
        SDL_Rect rect{0, 0, src->w, src->h};
        if (SDL_BlitSurface(src, &rect, copy, &rect) != 0) {
            SDL_FreeSurface(copy);
            return nullptr;
        }
        return copy;
    }

    const int dst_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(src->w) * scale)));
    const int dst_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(src->h) * scale)));

    SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, dst_w, dst_h, 32, SDL_PIXELFORMAT_RGBA8888);
    if (!dst) {
        return nullptr;
    }

    SDL_Rect src_rect{0, 0, src->w, src->h};
    SDL_Rect dst_rect{0, 0, dst_w, dst_h};
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    if (SDL_BlitScaled(src, &src_rect, dst, &dst_rect) != 0) {
        SDL_FreeSurface(dst);
        return nullptr;
    }

    return dst;
}

}  // namespace render_pipeline
