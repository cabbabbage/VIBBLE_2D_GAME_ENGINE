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
