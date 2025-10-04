#include "light_map.hpp"
#include "render/camera.hpp"
#include <algorithm>
#include <random>
#include <vector>
#include <iostream>
#include <cmath>
LightMap::LightMap(SDL_Renderer* renderer,
                   Assets* assets,
                   Global_Light_Source& main_light,
                   int screen_width,
                   int screen_height,
                   SDL_Texture* fullscreen_light_tex)
: renderer_(renderer),
assets_(assets),
main_light_(main_light),
screen_width_(screen_width),
screen_height_(screen_height),
fullscreen_light_tex_(fullscreen_light_tex),
lowres_mask_tex_(nullptr),
lowres_w_(0),
lowres_h_(0)
{}

LightMap::~LightMap() {
        if (lowres_mask_tex_) {
                SDL_DestroyTexture(lowres_mask_tex_);
                lowres_mask_tex_ = nullptr;
                lowres_w_ = 0;
                lowres_h_ = 0;
        }
}

void LightMap::render(bool debugging) {
	if (debugging) std::cout << "[render_asset_lights_z] start\n";
	static std::mt19937 flicker_rng{ std::random_device{}() };
	static std::vector<LightEntry> z_lights;
	z_lights.clear();
	collect_layers(z_lights, flicker_rng);
        const int downscale = 4;
        const int low_w = std::max(1, screen_width_  / downscale);
        const int low_h = std::max(1, screen_height_ / downscale);
        SDL_Texture* prev_target = SDL_GetRenderTarget(renderer_);
        SDL_Texture* lowres_mask = build_lowres_mask(z_lights, low_w, low_h, downscale);
        if (lowres_mask) {
                SDL_SetTextureBlendMode(lowres_mask, SDL_BLENDMODE_MOD);

                SDL_SetRenderTarget(renderer_, prev_target);
                SDL_RenderCopy(renderer_, lowres_mask, nullptr, nullptr);
        } else {
                SDL_SetRenderTarget(renderer_, prev_target);
        }
        if (debugging) std::cout << "[render_asset_lights_z] end\n";
}

void LightMap::collect_layers(std::vector<LightEntry>& out, std::mt19937& rng) {
	const float inv_scale = 1.0f / assets_->getView().get_scale();
	constexpr int min_visible_w = 1;
	constexpr int min_visible_h = 1;
	Uint8 main_alpha = main_light_.get_current_color().a;
        const auto& active = assets_->getFilteredActiveAssets();
        if (out.capacity() < active.size() + 3) {
                out.reserve(active.size() + 3);
        }
	if (fullscreen_light_tex_) {
		out.push_back({ fullscreen_light_tex_, { 0, 0, screen_width_, screen_height_ },
			static_cast<Uint8>(main_alpha / 2), SDL_FLIP_NONE, false });
	}
	if (SDL_Texture* map_tex = main_light_.get_texture()) {
		int lw = main_light_.get_cached_w();
		int lh = main_light_.get_cached_h();
		if (lw == 0 || lh == 0) SDL_QueryTexture(map_tex, nullptr, nullptr, &lw, &lh);
		SDL_Rect map_rect = get_scaled_position_rect(main_light_.get_position(), lw, lh, inv_scale, min_visible_w, min_visible_h);
		if (map_rect.w != 0 || map_rect.h != 0) {
			out.push_back({ map_tex, map_rect, main_alpha, SDL_FLIP_NONE, false });
		}
	}
        const float main_brightness = static_cast<float>(main_light_.get_brightness());
        for (Asset* a : active) {
                if (!a || !a->info || !a->info->is_light_source) continue;
                for (auto& light : a->info->light_sources) {
                        if (!light.texture) continue;
                        int offX = a->flipped ? -light.offset_x : light.offset_x;
                        int lw = light.cached_w, lh = light.cached_h;
                        if (lw == 0 || lh == 0) {
                                SDL_QueryTexture(light.texture, nullptr, nullptr, &lw, &lh);
                                light.cached_w = lw;
                                light.cached_h = lh;
                        }
                        SDL_Rect dst = get_scaled_position_rect(SDL_Point{ a->pos.x + offX, a->pos.y + light.offset_y },
                                           lw, lh, inv_scale,
                                           min_visible_w, min_visible_h);
                        if (dst.w == 0 && dst.h == 0) continue;
                        float alpha_f = main_brightness;
                        if (a == assets_->player) alpha_f *= 0.9f;
                        if (light.flicker > 0) {
                                        float intensity_scale = std::clamp(light.intensity / 255.0f, 0.0f, 1.0f);
                                        float max_jitter = (light.flicker / 100.0f) * intensity_scale;
                                        alpha_f *= (1.0f + std::uniform_real_distribution<float>(-max_jitter, max_jitter)(rng));
                        }
                        Uint8 alpha = static_cast<Uint8>(std::clamp(alpha_f, 0.0f, 255.0f));
                        out.push_back({ light.texture, dst, alpha,
                 a->flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE, true });
                }
        }
}

