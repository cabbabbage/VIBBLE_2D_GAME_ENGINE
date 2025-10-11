#include "render_asset.hpp"

#include "asset/Asset.hpp"
#include "render_pipeline/ScalingLogic.hpp"

#include <algorithm>
#include <cmath>

RenderAsset::RenderAsset(SDL_Renderer* renderer)
: renderer_(renderer) {}

SDL_Texture* RenderAsset::texture_for_scale(Asset* asset,
                                            SDL_Texture* base_tex,
                                            int base_w,
                                            int base_h,
                                            int target_w,
                                            int target_h) {
        if (!asset || !base_tex || base_w <= 0 || base_h <= 0 || target_w <= 0 || target_h <= 0) {
                return base_tex;
        }

        const float desired_scale = render_pipeline::ScalingLogic::ComputeScale(base_w, base_h, target_w, target_h);
        const render_pipeline::ScaleSelection selection = render_pipeline::ScalingLogic::Choose(desired_scale);

        if (selection.index <= 0 || selection.stored_scale >= 0.995f) {
                return base_tex;
        }

        if (static_cast<std::size_t>(selection.index) >= asset->downscale_cache_.size()) {
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
                        return base_tex;
                }
                entry.texture = scaled;
                entry.width   = expected_w;
                entry.height  = expected_h;
                entry.scale   = selection.stored_scale;
        }

        return entry.texture ? entry.texture : base_tex;
}
