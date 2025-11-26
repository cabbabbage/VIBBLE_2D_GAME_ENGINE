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

    // Clear existing cache
    clear_texture_cache();

    // Inherit core properties
    variant_steps_ = source.variant_steps_;
    locked = source.locked;
    playback_fps = source.playback_fps;

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
            if (!src_tex) {
                continue;
            }

            // Get source texture dimensions
            Uint32 fmt = SDL_PIXELFORMAT_RGBA8888;
            int access = 0;
            int tex_w = src_cache.widths[variant_idx];
            int tex_h = src_cache.heights[variant_idx];
            
            if (tex_w <= 0 || tex_h <= 0) {
                if (SDL_QueryTexture(src_tex, &fmt, &access, &tex_w, &tex_h) != 0 || tex_w <= 0 || tex_h <= 0) {
                    continue;
                }
            } else if (SDL_QueryTexture(src_tex, &fmt, &access, nullptr, nullptr) != 0) {
                fmt = SDL_PIXELFORMAT_RGBA8888;
            }

            // Create destination texture
            SDL_Texture* dst_tex = SDL_CreateTexture(renderer, fmt, SDL_TEXTUREACCESS_TARGET, tex_w, tex_h);
            if (!dst_tex) {
                continue;
            }

            SDL_SetTextureBlendMode(dst_tex, SDL_BLENDMODE_BLEND);
            apply_scale_mode(dst_tex);

            // Render source to destination with flip if needed
            SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
            SDL_SetRenderTarget(renderer, dst_tex);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            SDL_RenderClear(renderer);

            SDL_Rect rect{0, 0, tex_w, tex_h};
            if (flip_flags != SDL_FLIP_NONE) {
                SDL_RenderCopyEx(renderer, src_tex, nullptr, &rect, 0.0, nullptr, flip_flags);
            } else {
                SDL_RenderCopy(renderer, src_tex, nullptr, &rect);
            }

            SDL_SetRenderTarget(renderer, prev_target);

            dst_cache.textures[variant_idx] = dst_tex;
            dst_cache.widths[variant_idx] = tex_w;
            dst_cache.heights[variant_idx] = tex_h;

            // Copy mask texture if present
            SDL_Texture* src_mask = (variant_idx < src_cache.mask_textures.size()) ? src_cache.mask_textures[variant_idx] : nullptr;
            if (src_mask) {
                int mask_w = src_cache.mask_widths[variant_idx];
                int mask_h = src_cache.mask_heights[variant_idx];

                if (mask_w <= 0 || mask_h <= 0) {
                    Uint32 mask_fmt = SDL_PIXELFORMAT_RGBA8888;
                    int mask_access = 0;
                    if (SDL_QueryTexture(src_mask, &mask_fmt, &mask_access, &mask_w, &mask_h) != 0 || mask_w <= 0 || mask_h <= 0) {
                        mask_w = 0;
                        mask_h = 0;
                    }
                }

                if (mask_w > 0 && mask_h > 0) {
                    Uint32 mask_fmt = SDL_PIXELFORMAT_RGBA8888;
                    SDL_QueryTexture(src_mask, &mask_fmt, nullptr, nullptr, nullptr);
                    
                    SDL_Texture* dst_mask = SDL_CreateTexture(renderer, mask_fmt, SDL_TEXTUREACCESS_TARGET, mask_w, mask_h);
                    if (dst_mask) {
                        SDL_SetTextureBlendMode(dst_mask, SDL_BLENDMODE_BLEND);
                        apply_scale_mode(dst_mask);

                        SDL_Texture* prev_target_mask = SDL_GetRenderTarget(renderer);
                        SDL_SetRenderTarget(renderer, dst_mask);
                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                        SDL_RenderClear(renderer);

                        SDL_Rect mask_rect{0, 0, mask_w, mask_h};
                        if (flip_flags != SDL_FLIP_NONE) {
                            SDL_RenderCopyEx(renderer, src_mask, nullptr, &mask_rect, 0.0, nullptr, flip_flags);
                        } else {
                            SDL_RenderCopy(renderer, src_mask, nullptr, &mask_rect);
                        }

                        SDL_SetRenderTarget(renderer, prev_target_mask);

                        dst_cache.mask_textures[variant_idx] = dst_mask;
                        dst_cache.mask_widths[variant_idx] = mask_w;
                        dst_cache.mask_heights[variant_idx] = mask_h;
                    }
                }
            }

            // Note: Foreground/background/depthcue textures are not copied as they're not generated at runtime
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
    
    //if requested_scale == 25 50 75 100 percent return that exact matching frame variant
    
    if (!frame || frame->variants.empty()) return nullptr;
    
    int best_variant_idx = 0;
    if (!variant_steps_.empty()) {
        auto it = std::lower_bound(variant_steps_.begin(), variant_steps_.end(), requested_scale);
        if (it != variant_steps_.end()) {
            best_variant_idx = static_cast<int>(std::distance(variant_steps_.begin(), it));
        } else {
            best_variant_idx = static_cast<int>(variant_steps_.size()) - 1;
        }
    }
    
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
