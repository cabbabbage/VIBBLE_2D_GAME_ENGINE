#include "fade_textures.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <algorithm>
template <typename T>
T clamp(T value, T min_val, T max_val) {
        return std::max(min_val, std::min(value, max_val));
}

FadeTextureGenerator::FadeTextureGenerator(SDL_Renderer* renderer, SDL_Color color, double expand)
: renderer_(renderer), color_(color), expand_(expand) {}

std::vector<std::pair<SDL_Texture*, SDL_Rect>> FadeTextureGenerator::generate_all(const std::vector<Area>& areas) {
	std::vector<std::pair<SDL_Texture*, SDL_Rect>> results;
	size_t index = 0;
	for (const Area& area : areas) {
		std::cout << "  [FadeGen " << index << "] Starting...\n";
		auto [ominx, ominy, omaxx, omaxy] = area.get_bounds();
		int ow = omaxx - ominx + 1;
		int oh = omaxy - ominy + 1;
		if (ow <= 0 || oh <= 0) {
			std::cout << "    [FadeGen " << index << "] Invalid area bounds; skipping.\n";
			++index;
			continue;
		}
		float base_expand = 0.2f * static_cast<float>(std::min(ow, oh));
		base_expand = std::max(base_expand, 1.0f);
		int fw = static_cast<int>(std::ceil(base_expand * expand_));
		int minx = ominx - fw;
		int miny = ominy - fw;
		int maxx = omaxx + fw;
		int maxy = omaxy + fw;
		int w = maxx - minx + 1;
		int h = maxy - miny + 1;
		if (w <= 0 || h <= 0) {
			std::cout << "    [FadeGen " << index << "] Invalid final size; skipping.\n";
			++index;
			continue;
		}
		std::vector<std::pair<double, double>> poly;
		for (auto& [x, y] : area.get_points())
		poly.emplace_back(x - minx, y - miny);
		auto point_in_poly = [&](double px, double py) {
			bool inside = false;
			size_t n = poly.size();
			for (size_t i = 0, j = n - 1; i < n; j = i++) {
					auto [xi, yi] = poly[i];
					auto [xj, yj] = poly[j];
					bool intersect = ((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi + 1e-9) + xi);
					if (intersect) inside = !inside;
			}
			return inside;
};
		SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
		if (!tex) {
			std::cout << "    [FadeGen " << index << "] Texture creation failed; skipping.\n";
			++index;
			continue;
		}
		SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
		SDL_SetRenderTarget(renderer_, tex);
		SDL_SetRenderDrawColor(renderer_, color_.r, color_.g, color_.b, color_.a);
		SDL_RenderClear(renderer_);
		const float fade_radius = static_cast<float>(fw + 250);
		const int step = 25;
		for (int y = 0; y < h; y += step) {
			for (int x = 0; x < w; x += step) {
					double gx = x + 0.5;
					double gy = y + 0.5;
					bool inside = point_in_poly(gx, gy);
					float alpha = 0.0f;
					if (inside) {
								alpha = 1.0f;
					} else {
								float cx = static_cast<float>(ominx + ow / 2 - minx);
								float cy = static_cast<float>(ominy + oh / 2 - miny);
								float dx = gx - cx;
								float dy = gy - cy;
								float dist = std::sqrt(dx * dx + dy * dy);
								float falloff = 1.0f - clamp(dist / fade_radius, 0.0f, 1.0f);
								alpha = falloff * falloff;
					}
					if (alpha > 0.01f) {
								Uint8 a = static_cast<Uint8>(clamp(alpha, 0.0f, 1.0f) * 255);
								SDL_SetRenderDrawColor(renderer_, color_.r, color_.g, color_.b, a);
								for (int dy = 0; dy < step; ++dy)
								for (int dx = 0; dx < step; ++dx)
								SDL_RenderDrawPoint(renderer_, x + dx, y + dy);
					}
			}
		}
                SDL_Surface* raw = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
                if (!raw) {
                        SDL_DestroyTexture(tex);
                        std::cout << "    [FadeGen " << index << "] Surface creation failed; skipping.\n";
                        ++index;
                        continue;
                }
                SDL_SetRenderTarget(renderer_, tex);
                SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_RGBA32, raw->pixels, raw->pitch);
                SDL_SetRenderTarget(renderer_, nullptr);
                SDL_Texture* final_tex = SDL_CreateTextureFromSurface(renderer_, raw);
                SDL_FreeSurface(raw);
                if (!final_tex) {
                        SDL_DestroyTexture(tex);
                        std::cout << "    [FadeGen " << index << "] Texture creation from surface failed; skipping.\n";
                        ++index;
                        continue;
                }
                SDL_SetTextureBlendMode(final_tex, SDL_BLENDMODE_BLEND);
                SDL_Rect dst = { minx, miny, w, h };
                results.emplace_back(final_tex, dst);
                SDL_DestroyTexture(tex);
                std::cout << "    [FadeGen " << index << "] Texture stored. Size = " << w << "x" << h << "\n";
                ++index;
	}
	return results;
}
