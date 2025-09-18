#include "animation.hpp"
#include "asset/asset_info.hpp"
#include "utils/cache_manager.hpp"
#include <SDL_image.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <cstdint>
namespace fs = std::filesystem;

Animation::Animation() = default;

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
	if (speed_factor < 0.0f) {
		float mag = -speed_factor;
		if (mag < 0.0001f) mag = 0.0001f;
		speed_factor = 1.0f / mag;
	}
	loop      = anim_json.value("loop", false);
	randomize = anim_json.value("randomize", false);
	rnd_start = anim_json.value("rnd_start", false);
	on_end_animation = anim_json.value("on_end", std::string{"default"});
        total_dx = 0;
        total_dy = 0;
        frames_data.clear();
        bool movement_specified = false;
        if (anim_json.contains("movement") && anim_json["movement"].is_array()) {
                for (const auto& mv : anim_json["movement"]) {
                        if (!mv.is_array() || mv.size() < 2) continue;
                        AnimationFrame fm;
                        try { fm.dx = mv[0].get<int>(); } catch (...) { fm.dx = 0; }
                        try { fm.dy = mv[1].get<int>(); } catch (...) { fm.dy = 0; }
                        if (mv.size() >= 3 && mv[2].is_boolean()) {
                                        fm.z_resort = mv[2].get<bool>();
                        }
                        if (mv.size() >= 4 && mv[3].is_array() && mv[3].size() >= 3) {
                                        auto clamp = [](int v) { return (v < 0) ? 0 : (v > 255 ? 255 : v); };
                                        int r = 255, g = 255, b = 255;
                                        try { r = clamp(mv[3][0].get<int>()); } catch (...) { r = 255; }
                                        try { g = clamp(mv[3][1].get<int>()); } catch (...) { g = 255; }
                                        try { b = clamp(mv[3][2].get<int>()); } catch (...) { b = 255; }
                                        fm.rgb = SDL_Color{ static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255 };
                        }
                        if (fm.dx != 0 || fm.dy != 0 || mv.size() >= 3) {
                                movement_specified = true;
                        }
                        frames_data.push_back(fm);
                        total_dx += fm.dx;
                        total_dy += fm.dy;
                }
        }
	if (source.kind == "animation" && !source.name.empty()) {
		auto it = info.animations.find(source.name);
		if (it != info.animations.end()) {
			const auto& src_frames = it->second.frames;
			for (SDL_Texture* src : src_frames) {
					if (!src) continue;
					Uint32 fmt; int access, w, h;
					if (SDL_QueryTexture(src, &fmt, &access, &w, &h) != 0) {
								continue;
					}
					SDL_Texture* dst = SDL_CreateTexture(renderer, fmt, SDL_TEXTUREACCESS_TARGET, w, h);
					if (!dst) continue;
					SDL_SetTextureBlendMode(dst, SDL_BLENDMODE_BLEND);
					SDL_SetRenderTarget(renderer, dst);
					SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
					SDL_RenderClear(renderer);
					SDL_Rect r{0, 0, w, h};
					SDL_RendererFlip flip = flipped_source ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
					SDL_RenderCopyEx(renderer, src, nullptr, &r, 0.0, nullptr, flip);
					SDL_SetRenderTarget(renderer, nullptr);
					frames.push_back(dst);
			}
			if (reverse_source) {
					std::reverse(frames.begin(), frames.end());
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
		if (cache.load_metadata(meta_file, meta)) {
			if (meta.value("frame_count", -1) == expected_frames &&
			meta.value("scale_factor", -1.0f) == scale_factor &&
			meta.value("original_width", -1) == orig_w &&
			meta.value("original_height", -1) == orig_h)
			{
					use_cache = true;
			}
		}
		std::vector<SDL_Surface*> surfaces;
		if (use_cache) {
			use_cache = cache.load_surface_sequence(cache_folder, expected_frames, surfaces);
		}
		if (!use_cache) {
			surfaces.clear();
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
					surfaces.push_back(scaled);
			}
			cache.save_surface_sequence(cache_folder, surfaces);
			nlohmann::json new_meta;
			new_meta["frame_count"]     = expected_frames;
			new_meta["scale_factor"]    = scale_factor;
			new_meta["original_width"]  = orig_w;
			new_meta["original_height"] = orig_h;
			cache.save_metadata(meta_file, new_meta);
		}
		for (SDL_Surface* surf : surfaces) {
			SDL_Texture* tex = cache.surface_to_texture(renderer, surf);
			SDL_FreeSurface(surf);
			if (!tex) {
					std::cerr << "[Animation] Failed to create texture for '" << trigger << "'\n";
					continue;
			}
			frames.push_back(tex);
		}
		if (flipped_source && !frames.empty()) {
			std::vector<SDL_Texture*> flipped;
			flipped.reserve(frames.size());
			for (SDL_Texture* src : frames) {
					if (!src) { flipped.push_back(nullptr); continue; }
					Uint32 fmt; int access, w, h;
					if (SDL_QueryTexture(src, &fmt, &access, &w, &h) != 0) {
								flipped.push_back(nullptr);
								continue;
					}
					SDL_Texture* dst = SDL_CreateTexture(renderer, fmt, SDL_TEXTUREACCESS_TARGET, w, h);
					if (!dst) { flipped.push_back(nullptr); continue; }
					SDL_SetTextureBlendMode(dst, SDL_BLENDMODE_BLEND);
					SDL_SetRenderTarget(renderer, dst);
					SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
					SDL_RenderClear(renderer);
					SDL_Rect r{0, 0, w, h};
					SDL_RenderCopyEx(renderer, src, nullptr, &r, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
					SDL_SetRenderTarget(renderer, nullptr);
					flipped.push_back(dst);
			}
			for (SDL_Texture* t : frames) {
					if (t) SDL_DestroyTexture(t);
			}
			frames.swap(flipped);
		}
		if (reverse_source && !frames.empty()) {
			std::reverse(frames.begin(), frames.end());
		}
	}
        if (!movement_specified && source.kind == "animation" && !source.name.empty()) {
                auto it = info.animations.find(source.name);
                if (it != info.animations.end()) {
                        const auto& src_frames_data = it->second.frames_data;
                        if (!src_frames_data.empty()) {
                                const std::size_t count = src_frames_data.size();
                                frames_data.assign(count, AnimationFrame{});
                                total_dx = 0;
                                total_dy = 0;
                                for (std::size_t i = 0; i < count; ++i) {
                                        std::size_t src_index = reverse_source ? (count - 1 - i) : i;
                                        if (src_index >= src_frames_data.size()) {
                                                continue;
                                        }
                                        AnimationFrame dest;
                                        dest.dx = src_frames_data[src_index].dx;
                                        dest.dy = src_frames_data[src_index].dy;
                                        dest.z_resort = src_frames_data[src_index].z_resort;
                                        dest.rgb = src_frames_data[src_index].rgb;
                                        if (reverse_source) {
                                                dest.dx = -dest.dx;
                                                dest.dy = -dest.dy;
                                        }
                                        if (flipped_source) {
                                                dest.dx = -dest.dx;
                                        }
                                        frames_data[i] = dest;
                                        total_dx += dest.dx;
                                        total_dy += dest.dy;
                                }
                                movement_specified = true;
                        }
                }
        }
        movment = !(total_dx == 0 && total_dy == 0);
        number_of_frames = static_cast<int>(frames.size());
        if (trigger == "default" && !frames.empty()) {
                base_sprite = frames[0];
        }

        if (frames_data.size() < frames.size()) {
                frames_data.resize(frames.size());
        }
        for (std::size_t i = 0; i < frames_data.size(); ++i) {
                AnimationFrame& f = frames_data[i];
                f.prev = (i > 0) ? &frames_data[i - 1] : nullptr;
                f.next = (i + 1 < frames_data.size()) ? &frames_data[i + 1] : nullptr;
                f.is_first = (i == 0);
                f.is_last = (i + 1 == frames_data.size());
        }
}

SDL_Texture* Animation::get_frame(const AnimationFrame* frame) const {
        if (!frame) return nullptr;
        int index = index_of(frame);
        if (index < 0 || index >= static_cast<int>(frames.size())) return nullptr;
        return frames[index];
}

AnimationFrame* Animation::get_first_frame() {
        if (frames_data.empty()) return nullptr;
        return &frames_data[0];
}

int Animation::index_of(const AnimationFrame* frame) const {
        if (!frame || frames_data.empty()) return -1;
        const AnimationFrame* data = frames_data.data();
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(data);
        const std::uintptr_t ptr  = reinterpret_cast<std::uintptr_t>(frame);
        const std::uintptr_t end  = base + sizeof(AnimationFrame) * frames_data.size();
        if (ptr < base || ptr >= end) return -1;
        const std::uintptr_t offset = ptr - base;
        if (offset % sizeof(AnimationFrame) != 0) return -1;
        size_t index = offset / sizeof(AnimationFrame);
        return static_cast<int>(index);
}

void Animation::change(AnimationFrame*& frame, bool& static_flag) const {
        if (frozen) return;
        frame = const_cast<AnimationFrame*>(frames_data.empty() ? nullptr : &frames_data[0]);
        static_flag = is_static();
}

void Animation::freeze() { frozen = true; }

bool Animation::is_frozen() const { return frozen; }

bool Animation::is_static() const { return frames.size() <= 1; }
