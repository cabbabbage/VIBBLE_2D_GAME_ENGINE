#include "lighting/PreloadInputs.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "utils/log.hpp"
#include "world/chunk.hpp"

#include <algorithm>

namespace lighting {
namespace {

SDL_Rect build_local_rect(const world::Chunk& chunk, const Asset& asset) {
    SDL_Rect rect{0, 0, 0, 0};
    rect.x = asset.pos.x - chunk.world_bounds.x;
    rect.y = asset.pos.y - chunk.world_bounds.y;
    rect.w = std::max(asset.cached_w, 0);
    rect.h = std::max(asset.cached_h, 0);
    return rect;
}

void try_append_draw(std::vector<PreloadInputs::TextureDraw>& list,
                     SDL_Texture* texture,
                     const SDL_Rect& src_rect,
                     const SDL_Rect& dst_rect,
                     SDL_BlendMode blend) {
    if (!texture || src_rect.w <= 0 || src_rect.h <= 0 || dst_rect.w <= 0 || dst_rect.h <= 0) {
        return;
    }
    PreloadInputs::TextureDraw draw{};
    draw.texture = texture;
    draw.src     = src_rect;
    draw.dst     = dst_rect;
    draw.blend   = blend;
    list.push_back(draw);
}

} // namespace

PreloadInputs::PreloadInputs(SDL_Renderer* renderer, Assets* assets, world::Chunk& chunk)
    : renderer_(renderer)
    , assets_(assets)
    , chunk_(&chunk) {
    resolveChunkBounds();
    captureBlendConfig();
    stageBackgrounds();
    stageTiles();
    stageAssets();
    stageStaticLights();
}

void PreloadInputs::resolveChunkBounds() {
    if (!chunk_) {
        chunk_bounds_ = SDL_Rect{0, 0, 0, 0};
        target_size_  = SDL_Point{1, 1};
        return;
    }

    chunk_bounds_ = chunk_->world_bounds;
    if (chunk_bounds_.w <= 0) {
        chunk_bounds_.w = 1;
    }
    if (chunk_bounds_.h <= 0) {
        chunk_bounds_.h = 1;
    }
    target_size_ = SDL_Point{chunk_bounds_.w, chunk_bounds_.h};

    if (!renderer_) {
        pixel_format_ = SDL_PIXELFORMAT_RGBA32;
        return;
    }

    SDL_RendererInfo info{};
    if (SDL_GetRendererInfo(renderer_, &info) == 0 && info.num_texture_formats > 0) {
        pixel_format_ = info.texture_formats[0];
    } else {
        pixel_format_ = SDL_PIXELFORMAT_RGBA32;
    }
}

void PreloadInputs::stageBackgrounds() {
    backgrounds_.clear();
}

void PreloadInputs::stageTiles() {
    tiles_.clear();
}

void PreloadInputs::stageAssets() {
    assets_draws_.clear();
    if (!chunk_) {
        return;
    }

    for (Asset* asset : chunk_->assets) {
        if (!asset) {
            continue;
        }
        SDL_Texture* texture = asset->get_final_texture();
        if (!texture) {
            texture = asset->get_current_frame();
        }
        SDL_Rect rect = build_local_rect(*chunk_, *asset);
        SDL_Rect src{0, 0, rect.w, rect.h};
        try_append_draw(assets_draws_, texture, src, rect, SDL_BLENDMODE_BLEND);
    }
}

void PreloadInputs::stageStaticLights() {
    static_light_draws_.clear();
    if (!chunk_ || !assets_) {
        return;
    }

    const auto& static_lights = assets_->getActiveStaticLightAssets();
    for (Asset* asset : static_lights) {
        if (!asset || !asset->info) {
            continue;
        }
        for (const auto& light : asset->info->light_sources) {
            SDL_Texture* texture = light.texture;
            if (!texture) {
                continue;
            }

            int src_w = light.cached_w > 0 ? light.cached_w : 0;
            int src_h = light.cached_h > 0 ? light.cached_h : 0;
            if (src_w <= 0 || src_h <= 0) {
                SDL_QueryTexture(texture, nullptr, nullptr, &src_w, &src_h);
            }
            if (src_w <= 0 || src_h <= 0) {
                continue;
            }

            const int draw_w = std::max(1, src_w);
            const int draw_h = std::max(1, src_h);

            SDL_Point world_center{asset->pos.x + light.offset_x, asset->pos.y + light.offset_y};
            SDL_Rect  world_dst{world_center.x - draw_w / 2,
                                world_center.y - draw_h / 2,
                                draw_w,
                                draw_h};

            SDL_Rect intersection{};
            if (!SDL_IntersectRect(&world_dst, &chunk_->world_bounds, &intersection)) {
                continue;
            }

            SDL_Rect src_rect{};
            src_rect.x = std::clamp(intersection.x - world_dst.x, 0, std::max(0, draw_w));
            src_rect.y = std::clamp(intersection.y - world_dst.y, 0, std::max(0, draw_h));
            src_rect.w = intersection.w;
            src_rect.h = intersection.h;

            if (src_rect.x >= draw_w || src_rect.y >= draw_h) {
                continue;
            }

            src_rect.w = std::min(src_rect.w, draw_w - src_rect.x);
            src_rect.h = std::min(src_rect.h, draw_h - src_rect.y);
            if (src_rect.w <= 0 || src_rect.h <= 0) {
                continue;
            }

            SDL_Rect dst_rect{};
            dst_rect.x = std::max(0, intersection.x - chunk_->world_bounds.x);
            dst_rect.y = std::max(0, intersection.y - chunk_->world_bounds.y);
            dst_rect.w = src_rect.w;
            dst_rect.h = src_rect.h;

            try_append_draw(static_light_draws_, texture, src_rect, dst_rect, runtimeLightBlendMode());
        }
    }
}

void PreloadInputs::captureBlendConfig() {
    if (!renderer_) {
        light_blend_mode_ = SDL_BLENDMODE_BLEND;
        return;
    }

#if SDL_VERSION_ATLEAST(2, 0, 6)
    light_blend_mode_ = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO,
                                                   SDL_BLENDFACTOR_SRC_COLOR,
                                                   SDL_BLENDOPERATION_ADD,
                                                   SDL_BLENDFACTOR_ZERO,
                                                   SDL_BLENDFACTOR_ONE,
                                                   SDL_BLENDOPERATION_ADD);
#else
    light_blend_mode_ = SDL_BLENDMODE_ADD;
#endif
}

