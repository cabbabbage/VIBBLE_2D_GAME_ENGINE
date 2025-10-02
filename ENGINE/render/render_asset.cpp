#include "render_asset.hpp"
#include "global_light_source.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "core/AssetsManager.hpp"
#include "utils/light_utils.hpp"
#include "render/camera.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include <iostream>

RenderAsset::RenderAsset(SDL_Renderer* renderer,
                         Assets* assets,
                         camera& cam,
                         Global_Light_Source& main_light,
                         Asset* player)
: renderer_(renderer),
assets_(assets),
cam_(cam),
main_light_source_(main_light),
p(player) {}

SDL_Texture* RenderAsset::render_shadow_mask(Asset* a, int bw, int bh) {
	SDL_Texture* mask = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, bw, bh);
	if (!mask) return nullptr;
	SDL_SetTextureBlendMode(mask, SDL_BLENDMODE_BLEND);
	SDL_Texture* prev_target = SDL_GetRenderTarget(renderer_);
	SDL_SetRenderTarget(renderer_, mask);
	SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 0);
	SDL_RenderClear(renderer_);
	if (SDL_Texture* base = a->get_current_frame()) {
		SDL_SetTextureBlendMode(base, SDL_BLENDMODE_BLEND);
		SDL_SetTextureColorMod(base, 0, 0, 0);
		SDL_RenderCopy(renderer_, base, nullptr, nullptr);
		SDL_SetTextureColorMod(base, 255, 255, 255);
	}
        const camera::RenderEffects effects =
            cam_.compute_render_effects(SDL_Point{a->pos.x, a->pos.y}, 0.0f, 0.0f);
        SDL_Point parallax_pos = effects.screen_position;
	SDL_Rect bounds{ parallax_pos.x - bw / 2, parallax_pos.y - bh, bw, bh };
	const Uint8 light_alpha = static_cast<Uint8>(main_light_source_.get_brightness());
	render_shadow_received_static_lights(a, bounds, light_alpha);
	render_shadow_moving_lights(a, bounds, light_alpha);
	const Uint8 main_alpha = main_light_source_.get_current_color().a;
	render_shadow_orbital_lights(a, bounds, main_alpha);
	SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_MOD);
	SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 204);
	SDL_RenderFillRect(renderer_, nullptr);
	SDL_SetRenderTarget(renderer_, prev_target);
	return mask;
}

SDL_Texture* RenderAsset::regenerateFinalTexture(Asset* a) {
        if (!a) return nullptr;
        SDL_Texture* base = a->get_current_frame();
        if (!base) return nullptr;

        int bw = a->cached_w, bh = a->cached_h;
        if (bw == 0 || bh == 0) {
                SDL_QueryTexture(base, nullptr, nullptr, &bw, &bh);
        }

        SDL_Texture* existing_final = a->get_final_texture();
        SDL_Texture* final_tex      = existing_final;
        bool          reuse_texture = true;

        if (final_tex) {
                int query_w = 0, query_h = 0;
                Uint32 fmt = 0; int access = 0;
                if (SDL_QueryTexture(final_tex, &fmt, &access, &query_w, &query_h) != 0 ||
                    access != SDL_TEXTUREACCESS_TARGET || query_w != bw || query_h != bh) {
                        reuse_texture = false;
                }
        } else {
                reuse_texture = false;
        }

        if (!reuse_texture) {
                final_tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                              SDL_TEXTUREACCESS_TARGET, bw, bh);
                if (!final_tex) {
                        return nullptr;
                }
        }

        const bool low_quality = assets_ && assets_->is_dev_mode();

        SDL_SetTextureBlendMode(final_tex, SDL_BLENDMODE_BLEND);
        #if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(
            final_tex,
            low_quality ? SDL_ScaleModeNearest
                        : ((a && a->info && !a->info->smooth_scaling) ? SDL_ScaleModeNearest : SDL_ScaleModeBest));
        #endif

        a->clear_downscale_cache();

        SDL_Texture* prev_target = SDL_GetRenderTarget(renderer_);
        SDL_SetRenderTarget(renderer_, final_tex);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
        SDL_RenderClear(renderer_);

        const Uint8 main_alpha = main_light_source_.get_current_color().a;
        const float  c         = a->alpha_percentage;
        int alpha_mod = (c >= 1.0f) ? 255 : int(main_alpha * c);
        if (a->info->type == asset_types::player) {
                alpha_mod = std::min(255, alpha_mod * 3);
        }

        SDL_SetTextureColorMod(base, 255, 255, 255);
        SDL_RenderCopy(renderer_, base, nullptr, nullptr);
        SDL_SetTextureColorMod(base, 255, 255, 255);

        if (a->has_shading && !low_quality) {
                if (SDL_Texture* mask = render_shadow_mask(a, bw, bh)) {
                        SDL_SetRenderTarget(renderer_, final_tex);
                        SDL_SetTextureBlendMode(mask, SDL_BLENDMODE_MOD);
                        SDL_RenderCopy(renderer_, mask, nullptr, nullptr);
                        SDL_DestroyTexture(mask);
                }
        }

        SDL_SetRenderTarget(renderer_, prev_target);
        a->cached_w = bw;
        a->cached_h = bh;
        return final_tex;
}