SDL_Texture* LightMap::ensure_lowres_target(int low_w, int low_h) {
        if (low_w <= 0 || low_h <= 0) {
                return nullptr;
        }
        if (lowres_mask_tex_ && (lowres_w_ != low_w || lowres_h_ != low_h)) {
                SDL_DestroyTexture(lowres_mask_tex_);
                lowres_mask_tex_ = nullptr;
        }
        if (!lowres_mask_tex_) {
                lowres_mask_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, low_w, low_h);
                if (!lowres_mask_tex_) {
                        lowres_w_ = 0;
                        lowres_h_ = 0;
                        return nullptr;
                }
                SDL_SetTextureBlendMode(lowres_mask_tex_, SDL_BLENDMODE_NONE);
#if SDL_VERSION_ATLEAST(2,0,12)
                SDL_SetTextureScaleMode(lowres_mask_tex_, SDL_ScaleModeBest);
#endif
                lowres_w_ = low_w;
                lowres_h_ = low_h;
        }
        return lowres_mask_tex_;
}

SDL_Texture* LightMap::build_lowres_mask(const std::vector<LightEntry>& layers,
                                         int low_w, int low_h, int downscale) {
        SDL_Texture* lowres_mask = ensure_lowres_target(low_w, low_h);
        if (!lowres_mask) {
                return nullptr;
        }
        SDL_SetRenderTarget(renderer_, lowres_mask);
        SDL_SetTextureBlendMode(lowres_mask, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 200);
        SDL_RenderClear(renderer_);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
	for (auto& e : layers) {
		SDL_SetTextureBlendMode(e.tex, SDL_BLENDMODE_ADD);
		SDL_SetTextureAlphaMod(e.tex, e.alpha);
		SDL_SetTextureColorMod(e.tex, 255, 255, 220);
		SDL_Rect scaled_dst{
			e.dst.x / downscale,
			e.dst.y / downscale,
			e.dst.w / downscale,
			e.dst.h / downscale
};
		SDL_RenderCopyEx(renderer_, e.tex, nullptr, &scaled_dst, 0, nullptr, e.flip);
	}
	return lowres_mask;
}

SDL_Rect LightMap::get_scaled_position_rect(SDL_Point pos, int fw, int fh,
                                            float inv_scale, int min_w, int min_h) {
        float base_sw = static_cast<float>(fw) * inv_scale;
        float base_sh = static_cast<float>(fh) * inv_scale;
        if (base_sw < static_cast<float>(min_w) && base_sh < static_cast<float>(min_h)) {
                return {0, 0, 0, 0};
        }
        const camera::RenderEffects effects = assets_->getView().compute_render_effects(pos, base_sh, base_sh);
        float scaled_sw = base_sw * effects.distance_scale;
        float scaled_sh = base_sh * effects.distance_scale;
        float final_visible_h = scaled_sh * effects.vertical_scale;
        if (scaled_sw < static_cast<float>(min_w) && final_visible_h < static_cast<float>(min_h)) {
                return {0, 0, 0, 0};
        }
        int sw = std::max(1, static_cast<int>(std::lround(scaled_sw)));
        int sh = std::max(1, static_cast<int>(std::lround(final_visible_h)));
        if (sw < min_w && sh < min_h) {
                return {0, 0, 0, 0};
        }
        SDL_Point cp = effects.screen_position;
        return SDL_Rect{ cp.x - sw / 2, cp.y - sh / 2, sw, sh };
}
