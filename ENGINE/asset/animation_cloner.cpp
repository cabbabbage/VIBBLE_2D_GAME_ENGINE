#include "animation_cloner.hpp"

#include <algorithm>
#include <utility>

#include "asset/asset_info.hpp"

namespace {

void apply_scale_mode(SDL_Texture* tex, const AssetInfo& info) {
    if (!tex) return;
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(tex, info.smooth_scaling ? SDL_ScaleModeBest : SDL_ScaleModeNearest);
#endif
}

SDL_Texture* clone_texture(SDL_Texture* src,
                           int width_hint,
                           int height_hint,
                           SDL_RendererFlip flip_flags,
                           SDL_Renderer* renderer,
                           const AssetInfo& info,
                           int* out_w = nullptr,
                           int* out_h = nullptr) {
    if (!src || !renderer) {
        return nullptr;
    }

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
    apply_scale_mode(dst, info);

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
}

}

void AnimationCloner::ApplyChildFrameFlip(std::vector<AnimationChildFrameData>& children,
                                          const Options& opts) {
    const bool flip_children_h = opts.flip_horizontal || opts.flip_movement_horizontal;
    const bool flip_children_v = opts.flip_vertical   || opts.flip_movement_vertical;
    if (!flip_children_h && !flip_children_v) {
        return;
    }

    for (auto& child : children) {
        if (flip_children_h) child.dx = -child.dx;
        if (flip_children_v) child.dy = -child.dy;
    }
}

namespace {

void copy_children(const std::vector<AnimationChildFrameData>& src_children,
                   std::vector<AnimationChildFrameData>& dst_children,
                   const AnimationCloner::Options& opts) {
    dst_children = src_children;
    AnimationCloner::ApplyChildFrameFlip(dst_children, opts);
}

}

