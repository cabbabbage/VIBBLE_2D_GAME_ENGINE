#include "animation_loader.hpp"
#include "custom_controllers/Davey_controller.hpp"
#include "asset/asset_info.hpp"
#include "utils/cache_manager.hpp"
#include "asset/animation.hpp"
#include "render_pipeline/ScalingLogic.hpp"
#include <nlohmann/json.hpp>
#include <SDL.h>
#include <SDL_image.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>
#include <string>
#include <unordered_map>
#include "custom_controllers/Vibble_controller.hpp"
#include "custom_controllers/Bomb_controller.hpp"
#include "custom_controllers/Frog_controller.hpp"
#include "custom_controllers/default_controller.hpp"
using nlohmann::json;

namespace {
std::string format_steps(const std::vector<float>& steps) {
        std::ostringstream oss;
        oss << '[';
        for (std::size_t i = 0; i < steps.size(); ++i) {
                if (i != 0) {
                        oss << ", ";
                }
                oss << std::fixed << std::setprecision(2) << steps[i];
        }
        oss << ']';
        return oss.str();
}
}

void AnimationLoader::load(AssetInfo& info, SDL_Renderer* renderer) {
        if (info.anims_json_.is_null()) return;
        SDL_Texture* base_sprite = nullptr;
        int scaled_sprite_w = 0;
        int scaled_sprite_h = 0;
        info.generate_lights(renderer);
        CacheManager cache;
        std::string root_cache = "cache/" + info.name + "/animations";

        render_pipeline::ScalingLogic::ConfigureUsageStorage(std::filesystem::path("loading") / "scaling_profiles.json");
        const bool scaling_refresh_pending = render_pipeline::ScalingLogic::HasPendingUsageData();
        const auto profile = render_pipeline::ScalingLogic::ProfileForAsset(info.name);
        std::cout << "[AnimationLoader] " << info.name
                  << " scaling_refresh_pending=" << (scaling_refresh_pending ? "true" : "false")
                  << ", profile_revision=" << profile.revision
                  << ", profile_steps=" << format_steps(profile.steps)
                  << "\n";
        info.scale_variants = profile.steps;
        if (info.scale_variants.empty()) {
                const auto& defaults = render_pipeline::ScalingLogic::DefaultScaleSteps();
                info.scale_variants.assign(defaults.begin(), defaults.end());
                std::cout << "[AnimationLoader] " << info.name
                          << " falling back to default scaling steps: "
                          << format_steps(info.scale_variants) << "\n";
        }
        if (info.scale_variants.empty()) {
                info.scale_variants.push_back(1.0f);
        }
        std::sort(info.scale_variants.begin(), info.scale_variants.end(), std::greater<float>());
        info.scale_variants.erase(std::unique(info.scale_variants.begin(), info.scale_variants.end(), [](float a, float b) {
                return std::fabs(a - b) <= 1e-4f;
        }), info.scale_variants.end());
        if (std::fabs(info.scale_variants.front() - 1.0f) > 1e-4f) {
                info.scale_variants.insert(info.scale_variants.begin(), 1.0f);
        }
        std::cout << "[AnimationLoader] " << info.name
                  << " normalized asset scaling steps: " << format_steps(info.scale_variants)
                  << " (profile revision " << profile.revision << ")\n";
        info.scale_profile_revision = profile.revision;

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
                anim.load(trigger, anim_json, info, info.dir_path_, root_cache, info.scale_factor, renderer, base_sprite, scaled_sprite_w, scaled_sprite_h, info.original_canvas_width, info.original_canvas_height, scaling_refresh_pending);
		anim.on_end_mapping = anim_json.value("on_end", std::string{"default"});
		if (!anim.frames.empty()) {
			info.animations[trigger] = std::move(anim);
		}
	}
	for (const auto& item : alias_queue) {
		const std::string& trigger = item.first;
		const auto& anim_json = item.second;
		Animation anim;
                anim.load(trigger, anim_json, info, info.dir_path_, root_cache, info.scale_factor, renderer, base_sprite, scaled_sprite_w, scaled_sprite_h, info.original_canvas_width, info.original_canvas_height, scaling_refresh_pending);
		anim.on_end_mapping = anim_json.value("on_end", std::string{});
		if (!anim.frames.empty()) {
			info.animations[trigger] = std::move(anim);
		}
        }

        if (scaling_refresh_pending) {
                render_pipeline::ScalingLogic::ClearPendingUsageData();
        }

        info.moving_asset = false;
	for (const auto& kv : info.animations) {
		const Animation& a = kv.second;
		if (a.movment || a.total_dx != 0 || a.total_dy != 0) { info.moving_asset = true; break; }
	}
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
													named.area->set_cached_texture(tex);
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
					SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
					SDL_SetRenderTarget(renderer, tex);
					SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA8888, surf->pixels, surf->pitch);
					cache.save_surface_as_png(surf, bmp_file);
					SDL_FreeSurface(surf);
					meta["bounds"] = {minx, miny, maxx, maxy};
					cache.save_metadata(meta_file, meta);
					SDL_SetRenderTarget(renderer, prev_target);
			}
		}
	}
}
