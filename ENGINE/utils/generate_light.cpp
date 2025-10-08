#include "generate_light.hpp"
#include "cache_manager.hpp"
#include <SDL.h>
#include <SDL_image.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <vector>
#include <iostream>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

GenerateLight::GenerateLight(SDL_Renderer* renderer)
: renderer_(renderer) {}

SDL_Texture* GenerateLight::generate(SDL_Renderer* renderer,
                                     const std::string& asset_name,
                                     const LightSource& light,
                                     std::size_t light_index)
{
	if (!renderer) return nullptr;
	const std::string cache_root = "cache/" + asset_name + "/lights";
	const std::string folder     = cache_root + "/" + std::to_string(light_index);
	const std::string meta_file  = folder + "/metadata.json";
	const std::string img_file   = folder + "/light.png";
	const int blur_passes = 0;
	json meta;
	if (CacheManager::load_metadata(meta_file, meta)) {
		bool meta_ok =
		meta.value("radius",   -1) == light.radius && meta.value("fall_off", -1) == light.fall_off && meta.value("intensity",-1) == light.intensity && meta.value("flare",    -1) == light.flare && meta.value("blur_passes", -1) == blur_passes && meta.contains("color") && meta["color"].is_array() && meta["color"].size() == 3 && meta["color"][0].get<int>() == light.color.r && meta["color"][1].get<int>() == light.color.g && meta["color"][2].get<int>() == light.color.b;
		if (meta_ok) {
			if (SDL_Surface* surf = CacheManager::load_surface(img_file)) {
					SDL_Texture* tex = CacheManager::surface_to_texture(renderer, surf);
					SDL_FreeSurface(surf);
					if (tex) {
								SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
								return tex;
					}
			}
		}
	}
	fs::remove_all(folder);
	fs::create_directories(folder);
	const int radius    = light.radius;
	const int falloff   = std::clamp(light.fall_off, 0, 100);
        const SDL_Color col = SDL_Color{255, 255, 255, 255};
	const int intensity = std::clamp(light.intensity, 0, 255);
	const int flare     = std::clamp(light.flare, 0, 100);
	const int size = std::max(1, radius * 2);
	SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_RGBA32);
	if (!surf) {
		std::cerr << "[GenerateLight] Failed to create surface: " << SDL_GetError() << "\n";
		return nullptr;
	}
	if (SDL_LockSurface(surf) != 0) {
		std::cerr << "[GenerateLight] Failed to lock surface: " << SDL_GetError() << "\n";
		SDL_FreeSurface(surf);
		return nullptr;
	}
	Uint32* pixels = static_cast<Uint32*>(surf->pixels);
	SDL_PixelFormat* fmt = surf->format;
        float falloff_norm = std::clamp(static_cast<float>(falloff) / 100.0f, 0.0f, 1.0f);
        float falloff_ratio = 1.0f - falloff_norm;
        float fade_exponent = 0.6f + 3.4f * falloff_norm;
        float white_core_ratio  = 0.2f + falloff_ratio * 0.6f;
        float white_core_radius = static_cast<float>(radius) * white_core_ratio;
        auto put_pixel = [&](int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
                pixels[y * size + x] = SDL_MapRGBA(fmt, r, g, b, a);
};
        float radius_f = static_cast<float>(radius);
        float radius_sq = radius_f * radius_f;
        float inv_radius = 1.0f / radius_f;
        for (int y = 0; y < size; ++y) {
                for (int x = 0; x < size; ++x) {
                        float dx = x - radius + 0.5f;
                        float dy = y - radius + 0.5f;
                        float dist_sq = dx * dx + dy * dy;
                        if (dist_sq > radius_sq) {
                                        put_pixel(x, y, 0, 0, 0, 0);
                                        continue;
                        }
                        float dist = std::sqrt(dist_sq);
                        float base_gradient = std::max(0.0f, 1.0f - (dist * inv_radius));
                        float alpha_ratio  = std::pow(base_gradient, fade_exponent);
                        alpha_ratio = std::clamp(alpha_ratio, 0.0f, 1.0f);
			Uint8 alpha = static_cast<Uint8>(std::min(255.0f, intensity * alpha_ratio * 1.6f));
                        float brightness = 1.0f;
                        if (dist > white_core_radius) {
                                        float t = (dist - white_core_radius) / std::max(1e-6f, (radius - white_core_radius));
                                        brightness = std::clamp(1.0f - t * 0.6f, 0.0f, 1.0f);
                        }
                        Uint8 value = static_cast<Uint8>(std::clamp(brightness * 255.0f, 0.0f, 255.0f));
                        put_pixel(x, y, value, value, value, alpha);
		}
	}
	SDL_UnlockSurface(surf);
	SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
	if (!tex) {
		std::cerr << "[GenerateLight] Failed to create texture: " << SDL_GetError() << "\n";
		SDL_FreeSurface(surf);
		return nullptr;
	}
	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	CacheManager::save_surface_as_png(surf, img_file);
	SDL_FreeSurface(surf);
	json new_meta;
	new_meta["radius"]      = light.radius;
	new_meta["fall_off"]    = light.fall_off;
	new_meta["intensity"]   = light.intensity;
	new_meta["flare"]       = flare;
	new_meta["blur_passes"] = blur_passes;
	new_meta["color"]       = { col.r, col.g, col.b };
	CacheManager::save_metadata(meta_file, new_meta);
	return tex;
}
