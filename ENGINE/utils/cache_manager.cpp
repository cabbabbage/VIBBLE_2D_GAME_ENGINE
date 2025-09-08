#include "cache_manager.hpp"
#include <SDL.h>
#include <SDL_image.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <iostream>
namespace fs = std::filesystem;

bool CacheManager::load_metadata(const std::string& meta_file, nlohmann::json& out_meta) {
	if (!fs::exists(meta_file)) return false;
	std::ifstream in(meta_file);
	if (!in) return false;
	try {
		in >> out_meta;
		return true;
	} catch (...) {
		return false;
	}
}

bool CacheManager::save_metadata(const std::string& meta_file, const nlohmann::json& meta) {
	try {
		std::ofstream out(meta_file);
		out << meta.dump(4);
		return true;
	} catch (...) {
		return false;
	}
}

SDL_Surface* CacheManager::load_surface(const std::string& path) {
	return IMG_Load(path.c_str());
}

bool CacheManager::save_surface_as_png(SDL_Surface* surface, const std::string& path) {
	if (!surface) return false;
	return SDL_SaveBMP(surface, path.c_str()) == 0;
}

bool CacheManager::load_surface_sequence(const std::string& folder, int frame_count, std::vector<SDL_Surface*>& surfaces) {
	surfaces.clear();
	for (int i = 0; i < frame_count; ++i) {
		std::string file = folder + "/" + std::to_string(i) + ".bmp";
		SDL_Surface* s = IMG_Load(file.c_str());
		if (!s) return false;
		surfaces.push_back(s);
	}
	return true;
}

bool CacheManager::save_surface_sequence(const std::string& folder, const std::vector<SDL_Surface*>& surfaces) {
	fs::remove_all(folder);
	fs::create_directories(folder);
	for (size_t i = 0; i < surfaces.size(); ++i) {
		if (!save_surface_as_png(surfaces[i], folder + "/" + std::to_string(i) + ".bmp")) {
			return false;
		}
	}
	return true;
}

SDL_Surface* CacheManager::load_and_scale_surface(const std::string& path, float scale, int& out_w, int& out_h) {
	SDL_Surface* original = IMG_Load(path.c_str());
	if (!original) return nullptr;
	int new_w = static_cast<int>(original->w * scale + 0.5f);
	int new_h = static_cast<int>(original->h * scale + 0.5f);
	out_w = new_w;
	out_h = new_h;
	SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat( 0, new_w, new_h, original->format->BitsPerPixel, original->format->format );
	if (!scaled) {
		SDL_FreeSurface(original);
		return nullptr;
	}
	if (SDL_BlitScaled(original, nullptr, scaled, nullptr) < 0) {
		SDL_FreeSurface(original);
		SDL_FreeSurface(scaled);
		return nullptr;
	}
	SDL_FreeSurface(original);
	return scaled;
}

SDL_Texture* CacheManager::surface_to_texture(SDL_Renderer* renderer, SDL_Surface* surface) {
	if (!renderer || !surface) return nullptr;
	SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surface);
	if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	return tex;
}

std::vector<SDL_Texture*> CacheManager::surfaces_to_textures(SDL_Renderer* renderer, const std::vector<SDL_Surface*>& surfaces) {
	std::vector<SDL_Texture*> textures;
	for (SDL_Surface* surf : surfaces) {
		SDL_Texture* tex = surface_to_texture(renderer, surf);
		if (tex) textures.push_back(tex);
	}
	return textures;
}
