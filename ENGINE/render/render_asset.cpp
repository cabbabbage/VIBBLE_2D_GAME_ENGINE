#include "render_asset.hpp"
#include "global_light_source.hpp"
#include "asset\Asset.hpp"
#include "core\assetsManager.hpp"
#include "utils\light_utils.hpp"
#include "utils\parallax.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include <iostream>
RenderAsset::RenderAsset(SDL_Renderer* renderer,
Parallax& parallax,
Global_Light_Source& main_light,
Asset* player)
: renderer_(renderer),
parallax_(parallax),
main_light_source_(main_light),
p(player) {}

SDL_Texture* RenderAsset::render_shadow_mask(Asset* a, int bw, int bh) {
	SDL_Texture* mask = SDL_CreateTexture(renderer_,
	SDL_PIXELFORMAT_RGBA8888,
	SDL_TEXTUREACCESS_TARGET,
	bw, bh);
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
	SDL_Point parallax_pos = parallax_.apply(a->pos_X, a->pos_Y);
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
	const Uint8 main_alpha = main_light_source_.get_current_color().a;
	int bw = a->cached_w, bh = a->cached_h;
	if (bw == 0 || bh == 0) SDL_QueryTexture(base, nullptr, nullptr, &bw, &bh);
	SDL_Texture* final_tex = SDL_CreateTexture(renderer_,
	SDL_PIXELFORMAT_RGBA8888,
	SDL_TEXTUREACCESS_TARGET,
	bw, bh);
	if (!final_tex) return nullptr;
	SDL_SetTextureBlendMode(final_tex, SDL_BLENDMODE_BLEND);
	SDL_Texture* prev_target = SDL_GetRenderTarget(renderer_);
	SDL_SetRenderTarget(renderer_, final_tex);
	SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
	SDL_RenderClear(renderer_);
	const float c = a->alpha_percentage;
	int alpha_mod = (c >= 1.0f) ? 255 : int(main_alpha * c);
	if (a->info->type == "Player") alpha_mod = std::min(255, alpha_mod * 3);
	SDL_SetTextureColorMod(base, 255, 255, 255);
	SDL_RenderCopy(renderer_, base, nullptr, nullptr);
	SDL_SetTextureColorMod(base, 255, 255, 255);
	if (a->has_shading) {
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

void RenderAsset::render_shadow_moving_lights(Asset* a, const SDL_Rect& bounds, Uint8 alpha) {
	if (!p || !p->info) return;
	for (const auto& light : p->info->light_sources) {
		if (!light.texture) continue;
		const int world_lx = p->pos_X + light.offset_x;
		const int world_ly = p->pos_Y + light.offset_y;
		const double factor = LightUtils::calculate_static_alpha_percentage(a, p);
		const Uint8 inten = static_cast<Uint8>(alpha * factor);
		SDL_Point pnt = parallax_.apply(world_lx, world_ly);
		int lw = light.cached_w, lh = light.cached_h;
		if (lw == 0 || lh == 0) SDL_QueryTexture(light.texture, nullptr, nullptr, &lw, &lh);
		SDL_Rect dst {
			pnt.x - bounds.x - lw / 2,
			pnt.y - bounds.y - lh / 2,
			lw, lh
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
	for (const auto& light : a->info->orbital_light_sources) {
		if (!light.texture || light.x_radius <= 0 || light.y_radius <= 0) continue;
		const float lx = a->pos_X + std::cos(angle) * light.x_radius;
		const float ly = a->pos_Y - std::sin(angle) * light.y_radius;
		SDL_Point pnt = parallax_.apply(static_cast<int>(std::round(lx)),
		static_cast<int>(std::round(ly)));
		int lw = light.cached_w, lh = light.cached_h;
		if (lw == 0 || lh == 0) SDL_QueryTexture(light.texture, nullptr, nullptr, &lw, &lh);
		SDL_Rect dst {
			pnt.x - lw / 2 - bounds.x,
			pnt.y - lh / 2 - bounds.y,
			lw, lh
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
		SDL_Point pnt = parallax_.apply(a->pos_X + sl.offset_x, a->pos_Y + sl.offset_y);
		int lw = sl.source->cached_w, lh = sl.source->cached_h;
		if (lw == 0 || lh == 0) SDL_QueryTexture(sl.source->texture, nullptr, nullptr, &lw, &lh);
		SDL_Rect dst {
			pnt.x - lw / 2 - bounds.x,
			pnt.y - lh / 2 - bounds.y,
			lw, lh
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
