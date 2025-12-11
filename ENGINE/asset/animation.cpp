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

namespace {

inline void apply_scale_mode(SDL_Texture* tex, const AssetInfo& info) {
#if SDL_VERSION_ATLEAST(2, 0, 12)
    if (tex) {
        SDL_SetTextureScaleMode(tex, info.smooth_scaling ? SDL_ScaleModeBest : SDL_ScaleModeNearest);
    }
#else
    (void)tex;
    (void)info;
#endif
}

struct VariantLayerPaths {
    std::filesystem::path normal;
    std::filesystem::path foreground;
    std::filesystem::path background;
    std::filesystem::path mask;
};

VariantLayerPaths build_variant_paths(const std::string& cache_root,
                                      const std::vector<float>& variant_steps,
                                      std::size_t variant_idx) {
    VariantLayerPaths out;
    const std::string folder = render_pipeline::ScalingLogic::VariantFolder(cache_root, variant_steps, variant_idx);
    std::filesystem::path base(folder);
    out.normal     = base / "normal";
    out.foreground = base / "foreground";
    out.background = base / "background";
    out.mask       = base / "mask";
    return out;
}

SDL_Texture* load_texture_from_path(SDL_Renderer* renderer,
                                    const std::filesystem::path& path,
                                    int& out_w,
                                    int& out_h) {
    out_w = 0;
    out_h = 0;
    SDL_Texture* tex = nullptr;
    SDL_Surface* surf = CacheManager::load_surface(path.generic_string());
    if (!surf) {
        return nullptr;
    }
    tex = CacheManager::surface_to_texture(renderer, surf);
    if (tex) {
        out_w = surf->w;
        out_h = surf->h;
    }
    SDL_FreeSurface(surf);
    return tex;
}

void destroy_texture(SDL_Texture*& tex) {
    if (tex) {
        SDL_DestroyTexture(tex);
        tex = nullptr;
    }
}

}

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

bool Animation::rebuild_frame(int frame_index,
                              SDL_Renderer* renderer,
                              const AssetInfo& info,
                              const std::string& animation_id) {
    if (!renderer || frame_index < 0 || animation_id.empty()) {
        return false;
    }

    const std::size_t idx = static_cast<std::size_t>(frame_index);
    if (idx >= frame_cache_.size()) {
        return false;
    }

    std::vector<float> variant_steps = variant_steps_;
    if (variant_steps.empty()) {
        variant_steps = info.scale_variants;
    }
    if (variant_steps.empty()) {
        variant_steps.push_back(1.0f);
    }

    Animation::FrameCache& cache_entry = frame_cache_[idx];
    cache_entry.resize(variant_steps.size());

    const std::string cache_root = (std::filesystem::path("cache") / info.name / "animations").lexically_normal().generic_string();

    bool success = true;

    for (std::size_t variant_idx = 0; variant_idx < variant_steps.size(); ++variant_idx) {
        const VariantLayerPaths paths = build_variant_paths(cache_root, variant_steps, variant_idx);

        const std::filesystem::path base_path = paths.normal / (std::to_string(idx) + ".png");
        int base_w = 0, base_h = 0;
        SDL_Texture* base_tex = load_texture_from_path(renderer, base_path, base_w, base_h);
        if (!base_tex) {
            success = false;
            continue;
        }
        apply_scale_mode(base_tex, info);

        int fg_w = 0, fg_h = 0;
        SDL_Texture* fg_tex = load_texture_from_path(renderer, paths.foreground / (std::to_string(idx) + ".png"), fg_w, fg_h);
        if (fg_tex) {
            apply_scale_mode(fg_tex, info);
        }

        int bg_w = 0, bg_h = 0;
        SDL_Texture* bg_tex = load_texture_from_path(renderer, paths.background / (std::to_string(idx) + ".png"), bg_w, bg_h);
        if (bg_tex) {
            apply_scale_mode(bg_tex, info);
        }

        SDL_Texture* mask_tex = nullptr;
        int mask_w = 0, mask_h = 0;
        if (info.is_shaded) {
            mask_tex = load_texture_from_path(renderer, paths.mask / (std::to_string(idx) + ".png"), mask_w, mask_h);
            if (mask_tex) {
                apply_scale_mode(mask_tex, info);
            }
            if (!mask_tex) {
                success = false;
            }
        }

        destroy_texture(cache_entry.textures[variant_idx]);
        destroy_texture(cache_entry.foreground_textures[variant_idx]);
        destroy_texture(cache_entry.background_textures[variant_idx]);
        destroy_texture(cache_entry.mask_textures[variant_idx]);

        cache_entry.textures[variant_idx] = base_tex;
        cache_entry.widths[variant_idx] = base_w;
        cache_entry.heights[variant_idx] = base_h;
        cache_entry.foreground_textures[variant_idx] = fg_tex;
        cache_entry.background_textures[variant_idx] = bg_tex;
        cache_entry.mask_textures[variant_idx] = mask_tex;
        cache_entry.mask_widths[variant_idx] = mask_w;
        cache_entry.mask_heights[variant_idx] = mask_h;
    }

    for (auto& path : movement_paths_) {
        if (idx >= path.size()) {
            continue;
        }
        AnimationFrame& frame = path[idx];
        frame.variants.clear();
        frame.variants.reserve(variant_steps.size());
        for (std::size_t v = 0; v < variant_steps.size(); ++v) {
            FrameVariant variant;
            variant.varient = static_cast<int>(v);
            variant.base_texture = cache_entry.textures[v];
            variant.foreground_texture = (v < cache_entry.foreground_textures.size()) ? cache_entry.foreground_textures[v] : nullptr;
            variant.background_texture = (v < cache_entry.background_textures.size()) ? cache_entry.background_textures[v] : nullptr;
            variant.shadow_mask_texture = (v < cache_entry.mask_textures.size()) ? cache_entry.mask_textures[v] : nullptr;
            frame.variants.push_back(variant);
        }
    }

    if (idx == 0 && !frame_cache_.empty() && !frame_cache_[0].textures.empty()) {
        preview_texture = frame_cache_[0].textures[0];
    }

    return success;
}

