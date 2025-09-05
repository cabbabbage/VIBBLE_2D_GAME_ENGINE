#include "animation_loader.hpp"
#include "custom_controllers/Davey_controller.hpp"
#include "custom_controllers/Davey_default_controller.hpp"

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

using nlohmann::json;

void AnimationLoader::load(AssetInfo& info, SDL_Renderer* renderer) {
    if (info.anims_json_.is_null()) return;

    SDL_Texture* base_sprite = nullptr;
    int scaled_sprite_w = 0;
    int scaled_sprite_h = 0;
    // Generate light textures before loading animations
    // Delegates to LightingLoader through AssetInfo method if present
    info.generate_lights(renderer);

    CacheManager cache;
    std::string root_cache = "cache/" + info.name + "/animations";

    // --- Parse graph edges from info.json (new schema) ---
    // Map: animation_id -> mapping_id (end-of-animation transition)
    std::unordered_map<std::string, std::string> anim_to_mapping;
    // Map: "mapping_id::entry:i" or "mapping_id::option:i" -> animation_id
    std::unordered_map<std::string, std::string> slot_to_anim;

    const json& full_info = info.info_json_;
    const bool has_edges = full_info.contains("edges") && full_info["edges"].is_array();
    const json& maps_json = (full_info.contains("mappings") && full_info["mappings"].is_object())
                                ? full_info["mappings"]
                                : json::object();

    if (has_edges) {
        for (const auto& e : full_info["edges"]) {
            if (!e.is_array() || e.size() < 2) continue;
            if (!e[0].is_string() || !e[1].is_string()) continue;
            std::string src = e[0].get<std::string>();
            std::string dst = e[1].get<std::string>();

            const bool src_is_slot = (src.find("::entry:") != std::string::npos) ||
                                     (src.find("::option:") != std::string::npos);

            if (src_is_slot) {
                // Only record slot -> animation links
                if (info.anims_json_.contains(dst)) {
                    slot_to_anim[src] = dst;
                }
            } else {
                // Treat src as an animation id and dst as a mapping id
                if (info.anims_json_.contains(src) && maps_json.contains(dst)) {
                    anim_to_mapping[src] = dst;
                }
            }
        }
    }

    // --- Load animations in two passes: folder sources first, then aliases ---
    std::vector<std::pair<std::string, nlohmann::json>> alias_queue;

    // Pass 1: load concrete sources (e.g., folders)
    for (auto it = info.anims_json_.begin(); it != info.anims_json_.end(); ++it) {
        const std::string& trigger = it.key();
        const auto& anim_json = it.value();
        if (anim_json.is_null()) continue;

        // If this is an alias to another animation, queue it for pass 2
        if (anim_json.contains("source") && anim_json["source"].is_object()) {
            std::string kind = anim_json["source"].value("kind", std::string{"folder"});
            if (kind == "animation") {
                alias_queue.emplace_back(trigger, anim_json);
                continue;
            }
        }

        Animation anim;
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
                  info.original_canvas_height);

        auto eit = anim_to_mapping.find(trigger);
        if (eit != anim_to_mapping.end()) {
            anim.on_end_mapping = eit->second;
        }

        if (!anim.frames.empty()) {
            info.animations[trigger] = std::move(anim);
        }
    }

    // Pass 2: resolve alias animations now that sources are available
    for (const auto& item : alias_queue) {
        const std::string& trigger = item.first;
        const auto& anim_json = item.second;

        Animation anim;
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
                  info.original_canvas_height);

        auto eit = anim_to_mapping.find(trigger);
        if (eit != anim_to_mapping.end()) {
            anim.on_end_mapping = eit->second;
        }

        if (!anim.frames.empty()) {
            info.animations[trigger] = std::move(anim);
        }
    }

    // --- Build runtime mappings from new schema (typed mappings + edges) ---
    if (maps_json.is_object()) {
        // Overlay graph-derived mappings on top of any legacy mappings parsed earlier

        for (auto it = maps_json.begin(); it != maps_json.end(); ++it) {
            const std::string mapping_id = it.key();
            const json& m = it.value();
            std::vector<MappingEntry> built;

            const std::string type = m.value("type", std::string{"regular"});
            if (type == "regular") {
                const auto& entries = m.value("entries", json::array());
                if (entries.is_array()) {
                    for (size_t i = 0; i < entries.size(); ++i) {
                        MappingEntry entry;
                        const auto& ej = entries[i];
                        if (ej.is_object()) entry.condition = ej.value("condition", std::string{});
                        // Expect exactly one outgoing edge for this entry slot
                        std::string slot = mapping_id + "::entry:" + std::to_string(i);
                        auto sit = slot_to_anim.find(slot);
                        if (sit != slot_to_anim.end()) {
                            MappingOption opt{sit->second, 100.0f};
                            entry.options.push_back(opt);
                        }
                        built.push_back(std::move(entry));
                    }
                }
            } else if (type == "random") {
                // Aggregate all options under a single conditional entry
                MappingEntry entry;
                entry.condition = "";
                const auto& opts = m.value("options", json::array());
                if (opts.is_array()) {
                    for (size_t i = 0; i < opts.size(); ++i) {
                        float pct = 0.0f;
                        if (opts[i].is_object()) {
                            pct = opts[i].value("percent", 0.0f);
                        }
                        std::string slot = mapping_id + "::option:" + std::to_string(i);
                        auto sit = slot_to_anim.find(slot);
                        if (sit != slot_to_anim.end()) {
                            MappingOption opt{sit->second, pct};
                            entry.options.push_back(opt);
                        }
                    }
                }
                built.push_back(std::move(entry));
            } else if (m.is_array()) {
                // Legacy array form: keep existing parsed mappings as-is.
                // Don't overwrite here unless we actually built something.
            }

            if (!built.empty()) {
                info.mappings[mapping_id] = std::move(built);
            }
        }
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

