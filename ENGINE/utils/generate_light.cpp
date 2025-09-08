#include "generate_light.hpp"
#include "cache_manager.hpp"
#include <SDL.h>
#include <SDL_image.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <random>
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
	const SDL_Color col = light.color;
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
	float white_core_ratio  = std::pow(1.0f - falloff / 100.0f, 2.0f);
	float white_core_radius = radius * white_core_ratio;
	std::mt19937 rng(std::random_device{}());
	std::uniform_real_distribution<float> angle_dist(0.0f, 2.0f * float(M_PI));
	std::uniform_real_distribution<float> spread_dist(0.2f, 0.6f);
	std::uniform_int_distribution<int>    ray_count_dist(4, 7);
	const int ray_count = ray_count_dist(rng);
	std::vector<std::pair<float, float>> rays;
	rays.reserve(ray_count);
	for (int i = 0; i < ray_count; ++i) {
		rays.emplace_back(angle_dist(rng), spread_dist(rng));
	}
	auto put_pixel = [&](int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
		pixels[y * size + x] = SDL_MapRGBA(fmt, r, g, b, a);
	};
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			float dx = x - radius + 0.5f;
			float dy = y - radius + 0.5f;
			float dist = std::sqrt(dx * dx + dy * dy);
			if (dist > radius) {
					put_pixel(x, y, 0, 0, 0, 0);
					continue;
			}
			float angle = std::atan2(dy, dx);
			float ray_boost = 1.0f;
			for (const auto& rs : rays) {
					float ray_angle = rs.first;
					float spread    = rs.second;
					float diff = std::fabs(angle - ray_angle);
					diff = std::fmod(diff + 2.0f * float(M_PI), 2.0f * float(M_PI));
					if (diff > float(M_PI)) diff = 2.0f * float(M_PI) - diff;
					if (diff < spread) {
								float f = 1.0f - (diff / spread);
								ray_boost += f * 0.05f;
					}
			}
			ray_boost = std::clamp(ray_boost, 1.0f, 1.1f);
			float alpha_ratio = std::pow(1.0f - (dist / float(radius)), 1.4f);
			alpha_ratio = std::clamp(alpha_ratio * ray_boost, 0.0f, 1.0f);
			Uint8 alpha = static_cast<Uint8>(std::min(255.0f, intensity * alpha_ratio * 1.6f));
			SDL_Color final_color;
			if (dist <= white_core_radius) {
					final_color.r = static_cast<Uint8>((255 + col.r) / 2);
					final_color.g = static_cast<Uint8>((255 + col.g) / 2);
					final_color.b = static_cast<Uint8>((255 + col.b) / 2);
					final_color.a = alpha;
			} else {
					float t = (dist - white_core_radius) / std::max(1e-6f, (radius - white_core_radius));
					Uint8 core_r = static_cast<Uint8>((255 + col.r) / 2);
					Uint8 core_g = static_cast<Uint8>((255 + col.g) / 2);
					Uint8 core_b = static_cast<Uint8>((255 + col.b) / 2);
					final_color.r = static_cast<Uint8>((1.0f - t) * core_r + t * col.r);
					final_color.g = static_cast<Uint8>((1.0f - t) * core_g + t * col.g);
					final_color.b = static_cast<Uint8>((1.0f - t) * core_b + t * col.b);
					final_color.a = alpha;
			}
			put_pixel(x, y, final_color.r, final_color.g, final_color.b, final_color.a);
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