bool AnimationCloner::Clone(const Animation& source,
                            Animation&       dest,
                            const Options&   opts,
                            SDL_Renderer*    renderer,
                            AssetInfo&       info) {
    if (!renderer || source.frame_cache_.empty()) {
        return false;
    }

    dest.clear_texture_cache();

    dest.variant_steps_   = source.variant_steps_;
    dest.locked           = source.locked;
    dest.on_end_animation = source.on_end_animation;
    dest.randomize        = source.randomize;
    dest.loop             = source.loop;
    dest.rnd_start        = source.rnd_start;
    dest.frozen           = source.frozen;
    dest.movment          = source.movment;
    dest.total_dx         = source.total_dx;
    dest.total_dy         = source.total_dy;
    dest.child_asset_names_ = source.child_asset_names_;
    dest.audio_clip       = source.audio_clip;

    const std::size_t frame_count   = source.frame_cache_.size();
    const std::size_t variant_count = dest.variant_steps_.size();
    if (frame_count == 0 || variant_count == 0) {
        return false;
    }

    auto src_index_for = [&](std::size_t dst_idx) -> std::size_t {
        return opts.reverse_frames ? (frame_count - 1 - dst_idx) : dst_idx;
};

    SDL_RendererFlip flip_flags = SDL_FLIP_NONE;
    if (opts.flip_horizontal) flip_flags = static_cast<SDL_RendererFlip>(flip_flags | SDL_FLIP_HORIZONTAL);
    if (opts.flip_vertical)   flip_flags = static_cast<SDL_RendererFlip>(flip_flags | SDL_FLIP_VERTICAL);

    dest.frame_cache_.reserve(frame_count);
    for (std::size_t dst_idx = 0; dst_idx < frame_count; ++dst_idx) {
        const std::size_t src_idx = src_index_for(dst_idx);
        if (src_idx >= source.frame_cache_.size()) {
            continue;
        }
        const Animation::FrameCache& src_cache = source.frame_cache_[src_idx];
        Animation::FrameCache dst_cache;
        dst_cache.resize(variant_count);

        for (std::size_t v = 0; v < variant_count; ++v) {
            SDL_Texture* src_tex = (v < src_cache.textures.size()) ? src_cache.textures[v] : nullptr;
            int tex_w = (v < src_cache.widths.size()) ? src_cache.widths[v] : 0;
            int tex_h = (v < src_cache.heights.size()) ? src_cache.heights[v] : 0;
            dst_cache.textures[v] = clone_texture(src_tex, tex_w, tex_h, flip_flags, renderer, info, &tex_w, &tex_h);
            dst_cache.widths[v]   = tex_w;
            dst_cache.heights[v]  = tex_h;

            SDL_Texture* src_fg = (v < src_cache.foreground_textures.size()) ? src_cache.foreground_textures[v] : nullptr;
            if (src_fg) {
                dst_cache.foreground_textures[v] = clone_texture(src_fg, tex_w, tex_h, flip_flags, renderer, info);
            }
            SDL_Texture* src_bg = (v < src_cache.background_textures.size()) ? src_cache.background_textures[v] : nullptr;
            if (src_bg) {
                dst_cache.background_textures[v] = clone_texture(src_bg, tex_w, tex_h, flip_flags, renderer, info);
            }
            SDL_Texture* src_mask = (v < src_cache.mask_textures.size()) ? src_cache.mask_textures[v] : nullptr;

            int mask_w = (v < src_cache.mask_widths.size()) ? src_cache.mask_widths[v] : 0;
            int mask_h = (v < src_cache.mask_heights.size()) ? src_cache.mask_heights[v] : 0;
            dst_cache.mask_textures[v] = clone_texture(src_mask, mask_w, mask_h, flip_flags, renderer, info, &mask_w, &mask_h);
            dst_cache.mask_widths[v]   = mask_w;
            dst_cache.mask_heights[v]  = mask_h;
        }

        dest.frame_cache_.push_back(std::move(dst_cache));
    }

    dest.movement_paths_.clear();
    dest.frames.clear();
    dest.movement_paths_.resize(source.movement_paths_.size());

    for (std::size_t path_idx = 0; path_idx < dest.movement_paths_.size(); ++path_idx) {
        const auto& src_path = source.movement_paths_[path_idx];
        auto& dst_path       = dest.movement_paths_[path_idx];
        dst_path.resize(frame_count);

        for (std::size_t dst_idx = 0; dst_idx < frame_count; ++dst_idx) {
            const std::size_t src_idx = src_index_for(dst_idx);
            const AnimationFrame* src_frame = (src_idx < src_path.size()) ? &src_path[src_idx] : nullptr;
            AnimationFrame& dst_frame = dst_path[dst_idx];

            if (src_frame) {
                dst_frame.dx       = opts.flip_movement_horizontal ? -src_frame->dx : src_frame->dx;
                dst_frame.dy       = opts.flip_movement_vertical   ? -src_frame->dy : src_frame->dy;
                dst_frame.z_resort = src_frame->z_resort;
                dst_frame.rgb      = src_frame->rgb;
            } else {
                dst_frame.dx = dst_frame.dy = 0;
                dst_frame.z_resort = true;
                dst_frame.rgb = SDL_Color{255, 255, 255, 255};
            }

            dst_frame.frame_index = static_cast<int>(dst_idx);
            dst_frame.is_first    = (dst_idx == 0);
            dst_frame.is_last     = (dst_idx + 1 == frame_count);
            dst_frame.prev        = (dst_idx > 0) ? &dst_path[dst_idx - 1] : nullptr;
            dst_frame.next        = (dst_idx + 1 < frame_count) ? &dst_path[dst_idx + 1] : nullptr;

            dst_frame.variants.clear();
            dst_frame.variants.reserve(variant_count);

            const Animation::FrameCache& dst_cache = dest.frame_cache_[dst_idx];
            for (std::size_t v = 0; v < variant_count; ++v) {
                FrameVariant var;
                var.varient                     = static_cast<int>(v);
                var.base_texture                = (v < dst_cache.textures.size()) ? dst_cache.textures[v] : nullptr;
                var.foreground_texture          = (v < dst_cache.foreground_textures.size()) ? dst_cache.foreground_textures[v] : nullptr;
                var.background_texture          = (v < dst_cache.background_textures.size()) ? dst_cache.background_textures[v] : nullptr;
                var.shadow_mask_texture         = (v < dst_cache.mask_textures.size()) ? dst_cache.mask_textures[v] : nullptr;

                dst_frame.variants.push_back(var);
            }

            dst_frame.children.clear();
            if (src_frame) {
                copy_children(src_frame->children, dst_frame.children, opts);
                dst_frame.hit_geometry = src_frame->hit_geometry;
                dst_frame.attack_geometry = src_frame->attack_geometry;
            }

            if (path_idx == 0) {
                dest.frames.push_back(&dst_frame);
            }
        }
    }

    dest.total_dx = 0;
    dest.total_dy = 0;
    dest.movment = false;
    if (!dest.movement_paths_.empty()) {
        const auto& primary = dest.movement_paths_.front();
        for (const auto& f : primary) {
            dest.total_dx += f.dx;
            dest.total_dy += f.dy;
            if (f.dx != 0 || f.dy != 0) {
                dest.movment = true;
            }
        }
    }

    dest.number_of_frames = static_cast<int>(frame_count);
    dest.preview_texture = (!dest.frame_cache_.empty() && !dest.frame_cache_[0].textures.empty()) ? dest.frame_cache_[0].textures[0] : nullptr;

    return !dest.frame_cache_.empty();
}
