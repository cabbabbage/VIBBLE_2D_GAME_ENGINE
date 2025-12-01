#include "animation_loader.hpp"
#include "animation.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_types.hpp"
#include "asset/surface_utils.hpp"
#include "utils/cache_manager.hpp"
#include "render/render.hpp"
#include "utils/loading_status_notifier.hpp"
#include "utils/log.hpp"
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace {

// Count PNG files (0.png, 1.png, 2.png, etc.) in a folder
int count_png_files(const std::string& folder) {
        int count = 0;
        const fs::path folder_path(folder);
        
        std::error_code ec;
        if (!fs::exists(folder_path, ec) || ec) {
                std::cout << "[Animation] count_png_files: folder does not exist: " << folder << "\n";
                return 0;
        }
        
        while (true) {
                fs::path frame_path = folder_path / (std::to_string(count) + ".png");
                if (!fs::exists(frame_path, ec) || ec) {
                        break;
                }
                ++count;
        }
        
        std::cout << "[Animation] count_png_files: folder=" << folder << ", count=" << count << "\n";
        return count;
}

fs::path project_root_path() {
#ifdef PROJECT_ROOT
        return fs::path(PROJECT_ROOT);
#else
        return fs::current_path();
#endif
}

bool path_exists_safely(const fs::path& path) {
        std::error_code ec;
        return fs::exists(path, ec);
}

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

struct VariantLayerPaths {
        std::string scale_folder;
        std::string normal_folder;
        std::string foreground_folder;
        std::string background_folder;
        std::string mask_folder;
        std::string depthcue_foreground_folder;
        std::string depthcue_background_folder;
};

VariantLayerPaths build_variant_layer_paths(const std::string& cache_folder,
                                            const std::vector<float>& steps,
                                            std::size_t index) {
        VariantLayerPaths paths;
        paths.scale_folder     = render_pipeline::ScalingLogic::VariantFolder(cache_folder, steps, index);
        const fs::path scale_root(paths.scale_folder);
        paths.normal_folder     = (scale_root / "normal").string();
        paths.foreground_folder = (scale_root / "foreground").string();
        paths.background_folder = (scale_root / "background").string();
        paths.mask_folder       = (scale_root / "mask").string();
        paths.depthcue_foreground_folder = (scale_root / "depthcue_foreground").string();
        paths.depthcue_background_folder = (scale_root / "depthcue_background").string();
        
        // Log constructed paths for debugging
        std::cout << "[Animation] build_variant_layer_paths idx=" << index 
                  << " scale=" << (index < steps.size() ? steps[index] : 0.0f)
                  << " normal_folder=" << paths.normal_folder << "\n";
        
        return paths;
}

inline double sanitize_scale_factor(float value) {
        if (!std::isfinite(value) || value < 0.0f) {
                return 1.0;
        }
        return static_cast<double>(value);
}

inline int scaled_dimension(int base, double scale) {
        if (base <= 0) {
                return 0;
        }
        if (scale <= 0.0) {
                return 0;
        }
        const long long rounded = std::llround(static_cast<double>(base) * scale);
        if (rounded < 1) {
                return 1;
        }
        if (rounded > static_cast<long long>(std::numeric_limits<int>::max())) {
                return std::numeric_limits<int>::max();
        }
        return static_cast<int>(rounded);
}

using AudioCache = std::unordered_map<std::string, std::weak_ptr<Mix_Chunk>>;

AudioCache& get_audio_cache() {
        static AudioCache cache;
        return cache;
}

std::shared_ptr<Mix_Chunk> load_audio_clip(const std::string& path) {
        if (path.empty()) return {};
        auto& cache = get_audio_cache();
        auto it = cache.find(path);
        if (it != cache.end()) {
                if (auto existing = it->second.lock()) {
                        return existing;
                }
        }
        if (!std::filesystem::exists(path)) {
                std::cerr << "[Animation] Audio file not found: " << path << "\n";
                return {};
        }
        Mix_Chunk* raw = Mix_LoadWAV(path.c_str());
        if (!raw) {
                std::cerr << "[Animation] Failed to load audio '" << path << "': " << Mix_GetError() << "\n";
                return {};
        }
        std::shared_ptr<Mix_Chunk> chunk(raw, Mix_FreeChunk);
        cache[path] = chunk;
        return chunk;
}

#if SDL_VERSION_ATLEAST(2,0,12)
void apply_scale_mode(SDL_Texture* tex, const AssetInfo& info) {
        if (tex) {
                SDL_SetTextureScaleMode(tex, info.smooth_scaling ? SDL_ScaleModeBest : SDL_ScaleModeNearest);
        }
}
#else
void apply_scale_mode(SDL_Texture*, const AssetInfo&) {}
#endif

} // namespace