bool Animation::rebuild_animation(SDL_Renderer* renderer,
                                  const AssetInfo& info,
                                  const std::string& animation_id) {
    if (!renderer || animation_id.empty()) {
        return false;
    }
    const std::size_t frame_count = frame_cache_.size();
    if (frame_count == 0) {
        return false;
    }

    bool ok = true;
    for (std::size_t i = 0; i < frame_count; ++i) {
        ok = rebuild_frame(static_cast<int>(i), renderer, info, animation_id) && ok;
    }
    return ok;
}

void Animation::clear_texture_cache() {
    for (auto& cache_entry : frame_cache_) {
        for (SDL_Texture*& tex : cache_entry.textures) {
            if (tex) {
                SDL_DestroyTexture(tex);
                tex = nullptr;
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
        for (SDL_Texture*& mask_tex : cache_entry.mask_textures) {
            if (mask_tex) {
                SDL_DestroyTexture(mask_tex);
                mask_tex = nullptr;
            }
        }
    }
    frame_cache_.clear();
    if (audio_clip.chunk) {
        audio_clip.chunk.reset();
    }
}

const AnimationChildData* Animation::find_child_timeline(std::string_view child_name) const {
    if (child_name.empty()) {
        return nullptr;
    }
    const auto it = std::find_if(child_data_.begin(), child_data_.end(), [&](const AnimationChildData& entry) {
        return entry.asset_name == child_name;
    });
    return (it == child_data_.end()) ? nullptr : &(*it);
}

AnimationChildData* Animation::find_child_timeline(std::string_view child_name) {
    if (child_name.empty()) {
        return nullptr;
    }
    const auto it = std::find_if(child_data_.begin(), child_data_.end(), [&](const AnimationChildData& entry) {
        return entry.asset_name == child_name;
    });
    return (it == child_data_.end()) ? nullptr : &(*it);
}

void Animation::rebuild_child_timelines_from_frames() {
    if (child_asset_names_.empty()) {
        child_data_.clear();
        rebuild_child_start_events_from_timelines();
        return;
    }

    std::unordered_map<std::string, const AnimationChildData*> previous_by_asset;
    previous_by_asset.reserve(child_data_.size());
    for (const auto& existing : child_data_) {
        if (!existing.asset_name.empty()) {
            previous_by_asset.emplace(existing.asset_name, &existing);
        }
    }

    const bool has_parent_frames = !frames.empty();
    const std::size_t parent_frame_count = has_parent_frames ? frames.size() : 0;

    std::vector<AnimationChildData> rebuilt;
    rebuilt.reserve(child_asset_names_.size());

    for (std::size_t child_idx = 0; child_idx < child_asset_names_.size(); ++child_idx) {
        const std::string& asset_name = child_asset_names_[child_idx];
        const auto prev_it = previous_by_asset.find(asset_name);
        const AnimationChildData* previous = (prev_it != previous_by_asset.end()) ? prev_it->second : nullptr;

        AnimationChildData descriptor;
        descriptor.asset_name = asset_name;
        descriptor.name = previous ? previous->name : std::string{};
        descriptor.animation_override = previous ? previous->animation_override : std::string{};
        descriptor.mode = previous ? previous->mode : descriptor.mode;
        descriptor.auto_start = previous ? previous->auto_start : (descriptor.mode == AnimationChildMode::Static);

        auto make_default_sample = [&](int index) {
            AnimationChildFrameData sample{};
            sample.child_index = index;
            sample.render_in_front = true;
            sample.visible = false;
            sample.dx = 0;
            sample.dy = 0;
            sample.degree = 0.0f;
            return sample;
};

        if (descriptor.mode == AnimationChildMode::Static) {
            const std::size_t sample_count = (parent_frame_count > 0) ? parent_frame_count : ((previous && previous->is_static() && !previous->frames.empty()) ? previous->frames.size() : static_cast<std::size_t>(1));
            descriptor.frames.assign(sample_count, make_default_sample(static_cast<int>(child_idx)));
            for (std::size_t frame_idx = 0; frame_idx < sample_count; ++frame_idx) {
                if (has_parent_frames && frame_idx < frames.size()) {
                    AnimationFrame* frame = frames[frame_idx];
                    if (frame) {
                        const auto& legacy_children = frame->children;
                        auto legacy_it = std::find_if(legacy_children.begin(), legacy_children.end(), [&](const AnimationChildFrameData& entry) {
                            return entry.child_index == static_cast<int>(child_idx);
                        });
                        if (legacy_it != legacy_children.end()) {
                            descriptor.frames[frame_idx] = *legacy_it;
                            descriptor.frames[frame_idx].child_index = static_cast<int>(child_idx);
                            continue;
                        }
                    }
                }
                if (previous && previous->is_static() && frame_idx < previous->frames.size()) {
                    descriptor.frames[frame_idx] = previous->frames[frame_idx];
                    descriptor.frames[frame_idx].child_index = static_cast<int>(child_idx);
                }
            }
        } else {
            if (previous && previous->is_async() && !previous->frames.empty()) {
                descriptor.frames = previous->frames;
                for (auto& sample : descriptor.frames) {
                    sample.child_index = static_cast<int>(child_idx);
                }
            }
            if (descriptor.frames.empty()) {
                descriptor.frames.push_back(make_default_sample(static_cast<int>(child_idx)));
            }
        }

        rebuilt.push_back(std::move(descriptor));
    }

    child_data_ = std::move(rebuilt);
    rebuild_child_start_events_from_timelines();
}

void Animation::rebuild_child_start_events_from_timelines() {
    for (AnimationFrame* frame : frames) {
        if (frame) {
            frame->child_start_events.clear();
        }
    }
    if (child_data_.empty() || frames.empty()) {
        return;
    }

    for (std::size_t child_idx = 0; child_idx < child_data_.size(); ++child_idx) {
        const AnimationChildData& descriptor = child_data_[child_idx];
        if (!descriptor.is_static() || descriptor.frames.empty()) {
            continue;
        }

        const auto first_visible = std::find_if(descriptor.frames.begin(), descriptor.frames.end(), [](const AnimationChildFrameData& entry) {
            return entry.visible;
        });
        if (first_visible == descriptor.frames.end()) {
            continue;
        }

        const std::size_t frame_index = static_cast<std::size_t>(std::distance(descriptor.frames.begin(), first_visible));
        if (frame_index >= frames.size()) {
            continue;
        }
        AnimationFrame* target_frame = frames[frame_index];
        if (!target_frame) {
            continue;
        }
        auto& starts = target_frame->child_start_events;
        const int child_slot = static_cast<int>(child_idx);
        if (std::find(starts.begin(), starts.end(), child_slot) == starts.end()) {
            starts.push_back(child_slot);
        }
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
                    if (v < cache.foreground_textures.size()) {
                        variant.foreground_texture = cache.foreground_textures[v];
                    }
                    if (v < cache.background_textures.size()) {
                        variant.background_texture = cache.background_textures[v];
                    }
                    if (v < cache.mask_textures.size()) variant.shadow_mask_texture = cache.mask_textures[v];

                    frame.variants.push_back(variant);
                }
            }
            frames.push_back(&frame);
    }

    rebuild_child_timelines_from_frames();
}

