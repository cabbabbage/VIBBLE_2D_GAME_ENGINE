#include "animation.hpp"
#include "asset/asset_info.hpp"
#include "utils/cache_manager.hpp"
#include "utils/generate_faded_mask.hpp"
#include "render_pipeline/ScalingLogic.hpp"
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
namespace fs = std::filesystem;

namespace {
#if SDL_VERSION_ATLEAST(2,0,12)
void apply_scale_mode(SDL_Texture* tex, const AssetInfo& info);
#endif

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

std::string format_percent_steps(const std::vector<int>& steps) {
        std::ostringstream oss;
        oss << '[';
        for (std::size_t i = 0; i < steps.size(); ++i) {
                if (i != 0) {
                        oss << ", ";
                }
                oss << steps[i];
        }
        oss << ']';
        return oss.str();
}

using AudioCache = std::unordered_map<std::string, std::weak_ptr<Mix_Chunk>>;

constexpr int kAnimationCacheVersion = 3;

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

}

Animation::Animation() = default;

void Animation::clear_texture_cache() {
        for (auto& cache_entry : frame_cache_) {
                for (SDL_Texture*& tex : cache_entry.textures) {
                        if (tex) {
                                SDL_DestroyTexture(tex);
                                tex = nullptr;
                        }
                }
                for (SDL_Texture*& mask_tex : cache_entry.mask_textures) {
                        if (mask_tex) {
                                SDL_DestroyTexture(mask_tex);
                                mask_tex = nullptr;
                        }
                }
        }
        frame_cache_.clear();
        frames.clear();
        mask_frames.clear();
}

SDL_Texture* Animation::frame_variant(std::size_t frame_index, std::size_t variant_index) const {
        if (frame_index >= frame_cache_.size()) {
                return nullptr;
        }
        const FrameCache& cache_entry = frame_cache_[frame_index];
        if (cache_entry.textures.empty()) {
                return nullptr;
        }
        if (variant_index >= cache_entry.textures.size() || !cache_entry.textures[variant_index]) {
                return cache_entry.textures[0];
        }
        return cache_entry.textures[variant_index];
}

SDL_Texture* Animation::mask_variant(std::size_t frame_index, std::size_t variant_index) const {
        if (frame_index >= frame_cache_.size()) {
                return nullptr;
        }
        const FrameCache& cache_entry = frame_cache_[frame_index];
        if (cache_entry.mask_textures.empty()) {
                return nullptr;
        }
        if (variant_index >= cache_entry.mask_textures.size() || !cache_entry.mask_textures[variant_index]) {
                return cache_entry.mask_textures[0];
        }
        return cache_entry.mask_textures[variant_index];
}

