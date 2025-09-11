#include "animation_loader.hpp"
#include "custom_controllers/Davey_controller.hpp"
#include "asset/asset_info.hpp"
#include "utils/cache_manager.hpp"
#include "asset/animation.hpp"
#include <nlohmann/json.hpp>
#include <SDL.h>
#include <SDL_image.h>
#include <filesystem>
#include <vector>
#include <string>
#include <unordered_map>
#include "custom_controllers/Vibble_controller.hpp"
#include "custom_controllers/Bomb_controller.hpp"
#include "custom_controllers/Frog_controller.hpp"
#include "custom_controllers/default_controller.hpp"
using nlohmann::json;

void AnimationLoader::load(AssetInfo& info, SDL_Renderer* renderer) {
	if (info.anims_json_.is_null()) return;
	SDL_Texture* base_sprite = nullptr;
	int scaled_sprite_w = 0;
	int scaled_sprite_h = 0;
	info.generate_lights(renderer);
	CacheManager cache;
	std::string root_cache = "cache/" + info.name + "/animations";
	std::vector<std::pair<std::string, nlohmann::json>> alias_queue;
	for (auto it = info.anims_json_.begin(); it != info.anims_json_.end(); ++it) {
		const std::string& trigger = it.key();
		const auto& anim_json = it.value();
		if (anim_json.is_null()) continue;
		if (anim_json.contains("source") && anim_json["source"].is_object()) {
			std::string kind = anim_json["source"].value("kind", std::string{"folder"});
			if (kind == "animation") {
					alias_queue.emplace_back(trigger, anim_json);
					continue;
			}
		}
		Animation anim;
		anim.load(trigger, anim_json, info, info.dir_path_, root_cache, info.scale_factor, renderer, base_sprite, scaled_sprite_w, scaled_sprite_h, info.original_canvas_width, info.original_canvas_height);
		anim.on_end_mapping = anim_json.value("on_end", std::string{"default"});
		if (!anim.frames.empty()) {
			info.animations[trigger] = std::move(anim);
		}
	}
	for (const auto& item : alias_queue) {
		const std::string& trigger = item.first;
		const auto& anim_json = item.second;
		Animation anim;
		anim.load(trigger, anim_json, info, info.dir_path_, root_cache, info.scale_factor, renderer, base_sprite, scaled_sprite_w, scaled_sprite_h, info.original_canvas_width, info.original_canvas_height);
		anim.on_end_mapping = anim_json.value("on_end", std::string{});
		if (!anim.frames.empty()) {
			info.animations[trigger] = std::move(anim);
		}
	}
	// Compute whether this asset has any movement across animations
	info.moving_asset = false;
	for (const auto& kv : info.animations) {
		const Animation& a = kv.second;
		if (a.movment || a.total_dx != 0 || a.total_dy != 0) { info.moving_asset = true; break; }
	}
	get_area_textures(info, renderer);
}

void AnimationLoader::get_area_textures(AssetInfo& info, SDL_Renderer* renderer) {
	if (!renderer) return;
	CacheManager cache;
	for (auto& named : info.areas) {
		if (!named.area) continue;
		std::string folder = "cache/areas/" + info.name + "_" + named.name;
		std::string meta_file = folder + "/metadata.json";
		std::string bmp_file = folder + "/0.bmp";
		auto [minx, miny, maxx, maxy] = named.area->get_bounds();
		json meta;
		if (cache.load_metadata(meta_file, meta)) {
			if (meta.value("bounds", std::vector<int>{}) == std::vector<int>{minx, miny, maxx, maxy}) {
					SDL_Surface* surf = cache.load_surface(bmp_file);
					if (surf) {
								SDL_Texture* tex = cache.surface_to_texture(renderer, surf);
								SDL_FreeSurface(surf);
								if (tex) {
													named.area->create_area_texture(renderer);
													continue;
								}
					}
			}
		}
		named.area->create_area_texture(renderer);
		SDL_Texture* tex = named.area->get_texture();
		if (tex) {
			SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, maxx - minx + 1, maxy - miny + 1, 32, SDL_PIXELFORMAT_RGBA8888);
			if (surf) {
					SDL_SetRenderTarget(renderer, tex);
					SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA8888, surf->pixels, surf->pitch);
					cache.save_surface_as_png(surf, bmp_file);
					SDL_FreeSurface(surf);
					meta["bounds"] = {minx, miny, maxx, maxy};
					cache.save_metadata(meta_file, meta);
					SDL_SetRenderTarget(renderer, nullptr);
			}
		}
	}
}
