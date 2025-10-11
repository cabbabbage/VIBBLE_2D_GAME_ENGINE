#include "render_asset.hpp"

#include "asset/Asset.hpp"
#include "render_pipeline/ScalingLogic.hpp"

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
                if (asset) {
                        asset->update_scale_usage(1.0f, 1.0f, 1.0f, 0);
                }
                return base_tex;
        }

        const float desired_scale = render_pipeline::ScalingLogic::ComputeScale(base_w, base_h, target_w, target_h);
        const render_pipeline::ScaleSelection selection = render_pipeline::ScalingLogic::Choose(desired_scale);

        if (selection.index <= 0 || selection.stored_scale >= 0.995f) {
                asset->update_scale_usage(desired_scale, 1.0f, desired_scale, 0);
                return base_tex;
        }

        if (static_cast<std::size_t>(selection.index) >= asset->downscale_cache_.size()) {
                asset->update_scale_usage(desired_scale, 1.0f, desired_scale, 0);
                return base_tex;
        }

        Asset::DownscaleCacheEntry& entry = asset->downscale_cache_[selection.index];
        const int expected_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(base_w) * selection.stored_scale)));
        const int expected_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(base_h) * selection.stored_scale)));

        const bool needs_rebuild =
            !entry.texture ||
            entry.width  != expected_w ||
            entry.height != expected_h ||
            std::fabs(entry.scale - selection.stored_scale) > 0.0001f;

        if (needs_rebuild) {
                if (entry.texture) {
                        SDL_DestroyTexture(entry.texture);
                        entry.texture = nullptr;
                }
                SDL_Texture* scaled = render_pipeline::CreateScaledTexture(renderer_, base_tex, base_w, base_h, selection.stored_scale);
                if (!scaled) {
                        entry.texture = nullptr;
                        entry.width   = 0;
                        entry.height  = 0;
                        entry.scale   = selection.stored_scale;
                        asset->update_scale_usage(desired_scale, 1.0f, desired_scale, 0);
                        return base_tex;
                }
                entry.texture = scaled;
                entry.width   = expected_w;
                entry.height  = expected_h;
                entry.scale   = selection.stored_scale;
        }

        SDL_Texture* result = entry.texture ? entry.texture : base_tex;
        const bool using_cached = (result == entry.texture && result != nullptr);
        const float texture_scale = using_cached ? selection.stored_scale : 1.0f;
        const float remainder_scale = using_cached ? selection.remainder_scale : desired_scale;
        const int variant_index = using_cached ? selection.index : 0;
        asset->update_scale_usage(desired_scale, texture_scale, remainder_scale, variant_index);
        return result ? result : base_tex;
}
