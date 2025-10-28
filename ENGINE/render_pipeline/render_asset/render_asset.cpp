#include "render_asset.hpp"

#include "asset/Asset.hpp"
#include "render_pipeline/ScalingLogic.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

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

bool rerender_scaled_texture(SDL_Renderer* renderer,
                             SDL_Texture* destination,
                             SDL_Texture* source,
                             int dst_w,
                             int dst_h)
{
        if (!renderer || !destination || !source || dst_w <= 0 || dst_h <= 0) {
                return false;
        }

        SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
        if (SDL_SetRenderTarget(renderer, destination) != 0) {
                SDL_SetRenderTarget(renderer, previous_target);
                return false;
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        if (SDL_RenderClear(renderer) != 0) {
                SDL_SetRenderTarget(renderer, previous_target);
                return false;
        }

        SDL_Rect dst{0, 0, dst_w, dst_h};
        const int copy_result = SDL_RenderCopy(renderer, source, nullptr, &dst);

        SDL_SetRenderTarget(renderer, previous_target);
        return copy_result == 0;
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
                        asset->update_scale_usage(1.0f,
                                                  1.0f,
                                                  1.0f,
                                                  0,
                                                  0.0f,
                                                  std::numeric_limits<float>::max());
                }
                return base_tex;
        }

        const float desired_scale = render_pipeline::ScalingLogic::ComputeScale(base_w, base_h, target_w, target_h);
        const auto& scale_steps = (asset->info && !asset->info->scale_variants.empty()) ? static_cast<const std::vector<float>&>(asset->info->scale_variants) : render_pipeline::ScalingLogic::DefaultScaleSteps();

        if (asset->downscale_cache_.size() != scale_steps.size()) {
                asset->clear_downscale_cache();
        }

        render_pipeline::ScalingLogic::HysteresisState hysteresis_state{};
        const auto& variant_state = asset->scale_variant_state();
        hysteresis_state.last_index = variant_state.last_variant_index;
        hysteresis_state.min_scale  = variant_state.hysteresis_min;
        hysteresis_state.max_scale  = variant_state.hysteresis_max;

        render_pipeline::ScalingLogic::HysteresisOptions hysteresis_options{};
        hysteresis_options.margin         = render_pipeline::ScalingLogic::kDefaultHysteresisMargin;
        hysteresis_options.preload_margin = render_pipeline::ScalingLogic::kDefaultPreloadMargin;

        const float smoothed_scale = asset->smoothed_scale();
        const render_pipeline::ScaleSelection selection = render_pipeline::ScalingLogic::Choose(
            desired_scale,
            scale_steps,
            hysteresis_state,
            smoothed_scale,
            hysteresis_options);

        auto compute_bounds = [&](int index) {
                if (scale_steps.empty()) {
                        return std::make_pair(0.0f, std::numeric_limits<float>::max());
                }
                const float margin = hysteresis_options.margin;
                const int clamped_index = std::clamp(index, 0, static_cast<int>(scale_steps.size() - 1));
                const float step_value  = scale_steps[clamped_index];
                float min_bound = 0.0f;
                float max_bound = std::numeric_limits<float>::max();
                if (clamped_index + 1 < static_cast<int>(scale_steps.size())) {
                        const float boundary = 0.5f * (step_value + scale_steps[clamped_index + 1]);
                        min_bound = std::max(0.0f, boundary - margin);
                }
                if (clamped_index > 0) {
                        const float boundary = 0.5f * (step_value + scale_steps[clamped_index - 1]);
                        max_bound = boundary + margin;
                }
                if (min_bound > max_bound) {
                        const float midpoint = 0.5f * (min_bound + max_bound);
                        min_bound            = std::min(min_bound, midpoint);
                        max_bound            = std::max(max_bound, midpoint);
                }
                return std::make_pair(min_bound, max_bound);
        };

        auto ensure_downscale_entry = [&](int index, float scale_value) -> Asset::DownscaleCacheEntry* {
                if (index <= 0 || static_cast<std::size_t>(index) >= asset->downscale_cache_.size()) {
                        return nullptr;
                }
                Asset::DownscaleCacheEntry& entry = asset->downscale_cache_[index];
                const int expected_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(base_w) * scale_value)));
                const int expected_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(base_h) * scale_value)));
                const bool needs_rebuild =
                    !entry.texture ||
                    entry.width  != expected_w ||
                    entry.height != expected_h ||
                    std::fabs(entry.scale - scale_value) > 0.0001f;

                if (needs_rebuild) {
                        if (entry.texture) {
                                SDL_DestroyTexture(entry.texture);
                                entry.texture = nullptr;
                        }
                        SDL_Texture* scaled = render_pipeline::CreateScaledTexture(renderer_, base_tex, base_w, base_h, scale_value);
                        if (!scaled) {
                                entry.texture  = nullptr;
                                entry.width    = 0;
                                entry.height   = 0;
                                entry.scale    = scale_value;
                                entry.revision = 0;
                                return nullptr;
                        }
                        entry.texture  = scaled;
                        entry.width    = expected_w;
                        entry.height   = expected_h;
                        entry.scale    = scale_value;
                        entry.revision = asset->final_texture_revision_;
                } else if (entry.revision != asset->final_texture_revision_) {
                        if (!rerender_scaled_texture(renderer_, entry.texture, base_tex, expected_w, expected_h)) {
                                SDL_DestroyTexture(entry.texture);
                                entry.texture  = nullptr;
                                entry.width    = 0;
                                entry.height   = 0;
                                entry.scale    = scale_value;
                                entry.revision = 0;

                                SDL_Texture* scaled = render_pipeline::CreateScaledTexture(renderer_, base_tex, base_w, base_h, scale_value);
                                if (!scaled) {
                                        return nullptr;
                                }

                                entry.texture  = scaled;
                                entry.width    = expected_w;
                                entry.height   = expected_h;
                                entry.scale    = scale_value;
                        }

                        entry.width    = expected_w;
                        entry.height   = expected_h;
                        entry.scale    = scale_value;
                        entry.revision = asset->final_texture_revision_;
                }

                return entry.texture ? &entry : nullptr;
        };

        if (selection.preload_index > 0 && static_cast<std::size_t>(selection.preload_index) < scale_steps.size()) {
                ensure_downscale_entry(selection.preload_index, scale_steps[selection.preload_index]);
        }

        SDL_Texture* result          = base_tex;
        float        texture_scale   = 1.0f;
        float        remainder_scale = desired_scale;
        int          variant_index   = 0;
        auto         default_bounds  = compute_bounds(0);
        float        hysteresis_min  = default_bounds.first;
        float        hysteresis_max  = default_bounds.second;

        const bool can_use_variant =
            selection.index > 0 &&
            selection.stored_scale < 0.995f &&
            static_cast<std::size_t>(selection.index) < scale_steps.size();

        if (can_use_variant) {
                if (Asset::DownscaleCacheEntry* entry = ensure_downscale_entry(selection.index, selection.stored_scale)) {
                        if (entry->texture) {
                                result          = entry->texture;
                                texture_scale   = selection.stored_scale;
                                remainder_scale = selection.remainder_scale;
                                variant_index   = selection.index;
                                hysteresis_min  = selection.hysteresis_min;
                                hysteresis_max  = selection.hysteresis_max;
                        }
                }
        } else {
                hysteresis_min = selection.hysteresis_min;
                hysteresis_max = selection.hysteresis_max;
        }

        if (variant_index == 0) {
                auto bounds = compute_bounds(0);
                hysteresis_min = bounds.first;
                hysteresis_max = bounds.second;
        }

        asset->update_scale_usage(desired_scale,
                                  texture_scale,
                                  remainder_scale,
                                  variant_index,
                                  hysteresis_min,
                                  hysteresis_max);
        return result ? result : base_tex;
}
