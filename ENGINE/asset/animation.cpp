#include "animation.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_types.hpp"
#include "asset/surface_utils.hpp"
#include "utils/cache_manager.hpp"
#include "render/render.hpp"
#include "render/scaling_logic.hpp"
#include "utils/loading_status_notifier.hpp"
#include "utils/log.hpp"
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <algorithm>
#include <array>
#include <cctype>
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

Animation::Animation() = default;

Animation::OnEndDirective Animation::classify_on_end(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (char ch : value) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if (lowered.empty() || lowered == "default") {
        return OnEndDirective::Default;
    }
    if (lowered == "kill") {
        return OnEndDirective::Kill;
    }
    if (lowered == "lock") {
        return OnEndDirective::Lock;
    }
    if (lowered == "reverse") {
        return OnEndDirective::Reverse;
    }
    return OnEndDirective::Animation;
}

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
        for (SDL_Texture*& tex : cache_entry.foreground_textures) {
            if (tex) {
                SDL_DestroyTexture(tex);
                tex = nullptr;
            }
        }
        for (SDL_Texture*& tex : cache_entry.background_textures) {
            if (tex) {
                SDL_DestroyTexture(tex);
                tex = nullptr;
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
    if (audio_clip.chunk) {
        audio_clip.chunk.reset();
    }
}

void Animation::adopt_prebuilt_frames(std::vector<FrameCache> caches,
                                      std::vector<SDL_Texture*> base_frames,
                                      std::vector<SDL_Texture*> base_masks,
                                      std::vector<float> variant_steps) {
    clear_texture_cache();
    frame_cache_   = std::move(caches);
    variant_steps_ = std::move(variant_steps);
    number_of_frames = static_cast<int>(frame_cache_.size());

    movement_paths_.clear();
    if (number_of_frames <= 0) {
            movement_paths_.emplace_back();
            return;
    }

    movement_paths_.emplace_back();
    auto& path = movement_paths_.back();
    path.resize(number_of_frames);
    frames.reserve(number_of_frames);

    for (std::size_t idx = 0; idx < path.size(); ++idx) {
            auto& frame = path[idx];
            frame.frame_index = static_cast<int>(idx);
            frame.is_first   = (idx == 0);
            frame.is_last    = (idx + 1 == path.size());
            frame.next       = (idx + 1 < path.size()) ? &path[idx + 1] : nullptr;
            frame.prev       = (idx > 0) ? &path[idx - 1] : nullptr;
            
            if (idx < frame_cache_.size()) {
                const auto& cache = frame_cache_[idx];
                for (size_t v = 0; v < cache.textures.size(); ++v) {
                    FrameVariant variant;
                    variant.varient = static_cast<int>(v);
                    variant.base_texture = cache.textures[v];
                    if (v < cache.foreground_textures.size()) variant.foreground_texture = cache.foreground_textures[v];
                    if (v < cache.background_textures.size()) variant.background_texture = cache.background_textures[v];
                    if (v < cache.mask_textures.size()) variant.shadow_mask_texture = cache.mask_textures[v];
                    if (v < cache.depthcue_foreground_textures.size()) variant.depthcue_foreground_texture = cache.depthcue_foreground_textures[v];
                    if (v < cache.depthcue_background_textures.size()) variant.depthcue_background_texture = cache.depthcue_background_textures[v];
                    
                    frame.variants.push_back(variant);
                }
            }
            frames.push_back(&frame);
    }
}

bool Animation::copy_from(const Animation& source, bool flip_horizontal, bool flip_vertical, bool reverse_frames, SDL_Renderer* renderer, AssetInfo& info) {
    if (!renderer || source.frame_cache_.empty()) {
        return false;
    }

    // Helper to apply scale mode
    auto apply_scale_mode = [&info](SDL_Texture* tex) {
        if (!tex) return;
#if SDL_VERSION_ATLEAST(2, 0, 12)
        if (info.smooth_scaling) {
            SDL_SetTextureScaleMode(tex, SDL_ScaleModeBest);
        } else {
            SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
        }
#endif
    };

    // Helper to duplicate a texture (respecting flip) so derived animations own their assets
    auto clone_texture = [&](SDL_Texture* src, int width_hint, int height_hint, SDL_RendererFlip flip_flags, int* out_w = nullptr, int* out_h = nullptr) -> SDL_Texture* {
        if (!src) return nullptr;

        Uint32 fmt = SDL_PIXELFORMAT_RGBA8888;
        int access = 0;
        int tex_w = width_hint;
        int tex_h = height_hint;

        const bool need_dims = tex_w <= 0 || tex_h <= 0;
        if (SDL_QueryTexture(src, &fmt, &access, need_dims ? &tex_w : nullptr, need_dims ? &tex_h : nullptr) != 0 ||
            tex_w <= 0 || tex_h <= 0) {
            tex_w = std::max(1, tex_w);
            tex_h = std::max(1, tex_h);
        }

        SDL_Texture* dst = SDL_CreateTexture(renderer, fmt, SDL_TEXTUREACCESS_TARGET, tex_w, tex_h);
        if (!dst) {
            return nullptr;
        }

        SDL_SetTextureBlendMode(dst, SDL_BLENDMODE_BLEND);
        apply_scale_mode(dst);

        SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
        SDL_SetRenderTarget(renderer, dst);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);

        SDL_Rect rect{ 0, 0, tex_w, tex_h };
        if (flip_flags != SDL_FLIP_NONE) {
            SDL_RenderCopyEx(renderer, src, nullptr, &rect, 0.0, nullptr, flip_flags);
        } else {
            SDL_RenderCopy(renderer, src, nullptr, &rect);
        }

        SDL_SetRenderTarget(renderer, prev_target);
        if (out_w) *out_w = tex_w;
        if (out_h) *out_h = tex_h;
        return dst;
    };

    // Clear existing cache
    clear_texture_cache();

    // Inherit core properties
    variant_steps_ = source.variant_steps_;
    locked = source.locked;
    inherit_source_movement = source.inherit_source_movement;

    const std::size_t frame_count = source.frame_cache_.size();
    const std::size_t variant_count = variant_steps_.size();

    if (variant_count == 0 || frame_count == 0) {
        return false;
    }

    // Determine flip flags for SDL_RenderCopyEx
    SDL_RendererFlip flip_flags = SDL_FLIP_NONE;
    if (flip_horizontal) {
        flip_flags = static_cast<SDL_RendererFlip>(flip_flags | SDL_FLIP_HORIZONTAL);
    }
    if (flip_vertical) {
        flip_flags = static_cast<SDL_RendererFlip>(flip_flags | SDL_FLIP_VERTICAL);
    }

    // Copy all frames
    frame_cache_.reserve(frame_count);
    for (std::size_t frame_idx = 0; frame_idx < frame_count; ++frame_idx) {
        const FrameCache& src_cache = source.frame_cache_[frame_idx];
        FrameCache dst_cache;
        dst_cache.resize(variant_count);

        // Copy each variant
        for (std::size_t variant_idx = 0; variant_idx < variant_count; ++variant_idx) {
            if (variant_idx >= src_cache.textures.size()) {
                continue;
            }

            SDL_Texture* src_tex = src_cache.textures[variant_idx];
            int tex_w = src_cache.widths[variant_idx];
            int tex_h = src_cache.heights[variant_idx];

            SDL_Texture* dst_tex = clone_texture(src_tex, tex_w, tex_h, flip_flags, &tex_w, &tex_h);
            if (!dst_tex) {
                continue;
            }
            dst_cache.textures[variant_idx] = dst_tex;
            dst_cache.widths[variant_idx] = tex_w;
            dst_cache.heights[variant_idx] = tex_h;

            // Copy mask texture if present
            SDL_Texture* src_mask = (variant_idx < src_cache.mask_textures.size()) ? src_cache.mask_textures[variant_idx] : nullptr;
            if (src_mask) {
                int mask_w = src_cache.mask_widths[variant_idx];
                int mask_h = src_cache.mask_heights[variant_idx];

                SDL_Texture* dst_mask = clone_texture(src_mask, mask_w, mask_h, flip_flags, &mask_w, &mask_h);
                dst_cache.mask_textures[variant_idx] = dst_mask;
                dst_cache.mask_widths[variant_idx] = mask_w;
                dst_cache.mask_heights[variant_idx] = mask_h;
            }

            // Copy foreground/background/depthcue overlays so derived animations keep effects
            SDL_Texture* src_fg = (variant_idx < src_cache.foreground_textures.size()) ? src_cache.foreground_textures[variant_idx] : nullptr;
            SDL_Texture* src_bg = (variant_idx < src_cache.background_textures.size()) ? src_cache.background_textures[variant_idx] : nullptr;
            SDL_Texture* src_dfg = (variant_idx < src_cache.depthcue_foreground_textures.size()) ? src_cache.depthcue_foreground_textures[variant_idx] : nullptr;
            SDL_Texture* src_dbg = (variant_idx < src_cache.depthcue_background_textures.size()) ? src_cache.depthcue_background_textures[variant_idx] : nullptr;

            dst_cache.foreground_textures[variant_idx] = clone_texture(src_fg, tex_w, tex_h, flip_flags);
            dst_cache.background_textures[variant_idx] = clone_texture(src_bg, tex_w, tex_h, flip_flags);
            dst_cache.depthcue_foreground_textures[variant_idx] = clone_texture(src_dfg, tex_w, tex_h, flip_flags);
            dst_cache.depthcue_background_textures[variant_idx] = clone_texture(src_dbg, tex_w, tex_h, flip_flags);
        }

        frame_cache_.push_back(std::move(dst_cache));
    }

    // Reverse frame order if requested
    if (reverse_frames && !frame_cache_.empty()) {
        std::reverse(frame_cache_.begin(), frame_cache_.end());
    }

    return !frame_cache_.empty();
}

