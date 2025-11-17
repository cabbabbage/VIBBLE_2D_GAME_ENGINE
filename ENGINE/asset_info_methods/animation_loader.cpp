#include "animation_loader.hpp"
#include "animation_update/custom_controllers/Davey_controller.hpp"
#include "asset/asset_info.hpp"
#include "utils/cache_manager.hpp"
#include "asset/animation.hpp"
#include "render_pipeline/ScalingLogic.hpp"
#include "render/image_effect_settings.hpp"
#include <nlohmann/json.hpp>
#include <SDL.h>
#include <SDL_image.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <system_error>
#include <vector>
#include <string>
#include <unordered_map>
#include <fstream>
#include "animation_update/custom_controllers/Vibble_controller.hpp"
#include "animation_update/custom_controllers/Bomb_controller.hpp"
#include "animation_update/custom_controllers/Frog_controller.hpp"
#include "animation_update/custom_controllers/default_controller.hpp"
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

} // namespace

bool AnimationLoader::clear_asset_cache(const std::string& asset_name) {
        if (asset_name.empty()) {
                return false;
        }

        const std::filesystem::path asset_cache = std::filesystem::path("cache") / asset_name;
        std::error_code ec;
        const bool exists = std::filesystem::exists(asset_cache, ec);
        if (!exists || ec) {
                if (ec) {
                        std::cerr << "[AnimationLoader] Failed to query cache for " << asset_name
                                  << ": " << ec.message() << "\n";
                        return false;
                }
                return true;
        }

        ec.clear();
        std::filesystem::remove_all(asset_cache, ec);
        if (ec) {
                std::cerr << "[AnimationLoader] Failed to clear cache for " << asset_name << ": " << ec.message() << "\n";
                return false;
        }

        std::cout << "[AnimationLoader] " << asset_name << " cleared cache folder '" << asset_cache.string() << "'\n";
        return true;
}

AnimationLoader::LoadAttemptResult AnimationLoader::load_asset_animations_once(AssetInfo& info, SDL_Renderer* renderer, bool force_rebuild) {
        LoadAttemptResult result{};

        for (auto& entry : info.animations) {
                entry.second.clear_texture_cache();
        }
        info.animations.clear();
        info.moving_asset = false;
        info.generate_lights(renderer);

        SDL_Texture* base_sprite = nullptr;
        int          scaled_sprite_w = 0;
        int          scaled_sprite_h = 0;
        const std::string root_cache = "cache/" + info.name + "/animations";

        render_pipeline::ScalingLogic::LoadPrecomputedProfiles(true);
        const auto profile = render_pipeline::ScalingLogic::ProfileForAsset(info.name);
        const bool profile_refresh_requested = profile.created_entry || profile.revision_changed;
        const bool scaling_refresh_pending = force_rebuild || profile_refresh_requested;

        std::cout << "[AnimationLoader] " << info.name
                  << " profile_revision=" << profile.revision
                  << ", profile_steps=" << format_steps(profile.steps) << "\n";

        if (profile_refresh_requested) {
                if (profile.created_entry) {
                        std::cout << "[AnimationLoader] " << info.name
                                  << " detected new scaling profile entry -> clearing animation cache\n";
                } else if (profile.revision_changed) {
                        std::cout << "[AnimationLoader] " << info.name
                                  << " detected scaling profile revision change -> clearing animation cache\n";
                }

                if (!clear_asset_cache(info.name)) {
                        std::cerr << "[AnimationLoader] Unable to clear animation cache for " << info.name
                                  << " after scaling profile updates\n";
                }
        }

        info.scale_variants = profile.steps;
        render_pipeline::ScalingLogic::NormalizeVariantSteps(info.scale_variants);
        std::cout << "[AnimationLoader] " << info.name
                  << " normalized asset scaling steps: " << format_steps(info.scale_variants)
                  << " (profile revision " << profile.revision << ")\n";
        info.scale_profile_revision = profile.revision;

        auto load_animation_payload = [&](const std::string& trigger, const nlohmann::json& anim_json) -> bool {
                if (anim_json.is_null()) {
                        return true;
                }
                Animation anim;
                Animation::LoadDiagnostics diagnostics;
                try {
                        anim.load(trigger,
                                  anim_json,
                                  info,
                                  info.dir_path_,
                                  root_cache,
                                  info.scale_factor,
                                  renderer,
                                  base_sprite,
                                  scaled_sprite_w,
                                  scaled_sprite_h,
                                  info.original_canvas_width,
                                  info.original_canvas_height,
                                  scaling_refresh_pending,
                                  &diagnostics);
                } catch (const std::exception& ex) {
                        std::cerr << "[AnimationLoader] Exception while loading " << info.name << "::" << trigger
                                  << ": " << ex.what() << "\n";
                        return false;
                } catch (...) {
                        std::cerr << "[AnimationLoader] Unknown exception while loading " << info.name << "::" << trigger << "\n";
                        return false;
                }

                if (diagnostics.cache_invalid && !force_rebuild) {
                        result.cache_issue = true;
                }

                if (!anim.frames.empty()) {
                        info.animations[trigger] = std::move(anim);
                        return true;
                }

                std::cerr << "[AnimationLoader] " << info.name << "::" << trigger << " produced no frames\n";
                return false;
        };

        std::vector<std::pair<std::string, nlohmann::json>> alias_queue;
        for (auto it = info.anims_json_.begin(); it != info.anims_json_.end(); ++it) {
                const std::string& trigger  = it.key();
                const auto&        anim_json = it.value();
                if (anim_json.is_null()) {
                        continue;
                }
                if (anim_json.contains("source") && anim_json["source"].is_object()) {
                        std::string kind = anim_json["source"].value("kind", std::string{"folder"});
                        if (kind == "animation") {
                                alias_queue.emplace_back(trigger, anim_json);
                                continue;
                        }
                }
                if (!load_animation_payload(trigger, anim_json)) {
                        return result;
                }
        }

        {
                std::vector<std::pair<std::string, nlohmann::json>> pending = alias_queue;
                int         safety         = 0;
                while (!pending.empty() && safety < 1024) {
                        ++safety;
                        std::vector<std::pair<std::string, nlohmann::json>> next;
                        std::size_t resolved_this_pass = 0;
                        for (auto& item : pending) {
                                const std::string& trigger   = item.first;
                                const auto&        anim_json = item.second;
                                std::string        source_name;
                                try {
                                        if (anim_json.contains("source") && anim_json["source"].is_object()) {
                                                const auto& s = anim_json["source"];
                                                if (s.value("kind", std::string{"folder"}) == std::string{"animation"}) {
                                                        source_name = s.value("name", std::string{});
                                                        if (source_name.empty()) {
                                                                source_name = s.value("path", std::string{});
                                                        }
                                                }
                                        }
                                } catch (...) {
                                        source_name.clear();
                                }

                                if (!source_name.empty() && info.animations.find(source_name) == info.animations.end()) {
                                        next.emplace_back(trigger, anim_json);
                                        continue;
                                }

                                if (load_animation_payload(trigger, anim_json)) {
                                        ++resolved_this_pass;
                                } else {
                                        return result;
                                }
                        }

                        if (resolved_this_pass == 0 || next.size() == pending.size()) {
                                pending.swap(next);
                                break;
                        }

                        pending.swap(next);
                }

                for (const auto& item : pending) {
                        if (!load_animation_payload(item.first, item.second)) {
                                return result;
                        }
                }
        }

        info.moving_asset = false;
        for (const auto& kv : info.animations) {
                const Animation& a = kv.second;
                if (a.movment || a.total_dx != 0 || a.total_dy != 0) {
                        info.moving_asset = true;
                        break;
                }
        }

        result.success = true;
        return result;
}

