#include "render_asset.hpp"

#include "asset/Asset.hpp"

#include <algorithm>
#include <cmath>

RenderAsset::RenderAsset(SDL_Renderer* renderer)
: renderer_(renderer) {}

namespace {

SDL_Texture* create_half_scale(SDL_Renderer* renderer,
                               SDL_Texture* source,
                               Uint32 format,
                               int src_w,
                               int src_h) {
        if (!renderer || !source || src_w <= 0 || src_h <= 0) {
                return nullptr;
        }
        int dst_w = std::max(1, src_w / 2);
        int dst_h = std::max(1, src_h / 2);
        SDL_Texture* half = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_TARGET, dst_w, dst_h);
        if (!half) {
                return nullptr;
        }
        SDL_SetTextureBlendMode(half, SDL_BLENDMODE_BLEND);
        #if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(half, SDL_ScaleModeBest);
        #endif
        SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
        SDL_SetRenderTarget(renderer, half);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        SDL_Rect dst{0, 0, dst_w, dst_h};
        SDL_RenderCopy(renderer, source, nullptr, &dst);
        SDL_SetRenderTarget(renderer, prev_target);
        return half;
}

}

SDL_Texture* RenderAsset::texture_for_scale(Asset* asset,
                                            SDL_Texture* base_tex,
                                            int base_w,
                                            int base_h,
                                            int target_w,
                                            int target_h) {
        if (!asset || !base_tex || base_w <= 0 || base_h <= 0 || target_w <= 0 || target_h <= 0) {
                return base_tex;
        }

        const float ratio_w = static_cast<float>(target_w) / static_cast<float>(base_w);
        const float ratio_h = static_cast<float>(target_h) / static_cast<float>(base_h);
        const float ratio = std::min(ratio_w, ratio_h);
        if (ratio >= 0.95f) {
                return base_tex;
        }

        int levels = 0;
        int preview_w = base_w;
        int preview_h = base_h;
        float working_ratio = ratio;
        const int   kMaxLevels    = 4;
        const float kTargetRatio  = 0.55f;
        while (working_ratio < kTargetRatio && preview_w > 1 && preview_h > 1 && levels < kMaxLevels) {
                working_ratio *= 2.0f;
                preview_w = std::max(1, preview_w / 2);
                preview_h = std::max(1, preview_h / 2);
                ++levels;
        }

        if (levels <= 0) {
                return base_tex;
        }

        Uint32 format = SDL_PIXELFORMAT_RGBA8888;
        if (SDL_QueryTexture(base_tex, &format, nullptr, nullptr, nullptr) != 0) {
                format = SDL_PIXELFORMAT_RGBA8888;
        }

        SDL_Texture* current_tex = base_tex;
        int          current_w   = base_w;
        int          current_h   = base_h;
        float        current_scale = 1.0f;

        for (int level = 0; level < levels; ++level) {
                float next_scale = current_scale * 0.5f;
                const int next_w = std::max(1, current_w / 2);
                const int next_h = std::max(1, current_h / 2);

                auto it = std::find_if(asset->downscale_cache_.begin(),
                                       asset->downscale_cache_.end(),
                                       [next_scale](const Asset::DownscaleCacheEntry& entry) {
                                               return std::abs(entry.scale - next_scale) <= 0.0001f;
                                       });

                if (it == asset->downscale_cache_.end() || !it->texture || it->width != next_w || it->height != next_h) {
                        SDL_Texture* created = create_half_scale(renderer_, current_tex, format, current_w, current_h);
                        if (!created) {
                                return (level == 0) ? base_tex : current_tex;
                        }
                        Asset::DownscaleCacheEntry entry;
                        entry.scale   = next_scale;
                        entry.width   = next_w;
                        entry.height  = next_h;
                        entry.texture = created;
                        if (it == asset->downscale_cache_.end()) {
                                asset->downscale_cache_.push_back(entry);
                                it = asset->downscale_cache_.begin() + (asset->downscale_cache_.size() - 1);
                        } else {
                                if (it->texture) {
                                        SDL_DestroyTexture(it->texture);
                                }
                                *it = entry;
                        }
                }

                current_tex   = it->texture;
                current_w     = it->width;
                current_h     = it->height;
                current_scale = next_scale;
        }

        return current_tex ? current_tex : base_tex;
}
