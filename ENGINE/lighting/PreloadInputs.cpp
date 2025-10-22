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
                     const SDL_Rect& rect,
                     SDL_BlendMode blend) {
    if (!texture || rect.w <= 0 || rect.h <= 0) {
        return;
    }
    PreloadInputs::TextureDraw draw{};
    draw.texture = texture;
    draw.src     = SDL_Rect{0, 0, rect.w, rect.h};
    draw.dst     = rect;
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
        try_append_draw(assets_draws_, texture, rect, SDL_BLENDMODE_BLEND);
    }
}

void PreloadInputs::stageStaticLights() {
    static_light_draws_.clear();
    if (!chunk_ || !assets_) {
        return;
    }

    const auto& static_lights = assets_->getActiveStaticLightAssets();
    for (Asset* asset : static_lights) {
        if (!asset) {
            continue;
        }
        SDL_Texture* texture = asset->get_final_texture();
        if (!texture) {
            texture = asset->get_current_frame();
        }
        SDL_Rect rect = build_local_rect(*chunk_, *asset);
        try_append_draw(static_light_draws_, texture, rect, SDL_BLENDMODE_ADD);
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