bool Animation::copy_from(const Animation& source, bool flip_horizontal, bool flip_vertical, bool reverse_frames, SDL_Renderer* renderer, AssetInfo& info) {
    if (!renderer || source.frame_cache_.empty()) {
        return false;
    }

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

    clear_texture_cache();

    variant_steps_ = source.variant_steps_;
    locked = source.locked;
    inherit_source_movement = source.inherit_source_movement;

    const std::size_t frame_count = source.frame_cache_.size();
    const std::size_t variant_count = variant_steps_.size();

    if (variant_count == 0 || frame_count == 0) {
        return false;
    }

    SDL_RendererFlip flip_flags = SDL_FLIP_NONE;
    if (flip_horizontal) {
        flip_flags = static_cast<SDL_RendererFlip>(flip_flags | SDL_FLIP_HORIZONTAL);
    }
    if (flip_vertical) {
        flip_flags = static_cast<SDL_RendererFlip>(flip_flags | SDL_FLIP_VERTICAL);
    }

    frame_cache_.reserve(frame_count);
    for (std::size_t frame_idx = 0; frame_idx < frame_count; ++frame_idx) {
        const FrameCache& src_cache = source.frame_cache_[frame_idx];
        FrameCache dst_cache;
        dst_cache.resize(variant_count);

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

            SDL_Texture* src_fg = (variant_idx < src_cache.foreground_textures.size()) ? src_cache.foreground_textures[variant_idx] : nullptr;
            if (src_fg) {
                SDL_Texture* dst_fg = clone_texture(src_fg, tex_w, tex_h, flip_flags);
                dst_cache.foreground_textures[variant_idx] = dst_fg;
            }

            SDL_Texture* src_bg = (variant_idx < src_cache.background_textures.size()) ? src_cache.background_textures[variant_idx] : nullptr;
            if (src_bg) {
                SDL_Texture* dst_bg = clone_texture(src_bg, tex_w, tex_h, flip_flags);
                dst_cache.background_textures[variant_idx] = dst_bg;
            }

            SDL_Texture* src_mask = (variant_idx < src_cache.mask_textures.size()) ? src_cache.mask_textures[variant_idx] : nullptr;
            if (src_mask) {
                int mask_w = src_cache.mask_widths[variant_idx];
                int mask_h = src_cache.mask_heights[variant_idx];

                SDL_Texture* dst_mask = clone_texture(src_mask, mask_w, mask_h, flip_flags, &mask_w, &mask_h);
                dst_cache.mask_textures[variant_idx] = dst_mask;
                dst_cache.mask_widths[variant_idx] = mask_w;
                dst_cache.mask_heights[variant_idx] = mask_h;
            }
        }

        frame_cache_.push_back(std::move(dst_cache));
    }

    if (reverse_frames && !frame_cache_.empty()) {
        std::reverse(frame_cache_.begin(), frame_cache_.end());
    }

    rebuild_child_timelines_from_frames();
    return !frame_cache_.empty();
}

const FrameVariant* Animation::get_frame(const AnimationFrame* frame, float requested_scale) const {
    if (!frame || frame->variants.empty()) return nullptr;

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