void AnimationLoader::load(Animation& animation,
                     const std::string& trigger,
                     const nlohmann::json& anim_json,
                     AssetInfo& info,
                     const std::string& dir_path,
                     const std::string& root_cache,
                     float scale_factor,
                     SDL_Renderer* renderer,
                     SDL_Texture*& base_sprite,
                     int& scaled_sprite_w,
                     int& scaled_sprite_h,
                     int& original_canvas_width,
                     int& original_canvas_height,
                     bool scaling_refresh_pending,
                     LoadDiagnostics* diagnostics)
{
        const auto load_start = std::chrono::steady_clock::now();
        bool       loaded_from_cache = false;
        bool       reused_animation  = false;
        bool       cache_invalid_detected = false;
        const auto flush_diagnostics = [&]() {
                if (diagnostics) {
                        diagnostics->cache_invalid = diagnostics->cache_invalid || cache_invalid_detected;
                }
        };
        const double safe_scale = sanitize_scale_factor(scale_factor);
        animation.clear_texture_cache();
        const bool prefer_cached = !scaling_refresh_pending;
        // Image effects are now handled by Python, so depth cues are not supported at runtime
        const bool supports_depthcue_cache = false;
        bool effect_hash_mismatch = false;
        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                  << " profile steps (pre-normalize): " << format_steps(animation.variant_steps_) << ", prefer_cached=" << (prefer_cached ? "true" : "false") << ", scaling_refresh_pending=" << (scaling_refresh_pending ? "true" : "false") << "\n";
        animation.variant_steps_ = info.scale_variants;
        render_pipeline::ScalingLogic::NormalizeVariantSteps(animation.variant_steps_);
        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                  << " normalized profile steps: " << format_steps(animation.variant_steps_) << "\n";
        
        // Parse source information first to determine if we need to inherit scale from source animation
        if (anim_json.contains("source")) {
                const auto& s = anim_json["source"];
                try {
                        if (s.contains("kind") && s["kind"].is_string())
                        animation.source.kind = s["kind"].get<std::string>();
			else
			animation.source.kind = "folder";
		} catch (...) { animation.source.kind = "folder"; }
		try {
			if (s.contains("path") && s["path"].is_string())
			animation.source.path = s["path"].get<std::string>();
			else
			animation.source.path.clear();
		} catch (...) { animation.source.path.clear(); }
		try {
			if (s.contains("name") && s["name"].is_string())
			animation.source.name = s["name"].get<std::string>();
			else
			animation.source.name.clear();
		} catch (...) { animation.source.name.clear(); }
	}
        
        // If sourcing from another animation, inherit its variant_steps to ensure same scale
        if (animation.source.kind == "animation" && !animation.source.name.empty()) {
                auto it = info.animations.find(animation.source.name);
                if (it != info.animations.end()) {
                        const Animation& src_anim = it->second;
                        if (!src_anim.variant_steps_.empty()) {
                                animation.variant_steps_ = src_anim.variant_steps_;
                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                          << " inherited variant_steps from source animation '" << animation.source.name 
                                          << "': " << format_steps(animation.variant_steps_) << "\n";
                        }
                }
        }
        
        // Calculate variant count after potential inheritance
        const std::size_t initial_variant_count = animation.variant_steps_.size();
        
        animation.flipped_source = anim_json.value("flipped_source", false);
        animation.flip_vertical_source = anim_json.value("flip_vertical_source", false);
        animation.flip_movement_horizontal = anim_json.value("flip_movement_horizontal", false);
        animation.flip_movement_vertical = anim_json.value("flip_movement_vertical", false);
        animation.reverse_source = anim_json.value("reverse_source", false);
        const bool inherit_source_movement = anim_json.value("inherit_source_movement", (animation.source.kind == "animation"));
        if (animation.source.kind == "animation" && anim_json.contains("derived_modifiers") &&
            anim_json["derived_modifiers"].is_object()) {
                const auto& modifiers = anim_json["derived_modifiers"];
                animation.reverse_source = modifiers.value("reverse", animation.reverse_source);
                animation.flipped_source = modifiers.value("flipX", animation.flipped_source);
                animation.flip_vertical_source = modifiers.value("flipY", animation.flip_vertical_source);
                animation.flip_movement_horizontal = modifiers.value("flipMovementX", animation.flip_movement_horizontal);
                animation.flip_movement_vertical = modifiers.value("flipMovementY", animation.flip_movement_vertical);
        } else if (animation.source.kind != "animation") {
                animation.flip_vertical_source = false;
                animation.flip_movement_horizontal = false;
                animation.flip_movement_vertical = false;
        }

        animation.locked         = anim_json.value("locked", false);

	// New explicit playback FPS; prefer this when present
	int parsed_fps = 0;
	try {
		if (anim_json.contains("fps")) {
			if (anim_json["fps"].is_number_integer()) parsed_fps = anim_json["fps"].get<int>();
			else if (anim_json["fps"].is_number()) parsed_fps = static_cast<int>(anim_json["fps"].get<double>());
		}
	} catch (...) { parsed_fps = 0; }
	if (parsed_fps <= 0) {

			parsed_fps = 24;
		}
	animation.playback_fps = parsed_fps;
	animation.loop      = anim_json.value("loop", true);
	animation.randomize = anim_json.value("randomize", false);
	animation.rnd_start = anim_json.value("rnd_start", false);
	animation.on_end_animation = anim_json.value("on_end", std::string{"default"});
        animation.child_asset_names_.clear();
        if (!info.animation_children.empty()) {
                animation.child_asset_names_ = info.animation_children;
        } else if (anim_json.contains("children") && anim_json["children"].is_array()) {
                for (const auto& child_entry : anim_json["children"]) {
                        if (!child_entry.is_string()) continue;
                        std::string name = child_entry.get<std::string>();
                        if (!name.empty()) {
                                animation.child_asset_names_.push_back(std::move(name));
                        }
                }
        }
        if (animation.child_asset_names_.empty() && animation.source.kind == "animation" && !animation.source.name.empty()) {
                auto src_child_it = info.animations.find(animation.source.name);
                if (src_child_it != info.animations.end()) {
                        animation.child_asset_names_ = src_child_it->second.child_assets();
                }
        }
        // Deduplicate child asset list while preserving order
        if (!animation.child_asset_names_.empty()) {
                std::unordered_set<std::string> seen;
                std::vector<std::string> unique;
                unique.reserve(animation.child_asset_names_.size());
                for (const auto& n : animation.child_asset_names_) {
                        if (n.empty()) continue;
                        if (seen.insert(n).second) {
                                unique.push_back(n);
                        }
                }
                animation.child_asset_names_.swap(unique);
        }
        animation.total_dx = 0;
        animation.total_dy = 0;
        animation.movement_paths_.clear();
        animation.audio_clip = Animation::AudioClip{};
        bool movement_specified = false;

        auto parse_movement_sequence = [&](const nlohmann::json& seq, std::vector<AnimationFrame>& dest) {
                bool specified = false;
                if (!seq.is_array()) return specified;
                auto clamp = [](int v) { return (v < 0) ? 0 : (v > 255 ? 255 : v); };
                for (const auto& mv : seq) {
                        if (!mv.is_array() || mv.size() < 2) continue;
                        AnimationFrame fm;
                        try { fm.dx = mv[0].get<int>(); } catch (...) { fm.dx = 0; }
                        try { fm.dy = mv[1].get<int>(); } catch (...) { fm.dy = 0; }
                        if (mv.size() >= 3 && mv[2].is_boolean()) {
                                fm.z_resort = mv[2].get<bool>();
                        }
                        if (mv.size() >= 4 && mv[3].is_array() && mv[3].size() >= 3) {
                                int r = 255, g = 255, b = 255;
                                try { r = clamp(mv[3][0].get<int>()); } catch (...) { r = 255; }
                                try { g = clamp(mv[3][1].get<int>()); } catch (...) { g = 255; }
                                try { b = clamp(mv[3][2].get<int>()); } catch (...) { b = 255; }
                                fm.rgb = SDL_Color{ static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255 };
                        }
                        fm.children.clear();
                        if (mv.size() >= 5 && mv[4].is_array()) {
                                for (const auto& child_entry : mv[4]) {
                                        if (!child_entry.is_array() || child_entry.empty()) {
                                                continue;
                                        }
                                        AnimationChildFrameData child_data;
                                        try {
                                                child_data.child_index = child_entry[0].get<int>();
                                        } catch (...) {
                                                child_data.child_index = -1;
                                        }
                                        if (child_entry.size() >= 2 && child_entry[1].is_number()) {
                                                try { child_data.dx = child_entry[1].get<int>(); } catch (...) { child_data.dx = 0; }
                                        }
                                        if (child_entry.size() >= 3 && child_entry[2].is_number()) {
                                                try { child_data.dy = child_entry[2].get<int>(); } catch (...) { child_data.dy = 0; }
                                        }
                                        if (child_entry.size() >= 4 && child_entry[3].is_number()) {
                                                try { child_data.degree = static_cast<float>(child_entry[3].get<double>()); } catch (...) { child_data.degree = 0.0f; }
                                        }
                                        if (child_entry.size() >= 5) {
                                                if (child_entry[4].is_boolean()) {
                                                        child_data.visible = child_entry[4].get<bool>();
                                                } else if (child_entry[4].is_number_integer()) {
                                                        child_data.visible = child_entry[4].get<int>() != 0;
                                                }
                                                // Non-boolean legacy entries are ignored so defaults stay visible.
                                        }
                                        if (child_entry.size() >= 6) {
                                                if (child_entry[5].is_boolean()) {
                                                        child_data.render_in_front = child_entry[5].get<bool>();
                                                } else if (child_entry[5].is_number_integer()) {
                                                        child_data.render_in_front = child_entry[5].get<int>() != 0;
                                                }
                                        }
                                        if (child_data.child_index < 0 ||
                                            child_data.child_index >= static_cast<int>(animation.child_asset_names_.size())) {
                                                std::cout << "[AnimationLoader] Ignoring child entry with invalid index " << child_data.child_index << " for asset list size " << animation.child_asset_names_.size() << "\n";
                                                continue;
                                        }
                                        fm.children.push_back(child_data);
                                }
                        }
                        if (!fm.children.empty()) {
                                // Debug: print mapping of child indices -> asset names for this frame
                                std::cout << "[AnimationLoader] Parsed frame children: ";
                                for (const auto& cd : fm.children) {
                                        std::cout << "(idx=" << cd.child_index << ", dx=" << cd.dx << ", dy=" << cd.dy << ")";
                                        if (cd.child_index >= 0 && cd.child_index < static_cast<int>(animation.child_asset_names_.size())) {
                                                std::cout << "->'" << animation.child_asset_names_[cd.child_index] << "' ";
                                        } else {
                                                std::cout << "->'<invalid>' ";
                                        }
                                }
                                std::cout << "\n";
                        }
                        if (fm.dx != 0 || fm.dy != 0 || mv.size() >= 3) {
                                specified = true;
                        }
                        dest.push_back(std::move(fm));
                }
                return specified;
};

        std::vector<std::vector<AnimationFrame>> parsed_paths;
        if (anim_json.contains("movement_paths") && anim_json["movement_paths"].is_array()) {
                for (const auto& path_json : anim_json["movement_paths"]) {
                        std::vector<AnimationFrame> path_frames;
                        bool specified = parse_movement_sequence(path_json, path_frames);
                        if (!path_frames.empty()) {
                                parsed_paths.push_back(std::move(path_frames));
                        } else {
                                parsed_paths.emplace_back();
                        }
                        movement_specified = movement_specified || specified;
                }
        }

        std::vector<AnimationFrame> primary_path;
        if (anim_json.contains("movement") && anim_json["movement"].is_array()) {
                bool specified = parse_movement_sequence(anim_json["movement"], primary_path);
                movement_specified = movement_specified || specified;
        }

        if (!primary_path.empty()) {
                parsed_paths.insert(parsed_paths.begin(), std::move(primary_path));
        }

        if (parsed_paths.empty()) {
                parsed_paths.emplace_back();
        }

        animation.movement_paths_ = std::move(parsed_paths);
        if (animation.source.kind == "animation" && !animation.source.name.empty()) {
                auto it = info.animations.find(animation.source.name);
                if (it != info.animations.end()) {
                        const Animation& src_anim = it->second;
                        if (!src_anim.frames.empty()) {
                                // Use copy constructor to create derived animation with all modifiers applied
                                reused_animation = true;
                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                          << " copying from source animation '" << animation.source.name << "'"
                                          << " (flipH=" << animation.flipped_source
                                          << ", flipV=" << animation.flip_vertical_source
                                          << ", reverse=" << animation.reverse_source << ")\n";
                                
                                if (!animation.copy_from(src_anim, animation.flipped_source, animation.flip_vertical_source, animation.reverse_source, renderer, info)) {
                                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                                  << " failed to copy from source animation\n";
                                        flush_diagnostics();
                                        return;
                                }
                        } else {
                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                          << " source animation '" << animation.source.name
                                          << "' is not loaded yet; skipping copy for now\n";
                        }
                } else {
                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                  << " missing source animation '" << animation.source.name << "'\n";
                }
        } else if (animation.source.kind == "folder") {
                // Simplified cache loading - assume PNGs exist and load them directly
                const fs::path cache_folder_path = fs::path(root_cache) / trigger;
                std::string cache_folder = cache_folder_path.string();
                
                std::size_t variant_count = animation.variant_steps_.size();
                if (variant_count == 0) {
                        animation.variant_steps_.push_back(1.0f);
                        variant_count = 1;
                        info.scale_variants = animation.variant_steps_;
                }

                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                          << " loading from cache_folder=" << cache_folder
                          << " variant_count=" << variant_count << "\n";

                // Check for .ready marker to ensure Python finished writing
                const fs::path ready_marker = cache_folder_path / ".ready";
                std::error_code ec;
                bool ready_exists = fs::exists(ready_marker, ec);
                if (!ready_exists || ec) {
                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                  << " .ready marker not found, cache may not be complete\n";
                        // Signal that cache is invalid so it gets regenerated
                        cache_invalid_detected = true;
                        // Continue anyway, but warn
                }

                // Build paths for each variant
                std::vector<VariantLayerPaths> variant_paths;
                variant_paths.reserve(variant_count);
                for (std::size_t idx = 0; idx < variant_count; ++idx) {
                        variant_paths.push_back(build_variant_layer_paths(cache_folder, animation.variant_steps_, idx));
                }

                // Lambda for freeing surface lists
                auto free_surface_lists = [](std::vector<std::vector<SDL_Surface*>>& lists) {
                        for (auto& list : lists) {
                                for (SDL_Surface* surf : list) {
                                        if (surf) {
                                                SDL_FreeSurface(surf);
                                        }
                                }
                                list.clear();
                        }
                };

                // Find the first variant that exists to determine frame count
                int frame_count = 0;
                std::size_t working_variant_idx = 0;
                for (std::size_t idx = 0; idx < variant_paths.size(); ++idx) {
                        const fs::path normal_folder_path(variant_paths[idx].normal_folder);
                        const fs::path test_frame = normal_folder_path / "0.png";
                        
                        std::error_code test_ec;
                        if (fs::exists(test_frame, test_ec) && !test_ec) {
                                frame_count = count_png_files(variant_paths[idx].normal_folder);
                                if (frame_count > 0) {
                                        working_variant_idx = idx;
                                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                                  << " using variant " << idx << " (scale=" 
                                                  << (idx < animation.variant_steps_.size() ? animation.variant_steps_[idx] : 0.0f)
                                                  << ") to determine frame_count=" << frame_count << "\n";
                                        break;
                                }
                        }
                }
                
                if (frame_count == 0) {
                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                  << " no cached frames found in any variant folder\n";
                        for (std::size_t idx = 0; idx < variant_paths.size(); ++idx) {
                                std::cout << "[AnimationLoader]   variant " << idx << " normal_folder=" 
                                          << variant_paths[idx].normal_folder << "\n";
                        }
                        flush_diagnostics();
                        return;
                }

                // Try to load surfaces from the expected cache locations
                std::vector<std::vector<SDL_Surface*>> variant_surfaces(variant_count);
                std::vector<std::vector<SDL_Surface*>> foreground_surfaces(variant_count);
                std::vector<std::vector<SDL_Surface*>> background_surfaces(variant_count);
                std::vector<std::vector<SDL_Surface*>> mask_surfaces(variant_count);
                std::vector<std::vector<SDL_Surface*>> depthcue_foreground_surfaces(variant_count);
                std::vector<std::vector<SDL_Surface*>> depthcue_background_surfaces(variant_count);

                bool all_surfaces_loaded = true;
                const bool needs_masks = info.is_shaded;
                for (std::size_t idx = 0; idx < variant_count; ++idx) {
                        const VariantLayerPaths& paths = variant_paths[idx];
                        std::vector<SDL_Surface*> loaded;
                        bool loaded_ok = CacheManager::load_surface_sequence(paths.normal_folder, frame_count, loaded);
                        if (loaded_ok && static_cast<int>(loaded.size()) == frame_count) {
                                variant_surfaces[idx] = std::move(loaded);
                        } else {
                                all_surfaces_loaded = false;
                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                          << " failed to load variant " << idx << " from " << paths.normal_folder << "\n";
                                break; // If any variant fails, we need to regenerate
                        }
                        
                        // Load foreground textures if available
                        std::vector<SDL_Surface*> fg_loaded;
                        if (CacheManager::load_surface_sequence(paths.foreground_folder, frame_count, fg_loaded) && 
                            static_cast<int>(fg_loaded.size()) == frame_count) {
                                foreground_surfaces[idx] = std::move(fg_loaded);
                        }
                        
                        // Load background textures if available
                        std::vector<SDL_Surface*> bg_loaded;
                        if (CacheManager::load_surface_sequence(paths.background_folder, frame_count, bg_loaded) && 
                            static_cast<int>(bg_loaded.size()) == frame_count) {
                                background_surfaces[idx] = std::move(bg_loaded);
                        }

                        // Load mask sequences if present
                        std::vector<SDL_Surface*> mask_loaded;
                        if (CacheManager::load_surface_sequence(paths.mask_folder, frame_count, mask_loaded) &&
                            static_cast<int>(mask_loaded.size()) == frame_count) {
                                mask_surfaces[idx] = std::move(mask_loaded);
                        } else if (needs_masks) {
                                all_surfaces_loaded = false;
                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                          << " missing masks for variant " << idx << " at " << paths.mask_folder << "\n";
                                break;
                        }

                        // Load depthcue foreground textures if available
                        std::vector<SDL_Surface*> dfg_loaded;
                        if (CacheManager::load_surface_sequence(paths.depthcue_foreground_folder, frame_count, dfg_loaded) && 
                            static_cast<int>(dfg_loaded.size()) == frame_count) {
                                depthcue_foreground_surfaces[idx] = std::move(dfg_loaded);
                        }

                        // Load depthcue background textures if available
                        std::vector<SDL_Surface*> dbg_loaded;
                        if (CacheManager::load_surface_sequence(paths.depthcue_background_folder, frame_count, dbg_loaded) &&
                            static_cast<int>(dbg_loaded.size()) == frame_count) {
                                depthcue_background_surfaces[idx] = std::move(dbg_loaded);
                        }
                }

                if (!all_surfaces_loaded || variant_surfaces[0].empty() || !variant_surfaces[0][0]) {
                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                  << " cache surfaces not found or incomplete, cannot load animation\n";
                        free_surface_lists(variant_surfaces);
                        free_surface_lists(foreground_surfaces);
                        free_surface_lists(background_surfaces);
                        free_surface_lists(mask_surfaces);
                        free_surface_lists(depthcue_foreground_surfaces);
                        free_surface_lists(depthcue_background_surfaces);
                        cache_invalid_detected = true;
                        flush_diagnostics();
                        return;
                }

                const int expected_frames = static_cast<int>(variant_surfaces[0].size());
                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                          << " loaded " << expected_frames << " cached frame(s) for "
                          << variant_count << " variant(s)\n";

                original_canvas_width  = variant_surfaces[0][0]->w;
                original_canvas_height = variant_surfaces[0][0]->h;
                scaled_sprite_w        = scaled_dimension(variant_surfaces[0][0]->w, safe_scale);
                scaled_sprite_h        = scaled_dimension(variant_surfaces[0][0]->h, safe_scale);

                // Cache is valid - load textures
                int orig_w = variant_surfaces[0][0]->w;
                int orig_h = variant_surfaces[0][0]->h;

                if ((scaled_sprite_w <= 0 || scaled_sprite_h <= 0) && orig_w > 0 && orig_h > 0) {
                        int fallback_w = scaled_dimension(orig_w, safe_scale);
                        int fallback_h = scaled_dimension(orig_h, safe_scale);
                        if (fallback_w <= 0) fallback_w = 1;
                        if (fallback_h <= 0) fallback_h = 1;
                        scaled_sprite_w = fallback_w;
                        scaled_sprite_h = fallback_h;
                }

                // Mask load handled above for cache path

                animation.frames.clear();
                animation.frame_cache_.clear();
                animation.frames.reserve(expected_frames);

                animation.frame_cache_.reserve(expected_frames);

                for (std::size_t frame_idx = 0; frame_idx < variant_surfaces[0].size(); ++frame_idx) {
                        Animation::FrameCache cache_entry;
                        cache_entry.resize(variant_count);
                        for (std::size_t variant_idx = 0; variant_idx < variant_count; ++variant_idx) {
                                SDL_Surface* surface = (frame_idx < variant_surfaces[variant_idx].size()) ? variant_surfaces[variant_idx][frame_idx] : nullptr;
                                SDL_Texture* tex_variant = nullptr;
                                if (surface) {
                                        tex_variant = CacheManager::surface_to_texture(renderer, surface);
                                        if (tex_variant) {
                                                apply_scale_mode(tex_variant, info);
                                        }
                                }
                                int tex_w = surface ? surface->w : 0;
                                int tex_h = surface ? surface->h : 0;
                                if (tex_variant && (tex_w == 0 || tex_h == 0)) {
                                        SDL_QueryTexture(tex_variant, nullptr, nullptr, &tex_w, &tex_h);
                                }
                                cache_entry.textures[variant_idx] = tex_variant;
                                cache_entry.widths[variant_idx]   = tex_w;
                                cache_entry.heights[variant_idx]  = tex_h;

                                // Load foreground overlay if available
                                SDL_Texture* fg_tex = nullptr;
                                if (frame_idx < foreground_surfaces[variant_idx].size() && foreground_surfaces[variant_idx][frame_idx]) {
                                        fg_tex = CacheManager::surface_to_texture(renderer, foreground_surfaces[variant_idx][frame_idx]);
                                        if (fg_tex) {
                                                apply_scale_mode(fg_tex, info);
                                        }
                                }
                                cache_entry.foreground_textures[variant_idx] = fg_tex;

                                // Load background overlay if available
                                SDL_Texture* bg_tex = nullptr;
                                if (frame_idx < background_surfaces[variant_idx].size() && background_surfaces[variant_idx][frame_idx]) {
                                        bg_tex = CacheManager::surface_to_texture(renderer, background_surfaces[variant_idx][frame_idx]);
                                        if (bg_tex) {
                                                apply_scale_mode(bg_tex, info);
                                        }
                                }
                                cache_entry.background_textures[variant_idx] = bg_tex;

                                SDL_Texture* mask_tex = nullptr;
                                if (frame_idx < mask_surfaces[variant_idx].size() && mask_surfaces[variant_idx][frame_idx]) {
                                        SDL_Surface* mask_surface = mask_surfaces[variant_idx][frame_idx];
                                        mask_tex = CacheManager::surface_to_texture(renderer, mask_surface);
                                        if (mask_tex) {
                                                apply_scale_mode(mask_tex, info);
                                        }
                                        int mask_w = mask_surface->w;
                                        int mask_h = mask_surface->h;
                                        cache_entry.mask_widths[variant_idx]  = mask_w;
                                        cache_entry.mask_heights[variant_idx] = mask_h;
                                } else {
                                        cache_entry.mask_widths[variant_idx]  = 0;
                                        cache_entry.mask_heights[variant_idx] = 0;
                                }
                                cache_entry.mask_textures[variant_idx] = mask_tex;

                                // Load depthcue foreground overlay if available
                                SDL_Texture* dfg_tex = nullptr;
                                if (frame_idx < depthcue_foreground_surfaces[variant_idx].size() && depthcue_foreground_surfaces[variant_idx][frame_idx]) {
                                        dfg_tex = CacheManager::surface_to_texture(renderer, depthcue_foreground_surfaces[variant_idx][frame_idx]);
                                        if (dfg_tex) {
                                                apply_scale_mode(dfg_tex, info);
                                        }
                                }
                                cache_entry.depthcue_foreground_textures[variant_idx] = dfg_tex;

                                // Load depthcue background overlay if available
                                SDL_Texture* dbg_tex = nullptr;
                                if (frame_idx < depthcue_background_surfaces[variant_idx].size() && depthcue_background_surfaces[variant_idx][frame_idx]) {
                                        dbg_tex = CacheManager::surface_to_texture(renderer, depthcue_background_surfaces[variant_idx][frame_idx]);
                                        if (dbg_tex) {
                                                apply_scale_mode(dbg_tex, info);
                                        }
                                }
                                cache_entry.depthcue_background_textures[variant_idx] = dbg_tex;
                        }
                        animation.frame_cache_.push_back(std::move(cache_entry));
                }

                free_surface_lists(variant_surfaces);
                free_surface_lists(foreground_surfaces);
                free_surface_lists(background_surfaces);
                free_surface_lists(mask_surfaces);
                free_surface_lists(depthcue_foreground_surfaces);
                free_surface_lists(depthcue_background_surfaces);

                // Flip processing disabled for cached loading
                if (animation.reverse_source && !animation.frame_cache_.empty()) {
                        std::reverse(animation.frame_cache_.begin(), animation.frame_cache_.end());
                }
                loaded_from_cache = true;
        }




        if (!movement_specified && animation.source.kind == "animation" && inherit_source_movement && !animation.source.name.empty()) {
                auto it = info.animations.find(animation.source.name);
                if (it != info.animations.end()) {
                        const Animation& src_anim = it->second;
                        animation.movement_paths_           = src_anim.movement_paths_;
                        if (!animation.movement_paths_.empty()) {
                                if (animation.reverse_source) {
                                        for (auto& path : animation.movement_paths_) {
                                                std::reverse(path.begin(), path.end());
                                        }
                                }
                                if (animation.flip_movement_horizontal) {
                                        for (auto& path : animation.movement_paths_) {
                                                for (auto& frame : path) {
                                                        frame.dx = -frame.dx;
                                                }
                                        }
                                }
                                if (animation.flip_movement_vertical) {
                                        for (auto& path : animation.movement_paths_) {
                                                for (auto& frame : path) {
                                                        frame.dy = -frame.dy;
                                                }
                                        }
                                }
                                movement_specified = true;
                        }
                }
        }
        // If movement is explicitly specified for a derived animation, still apply requested transforms
        if (movement_specified && animation.source.kind == "animation") {
                if (animation.reverse_source) {
                        for (auto& path : animation.movement_paths_) {
                                std::reverse(path.begin(), path.end());
                        }
                }
                if (animation.flip_movement_horizontal) {
                        for (auto& path : animation.movement_paths_) {
                                for (auto& frame : path) {
                                        frame.dx = -frame.dx;
                                }
                        }
                }
                if (animation.flip_movement_vertical) {
                        for (auto& path : animation.movement_paths_) {
                                for (auto& frame : path) {
                                        frame.dy = -frame.dy;
                                }
                        }
                }
        }
        const bool has_audio_json = anim_json.contains("audio") && anim_json["audio"].is_object();
        const nlohmann::json* audio_json = has_audio_json ? &anim_json["audio"] : nullptr;
        auto clamp_volume = [](int value) {
                if (value < 0) return 0;
                if (value > 100) return 100;
                return value;
};
        if (audio_json) {
                animation.audio_clip.volume = clamp_volume(audio_json->value("volume", animation.audio_clip.volume));
                animation.audio_clip.effects = audio_json->value("effects", animation.audio_clip.effects);
                try {
                        std::string clip_name = audio_json->value("name", std::string{});
                        if (!clip_name.empty()) {
                                animation.audio_clip.name = clip_name;
                                std::filesystem::path clip_path = std::filesystem::path(dir_path) / (clip_name + ".wav");
                                animation.audio_clip.path = clip_path.lexically_normal().string();
                                animation.audio_clip.chunk = load_audio_clip(animation.audio_clip.path);
                        }
                } catch (...) {

                }
        }
        if (!animation.audio_clip.chunk && animation.source.kind == "animation" && !animation.source.name.empty()) {
                auto it = info.animations.find(animation.source.name);
                if (it != info.animations.end()) {
                        animation.audio_clip = it->second.audio_clip;
                        if (audio_json) {
                                if (audio_json->contains("volume")) {
                                        animation.audio_clip.volume = clamp_volume(audio_json->value("volume", animation.audio_clip.volume));
                                }
                                if (audio_json->contains("effects")) {
                                        animation.audio_clip.effects = audio_json->value("effects", animation.audio_clip.effects);
                                }
                        }
                }
        }
        const std::size_t frame_count = animation.frame_cache_.size();
        if (animation.movement_paths_.empty()) {
                animation.movement_paths_.emplace_back();
        }

        animation.frames.clear();

        bool any_motion = false;
        for (std::size_t path_idx = 0; path_idx < animation.movement_paths_.size(); ++path_idx) {
                auto& path = animation.movement_paths_[path_idx];
                if (path.size() != frame_count) {
                        path.resize(frame_count);
                }
                for (std::size_t i = 0; i < path.size(); ++i) {
                        AnimationFrame& f = path[i];
                        f.prev        = (i > 0) ? &path[i - 1] : nullptr;
                        f.next        = (i + 1 < path.size()) ? &path[i + 1] : nullptr;
                        f.is_first    = (i == 0);
                        f.is_last     = (i + 1 == path.size());
                        f.frame_index = static_cast<int>(i);
                        
                        f.variants.clear();
                        if (i < animation.frame_cache_.size()) {
                            const auto& cache = animation.frame_cache_[i];
                            for (size_t v = 0; v < cache.textures.size(); ++v) {
                                FrameVariant variant;
                                variant.varient = static_cast<int>(v);
                                variant.base_texture = cache.textures[v];
                                if (v < cache.foreground_textures.size()) variant.foreground_texture = cache.foreground_textures[v];
                                if (v < cache.background_textures.size()) variant.background_texture = cache.background_textures[v];
                                if (v < cache.mask_textures.size()) variant.shadow_mask_texture = cache.mask_textures[v];
                                f.variants.push_back(variant);
                            }
                        }

                        if (f.dx != 0 || f.dy != 0) {
                                any_motion = true;
                        }
                        // Only add frames from the primary path (index 0) to the frames list
                        if (path_idx == 0) {
                                animation.frames.push_back(&f);
                        }
                }
        }

        animation.total_dx = 0;
        animation.total_dy = 0;
        if (!animation.movement_paths_.empty()) {
                const auto& primary = animation.movement_paths_.front();
                for (const auto& frame : primary) {
                        animation.total_dx += frame.dx;
                        animation.total_dy += frame.dy;
                        if (frame.dx != 0 || frame.dy != 0) {
                                any_motion = true;
                        }
                }
        }

        animation.movment = any_motion;
        animation.number_of_frames = static_cast<int>(frame_count);
        if (trigger == "default" && !animation.frames.empty() && !animation.frames[0]->variants.empty()) {
                base_sprite = animation.frames[0]->variants[0].base_texture;
                info.preview_texture = animation.frames[0]->variants[0].base_texture;
        }
        
        // Set preview texture
        if (!animation.frames.empty() && !animation.frames[0]->variants.empty()) {
            animation.preview_texture = animation.frames[0]->variants[0].base_texture;
        } else {
            animation.preview_texture = nullptr;
        }

        int frame_width  = 0;
        int frame_height = 0;
        if (!animation.frame_cache_.empty()) {
                frame_width  = animation.frame_cache_[0].widths[0];
                frame_height = animation.frame_cache_[0].heights[0];
                if ((frame_width <= 0 || frame_height <= 0) && animation.frame_cache_[0].textures[0]) {
                        SDL_QueryTexture(animation.frame_cache_[0].textures[0], nullptr, nullptr, &frame_width, &frame_height);
                }
        }

        const auto load_end        = std::chrono::steady_clock::now();
        const double elapsed_secs  = std::chrono::duration<double>(load_end - load_start).count();
        std::string   origin_label = loaded_from_cache ? "cache" : "source";
        if (reused_animation) {
                origin_label = "animation '" + animation.source.name + "'";
        }

        {
                std::ostringstream oss;
                oss << "[AnimationLoader] " << info.name << "::" << trigger
                    << " -> " << animation.frames.size() << " frame(s)";
                if (frame_width > 0 && frame_height > 0) {
                        oss << " @ " << frame_width << "x" << frame_height;
                }
                oss << " from " << origin_label << " in " << std::fixed << std::setprecision(3) << elapsed_secs << "s";
                vibble::log::debug(oss.str());
        }
        flush_diagnostics();
}