void PreloadInputs::disableScreenLightAndMovingLights() {
    if (!chunk_ || backup_.valid) {
        return;
    }

    backup_.brightness_strength   = chunk_->brightness_strength;
    backup_.opacity_strength      = chunk_->opacity_strength;
    backup_.scale_strength        = chunk_->scale_strength;
    backup_.offset_x              = chunk_->offset_x;
    backup_.offset_y              = chunk_->offset_y;
    backup_.has_dynamic_overlay   = chunk_->has_dynamic_overlay;
    backup_.lighting_active       = chunk_->lighting.is_active;
    backup_.moving_light_occupied = chunk_->lighting.is_occupied_by_moving_source;
    backup_.current_strength      = chunk_->lighting.current_strength;
    backup_.valid                 = true;

    chunk_->brightness_strength              = 0.0f;
    chunk_->opacity_strength                 = 0.0f;
    chunk_->scale_strength                   = 1.0f;
    chunk_->offset_x                         = 0;
    chunk_->offset_y                         = 0;
    chunk_->has_dynamic_overlay              = false;
    chunk_->lighting.is_active               = false;
    chunk_->lighting.is_occupied_by_moving_source = false;
    chunk_->lighting.current_strength        = 0.0f;
}

void PreloadInputs::restoreRuntimeLighting() {
    if (!chunk_ || !backup_.valid) {
        return;
    }

    chunk_->brightness_strength              = backup_.brightness_strength;
    chunk_->opacity_strength                 = backup_.opacity_strength;
    chunk_->scale_strength                   = backup_.scale_strength;
    chunk_->offset_x                         = backup_.offset_x;
    chunk_->offset_y                         = backup_.offset_y;
    chunk_->has_dynamic_overlay              = backup_.has_dynamic_overlay;
    chunk_->lighting.is_active               = backup_.lighting_active;
    chunk_->lighting.is_occupied_by_moving_source = backup_.moving_light_occupied;
    chunk_->lighting.current_strength        = backup_.current_strength;

    backup_.valid = false;
}

} // namespace lighting

