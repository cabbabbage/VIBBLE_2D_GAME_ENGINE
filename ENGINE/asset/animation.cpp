#include "animation.hpp"
#include "asset/asset_info.hpp"
#include "utils/cache_manager.hpp"
#include "utils/generate_faded_mask.hpp"
#include "render_pipeline/ScalingLogic.hpp"
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
#if SDL_VERSION_ATLEAST(2,0,12)
void apply_scale_mode(SDL_Texture* tex, const AssetInfo& info);
#endif

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

fs::path resolve_source_folder(const std::string& dir_path, const std::string& source_path) {
        const fs::path base = fs::path(dir_path).lexically_normal();
        if (source_path.empty()) {
                return base;
        }

        fs::path source = fs::path(source_path).lexically_normal();
        if (source.is_absolute()) {
                return source;
        }

        const auto starts_with = [](const std::string& value, const std::string& prefix) {
                return value.rfind(prefix, 0) == 0;
};

        const std::string source_string = source.generic_string();
        if (starts_with(source_string, "SRC/") || starts_with(source_string, "SRC\\")) {
                fs::path resolved = (project_root_path() / source).lexically_normal();
                if (path_exists_safely(resolved)) {
                        return resolved;
                }
        }

        if (!base.empty()) {
                fs::path resolved = (base / source).lexically_normal();
                if (path_exists_safely(resolved)) {
                        return resolved;
                }
        }

        {
                fs::path resolved = (project_root_path() / source).lexically_normal();
                if (path_exists_safely(resolved)) {
                        return resolved;
                }
        }

        if (!base.empty()) {
                return (base / source).lexically_normal();
        }

        return (project_root_path() / source).lexically_normal();
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

struct SourceSignatureResult {
        std::uint64_t value = 0;
        bool          success = false;
};

constexpr std::uint64_t kSignatureOffset = 1469598103934665603ull;
constexpr std::uint64_t kSignaturePrime  = 1099511628211ull;

std::uint64_t mix_signature(std::uint64_t seed, std::uint64_t value) {
        seed ^= value;
        seed *= kSignaturePrime;
        return seed;
}

SourceSignatureResult compute_source_signature(const fs::path& folder, int frame_count) {
        if (frame_count <= 0) {
                return {};
        }

        std::uint64_t signature = kSignatureOffset;
        std::error_code ec;
        for (int idx = 0; idx < frame_count; ++idx) {
                const fs::path frame_path = folder / (std::to_string(idx) + ".png");
                if (!fs::exists(frame_path, ec) || ec) {
                        return {};
                }

                const auto file_size = fs::file_size(frame_path, ec);
                if (ec) {
                        return {};
                }

                const auto write_time = fs::last_write_time(frame_path, ec);
                if (ec) {
                        return {};
                }

                signature = mix_signature(signature, static_cast<std::uint64_t>(idx));
                signature = mix_signature(signature, static_cast<std::uint64_t>(file_size));
                const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(write_time.time_since_epoch()).count();
                signature = mix_signature(signature, static_cast<std::uint64_t>(nanos));
        }

        signature = mix_signature(signature, static_cast<std::uint64_t>(frame_count));
        return {signature, true};
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

const std::array<SDL_Point, 5> kDepthcueForegroundOffsets{{
        SDL_Point{0, 0},
        SDL_Point{1, 0},
        SDL_Point{-1, 0},
        SDL_Point{0, 1},
        SDL_Point{0, -1}
}};

const std::array<SDL_Point, 9> kDepthcueBackgroundOffsets{{
        SDL_Point{0, 0},
        SDL_Point{2, 0},
        SDL_Point{-2, 0},
        SDL_Point{0, 2},
        SDL_Point{0, -2},
        SDL_Point{2, 2},
        SDL_Point{-2, 2},
        SDL_Point{2, -2},
        SDL_Point{-2, -2}
}};

const std::array<SDL_Point, 1> kDepthcueCenterOffset{{ SDL_Point{0, 0} }};

template<std::size_t N>
void render_with_offsets(SDL_Renderer* renderer,
                         SDL_Texture* source,
                         const std::array<SDL_Point, N>& offsets,
                         int width,
                         int height,
                         SDL_Color color,
                         Uint8 alpha) {
        SDL_SetTextureColorMod(source, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(source, alpha);
        SDL_Rect dst{0, 0, width, height};
        for (const SDL_Point& offset : offsets) {
                dst.x = offset.x;
                dst.y = offset.y;
                SDL_RenderCopy(renderer, source, nullptr, &dst);
        }
}

SDL_Texture* create_depthcue_texture(SDL_Renderer* renderer,
                                     SDL_Texture* source,
                                     int width,
                                     int height,
                                     bool foreground) {
        if (!renderer || !source || width <= 0 || height <= 0) {
                return nullptr;
        }
        SDL_Texture* target = SDL_CreateTexture(renderer,
                                                SDL_PIXELFORMAT_RGBA8888,
                                                SDL_TEXTUREACCESS_TARGET,
                                                width,
                                                height);
        if (!target) {
                return nullptr;
        }
        SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);

        SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
        SDL_BlendMode prev_draw_mode = SDL_BLENDMODE_BLEND;
        SDL_GetRenderDrawBlendMode(renderer, &prev_draw_mode);
        SDL_SetRenderTarget(renderer, target);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);

        Uint8 prev_r = 255;
        Uint8 prev_g = 255;
        Uint8 prev_b = 255;
        Uint8 prev_a = 255;
        SDL_BlendMode prev_tex_blend = SDL_BLENDMODE_BLEND;
        SDL_GetTextureColorMod(source, &prev_r, &prev_g, &prev_b);
        SDL_GetTextureAlphaMod(source, &prev_a);
        SDL_GetTextureBlendMode(source, &prev_tex_blend);
        SDL_SetTextureBlendMode(source, SDL_BLENDMODE_BLEND);

        if (foreground) {
                render_with_offsets(renderer,
                                    source,
                                    kDepthcueForegroundOffsets,
                                    width,
                                    height,
                                    SDL_Color{255, 210, 170, 255},
                                    170);
                render_with_offsets(renderer,
                                    source,
                                    kDepthcueCenterOffset,
                                    width,
                                    height,
                                    SDL_Color{255, 235, 210, 255},
                                    255);
        } else {
                render_with_offsets(renderer,
                                    source,
                                    kDepthcueBackgroundOffsets,
                                    width,
                                    height,
                                    SDL_Color{160, 220, 255, 255},
                                    150);
                render_with_offsets(renderer,
                                    source,
                                    kDepthcueCenterOffset,
                                    width,
                                    height,
                                    SDL_Color{150, 205, 255, 255},
                                    210);
                SDL_SetRenderDrawColor(renderer, 10, 20, 40, 35);
                SDL_RenderFillRect(renderer, nullptr);
        }

        SDL_SetRenderDrawBlendMode(renderer, prev_draw_mode);
        SDL_SetTextureColorMod(source, prev_r, prev_g, prev_b);
        SDL_SetTextureAlphaMod(source, prev_a);
        SDL_SetTextureBlendMode(source, prev_tex_blend);
        SDL_SetRenderTarget(renderer, prev_target);
        return target;
}

void rebuild_depthcue_textures(SDL_Renderer* renderer,
                               SDL_Texture* source,
                               int width,
                               int height,
                               SDL_Texture*& foreground_slot,
                               SDL_Texture*& background_slot) {
        if (foreground_slot) {
                SDL_DestroyTexture(foreground_slot);
                foreground_slot = nullptr;
        }
        if (background_slot) {
                SDL_DestroyTexture(background_slot);
                background_slot = nullptr;
        }
        if (!renderer || !source || width <= 0 || height <= 0) {
                return;
        }
        SDL_Texture* fg = create_depthcue_texture(renderer, source, width, height, true);
        SDL_Texture* bg = create_depthcue_texture(renderer, source, width, height, false);
        foreground_slot = fg;
        background_slot = bg;
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
                for (SDL_Texture*& tex : cache_entry.depthcue_foreground_textures) {
                        if (tex) {
                                SDL_DestroyTexture(tex);
                                tex = nullptr;
                        }
                }
                for (SDL_Texture*& tex : cache_entry.depthcue_background_textures) {
                        if (tex) {
                                SDL_DestroyTexture(tex);
                                tex = nullptr;
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

SDL_Texture* Animation::depthcue_foreground_variant(std::size_t frame_index, std::size_t variant_index) const {
        if (frame_index >= frame_cache_.size()) {
                return nullptr;
        }
        const FrameCache& cache_entry = frame_cache_[frame_index];
        if (cache_entry.depthcue_foreground_textures.empty()) {
                return nullptr;
        }
        if (variant_index >= cache_entry.depthcue_foreground_textures.size() ||
            !cache_entry.depthcue_foreground_textures[variant_index]) {
                return cache_entry.depthcue_foreground_textures[0];
        }
        return cache_entry.depthcue_foreground_textures[variant_index];
}

SDL_Texture* Animation::depthcue_background_variant(std::size_t frame_index, std::size_t variant_index) const {
        if (frame_index >= frame_cache_.size()) {
            return nullptr;
        }
        const FrameCache& cache_entry = frame_cache_[frame_index];
        if (cache_entry.depthcue_background_textures.empty()) {
                return nullptr;
        }
        if (variant_index >= cache_entry.depthcue_background_textures.size() ||
            !cache_entry.depthcue_background_textures[variant_index]) {
                return cache_entry.depthcue_background_textures[0];
        }
        return cache_entry.depthcue_background_textures[variant_index];
}

SDL_Texture* Animation::depthcue_foreground_texture(const AnimationFrame* frame) const {
        if (!frame || frame->frame_index < 0) {
                return nullptr;
        }
        return depthcue_foreground_variant(static_cast<std::size_t>(frame->frame_index), 0);
}

SDL_Texture* Animation::depthcue_background_texture(const AnimationFrame* frame) const {
        if (!frame || frame->frame_index < 0) {
                return nullptr;
        }
        return depthcue_background_variant(static_cast<std::size_t>(frame->frame_index), 0);
}

void Animation::adopt_prebuilt_frames(std::vector<FrameCache> caches,
                                      std::vector<SDL_Texture*> base_frames,
                                      std::vector<SDL_Texture*> base_masks,
                                      std::vector<float> variant_steps) {
        clear_texture_cache();
        frame_cache_   = std::move(caches);
        frames         = std::move(base_frames);
        mask_frames    = std::move(base_masks);
        variant_steps_ = std::move(variant_steps);
        number_of_frames = static_cast<int>(frames.size());

        movement_paths_.clear();
        if (number_of_frames <= 0) {
                movement_paths_.emplace_back();
                return;
        }

        movement_paths_.emplace_back();
        auto& path = movement_paths_.back();
        path.resize(frames.size());
        for (std::size_t idx = 0; idx < path.size(); ++idx) {
                auto& frame = path[idx];
                frame.frame_index = static_cast<int>(idx);
                frame.is_first   = (idx == 0);
                frame.is_last    = (idx + 1 == path.size());
                frame.next       = (idx + 1 < path.size()) ? &path[idx + 1] : nullptr;
                frame.prev       = (idx > 0) ? &path[idx - 1] : nullptr;
        }
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
                     bool scaling_refresh_pending,
                     LoadDiagnostics* diagnostics)
{
        CacheManager cache;
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
        clear_texture_cache();
        const bool prefer_cached = !scaling_refresh_pending;
        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                  << " profile steps (pre-normalize): " << format_steps(variant_steps_) << ", prefer_cached=" << (prefer_cached ? "true" : "false") << ", scaling_refresh_pending=" << (scaling_refresh_pending ? "true" : "false") << "\n";
        variant_steps_ = info.scale_variants;
        render_pipeline::ScalingLogic::NormalizeVariantSteps(variant_steps_);
        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                  << " normalized profile steps: " << format_steps(variant_steps_) << "\n";
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
        flip_vertical_source = anim_json.value("flip_vertical_source", false);
        flip_movement_horizontal = anim_json.value("flip_movement_horizontal", false);
        flip_movement_vertical = anim_json.value("flip_movement_vertical", false);
        reverse_source = anim_json.value("reverse_source", false);
        const bool inherit_source_movement = anim_json.value("inherit_source_movement", (source.kind == "animation"));
        if (source.kind == "animation" && anim_json.contains("derived_modifiers") &&
            anim_json["derived_modifiers"].is_object()) {
                const auto& modifiers = anim_json["derived_modifiers"];
                reverse_source = modifiers.value("reverse", reverse_source);
                flipped_source = modifiers.value("flipX", flipped_source);
                flip_vertical_source = modifiers.value("flipY", flip_vertical_source);
                flip_movement_horizontal = modifiers.value("flipMovementX", flip_movement_horizontal);
                flip_movement_vertical = modifiers.value("flipMovementY", flip_movement_vertical);
        } else if (source.kind != "animation") {
                flip_vertical_source = false;
                flip_movement_horizontal = false;
                flip_movement_vertical = false;
        }

        locked         = anim_json.value("locked", false);
	// Legacy speed control: speed_factor (multiplier of base 24fps)
	speed_factor   = anim_json.value("speed_factor", 1.0f);
	// New explicit playback FPS; prefer this when present
	int parsed_fps = 0;
	try {
		if (anim_json.contains("fps")) {
			if (anim_json["fps"].is_number_integer()) parsed_fps = anim_json["fps"].get<int>();
			else if (anim_json["fps"].is_number()) parsed_fps = static_cast<int>(anim_json["fps"].get<double>());
		}
	} catch (...) { parsed_fps = 0; }
	if (parsed_fps <= 0) {
		// Fallback to legacy speed_factor if defined; otherwise default 24
		if (std::isfinite(speed_factor) && speed_factor > 0.0f) {
			parsed_fps = std::max(1, static_cast<int>(std::lround(24.0f * std::fabs(speed_factor))));
		} else {
			parsed_fps = 24;
		}
	}
	playback_fps = parsed_fps;
	loop      = anim_json.value("loop", true);
	randomize = anim_json.value("randomize", false);
	rnd_start = anim_json.value("rnd_start", false);
	on_end_animation = anim_json.value("on_end", std::string{"default"});
        child_asset_names_.clear();
        bool children_specified = false;
        if (anim_json.contains("children") && anim_json["children"].is_array()) {
                for (const auto& child_entry : anim_json["children"]) {
                        if (!child_entry.is_string()) {
                                continue;
                        }
                        std::string name = child_entry.get<std::string>();
                        if (name.empty()) {
                                continue;
                        }
                        child_asset_names_.push_back(std::move(name));
                }
                children_specified = true;
        }
        if (!children_specified && source.kind == "animation" && !source.name.empty()) {
                auto src_child_it = info.animations.find(source.name);
                if (src_child_it != info.animations.end()) {
                        child_asset_names_ = src_child_it->second.child_assets();
                }
        }
        // Deduplicate child asset list while preserving order
        if (!child_asset_names_.empty()) {
                std::unordered_set<std::string> seen;
                std::vector<std::string> unique;
                unique.reserve(child_asset_names_.size());
                for (const auto& n : child_asset_names_) {
                        if (n.empty()) continue;
                        if (seen.insert(n).second) {
                                unique.push_back(n);
                        }
                }
                child_asset_names_.swap(unique);
        }
        total_dx = 0;
        total_dy = 0;
        movement_paths_.clear();
        audio_clip = AudioClip{};
        bool movement_specified = false;

        auto parse_movement_sequence = [this](const nlohmann::json& seq, std::vector<AnimationFrame>& dest) {
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
                                                } else {
                                                        child_data.visible = false;
                                                }
                                        }
                                        if (child_data.child_index < 0 ||
                                            child_data.child_index >= static_cast<int>(child_asset_names_.size())) {
                                                continue;
                                        }
                                        fm.children.push_back(child_data);
                                }
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

        movement_paths_ = std::move(parsed_paths);
        if (source.kind == "animation" && !source.name.empty()) {
                auto it = info.animations.find(source.name);
                if (it != info.animations.end()) {
                        const Animation& src_anim = it->second;
                        if (!src_anim.frames.empty()) {
                                // Inherit frame locking and playback FPS from source animation
                                locked = src_anim.locked;
                                playback_fps = src_anim.playback_fps;
                                reused_animation = true;
                                // Normalize variant count for derived cloning; mirror non-derived path behavior
                                std::size_t variant_count = initial_variant_count;
                                if (variant_count == 0) {
                                        // Ensure at least one variant; update steps and info to stay consistent
                                        variant_steps_.push_back(1.0f);
                                        variant_count = 1;
                                        info.scale_variants = variant_steps_;
                                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                                  << " normalized zero-variant derived source to one step: "
                                                  << format_steps(variant_steps_) << "\n";
                                }
                                std::vector<SDL_Texture*> new_frames;
                                std::vector<FrameCache>   new_caches;
                                std::vector<SDL_Texture*> new_mask_frames;
                                new_frames.reserve(src_anim.frames.size());
                                new_caches.reserve(src_anim.frames.size());
                                new_mask_frames.reserve(src_anim.frames.size());
                                for (std::size_t frame_idx = 0; frame_idx < src_anim.frames.size(); ++frame_idx) {
                                        FrameCache cache_entry;
                                        cache_entry.resize(variant_count);
                                        bool base_ok = false;
                                        SDL_Texture* base_mask = nullptr;
                                        for (std::size_t variant_idx = 0; variant_idx < variant_count; ++variant_idx) {
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
                                                rebuild_depthcue_textures(renderer,
                                                                          dst,
                                                                          tex_w,
                                                                          tex_h,
                                                                          cache_entry.depthcue_foreground_textures[variant_idx],
                                                                          cache_entry.depthcue_background_textures[variant_idx]);
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
                                        // Bail out cleanly if no textures were produced; avoid indexing empty containers
                                        if (!base_ok || cache_entry.textures.empty() || !cache_entry.textures[0]) {
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
                                                for (SDL_Texture*& tex : cache_entry.depthcue_foreground_textures) {
                                                        if (tex) {
                                                                SDL_DestroyTexture(tex);
                                                                tex = nullptr;
                                                        }
                                                }
                                                for (SDL_Texture*& tex : cache_entry.depthcue_background_textures) {
                                                        if (tex) {
                                                                SDL_DestroyTexture(tex);
                                                                tex = nullptr;
                                                        }
                                                }
                                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                                          << " skipped cloned frame index " << frame_idx
                                                          << " due to missing base texture\n";
                                                continue;
                                        }
                                        // Defensive: ensure a texture exists before pushing
                                        if (!cache_entry.textures.empty() && cache_entry.textures[0]) {
                                                new_frames.push_back(cache_entry.textures[0]);
                                                new_mask_frames.push_back(base_mask);
                                                new_caches.push_back(std::move(cache_entry));
                                        } else {
                                                // Should be unreachable due to check above, but stay safe
                                                for (SDL_Texture*& tex : cache_entry.textures) {
                                                        if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
                                                }
                                                for (SDL_Texture*& mask_tex : cache_entry.mask_textures) {
                                                        if (mask_tex) { SDL_DestroyTexture(mask_tex); mask_tex = nullptr; }
                                                }
                                                for (SDL_Texture*& tex : cache_entry.depthcue_foreground_textures) {
                                                        if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
                                                }
                                                for (SDL_Texture*& tex : cache_entry.depthcue_background_textures) {
                                                        if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
                                                }
                                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                                          << " failed to push cloned frame index " << frame_idx
                                                          << " due to empty texture container\n";
                                        }
                                }
                                frames.insert(frames.end(), new_frames.begin(), new_frames.end());
                                frame_cache_.insert(frame_cache_.end(), std::make_move_iterator(new_caches.begin()), std::make_move_iterator(new_caches.end()));
                                mask_frames.insert(mask_frames.end(), new_mask_frames.begin(), new_mask_frames.end());

                                // Apply texture flips to derived frames if requested
                                if ((flipped_source || flip_vertical_source) && renderer && !frame_cache_.empty()) {
                                        SDL_RendererFlip flip_flags = SDL_FLIP_NONE;
                                        if (flipped_source) {
                                                flip_flags = static_cast<SDL_RendererFlip>(flip_flags | SDL_FLIP_HORIZONTAL);
                                        }
                                        if (flip_vertical_source) {
                                                flip_flags = static_cast<SDL_RendererFlip>(flip_flags | SDL_FLIP_VERTICAL);
                                        }
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
                                                        SDL_RenderCopyEx(renderer, src_tex, nullptr, &rect, 0.0, nullptr, flip_flags);
                                                        SDL_SetRenderTarget(renderer, prev_target);
                                                        SDL_DestroyTexture(src_tex);
                                                        cache_entry.textures[variant_idx] = dst;
                                                        cache_entry.widths[variant_idx]   = tex_w;
                                                        cache_entry.heights[variant_idx]  = tex_h;
                                                        rebuild_depthcue_textures(renderer,
                                                                                  dst,
                                                                                  tex_w,
                                                                                  tex_h,
                                                                                  cache_entry.depthcue_foreground_textures[variant_idx],
                                                                                  cache_entry.depthcue_background_textures[variant_idx]);

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
                                                                        SDL_RenderCopyEx(renderer, src_mask, nullptr, &rect, 0.0, nullptr, flip_flags);
                                                                        SDL_SetRenderTarget(renderer, prev_target_mask);
                                                                } else {
                                                                        // Failed to create flipped mask; clear it to remain consistent
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
                                                        cache_entry.textures.size(); // no-op to keep static analyzers happy
                                                        frames[frame_index] = cache_entry.textures[0];
                                                }
                                                if (frame_index < mask_frames.size() && !cache_entry.mask_textures.empty()) {
                                                        mask_frames[frame_index] = cache_entry.mask_textures[0];
                                                }
                                        }
                                }
                                // Reverse frame order for derived animations if requested
                                if (reverse_source && !frames.empty()) {
                                        std::reverse(frames.begin(), frames.end());
                                        std::reverse(mask_frames.begin(), mask_frames.end());
                                        std::reverse(frame_cache_.begin(), frame_cache_.end());
                                }
                        }
                }
        } else {
                fs::path src_folder      = resolve_source_folder(dir_path, source.path);
                std::string cache_folder = root_cache + "/" + trigger;
                std::string meta_file    = cache_folder + "/metadata.json";
                int expected_frames = 0;
                int orig_w = 0, orig_h = 0;
                while (true) {
                        fs::path frame_path = src_folder / (std::to_string(expected_frames) + ".png");
                        if (!fs::exists(frame_path)) break;
                        if (expected_frames == 0) {
                                        if (SDL_Surface* s = IMG_Load(frame_path.string().c_str())) {
                                                                orig_w = s->w;
                                                                orig_h = s->h;
                                                                SDL_FreeSurface(s);
                                        }
                        }
			++expected_frames;
		}
		if (expected_frames == 0) {
                        flush_diagnostics();
                        return;
                }
                bool metadata_valid = false;
                nlohmann::json meta;
                std::vector<int> expected_steps = render_pipeline::ScalingLogic::PercentSteps(variant_steps_);
                const std::uint64_t expected_revision = info.scale_profile_revision;
                if (cache.load_metadata(meta_file, meta)) {
                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                  << " found metadata (revision "
                                  << meta.value("scale_profile_revision", static_cast<std::uint64_t>(0)) << ") expecting revision " << expected_revision << "\n";
                        const bool meta_has_masks = meta.value("has_masks", false);
                        bool meta_ok = ( meta.value("cache_version", 0) == kAnimationCacheVersion && meta.value("frame_count", -1) == expected_frames && meta.value("original_width", -1) == orig_w && meta.value("original_height", -1) == orig_h && (meta_has_masks == info.is_shaded));
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
                                                          << " metadata steps " << format_percent_steps(stored_steps) << " differ from profile " << format_percent_steps(expected_steps) << " -> rebuild required\n";
                                                meta_ok = false;
                                        }
                                } else {
                                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                                  << " metadata missing scale_steps -> forcing rebuild\n";
                                        meta_ok = false;
                                }
                        }
                        if (meta_ok && prefer_cached) {
                                const bool has_signature =
                                        meta.contains("source_signature") &&
                                        (meta["source_signature"].is_number_unsigned() || meta["source_signature"].is_number_integer());
                                if (!has_signature) {
                                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                                  << " metadata missing source signature -> forcing rebuild\n";
                                        meta_ok = false;
                                } else {
                                        std::uint64_t stored_signature = 0;
                                        try {
                                                stored_signature = meta["source_signature"].get<std::uint64_t>();
                                        } catch (...) {
                                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                                          << " metadata source signature invalid -> forcing rebuild\n";
                                                meta_ok = false;
                                        }
                                        if (meta_ok) {
                                                const auto current_signature = compute_source_signature(src_folder, expected_frames);
                                                if (!current_signature.success) {
                                                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                                                  << " unable to compute source signature for '" << src_folder.string()
                                                                  << "' -> forcing rebuild\n";
                                                        meta_ok = false;
                                                } else if (stored_signature != current_signature.value) {
                                                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                                                  << " source signature mismatch (0x" << std::hex << stored_signature
                                                                  << " != 0x" << current_signature.value << std::dec << ") -> rebuild\n";
                                                        meta_ok = false;
                                                }
                                        }
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
                        metadata_valid = meta_ok;
                        if (!metadata_valid) {
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
                std::vector<bool> rebuild_variant(variant_count, false);

                if (!metadata_valid) {
                        std::fill(rebuild_variant.begin(), rebuild_variant.end(), true);
                }

                if (!prefer_cached) {
                        std::fill(rebuild_variant.begin(), rebuild_variant.end(), true);
                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                  << " prefer_cached disabled -> forcing rebuild\n";
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

                cleanup_variant_directories(cache_folder);
                cleanup_variant_directories(mask_cache_folder);

                const bool attempt_cache_load = metadata_valid && prefer_cached;
                if (attempt_cache_load) {
                        bool legacy_detected = false;
                        for (std::size_t idx = 0; idx < variant_count; ++idx) {
                                std::string variant_path = render_pipeline::ScalingLogic::VariantFolder(cache_folder, variant_steps_, idx);
                                std::vector<SDL_Surface*> loaded;
                                if (!cache.load_surface_sequence(variant_path, expected_frames, loaded)) {
                                        cache_invalid_detected = true;
                                        if (idx == 0 && cache.load_surface_sequence(cache_folder, expected_frames, loaded)) {
                                                legacy_detected = true;
                                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                                          << " legacy cache format detected for variant " << idx
                                                          << " -> metadata refresh required\n";
                                        } else {
                                                rebuild_variant[idx] = true;
                                                continue;
                                        }
                                }
                                variant_surfaces[idx] = std::move(loaded);
                        }
                        if (legacy_detected) {
                                cache_invalid_detected = true;
                                std::fill(rebuild_variant.begin(), rebuild_variant.end(), true);
                                free_surface_lists(variant_surfaces);
                        }
                }

                if (variant_surfaces.empty() || variant_surfaces[0].empty() || !variant_surfaces[0][0]) {
                        cache_invalid_detected = true;
                        std::fill(rebuild_variant.begin(), rebuild_variant.end(), true);
                        free_surface_lists(variant_surfaces);
                } else if (!rebuild_variant[0]) {
                        original_canvas_width  = orig_w;
                        original_canvas_height = orig_h;
                        scaled_sprite_w        = scaled_dimension(variant_surfaces[0][0]->w, safe_scale);
                        scaled_sprite_h        = scaled_dimension(variant_surfaces[0][0]->h, safe_scale);
                }

                bool need_generation = std::any_of(rebuild_variant.begin(), rebuild_variant.end(), [](bool v) { return v; });
                if (need_generation) {
                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                  << " generating variant surfaces for " << expected_frames
                                  << " frame(s) across " << variant_count << " step(s)\n";
                        for (std::size_t idx = 0; idx < variant_count; ++idx) {
                                if (!rebuild_variant[idx]) {
                                        continue;
                                }
                                for (SDL_Surface* surf : variant_surfaces[idx]) {
                                        if (surf) {
                                                SDL_FreeSurface(surf);
                                        }
                                }
                                variant_surfaces[idx].clear();
                        }
                        if (rebuild_variant[0]) {
                                std::vector<SDL_Surface*> base_surfaces;
                                base_surfaces.reserve(expected_frames);
                                for (int i = 0; i < expected_frames; ++i) {
                                        fs::path frame_path = src_folder / (std::to_string(i) + ".png");
                                        int new_w = 0, new_h = 0;
                                        SDL_Surface* scaled = cache.load_and_scale_surface(frame_path.string(), 1.0f, new_w, new_h);
                                        if (!scaled) {
                                                std::cerr << "[Animation] Failed to load or scale: " << frame_path.string() << "\n";
                                                continue;
                                        }
                                        if (i == 0) {
                                                original_canvas_width  = orig_w;
                                                original_canvas_height = orig_h;
                                                scaled_sprite_w        = scaled_dimension(new_w, safe_scale);
                                                scaled_sprite_h        = scaled_dimension(new_h, safe_scale);
                                        }
                                        base_surfaces.push_back(scaled);
                                }
                                if (base_surfaces.size() != static_cast<std::size_t>(expected_frames)) {
                                        for (SDL_Surface* surf : base_surfaces) {
                                                if (surf) {
                                                        SDL_FreeSurface(surf);
                                                }
                                        }
                                        flush_diagnostics();
                                        return;
                                }
                                variant_surfaces[0] = std::move(base_surfaces);
                        }
                        const std::vector<SDL_Surface*>& base_reference = variant_surfaces[0];
                        for (std::size_t idx = 1; idx < variant_count; ++idx) {
                                if (!rebuild_variant[idx]) {
                                        continue;
                                }
                                const float scale_step = variant_steps_[idx];
                                variant_surfaces[idx].reserve(base_reference.size());
                                for (SDL_Surface* base_surface : base_reference) {
                                        SDL_Surface* scaled = base_surface ? render_pipeline::CreateScaledSurface(base_surface, scale_step) : nullptr;
                                        variant_surfaces[idx].push_back(scaled);
                                }
                        }
                        for (std::size_t idx = 0; idx < variant_count; ++idx) {
                                if (!rebuild_variant[idx]) {
                                        continue;
                                }
                                const std::string variant_path = render_pipeline::ScalingLogic::VariantFolder(cache_folder, variant_steps_, idx);
                                cache.save_surface_sequence(variant_path, variant_surfaces[idx]);
                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                          << " stored " << variant_surfaces[idx].size() << " frame(s) for variant index " << idx << " (scale " << std::fixed << std::setprecision(2) << variant_steps_[idx] << std::defaultfloat << ")\n";
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
                        new_meta["has_masks"] = info.is_shaded;
                        const auto generated_signature = compute_source_signature(src_folder, expected_frames);
                        if (generated_signature.success) {
                                new_meta["source_signature"] = generated_signature.value;
                        } else {
                                new_meta["source_signature"] = 0;
                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                          << " unable to record source signature for '" << src_folder.string()
                                          << "' -> metadata will be refreshed on next load\n";
                        }
                        cache.save_metadata(meta_file, new_meta);
                        std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                  << " wrote metadata with steps "
                                  << format_percent_steps(expected_steps) << " revision " << expected_revision << "\n";
                } else if (!variant_surfaces.empty() && !variant_surfaces[0].empty() && variant_surfaces[0][0]) {
                        if (scaled_sprite_w <= 0 || scaled_sprite_h <= 0) {
                                scaled_sprite_w = scaled_dimension(variant_surfaces[0][0]->w, safe_scale);
                                scaled_sprite_h = scaled_dimension(variant_surfaces[0][0]->h, safe_scale);
                        }
                        original_canvas_width  = orig_w;
                        original_canvas_height = orig_h;
                }

                const bool cached_variants_loaded = (!need_generation && attempt_cache_load);

                if (info.is_shaded) {
                        bool announced_generation = false;
                        loading_status::notify("Creating texture/mask for " + info.name);
                        auto mask_result = GenerateFadedMask::BuildMasks(info.name, trigger, expected_steps, variant_surfaces, info.shadow_mask_settings);
                        mask_surfaces            = std::move(mask_result.first);
                        masks_loaded_from_cache  = mask_result.second;
                        if (masks_loaded_from_cache) {
                                loading_status::notify("Loading assets");
                        } else {
                                announced_generation = true;
                        }
                        if (mask_surfaces.size() != variant_surfaces.size()) {
                                mask_surfaces.resize(variant_surfaces.size());
                        }
                        if (masks_loaded_from_cache) {
                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                          << " loaded faded mask surfaces from cache\n";
                        } else {
                                const std::size_t mask_frame_count = (!mask_surfaces.empty() && !mask_surfaces.front().empty()) ? mask_surfaces.front().size() : 0;
                                std::cout << "[AnimationLoader] " << info.name << "::" << trigger
                                          << " generated " << mask_frame_count
                                          << " faded mask frame(s) across " << mask_surfaces.size() << " variant(s)\n";
                                if (announced_generation) {
                                        loading_status::notify("Loading assets");
                                }
                        }
                } else {
                        mask_surfaces.resize(variant_surfaces.size());
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
                                SDL_Surface* surface = (frame_idx < variant_surfaces[variant_idx].size()) ? variant_surfaces[variant_idx][frame_idx] : nullptr;
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
                                rebuild_depthcue_textures(renderer,
                                                          tex_variant,
                                                          tex_w,
                                                          tex_h,
                                                          cache_entry.depthcue_foreground_textures[variant_idx],
                                                          cache_entry.depthcue_background_textures[variant_idx]);

                                SDL_Surface* mask_surface = (variant_idx < mask_surfaces.size() && frame_idx < mask_surfaces[variant_idx].size()) ? mask_surfaces[variant_idx][frame_idx] : nullptr;
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
                if ((flipped_source || flip_vertical_source) && renderer && !frame_cache_.empty()) {
                        SDL_RendererFlip flip_flags = SDL_FLIP_NONE;
                        if (flipped_source) {
                                flip_flags = static_cast<SDL_RendererFlip>(flip_flags | SDL_FLIP_HORIZONTAL);
                        }
                        if (flip_vertical_source) {
                                flip_flags = static_cast<SDL_RendererFlip>(flip_flags | SDL_FLIP_VERTICAL);
                        }
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
                                        SDL_RenderCopyEx(renderer, src_tex, nullptr, &rect, 0.0, nullptr, flip_flags);
                                        SDL_SetRenderTarget(renderer, prev_target);
                                        SDL_DestroyTexture(src_tex);
                                        cache_entry.textures[variant_idx] = dst;
                                        cache_entry.widths[variant_idx]   = tex_w;
                                        cache_entry.heights[variant_idx]  = tex_h;
                                        rebuild_depthcue_textures(renderer,
                                                                  dst,
                                                                  tex_w,
                                                                  tex_h,
                                                                  cache_entry.depthcue_foreground_textures[variant_idx],
                                                                  cache_entry.depthcue_background_textures[variant_idx]);
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
                                                        SDL_RenderCopyEx(renderer, src_mask, nullptr, &rect, 0.0, nullptr, flip_flags);
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
                loaded_from_cache = cached_variants_loaded;
        }
        if (!movement_specified && source.kind == "animation" && inherit_source_movement && !source.name.empty()) {
                auto it = info.animations.find(source.name);
                if (it != info.animations.end()) {
                        const Animation& src_anim = it->second;
                        movement_paths_           = src_anim.movement_paths_;
                        if (!movement_paths_.empty()) {
                                if (reverse_source) {
                                        for (auto& path : movement_paths_) {
                                                std::reverse(path.begin(), path.end());
                                        }
                                }
                                if (flip_movement_horizontal) {
                                        for (auto& path : movement_paths_) {
                                                for (auto& frame : path) {
                                                        frame.dx = -frame.dx;
                                                }
                                        }
                                }
                                if (flip_movement_vertical) {
                                        for (auto& path : movement_paths_) {
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
        if (movement_specified && source.kind == "animation") {
                if (reverse_source) {
                        for (auto& path : movement_paths_) {
                                std::reverse(path.begin(), path.end());
                        }
                }
                if (flip_movement_horizontal) {
                        for (auto& path : movement_paths_) {
                                for (auto& frame : path) {
                                        frame.dx = -frame.dx;
                                }
                        }
                }
                if (flip_movement_vertical) {
                        for (auto& path : movement_paths_) {
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

        {
                std::ostringstream oss;
                oss << "[AnimationLoader] " << info.name << "::" << trigger
                    << " -> " << frames.size() << " frame(s)";
                if (frame_width > 0 && frame_height > 0) {
                        oss << " @ " << frame_width << "x" << frame_height;
                }
                oss << " from " << origin_label << " in " << std::fixed << std::setprecision(3) << elapsed_secs << "s";
                vibble::log::debug(oss.str());
        }
        flush_diagnostics();
}

SDL_Texture* Animation::get_frame(const AnimationFrame* frame) const {
        if (!frame) return nullptr;
        const int index = frame->frame_index;
        if (index < 0 || index >= static_cast<int>(frames.size())) return nullptr;
        return frames[index];
}

const AnimationFrame* Animation::get_first_frame(std::size_t path_index) const {
        if (movement_paths_.empty()) return nullptr;
        path_index = clamp_path_index(path_index);
        const auto& path = movement_paths_[path_index];
        if (path.empty()) return nullptr;
        return &path[0];
}

AnimationFrame* Animation::get_first_frame(std::size_t path_index) {
        return const_cast<AnimationFrame*>(std::as_const(*this).get_first_frame(path_index));
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