const FrameVariant* Animation::get_frame(const AnimationFrame* frame, float requested_scale) const {
    if (!frame || frame->variants.empty()) return nullptr;

    // Select the smallest available variant that is at least as large as the requested scale.
    const auto selection = render_pipeline::ScalingLogic::Choose(requested_scale, variant_steps_);
    int best_variant_idx = selection.index;

    if (best_variant_idx < 0) best_variant_idx = 0;
    if (best_variant_idx >= static_cast<int>(frame->variants.size())) best_variant_idx = static_cast<int>(frame->variants.size()) - 1;

    return &frame->variants[best_variant_idx];
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
    static_flag = is_frozen() || locked;
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

void Animation::inherit_movement_from(const Animation& source) {
    movement_paths_ = source.movement_paths_;
    if (movement_paths_.empty()) {
        return;
    }
    if (reverse_source) {
        for (auto& path : movement_paths_) {
            std::reverse(path.begin(), path.end());
        }
    }
    if (flip_movement_horizontal) {
        for (auto& path : movement_paths_) {
            for (auto& frame : path) {
                frame.dx = -frame.dx;
                for (auto& child : frame.children) {
                    child.dx = -child.dx;
                }
            }
        }
    }
    if (flip_movement_vertical) {
        for (auto& path : movement_paths_) {
            for (auto& frame : path) {
                frame.dy = -frame.dy;
                for (auto& child : frame.children) {
                    child.dy = -child.dy;
                }
            }
        }
    }
}

std::size_t Animation::clamp_path_index(std::size_t index) const {
    if (movement_paths_.empty()) return 0;
    if (index >= movement_paths_.size()) return 0;
    return index;
}

void Animation::freeze() { frozen = true; }

bool Animation::is_frozen() const { return frozen || frames.size() <= 1; }

bool Animation::has_audio() const { return static_cast<bool>(audio_clip.chunk); }

Mix_Chunk* Animation::audio_chunk() const { return audio_clip.chunk.get(); }

const Animation::AudioClip* Animation::audio_data() const {
    if (!audio_clip.chunk) {
        return nullptr;
    }
    return &audio_clip;
}