void AnimationLoader::load(AssetInfo& info, SDL_Renderer* renderer) {
        if (info.anims_json_.is_null()) {
                info.generate_lights(renderer);
                return;
        }

        LoadAttemptResult attempt = load_asset_animations_once(info, renderer, false);
        if (attempt.success && !attempt.cache_issue) {
                return;
        }

        if (attempt.cache_issue) {
                std::cout << "[AnimationLoader] " << info.name << " detected invalid cache structure -> calling Python script\n";
        } else {
                std::cout << "[AnimationLoader] " << info.name << " encountered errors -> calling Python script\n";
        }

        // Call Python script to regenerate cache
        if (!call_python_script_for_asset(info)) {
                std::cerr << "[AnimationLoader] " << info.name << " Python script failed, skipping animations\n";
                return;
        }

        // Retry loading after Python script
        LoadAttemptResult retry = load_asset_animations_once(info, renderer, false);
        if (!retry.success) {
                std::cerr << "[AnimationLoader] " << info.name << " failed to load animations after Python script\n";
        } else if (retry.cache_issue) {
                std::cerr << "[AnimationLoader] " << info.name << " Python script succeeded but cache inconsistencies persist\n";
        }
}

bool AnimationLoader::call_python_script_for_asset(const AssetInfo& info) {
    // Construct source directory path: SRC/assets/<AssetName>/
    std::filesystem::path src_dir = std::filesystem::path("SRC") / "assets" / info.name;
    std::string src_dir_arg = src_dir.string();

    // Convert scales to percentages for command line
    std::string scales_arg;
    for (size_t i = 0; i < info.scale_variants.size(); ++i) {
        if (i > 0) scales_arg += ",";
        scales_arg += std::to_string(static_cast<int>(std::lround(info.scale_variants[i] * 100.0f)));
    }

    // Construct command using unified asset_tool.py
    std::string cmd = "python tools/asset_tool.py build-texture";
    cmd += " --asset-src-dir \"" + src_dir_arg + "\"";
    cmd += " --asset-name \"" + info.name + "\"";
    cmd += " --manifest-path manifest.json";
    cmd += " --scales \"" + scales_arg + "\"";

    std::cout << "[AnimationLoader] Calling Python tool: " << cmd << "\n";

    // Execute command
    int result = std::system(cmd.c_str());

    return result == 0;
}

void AnimationLoader::get_area_textures(AssetInfo& info, SDL_Renderer* renderer) {
	if (!renderer) return;
	for (auto& named : info.areas) {
		if (!named.area) continue;
		std::string folder = "cache/areas/" + info.name + "_" + named.name;
		std::string meta_file = folder + "/metadata.json";
		std::string png_file = folder + "/0.png";
		std::string bmp_fallback = folder + "/0.bmp";
		auto [minx, miny, maxx, maxy] = named.area->get_bounds();
		json meta;
		if (CacheManager::load_metadata(meta_file, meta)) {
					if (meta.value("bounds", std::vector<int>{}) == std::vector<int>{minx, miny, maxx, maxy}) {
					SDL_Surface* surf = CacheManager::load_surface(png_file);
					if (!surf) surf = CacheManager::load_surface(bmp_fallback);
					if (surf) {
								SDL_Texture* tex = CacheManager::surface_to_texture(renderer, surf);
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
					// Persist as PNG (older BMP caches remain as fallback reads)
					CacheManager::save_surface_as_png(surf, png_file);
					SDL_FreeSurface(surf);
					meta["bounds"] = {minx, miny, maxx, maxy};
					CacheManager::save_metadata(meta_file, meta);
					SDL_SetRenderTarget(renderer, prev_target);
			}
		}
	}
}
