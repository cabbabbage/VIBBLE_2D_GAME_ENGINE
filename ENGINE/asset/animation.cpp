#include "animation.hpp"
#include "asset/asset_info.hpp"
#include "utils/cache_manager.hpp"
#include "render_pipeline/ScalingLogic.hpp"
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <iterator>
namespace fs = std::filesystem;

namespace {
#if SDL_VERSION_ATLEAST(2,0,12)
void apply_scale_mode(SDL_Texture* tex, const AssetInfo& info);
#endif

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
        }
        frame_cache_.clear();
        frames.clear();
}

SDL_Texture* Animation::frame_variant(std::size_t frame_index, std::size_t variant_index) const {
        if (variant_index >= render_pipeline::ScalingLogic::kVariantCount) {
                variant_index = 0;
        }
        if (frame_index >= frame_cache_.size()) {
                return nullptr;
        }
        return frame_cache_[frame_index].textures[variant_index];
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
                     int& original_canvas_height)
{
        CacheManager cache;
        clear_texture_cache();
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
                                std::vector<SDL_Texture*> new_frames;
                                std::vector<FrameCache>   new_caches;
                                new_frames.reserve(src_anim.frames.size());
                                new_caches.reserve(src_anim.frames.size());
                                for (std::size_t frame_idx = 0; frame_idx < src_anim.frames.size(); ++frame_idx) {
                                        FrameCache cache_entry;
                                        bool base_ok = false;
                                        for (std::size_t variant_idx = 0; variant_idx < render_pipeline::ScalingLogic::kVariantCount; ++variant_idx) {
                                                SDL_Texture* source_tex = src_anim.frame_variant(frame_idx, variant_idx);
                                                if (!source_tex) {
                                                        cache_entry.textures[variant_idx] = nullptr;
                                                        cache_entry.widths[variant_idx]   = 0;
                                                        cache_entry.heights[variant_idx]  = 0;
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
                                        }
                                        if (!base_ok || !cache_entry.textures[0]) {
                                                for (SDL_Texture*& tex : cache_entry.textures) {
                                                        if (tex) {
                                                                SDL_DestroyTexture(tex);
                                                                tex = nullptr;
                                                        }
                                                }
                                                continue;
                                        }
                                        new_frames.push_back(cache_entry.textures[0]);
                                        new_caches.push_back(std::move(cache_entry));
                                }
                                frames.insert(frames.end(), new_frames.begin(), new_frames.end());
                                frame_cache_.insert(frame_cache_.end(), std::make_move_iterator(new_caches.begin()), std::make_move_iterator(new_caches.end()));
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
                const auto expected_steps = render_pipeline::ScalingLogic::PercentSteps();
		if (cache.load_metadata(meta_file, meta)) {
                        bool meta_ok = (
                            meta.value("frame_count", -1) == expected_frames &&
                            meta.value("scale_factor", -1.0f) == scale_factor &&
                            meta.value("original_width", -1) == orig_w &&
                            meta.value("original_height", -1) == orig_h);
                        if (meta_ok) {
                                if (meta.contains("scale_steps") && meta["scale_steps"].is_array()) {
                                        const auto& stored = meta["scale_steps"];
                                        if (stored.size() == render_pipeline::ScalingLogic::kVariantCount) {
                                                for (std::size_t idx = 0; idx < render_pipeline::ScalingLogic::kVariantCount; ++idx) {
                                                        int stored_pct = stored[idx].get<int>();
                                                        if (stored_pct != expected_steps[idx]) {
                                                                meta_ok = false;
                                                                break;
                                                        }
                                                }
                                        } else {
                                                meta_ok = false;
                                        }
                                } else {
                                        meta_ok = false;
                                }
                        }
			use_cache = meta_ok;
		}

                std::array<std::vector<SDL_Surface*>, render_pipeline::ScalingLogic::kVariantCount> variant_surfaces;
                if (use_cache) {
                        bool variants_loaded = true;
                        for (std::size_t idx = 0; idx < render_pipeline::ScalingLogic::kVariantCount; ++idx) {
                                std::string variant_path = render_pipeline::ScalingLogic::VariantFolder(cache_folder, idx);
                                std::vector<SDL_Surface*> loaded;
                                if (!cache.load_surface_sequence(variant_path, expected_frames, loaded)) {
                                        if (idx == 0 && cache.load_surface_sequence(cache_folder, expected_frames, loaded)) {
                                                // legacy cache, continue but ensure metadata regenerated later
                                                use_cache = false;
                                        } else {
                                                variants_loaded = false;
                                                break;
                                        }
                                }
                                variant_surfaces[idx] = std::move(loaded);
                        }
                        if (!variants_loaded) {
                                for (auto& list : variant_surfaces) {
                                        for (SDL_Surface* surf : list) {
                                                if (surf) SDL_FreeSurface(surf);
                                        }
                                        list.clear();
                                }
                                use_cache = false;
                        } else if (use_cache) {
                                original_canvas_width  = orig_w;
                                original_canvas_height = orig_h;
                                if (!variant_surfaces[0].empty() && variant_surfaces[0][0]) {
                                        scaled_sprite_w = variant_surfaces[0][0]->w;
                                        scaled_sprite_h = variant_surfaces[0][0]->h;
                                }
                        }
                }

                if (!use_cache) {
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
                                        SDL_Surface* scaled = cache.load_and_scale_surface(f, scale_factor, new_w, new_h);
					if (!scaled) {
								std::cerr << "[Animation] Failed to load or scale: " << f << "\n";
								continue;
					}
					if (i == 0) {
								original_canvas_width  = orig_w;
								original_canvas_height = orig_h;
								scaled_sprite_w = new_w;
								scaled_sprite_h = new_h;
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
                        for (std::size_t idx = 1; idx < render_pipeline::ScalingLogic::kVariantCount; ++idx) {
                                const float scale_step = render_pipeline::ScalingLogic::kScaleSteps[idx];
                                variant_surfaces[idx].reserve(variant_surfaces[0].size());
                                for (SDL_Surface* base_surface : variant_surfaces[0]) {
                                        SDL_Surface* scaled = render_pipeline::CreateScaledSurface(base_surface, scale_step);
                                        variant_surfaces[idx].push_back(scaled);
                                }
                        }
                        for (std::size_t idx = 0; idx < render_pipeline::ScalingLogic::kVariantCount; ++idx) {
                                const std::string variant_path = render_pipeline::ScalingLogic::VariantFolder(cache_folder, idx);
                                cache.save_surface_sequence(variant_path, variant_surfaces[idx]);
                        }
                        nlohmann::json new_meta;
                        new_meta["frame_count"]     = expected_frames;
                        new_meta["scale_factor"]    = scale_factor;
                        new_meta["original_width"]  = orig_w;
                        new_meta["original_height"] = orig_h;
                        nlohmann::json step_arr = nlohmann::json::array();
                        for (std::size_t idx = 0; idx < render_pipeline::ScalingLogic::kVariantCount; ++idx) {
                                step_arr.push_back(expected_steps[idx]);
                        }
                        new_meta["scale_steps"] = std::move(step_arr);
                        cache.save_metadata(meta_file, new_meta);
                }

                if (!variant_surfaces[0].empty() && variant_surfaces[0][0] && (scaled_sprite_w <= 0 || scaled_sprite_h <= 0)) {
                        scaled_sprite_w = variant_surfaces[0][0]->w;
                        scaled_sprite_h = variant_surfaces[0][0]->h;
                }
                if (original_canvas_width <= 0 && orig_w > 0) {
                        original_canvas_width = orig_w;
                }
                if (original_canvas_height <= 0 && orig_h > 0) {
                        original_canvas_height = orig_h;
                }
                if ((scaled_sprite_w <= 0 || scaled_sprite_h <= 0) && orig_w > 0 && orig_h > 0) {
                        int fallback_w = static_cast<int>(orig_w * scale_factor);
                        int fallback_h = static_cast<int>(orig_h * scale_factor);
                        if (fallback_w <= 0) fallback_w = 1;
                        if (fallback_h <= 0) fallback_h = 1;
                        scaled_sprite_w = fallback_w;
                        scaled_sprite_h = fallback_h;
                }
                frames.clear();
                frame_cache_.clear();
                frames.reserve(expected_frames);
                frame_cache_.reserve(expected_frames);

                for (std::size_t frame_idx = 0; frame_idx < variant_surfaces[0].size(); ++frame_idx) {
                        FrameCache cache_entry;
                        for (std::size_t variant_idx = 0; variant_idx < render_pipeline::ScalingLogic::kVariantCount; ++variant_idx) {
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
                                if (variant_idx == 0) {
                                        frames.push_back(tex_variant);
                                }
                        }
                        frame_cache_.push_back(std::move(cache_entry));
                }

                for (auto& list : variant_surfaces) {
                        for (SDL_Surface* surf : list) {
                                if (surf) {
                                        SDL_FreeSurface(surf);
                                }
                        }
                        list.clear();
                }
		if (flipped_source && renderer && !frame_cache_.empty()) {
                        for (std::size_t frame_index = 0; frame_index < frame_cache_.size(); ++frame_index) {
                                FrameCache& cache_entry = frame_cache_[frame_index];
                                for (std::size_t variant_idx = 0; variant_idx < render_pipeline::ScalingLogic::kVariantCount; ++variant_idx) {
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
                                }
                                if (frame_index < frames.size()) {
                                        frames[frame_index] = cache_entry.textures[0];
                                }
                        }
		}
		if (reverse_source && !frames.empty()) {
			std::reverse(frames.begin(), frames.end());
                        std::reverse(frame_cache_.begin(), frame_cache_.end());
		}
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
        return -1;
}

void Animation::change(AnimationFrame*& frame, bool& static_flag) const {
        if (frozen) return;
        auto& self = const_cast<Animation&>(*this);
        frame      = self.get_first_frame();
        static_flag = is_static();
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