void Animation::load(const std::string& trigger,
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
                     bool scaling_refresh_pending)
{
        CacheManager cache;
        const auto load_start = std::chrono::steady_clock::now();
        bool       loaded_from_cache = false;
        bool       reused_animation  = false;
        const double safe_scale = sanitize_scale_factor(scale_factor);
        clear_texture_cache();
        const bool prefer_cached = !scaling_refresh_pending;
        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                  << " profile steps (pre-normalize): " << format_steps(variant_steps_)
                  << ", prefer_cached=" << (prefer_cached ? "true" : "false")
                  << ", scaling_refresh_pending=" << (scaling_refresh_pending ? "true" : "false")
                  << "\n";
        auto normalize_steps = [](std::vector<float>& steps) {
                if (steps.empty()) {
                        steps.push_back(1.0f);
                        return;
                }
                std::sort(steps.begin(), steps.end(), std::greater<float>());
                steps.erase(std::unique(steps.begin(), steps.end(), [](float a, float b) {
                        return std::fabs(a - b) <= 1e-4f;
                }), steps.end());
                if (steps.empty()) {
                        steps.push_back(1.0f);
                        return;
                }
                if (std::fabs(steps.front() - 1.0f) > 1e-4f) {
                        steps.insert(steps.begin(), 1.0f);
                }
        };
        variant_steps_ = info.scale_variants;
        if (variant_steps_.empty()) {
                const auto& defaults = render_pipeline::ScalingLogic::DefaultScaleSteps();
                variant_steps_.assign(defaults.begin(), defaults.end());
        }
        normalize_steps(variant_steps_);
        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                  << " normalized profile steps: " << format_steps(variant_steps_)
                  << "\n";
        const std::size_t initial_variant_count = variant_steps_.size();
        if (anim_json.contains("source")) {
                const auto& s = anim_json["source"];
                try {
                        if (s.contains("kind") && s["kind"].is_string())
                        source.kind = s["kind"].get<std::string>();
			else
			source.kind = "folder";
		} catch (...) { source.kind = "folder"; }
		try {
			if (s.contains("path") && s["path"].is_string())
			source.path = s["path"].get<std::string>();
			else
			source.path.clear();
		} catch (...) { source.path.clear(); }
		try {
			if (s.contains("name") && s["name"].is_string())
			source.name = s["name"].get<std::string>();
			else
			source.name.clear();
		} catch (...) { source.name.clear(); }
	}
	flipped_source = anim_json.value("flipped_source", false);
	reverse_source = anim_json.value("reverse_source", false);
	locked         = anim_json.value("locked", false);
	speed_factor   = anim_json.value("speed_factor", 1.0f);
	loop      = anim_json.value("loop", false);
	randomize = anim_json.value("randomize", false);
	rnd_start = anim_json.value("rnd_start", false);
	on_end_animation = anim_json.value("on_end", std::string{"default"});
        total_dx = 0;
        total_dy = 0;
        movement_paths_.clear();
        audio_clip = AudioClip{};
        bool movement_specified = false;

        auto parse_movement_sequence = [](const nlohmann::json& seq, std::vector<AnimationFrame>& dest) {
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
                        if (fm.dx != 0 || fm.dy != 0 || mv.size() >= 3) {
                                specified = true;
                        }
                        dest.push_back(fm);
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

        movement_paths_ = std::move(parsed_paths);
        if (source.kind == "animation" && !source.name.empty()) {
                auto it = info.animations.find(source.name);
                if (it != info.animations.end()) {
                        const Animation& src_anim = it->second;
                        if (!src_anim.frames.empty()) {
                                reused_animation = true;
                                std::vector<SDL_Texture*> new_frames;
                                std::vector<FrameCache>   new_caches;
                                std::vector<SDL_Texture*> new_mask_frames;
                                new_frames.reserve(src_anim.frames.size());
                                new_caches.reserve(src_anim.frames.size());
                                new_mask_frames.reserve(src_anim.frames.size());
                                for (std::size_t frame_idx = 0; frame_idx < src_anim.frames.size(); ++frame_idx) {
                                        FrameCache cache_entry;
                                        cache_entry.resize(initial_variant_count);
                                        bool base_ok = false;
                                        SDL_Texture* base_mask = nullptr;
                                        for (std::size_t variant_idx = 0; variant_idx < initial_variant_count; ++variant_idx) {
                                                SDL_Texture* source_tex = src_anim.frame_variant(frame_idx, variant_idx);
                                                if (!source_tex) {
                                                        cache_entry.textures[variant_idx] = nullptr;
                                                        cache_entry.widths[variant_idx]   = 0;
                                                        cache_entry.heights[variant_idx]  = 0;
                                                        cache_entry.mask_textures[variant_idx] = nullptr;
                                                        cache_entry.mask_widths[variant_idx]   = 0;
                                                        cache_entry.mask_heights[variant_idx]  = 0;
                                                        continue;
                                                }
                                                Uint32 fmt = SDL_PIXELFORMAT_RGBA8888;
                                                int access = 0;
                                                int tex_w = 0;
                                                int tex_h = 0;
                                                if (SDL_QueryTexture(source_tex, &fmt, &access, &tex_w, &tex_h) != 0 || tex_w <= 0 || tex_h <= 0) {
                                                        cache_entry.textures[variant_idx] = nullptr;
                                                        cache_entry.widths[variant_idx]   = 0;
                                                        cache_entry.heights[variant_idx]  = 0;
                                                        continue;
                                                }
                                                SDL_Texture* dst = SDL_CreateTexture(renderer, fmt, SDL_TEXTUREACCESS_TARGET, tex_w, tex_h);
                                                if (!dst) {
                                                        cache_entry.textures[variant_idx] = nullptr;
                                                        cache_entry.widths[variant_idx]   = 0;
                                                        cache_entry.heights[variant_idx]  = 0;
                                                        continue;
                                                }
                                                SDL_SetTextureBlendMode(dst, SDL_BLENDMODE_BLEND);
                                                apply_scale_mode(dst, info);
                                                SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
                                                SDL_SetRenderTarget(renderer, dst);
                                                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                                                SDL_RenderClear(renderer);
                                                SDL_Rect r{0, 0, tex_w, tex_h};
                                                SDL_RenderCopy(renderer, source_tex, nullptr, &r);
                                                SDL_SetRenderTarget(renderer, prev_target);
                                                cache_entry.textures[variant_idx] = dst;
                                                cache_entry.widths[variant_idx]   = tex_w;
                                                cache_entry.heights[variant_idx]  = tex_h;
                                                if (variant_idx == 0) {
                                                        base_ok = true;
                                                }

                                                SDL_Texture* source_mask = src_anim.mask_variant(frame_idx, variant_idx);
                                                SDL_Texture* mask_copy   = nullptr;
                                                int mask_w = 0;
                                                int mask_h = 0;
                                                if (source_mask) {
                                                        Uint32 mask_fmt = SDL_PIXELFORMAT_RGBA8888;
                                                        int mask_access = 0;
                                                        if (SDL_QueryTexture(source_mask, &mask_fmt, &mask_access, &mask_w, &mask_h) != 0 || mask_w <= 0 || mask_h <= 0) {
                                                                mask_w = 0;
                                                                mask_h = 0;
                                                        } else {
                                                                SDL_Texture* prev_target_mask = SDL_GetRenderTarget(renderer);
                                                                mask_copy = SDL_CreateTexture(renderer, mask_fmt, SDL_TEXTUREACCESS_TARGET, mask_w, mask_h);
                                                                if (mask_copy) {
                                                                        SDL_SetTextureBlendMode(mask_copy, SDL_BLENDMODE_BLEND);
                                                                        apply_scale_mode(mask_copy, info);
                                                                        SDL_SetRenderTarget(renderer, mask_copy);
                                                                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                                                                        SDL_RenderClear(renderer);
                                                                        SDL_Rect mask_rect{0, 0, mask_w, mask_h};
                                                                        SDL_RenderCopy(renderer, source_mask, nullptr, &mask_rect);
                                                                        SDL_SetRenderTarget(renderer, prev_target_mask);
                                                                } else {
                                                                        mask_w = 0;
                                                                        mask_h = 0;
                                                                        SDL_SetRenderTarget(renderer, prev_target_mask);
                                                                }
                                                        }
                                                }
                                                cache_entry.mask_textures[variant_idx] = mask_copy;
                                                if (!mask_copy) {
                                                        mask_w = 0;
                                                        mask_h = 0;
                                                }
                                                cache_entry.mask_widths[variant_idx]   = mask_w;
                                                cache_entry.mask_heights[variant_idx]  = mask_h;
                                                if (variant_idx == 0) {
                                                        base_mask = mask_copy;
                                                }
                                        }
                                        if (!base_ok || !cache_entry.textures[0]) {
                                                for (SDL_Texture*& tex : cache_entry.textures) {
                                                        if (tex) {
                                                                SDL_DestroyTexture(tex);
                                                                tex = nullptr;
                                                        }
                                                }
                                                for (SDL_Texture*& mask_tex : cache_entry.mask_textures) {
                                                        if (mask_tex) {
                                                                SDL_DestroyTexture(mask_tex);
                                                                mask_tex = nullptr;
                                                        }
                                                }
                                                continue;
                                        }
                                        new_frames.push_back(cache_entry.textures[0]);
                                        new_mask_frames.push_back(base_mask);
                                        new_caches.push_back(std::move(cache_entry));
                                }
                                frames.insert(frames.end(), new_frames.begin(), new_frames.end());
                                frame_cache_.insert(frame_cache_.end(), std::make_move_iterator(new_caches.begin()), std::make_move_iterator(new_caches.end()));
                                mask_frames.insert(mask_frames.end(), new_mask_frames.begin(), new_mask_frames.end());
                        }
                }
        } else {
                std::string src_folder   = dir_path + "/" + source.path;
                std::string cache_folder = root_cache + "/" + trigger;
                std::string meta_file    = cache_folder + "/metadata.json";
		int expected_frames = 0;
		int orig_w = 0, orig_h = 0;
		while (true) {
			std::string f = src_folder + "/" + std::to_string(expected_frames) + ".png";
			if (!fs::exists(f)) break;
			if (expected_frames == 0) {
					if (SDL_Surface* s = IMG_Load(f.c_str())) {
								orig_w = s->w;
								orig_h = s->h;
								SDL_FreeSurface(s);
					}
			}
			++expected_frames;
		}
		if (expected_frames == 0) return;
                bool use_cache = false;
		nlohmann::json meta;
                std::vector<int> expected_steps = render_pipeline::ScalingLogic::PercentSteps(variant_steps_);
                const std::uint64_t expected_revision = info.scale_profile_revision;
                if (cache.load_metadata(meta_file, meta)) {
                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                  << " found metadata (revision "
                                  << meta.value("scale_profile_revision", static_cast<std::uint64_t>(0))
                                  << ") expecting revision " << expected_revision << "\n";
                        bool meta_ok = (
                            meta.value("cache_version", 0) == kAnimationCacheVersion &&
                            meta.value("frame_count", -1) == expected_frames &&
                            meta.value("original_width", -1) == orig_w &&
                            meta.value("original_height", -1) == orig_h &&
                            meta.value("has_masks", false));
                        if (meta_ok) {
                                if (meta.contains("scale_steps") && meta["scale_steps"].is_array()) {
                                        const auto& stored = meta["scale_steps"];
                                        std::vector<int> stored_steps;
                                        stored_steps.reserve(stored.size());
                                        bool stored_valid = true;
                                        for (const auto& value : stored) {
                                                if (!value.is_number_integer()) {
                                                        stored_valid = false;
                                                        break;
                                                }
                                                stored_steps.push_back(value.get<int>());
                                        }
                                        if (!stored_valid) {
                                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                                          << " metadata scale_steps invalid -> forcing rebuild\n";
                                                meta_ok = false;
                                        } else if (stored_steps != expected_steps) {
                                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                                          << " metadata steps " << format_percent_steps(stored_steps)
                                                          << " differ from profile " << format_percent_steps(expected_steps)
                                                          << (prefer_cached ? " -> adopting cached ordering" : " -> rebuild required")
                                                          << "\n";
                                                if (prefer_cached) {
                                                        std::vector<float> adopted_steps;
                                                        adopted_steps.reserve(stored_steps.size());
                                                        for (int percent : stored_steps) {
                                                                const float scale = std::clamp(percent / 100.0f, 0.05f, 2.0f);
                                                                adopted_steps.push_back(scale);
                                                        }
                                                        variant_steps_ = std::move(adopted_steps);
                                                        normalize_steps(variant_steps_);
                                                        if (variant_steps_.empty()) {
                                                                meta_ok = false;
                                                        } else {
                                                                info.scale_variants = variant_steps_;
                                                                expected_steps = render_pipeline::ScalingLogic::PercentSteps(variant_steps_);
                                                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                                                          << " updated runtime scale variants to cached ordering: "
                                                                          << format_steps(variant_steps_) << "\n";
                                                        }
                                                } else {
                                                        meta_ok = false;
                                                }
                                        }
                                } else {
                                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                                  << " metadata missing scale_steps -> forcing rebuild\n";
                                        meta_ok = false;
                                }
                        }
                        if (meta_ok) {
                                const std::uint64_t stored_revision = meta.value("scale_profile_revision", static_cast<std::uint64_t>(0));
                                if (!prefer_cached && stored_revision != expected_revision) {
                                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                                  << " metadata revision mismatch (" << stored_revision << " != "
                                                  << expected_revision << ") -> rebuild\n";
                                        meta_ok = false;
                                }
                        }
                        use_cache = meta_ok;
                        if (!use_cache) {
                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                          << " cache metadata rejected -> rebuilding variants\n";
                        }
                } else {
                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                  << " cache metadata missing -> rebuilding variants\n";
                }

                std::size_t variant_count = variant_steps_.size();
                if (variant_count == 0) {
                        variant_steps_.push_back(1.0f);
                        variant_count = 1;
                        info.scale_variants = variant_steps_;
                        expected_steps = render_pipeline::ScalingLogic::PercentSteps(variant_steps_);
                }
                if (!prefer_cached) {
                        use_cache = false;
                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                  << " prefer_cached disabled -> forcing rebuild\n";
                }
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
                const std::string mask_cache_folder = cache_folder + "/masks";
                std::vector<std::vector<SDL_Surface*>> variant_surfaces(variant_count);
                GenerateFadedMask::MaskVariants mask_surfaces;
                bool masks_loaded_from_cache = false;
                if (use_cache) {
                        bool variants_loaded = true;
                        for (std::size_t idx = 0; idx < variant_count; ++idx) {
                                std::string variant_path = render_pipeline::ScalingLogic::VariantFolder(cache_folder, variant_steps_, idx);
                                std::vector<SDL_Surface*> loaded;
                                if (!cache.load_surface_sequence(variant_path, expected_frames, loaded)) {
                                        if (idx == 0 && cache.load_surface_sequence(cache_folder, expected_frames, loaded)) {
                                                // legacy cache, continue but ensure metadata regenerated later
                                                use_cache = false;
                                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                                          << " legacy cache format detected for variant " << idx
                                                          << " -> metadata refresh required\n";
                                        } else {
                                                variants_loaded = false;
                                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                                          << " missing cached surfaces for variant " << idx
                                                          << " -> forcing rebuild\n";
                                                break;
                                        }
                                }
                                variant_surfaces[idx] = std::move(loaded);
                        }
                        if (!variants_loaded) {
                                free_surface_lists(variant_surfaces);
                                use_cache = false;
                        } else if (use_cache) {
                                original_canvas_width  = orig_w;
                                original_canvas_height = orig_h;
                                if (!variant_surfaces[0].empty() && variant_surfaces[0][0]) {
                                        scaled_sprite_w = scaled_dimension(variant_surfaces[0][0]->w, safe_scale);
                                        scaled_sprite_h = scaled_dimension(variant_surfaces[0][0]->h, safe_scale);
                                }
                        }
                }

                if (!use_cache && prefer_cached) {
                        const auto& defaults = render_pipeline::ScalingLogic::DefaultScaleSteps();
                        variant_steps_.assign(defaults.begin(), defaults.end());
                        normalize_steps(variant_steps_);
                        info.scale_variants = variant_steps_;
                        variant_count = variant_steps_.size();
                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                  << " reverting to default scale steps: " << format_steps(variant_steps_)
                                  << "\n";
                        if (variant_count == 0) {
                                variant_steps_.push_back(1.0f);
                                variant_count = 1;
                        }
                        expected_steps = render_pipeline::ScalingLogic::PercentSteps(variant_steps_);
                        variant_surfaces.clear();
                        variant_surfaces.resize(variant_count);
                }

                auto cleanup_variant_directories = [&](const std::string& folder) {
                        try {
                                std::unordered_set<std::string> expected_names;
                                expected_names.reserve(variant_count);
                                for (std::size_t idx = 0; idx < variant_count; ++idx) {
                                        const int percent = (idx < expected_steps.size()) ? expected_steps[idx] : static_cast<int>(std::lround(variant_steps_[idx] * 100.0f));
                                        expected_names.insert("scale_" + std::to_string(percent));
                                }
                                for (const auto& entry : fs::directory_iterator(folder)) {
                                        if (!entry.is_directory()) {
                                                continue;
                                        }
                                        const std::string name = entry.path().filename().string();
                                        if (name.rfind("scale_", 0) == 0 && expected_names.find(name) == expected_names.end()) {
                                                fs::remove_all(entry.path());
                                        }
                                }
                        } catch (...) {
                        }
                };

                if (!use_cache) {
                        cleanup_variant_directories(cache_folder);
                        cleanup_variant_directories(mask_cache_folder);
                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                  << " generating variant surfaces for " << expected_frames
                                  << " frame(s) across " << variant_count << " step(s)\n";
                        for (auto& list : variant_surfaces) {
                                for (SDL_Surface* surf : list) {
                                        if (surf) SDL_FreeSurface(surf);
                                }
                                list.clear();
                        }
                        std::vector<SDL_Surface*> base_surfaces;
                        base_surfaces.reserve(expected_frames);
                        for (int i = 0; i < expected_frames; ++i) {
                                        std::string f = src_folder + "/" + std::to_string(i) + ".png";
                                        int new_w = 0, new_h = 0;
                                        SDL_Surface* scaled = cache.load_and_scale_surface(f, 1.0f, new_w, new_h);
					if (!scaled) {
								std::cerr << "[Animation] Failed to load or scale: " << f << "\n";
								continue;
					}
					if (i == 0) {
								original_canvas_width  = orig_w;
								original_canvas_height = orig_h;
                                                                scaled_sprite_w = scaled_dimension(new_w, safe_scale);
                                                                scaled_sprite_h = scaled_dimension(new_h, safe_scale);
					}
                                        base_surfaces.push_back(scaled);
                        }
                        if (base_surfaces.size() != static_cast<std::size_t>(expected_frames)) {
                                for (SDL_Surface* surf : base_surfaces) {
                                        if (surf) SDL_FreeSurface(surf);
                                }
                                return;
                        }
                        variant_surfaces[0] = std::move(base_surfaces);
                        for (std::size_t idx = 1; idx < variant_count; ++idx) {
                                const float scale_step = variant_steps_[idx];
                                variant_surfaces[idx].reserve(variant_surfaces[0].size());
                                for (SDL_Surface* base_surface : variant_surfaces[0]) {
                                        SDL_Surface* scaled = render_pipeline::CreateScaledSurface(base_surface, scale_step);
                                        variant_surfaces[idx].push_back(scaled);
                                }
                        }
                        for (std::size_t idx = 0; idx < variant_count; ++idx) {
                                const std::string variant_path = render_pipeline::ScalingLogic::VariantFolder(cache_folder, variant_steps_, idx);
                                cache.save_surface_sequence(variant_path, variant_surfaces[idx]);
                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                          << " stored " << variant_surfaces[idx].size()
                                          << " frame(s) for variant index " << idx
                                          << " (scale " << std::fixed << std::setprecision(2) << variant_steps_[idx]
                                          << std::defaultfloat << ")\n";
                        }
                        nlohmann::json new_meta;
                        new_meta["cache_version"]    = kAnimationCacheVersion;
                        new_meta["frame_count"]     = expected_frames;
                        new_meta["original_width"]  = orig_w;
                        new_meta["original_height"] = orig_h;
                        nlohmann::json step_arr = nlohmann::json::array();
                        for (std::size_t idx = 0; idx < expected_steps.size(); ++idx) {
                                step_arr.push_back(expected_steps[idx]);
                        }
                        new_meta["scale_steps"] = std::move(step_arr);
                        new_meta["scale_profile_revision"] = expected_revision;
                        new_meta["has_masks"] = true;
                        cache.save_metadata(meta_file, new_meta);
                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                  << " wrote metadata with steps "
                                  << format_percent_steps(expected_steps)
                                  << " revision " << expected_revision << "\n";
                }

                auto mask_result = GenerateFadedMask::BuildMasks(info.name, trigger, expected_steps, variant_surfaces);
                mask_surfaces            = std::move(mask_result.first);
                masks_loaded_from_cache  = mask_result.second;
                if (mask_surfaces.size() != variant_surfaces.size()) {
                        mask_surfaces.resize(variant_surfaces.size());
                }
                if (masks_loaded_from_cache) {
                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                  << " loaded faded mask surfaces from cache\n";
                } else {
                        const std::size_t mask_frame_count = (!mask_surfaces.empty() && !mask_surfaces.front().empty())
                                                                 ? mask_surfaces.front().size()
                                                                 : 0;
                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                  << " generated " << mask_frame_count
                                  << " faded mask frame(s) across " << mask_surfaces.size()
                                  << " variant(s)\n";
                }

                if (!variant_surfaces[0].empty() && variant_surfaces[0][0] && (scaled_sprite_w <= 0 || scaled_sprite_h <= 0)) {
                        scaled_sprite_w = scaled_dimension(variant_surfaces[0][0]->w, safe_scale);
                        scaled_sprite_h = scaled_dimension(variant_surfaces[0][0]->h, safe_scale);
                }
                if (original_canvas_width <= 0 && orig_w > 0) {
                        original_canvas_width = orig_w;
                }
                if (original_canvas_height <= 0 && orig_h > 0) {
                        original_canvas_height = orig_h;
                }
                if ((scaled_sprite_w <= 0 || scaled_sprite_h <= 0) && orig_w > 0 && orig_h > 0) {
                        int fallback_w = scaled_dimension(orig_w, safe_scale);
                        int fallback_h = scaled_dimension(orig_h, safe_scale);
                        if (fallback_w <= 0) fallback_w = 1;
                        if (fallback_h <= 0) fallback_h = 1;
                        scaled_sprite_w = fallback_w;
                        scaled_sprite_h = fallback_h;
                }
                frames.clear();
                mask_frames.clear();
                frame_cache_.clear();
                frames.reserve(expected_frames);
                mask_frames.reserve(expected_frames);
                frame_cache_.reserve(expected_frames);

                for (std::size_t frame_idx = 0; frame_idx < variant_surfaces[0].size(); ++frame_idx) {
                        FrameCache cache_entry;
                        cache_entry.resize(variant_count);
                        SDL_Texture* variant0_mask = nullptr;
                        for (std::size_t variant_idx = 0; variant_idx < variant_count; ++variant_idx) {
                                SDL_Surface* surface = (frame_idx < variant_surfaces[variant_idx].size())
                                                        ? variant_surfaces[variant_idx][frame_idx]
                                                        : nullptr;
                                SDL_Texture* tex_variant = nullptr;
                                if (surface) {
                                        tex_variant = cache.surface_to_texture(renderer, surface);
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

                                SDL_Surface* mask_surface = (variant_idx < mask_surfaces.size() && frame_idx < mask_surfaces[variant_idx].size())
                                                                 ? mask_surfaces[variant_idx][frame_idx]
                                                                 : nullptr;
                                SDL_Texture* mask_variant = nullptr;
                                int mask_w = mask_surface ? mask_surface->w : 0;
                                int mask_h = mask_surface ? mask_surface->h : 0;
                                if (mask_surface) {
                                        mask_variant = cache.surface_to_texture(renderer, mask_surface);
                                        if (mask_variant) {
                                                apply_scale_mode(mask_variant, info);
                                        }
                                        if (mask_variant && (mask_w == 0 || mask_h == 0)) {
                                                SDL_QueryTexture(mask_variant, nullptr, nullptr, &mask_w, &mask_h);
                                        }
                                }
                                if (!mask_variant) {
                                        mask_w = 0;
                                        mask_h = 0;
                                }
                                cache_entry.mask_textures[variant_idx] = mask_variant;
                                cache_entry.mask_widths[variant_idx]   = mask_w;
                                cache_entry.mask_heights[variant_idx]  = mask_h;
                                if (variant_idx == 0) {
                                        variant0_mask = mask_variant;
                                }
                                if (variant_idx == 0) {
                                        frames.push_back(tex_variant);
                                }
                        }
                        mask_frames.push_back(variant0_mask);
                        frame_cache_.push_back(std::move(cache_entry));
                }

                free_surface_lists(variant_surfaces);
                free_surface_lists(mask_surfaces);
                if (flipped_source && renderer && !frame_cache_.empty()) {
                        for (std::size_t frame_index = 0; frame_index < frame_cache_.size(); ++frame_index) {
                                FrameCache& cache_entry = frame_cache_[frame_index];
                                for (std::size_t variant_idx = 0; variant_idx < cache_entry.textures.size(); ++variant_idx) {
                                        SDL_Texture* src_tex = cache_entry.textures[variant_idx];
                                        if (!src_tex) {
                                                continue;
                                        }
                                        Uint32 fmt = SDL_PIXELFORMAT_RGBA8888;
                                        int access = 0;
                                        int tex_w = cache_entry.widths[variant_idx];
                                        int tex_h = cache_entry.heights[variant_idx];
                                        if (tex_w <= 0 || tex_h <= 0) {
                                                if (SDL_QueryTexture(src_tex, &fmt, &access, &tex_w, &tex_h) != 0 || tex_w <= 0 || tex_h <= 0) {
                                                        continue;
                                                }
                                        } else if (SDL_QueryTexture(src_tex, &fmt, &access, nullptr, nullptr) != 0) {
                                                fmt = SDL_PIXELFORMAT_RGBA8888;
                                        }
                                        SDL_Texture* dst = SDL_CreateTexture(renderer, fmt, SDL_TEXTUREACCESS_TARGET, tex_w, tex_h);
                                        if (!dst) {
                                                continue;
                                        }
                                        SDL_SetTextureBlendMode(dst, SDL_BLENDMODE_BLEND);
                                        apply_scale_mode(dst, info);
                                        SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
                                        SDL_SetRenderTarget(renderer, dst);
                                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                                        SDL_RenderClear(renderer);
                                        SDL_Rect rect{0, 0, tex_w, tex_h};
                                        SDL_RenderCopyEx(renderer, src_tex, nullptr, &rect, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
                                        SDL_SetRenderTarget(renderer, prev_target);
                                        SDL_DestroyTexture(src_tex);
                                        cache_entry.textures[variant_idx] = dst;
                                        cache_entry.widths[variant_idx]   = tex_w;
                                        cache_entry.heights[variant_idx]  = tex_h;
                                        SDL_Texture* src_mask = nullptr;
                                        if (variant_idx < cache_entry.mask_textures.size()) {
                                                src_mask = cache_entry.mask_textures[variant_idx];
                                        }
                                        if (src_mask) {
                                                Uint32 mask_fmt = SDL_PIXELFORMAT_RGBA8888;
                                                int mask_access = 0;
                                                int mask_w = cache_entry.mask_widths[variant_idx];
                                                int mask_h = cache_entry.mask_heights[variant_idx];
                                                if (mask_w <= 0 || mask_h <= 0) {
                                                        if (SDL_QueryTexture(src_mask, &mask_fmt, &mask_access, &mask_w, &mask_h) != 0 || mask_w <= 0 || mask_h <= 0) {
                                                                SDL_DestroyTexture(src_mask);
                                                                cache_entry.mask_textures[variant_idx] = nullptr;
                                                                cache_entry.mask_widths[variant_idx]   = 0;
                                                                cache_entry.mask_heights[variant_idx]  = 0;
                                                                continue;
                                                        }
                                                } else if (SDL_QueryTexture(src_mask, &mask_fmt, &mask_access, nullptr, nullptr) != 0) {
                                                        mask_fmt = SDL_PIXELFORMAT_RGBA8888;
                                                }
                                                SDL_Texture* mask_dst = SDL_CreateTexture(renderer, mask_fmt, SDL_TEXTUREACCESS_TARGET, mask_w, mask_h);
                                                if (mask_dst) {
                                                        SDL_SetTextureBlendMode(mask_dst, SDL_BLENDMODE_BLEND);
                                                        apply_scale_mode(mask_dst, info);
                                                        SDL_Texture* prev_target_mask = SDL_GetRenderTarget(renderer);
                                                        SDL_SetRenderTarget(renderer, mask_dst);
                                                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                                                        SDL_RenderClear(renderer);
                                                        SDL_Rect rect{0, 0, mask_w, mask_h};
                                                        SDL_RenderCopyEx(renderer, src_mask, nullptr, &rect, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
                                                        SDL_SetRenderTarget(renderer, prev_target_mask);
                                                } else {
                                                        mask_w = 0;
                                                        mask_h = 0;
                                                }
                                                SDL_DestroyTexture(src_mask);
                                                cache_entry.mask_textures[variant_idx] = mask_dst;
                                                cache_entry.mask_widths[variant_idx]   = mask_w;
                                                cache_entry.mask_heights[variant_idx]  = mask_h;
                                        }
                                }
                                if (frame_index < frames.size()) {
                                        frames[frame_index] = cache_entry.textures[0];
                                }
                                if (frame_index < mask_frames.size() && !cache_entry.mask_textures.empty()) {
                                        mask_frames[frame_index] = cache_entry.mask_textures[0];
                                }
                        }
                }
                if (reverse_source && !frames.empty()) {
                        std::reverse(frames.begin(), frames.end());
                        std::reverse(mask_frames.begin(), mask_frames.end());
                        std::reverse(frame_cache_.begin(), frame_cache_.end());
                }
                loaded_from_cache = use_cache;
        }
        if (!movement_specified && source.kind == "animation" && !source.name.empty()) {
                auto it = info.animations.find(source.name);
                if (it != info.animations.end()) {
                        const Animation& src_anim = it->second;
                        movement_paths_           = src_anim.movement_paths_;
                        if (!movement_paths_.empty()) {
                                if (reverse_source) {
                                        for (auto& path : movement_paths_) {
                                                std::reverse(path.begin(), path.end());
                                                for (auto& frame : path) {
                                                        frame.dx = -frame.dx;
                                                        frame.dy = -frame.dy;
                                                }
                                        }
                                }
                                if (flipped_source) {
                                        for (auto& path : movement_paths_) {
                                                for (auto& frame : path) {
                                                        frame.dx = -frame.dx;
                                                }
                                        }
                                }
                                movement_specified = true;
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
                audio_clip.volume = clamp_volume(audio_json->value("volume", audio_clip.volume));
                audio_clip.effects = audio_json->value("effects", audio_clip.effects);
                try {
                        std::string clip_name = audio_json->value("name", std::string{});
                        if (!clip_name.empty()) {
                                audio_clip.name = clip_name;
                                std::filesystem::path clip_path = std::filesystem::path(dir_path) / (clip_name + ".wav");
                                audio_clip.path = clip_path.lexically_normal().string();
                                audio_clip.chunk = load_audio_clip(audio_clip.path);
                        }
                } catch (...) {

                }
        }
        if (!audio_clip.chunk && source.kind == "animation" && !source.name.empty()) {
                auto it = info.animations.find(source.name);
                if (it != info.animations.end()) {
                        audio_clip = it->second.audio_clip;
                        if (audio_json) {
                                if (audio_json->contains("volume")) {
                                        audio_clip.volume = clamp_volume(audio_json->value("volume", audio_clip.volume));
                                }
                                if (audio_json->contains("effects")) {
                                        audio_clip.effects = audio_json->value("effects", audio_clip.effects);
                                }
                        }
                }
        }
        const std::size_t frame_count = frames.size();
        if (movement_paths_.empty()) {
                movement_paths_.emplace_back();
        }

        bool any_motion = false;
        for (auto& path : movement_paths_) {
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
                        if (f.dx != 0 || f.dy != 0) {
                                any_motion = true;
                        }
                }
        }

        total_dx = 0;
        total_dy = 0;
        if (!movement_paths_.empty()) {
                const auto& primary = movement_paths_.front();
                for (const auto& frame : primary) {
                        total_dx += frame.dx;
                        total_dy += frame.dy;
                        if (frame.dx != 0 || frame.dy != 0) {
                                any_motion = true;
                        }
                }
        }

        movment = any_motion;
        number_of_frames = static_cast<int>(frames.size());
        if (trigger == "default" && !frames.empty()) {
                base_sprite = frames[0];
        }

        int frame_width  = 0;
        int frame_height = 0;
        if (!frame_cache_.empty()) {
                frame_width  = frame_cache_[0].widths[0];
                frame_height = frame_cache_[0].heights[0];
                if ((frame_width <= 0 || frame_height <= 0) && frame_cache_[0].textures[0]) {
                        SDL_QueryTexture(frame_cache_[0].textures[0], nullptr, nullptr, &frame_width, &frame_height);
                }
        }

        const auto load_end        = std::chrono::steady_clock::now();
        const double elapsed_secs  = std::chrono::duration<double>(load_end - load_start).count();
        std::string   origin_label = loaded_from_cache ? "cache" : "source";
        if (reused_animation) {
                origin_label = "animation '" + source.name + "'";
        }

        std::cout << "[AnimationLoader] " << info.name << "::" << trigger << " -> "
                  << frames.size() << " frame(s)";
        if (frame_width > 0 && frame_height > 0) {
                std::cout << " @ " << frame_width << "x" << frame_height;
        }
        std::cout << " from " << origin_label << " in " << elapsed_secs << "s\n";
}

SDL_Texture* Animation::get_frame(const AnimationFrame* frame) const {
        if (!frame) return nullptr;
        const int index = frame->frame_index;
        if (index < 0 || index >= static_cast<int>(frames.size())) return nullptr;
        return frames[index];
}

AnimationFrame* Animation::get_first_frame(std::size_t path_index) {
        if (movement_paths_.empty()) return nullptr;
        path_index = clamp_path_index(path_index);
        auto& path = movement_paths_[path_index];
        if (path.empty()) return nullptr;
        return &path[0];
}

int Animation::index_of(const AnimationFrame* frame) const {
        if (!frame) return -1;
        const int index = frame->frame_index;
        if (index < 0 || index >= static_cast<int>(frames.size())) return -1;
        for (const auto& path : movement_paths_) {
                if (path.empty()) continue;
                const AnimationFrame* data = path.data();
                const AnimationFrame* end  = data + path.size();
                if (frame >= data && frame < end) {
                        return index;
                }
        }
        // Some animations reuse movement data from cached sources which can
        // invalidate the raw AnimationFrame pointers held by assets after the
        // vectors are reallocated. The frame_index, however, is still valid for
        // locating the texture within the animation. If the pointer lookup
        // above fails, fall back to the recorded frame_index so that callers
        // such as Asset::get_current_frame() don't reset to the first frame on
        // every render pass.
        return index;
}

void Animation::change(AnimationFrame*& frame, bool& static_flag) const {
        if (frozen) return;
        auto& self = const_cast<Animation&>(*this);
        frame      = self.get_first_frame();
        static_flag = is_static() || locked;
}

std::size_t Animation::movement_path_count() const { return movement_paths_.size(); }

const std::vector<AnimationFrame>& Animation::movement_path(std::size_t index) const {
        static const std::vector<AnimationFrame> kEmpty{};
        if (movement_paths_.empty()) return kEmpty;
        if (index >= movement_paths_.size()) index = 0;
        return movement_paths_[index];
}

std::vector<AnimationFrame>& Animation::movement_path(std::size_t index) {
        if (movement_paths_.empty()) movement_paths_.emplace_back();
        if (index >= movement_paths_.size()) index = 0;
        return movement_paths_[index];
}

std::size_t Animation::clamp_path_index(std::size_t index) const {
        if (movement_paths_.empty()) return 0;
        if (index >= movement_paths_.size()) return 0;
        return index;
}

void Animation::freeze() { frozen = true; }

bool Animation::is_frozen() const { return frozen; }

bool Animation::is_static() const { return frames.size() <= 1; }

bool Animation::has_audio() const { return static_cast<bool>(audio_clip.chunk); }

Mix_Chunk* Animation::audio_chunk() const { return audio_clip.chunk.get(); }

const Animation::AudioClip* Animation::audio_data() const {
        if (!audio_clip.chunk) {
                return nullptr;
        }
        return &audio_clip;
}