namespace {

SDL_Texture* create_half_scale(SDL_Renderer* renderer,
                               SDL_Texture* source,
                               Uint32 format,
                               int src_w,
                               int src_h,
                               bool low_quality) {
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
        SDL_SetTextureScaleMode(half, low_quality ? SDL_ScaleModeNearest : SDL_ScaleModeBest);
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

} // namespace

SDL_Texture* RenderAsset::texture_for_scale(Asset* asset,
                                            SDL_Texture* base_tex,
                                            int base_w,
                                            int base_h,
                                            int target_w,
                                            int target_h,
                                            float camera_scale) {
        if (!asset || !base_tex || base_w <= 0 || base_h <= 0 || target_w <= 0 || target_h <= 0) {
                return base_tex;
        }

        if (asset->last_scaled_texture_ && asset->last_scaled_source_ == base_tex &&
            asset->last_scaled_w_ == target_w && asset->last_scaled_h_ == target_h &&
            std::abs(asset->last_scaled_camera_scale_ - camera_scale) <= 0.0001f) {
                return asset->last_scaled_texture_;
        }

        auto remember_result = [&](SDL_Texture* tex) {
                asset->last_scaled_source_       = base_tex;
                asset->last_scaled_texture_      = tex;
                asset->last_scaled_w_            = target_w;
                asset->last_scaled_h_            = target_h;
                asset->last_scaled_camera_scale_ = camera_scale;
                return tex ? tex : base_tex;
        };

        const bool low_quality = assets_ && assets_->is_dev_mode();
        if (low_quality) {
                return remember_result(base_tex);
        }

        const float ratio_w = static_cast<float>(target_w) / static_cast<float>(base_w);
        const float ratio_h = static_cast<float>(target_h) / static_cast<float>(base_h);
        float ratio = std::min(ratio_w, ratio_h);
        if (camera_scale > 2.0f) {
                const float extra_zoom = std::min(camera_scale - 2.0f, 10.0f);
                const float bias = static_cast<float>(std::exp2(-extra_zoom));
                ratio *= std::max(0.0001f, bias);
        }
        if (ratio >= 0.95f) {
                return remember_result(base_tex);
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
                return remember_result(base_tex);
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
                        SDL_Texture* created = create_half_scale(renderer_, current_tex, format, current_w, current_h, low_quality);
                        if (!created) {
                                SDL_Texture* fallback = (level == 0) ? base_tex : current_tex;
                                return remember_result(fallback);
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

        return remember_result(current_tex ? current_tex : base_tex);
}

void RenderAsset::render_shadow_moving_lights(Asset* a, const SDL_Rect& bounds, Uint8 alpha) {
	if (!p || !p->info || !a) return;
	for (auto& light : p->info->light_sources) {
		if (!light.texture) continue;
		const int world_lx = p->pos.x + light.offset_x;
		const int world_ly = p->pos.y + light.offset_y;
		const double factor = LightUtils::calculate_static_alpha_percentage(a, p);
		const Uint8 inten = static_cast<Uint8>(alpha * factor);

		// Compute local (asset-space) placement so zoom/parallax do not affect the mask.
		const int dx_world = world_lx - a->pos.x;
		const int dy_world = world_ly - a->pos.y;

		int lw = light.cached_w, lh = light.cached_h;
		if (lw == 0 || lh == 0) {
			SDL_QueryTexture(light.texture, nullptr, nullptr, &lw, &lh);
			light.cached_w = lw;
			light.cached_h = lh;
		}

		const int bw = bounds.w;
		const int bh = bounds.h;
		SDL_Rect dst{
			// Anchor is bottom-center of the asset in mask space.
			(bw / 2) + dx_world - (lw / 2),
			bh + dy_world - (lh / 2),
			lw,
			lh
		};

		SDL_SetTextureBlendMode(light.texture, SDL_BLENDMODE_ADD);
		SDL_SetTextureAlphaMod(light.texture, inten);
		SDL_RenderCopy(renderer_, light.texture, nullptr, &dst);
		SDL_SetTextureAlphaMod(light.texture, 255);
	}
}

void RenderAsset::render_shadow_orbital_lights(Asset* a, const SDL_Rect& bounds, Uint8 alpha) {
	if (!a || !a->info) return;
	const float angle = main_light_source_.get_angle();
	for (auto& light : a->info->orbital_light_sources) {
		if (!light.texture || light.x_radius <= 0 || light.y_radius <= 0) continue;
		const bool flipped = a->flipped;
		const float offset_x = flipped ? -static_cast<float>(light.offset_x) : static_cast<float>(light.offset_x);
		float orbit_x = std::cos(angle) * light.x_radius;
		if (flipped) orbit_x = -orbit_x;
		const float lx = static_cast<float>(a->pos.x) + offset_x + orbit_x;
		const float ly = static_cast<float>(a->pos.y) + light.offset_y - std::sin(angle) * light.y_radius;

		// Local (asset-space) offset to keep path/look invariant with zoom.
		const int dx_world = static_cast<int>(std::lround(lx)) - a->pos.x;
		const int dy_world = static_cast<int>(std::lround(ly)) - a->pos.y;

		int lw = light.cached_w, lh = light.cached_h;
		if (lw == 0 || lh == 0) {
			SDL_QueryTexture(light.texture, nullptr, nullptr, &lw, &lh);
			light.cached_w = lw;
			light.cached_h = lh;
		}

		const int bw = bounds.w;
		const int bh = bounds.h;
		SDL_Rect dst{
			(bw / 2) + dx_world - (lw / 2),
			bh + dy_world - (lh / 2),
			lw,
			lh
		};

		SDL_SetTextureBlendMode(light.texture, SDL_BLENDMODE_ADD);
		SDL_SetTextureAlphaMod(light.texture, alpha);
		SDL_RenderCopy(renderer_, light.texture, nullptr, &dst);
	}
}

void RenderAsset::render_shadow_received_static_lights(Asset* a, const SDL_Rect& bounds, Uint8 alpha) {
	if (!a) return;
	static std::mt19937 flicker_rng{ std::random_device{}() };
	for (const auto& sl : a->static_lights) {
		if (!sl.source || !sl.source->texture) continue;

		// Local (asset-space) offset of this static light contribution.
		const int dx_world = sl.offset.x;
		const int dy_world = sl.offset.y;

		int lw = sl.source->cached_w, lh = sl.source->cached_h;
		if (lw == 0 || lh == 0) {
			SDL_QueryTexture(sl.source->texture, nullptr, nullptr, &lw, &lh);
			sl.source->cached_w = lw;
			sl.source->cached_h = lh;
		}

		const int bw = bounds.w;
		const int bh = bounds.h;
		SDL_Rect dst{
			(bw / 2) + dx_world - (lw / 2),
			bh + dy_world - (lh / 2),
			lw,
			lh
		};

		SDL_SetTextureBlendMode(sl.source->texture, SDL_BLENDMODE_ADD);
		float base_alpha = static_cast<float>(alpha) * sl.alpha_percentage;
		if (sl.source->flicker > 0) {
			const float brightness_scale = std::clamp(sl.source->intensity / 255.0f, 0.0f, 1.0f);
			const float max_jitter = (sl.source->flicker / 100.0f) * brightness_scale;
			std::uniform_real_distribution<float> dist(-max_jitter, max_jitter);
			base_alpha *= (1.0f + dist(flicker_rng));
		}
		SDL_SetTextureAlphaMod(sl.source->texture, static_cast<Uint8>(std::clamp(base_alpha, 0.0f, 255.0f)));
		SDL_RenderCopy(renderer_, sl.source->texture, nullptr, &dst);
	}
}
