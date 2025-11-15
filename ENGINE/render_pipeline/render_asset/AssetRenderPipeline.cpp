#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"

#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "render/camera.hpp"
#include "render/global_light_source.hpp"
#include "render_pipeline/render_asset/IRenderStage.hpp"
#include "render_pipeline/render_asset/shading/RenderShadingStages.hpp"
#include "world/chunk.hpp"
#include "world/grid.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

float compute_asset_screen_height(Asset& asset, float inv_scale) {
    int cached_w = asset.cached_w;
    int cached_h = asset.cached_h;
    if ((cached_w <= 0 || cached_h <= 0)) {
        if (SDL_Texture* final = asset.get_final_texture()) {
            SDL_QueryTexture(final, nullptr, nullptr, &cached_w, &cached_h);
}

}
    if ((cached_w <= 0 || cached_h <= 0)) {
        if (SDL_Texture* frame = asset.get_current_frame()) {
            SDL_QueryTexture(frame, nullptr, nullptr, &cached_w, &cached_h);
        }
    }

    if (cached_w > 0) {
        asset.cached_w = cached_w;
    }
    if (cached_h > 0) {
        asset.cached_h = cached_h;
    }

    if (cached_h <= 0) {
        return 0.0f;
    }

    float scale = 1.0f;
    if (asset.info && std::isfinite(asset.info->scale_factor) && asset.info->scale_factor >= 0.0f) {
        scale = asset.info->scale_factor;
    }
    return static_cast<float>(cached_h) * scale * inv_scale;
}

}

Uint8 StageContext::main_light_alpha() const {
    return lighting ? lighting->main_light.get_current_color().a : 0;
}

Uint8 StageContext::main_light_brightness() const {
    return lighting ? static_cast<Uint8>(lighting->main_light.get_brightness()) : 0;
}

Global_Light_Source& StageContext::main_light() {
    return lighting->main_light;
}

const Global_Light_Source& StageContext::main_light() const {
    return lighting->main_light;
}

camera& StageContext::camera_view() {
    return lighting->camera_view;
}

const camera& StageContext::camera_view() const {
    return lighting->camera_view;
}

Asset* StageContext::player() const {
    return lighting ? lighting->player : nullptr;
}

void StageContext::update_projection(Asset& asset) {
    base_shadow_opacity     = 204.0f / 255.0f;
    screen_rect             = SDL_Rect{ 0, 0, 0, 0 };
    screen_center           = SDL_FPoint{ 0.0f, 0.0f };
    reference_screen_height = 1.0f;
    static_light_strength   = 1.0f;
    dynamic_light_strength  = 1.0f;
    blended_light_strength  = 1.0f;
    runtime_light_color     = SDL_Color{255, 255, 255, 255};
    has_runtime_light_color = false;

    if (!lighting || width <= 0 || height <= 0) {
        return;
    }

    camera& cam = lighting->camera_view;
    const float scale = cam.get_scale();
    const float inv_scale = (std::isfinite(scale) && scale > 1e-6f) ? (1.0f / scale) : 1.0f;

    float asset_scale = 1.0f;
    if (asset.info && std::isfinite(asset.info->scale_factor) && asset.info->scale_factor >= 0.0f) {
        asset_scale = asset.info->scale_factor;
    }

    const float scaled_fw = static_cast<float>(width) * asset_scale;
    const float scaled_fh = static_cast<float>(height) * asset_scale;
    if (scaled_fw <= 0.0f || scaled_fh <= 0.0f) {
        return;
    }

    const float base_sw = scaled_fw * inv_scale;
    const float base_sh = scaled_fh * inv_scale;

    float reference_height = 1.0f;
    if (Asset* player_asset = player()) {
        reference_height = compute_asset_screen_height(*player_asset, inv_scale);
    }
    if (!std::isfinite(reference_height) || reference_height <= 0.0f) {
        reference_height = 1.0f;
    }
    reference_screen_height = reference_height;

    const float world_x = asset.smoothed_translation_x();
    const float world_y = asset.smoothed_translation_y();
    const SDL_Point world_point{
        static_cast<int>(std::lround(world_x)),
        static_cast<int>(std::lround(world_y))
    };
    const camera::RenderEffects effects =
        cam.compute_render_effects(
            SDL_Point{ static_cast<int>(std::lround(world_x)), static_cast<int>(std::lround(world_y)) },
            base_sh,
            reference_height,
            reinterpret_cast<camera::RenderSmoothingKey>(&asset));

    world::Grid* grid = (lighting && lighting->world_grid) ? lighting->world_grid : nullptr;
    const float distance_scale  = (asset.info && asset.info->apply_distance_scaling) ? effects.distance_scale : 1.0f;
    const float vertical_scale  = (asset.info && asset.info->apply_vertical_scaling) ? effects.vertical_scale : 1.0f;

    const float scaled_sw       = base_sw * distance_scale;
    const float scaled_sh       = base_sh * distance_scale;
    const float final_visible_h = scaled_sh * vertical_scale;

    if (!std::isfinite(scaled_sw) || !std::isfinite(final_visible_h) || scaled_sw <= 0.0f || final_visible_h <= 0.0f) {
        return;
    }

    // Do not apply grid parallax to the player asset
    const bool is_player_asset = (&asset == player());
    const float center_x = (!grid || is_player_asset)
        ? effects.screen_position.x
        : grid->parallax_adjusted_screen_x(world_point, effects.screen_position.x);
    const float center_y = effects.screen_position.y;

    const float rect_w = std::max(scaled_sw, 1.0f);
    const float rect_h = std::max(final_visible_h, 1.0f);

    const float left_f = center_x - rect_w * 0.5f;
    const float top_f  = center_y - rect_h;

    screen_center = SDL_FPoint{ center_x, center_y - rect_h * 0.5f };

    const int sw = std::max(1, static_cast<int>(std::lround(rect_w)));
    const int sh = std::max(1, static_cast<int>(std::lround(rect_h)));
    const int left = static_cast<int>(std::lround(left_f));
    const int top  = static_cast<int>(std::lround(top_f));
    screen_rect    = SDL_Rect{ left, top, sw, sh };

    if (const LightMap* light_map_sampler = light_map()) {
        const LightMap::SampledBrightness sample =
            light_map_sampler->sample_lighting(asset.pos.x, asset.pos.y);
        static_light_strength  = sample.static_component;
        dynamic_light_strength = sample.dynamic_component;
        blended_light_strength = sample.blended;
        if (sample.has_color) {
            runtime_light_color     = sample.color;
            has_runtime_light_color = true;
        }
    }
}

AssetRenderPipeline::AssetRenderPipeline(SDL_Renderer* renderer, const SceneLighting& lighting)
: renderer_(renderer)
, lighting_(lighting)
, render_asset_(renderer) {
    using render_pipeline::shading::RenderAsset;
    using render_pipeline::shading::RenderShadowMask;

    stages_.push_back(StageEntry{ std::make_unique<RenderAsset>(), SDL_BLENDMODE_BLEND, false, false });
    stages_.push_back(StageEntry{ std::make_unique<RenderShadowMask>(), SDL_BLENDMODE_BLEND, true, false });
}

SDL_Texture* AssetRenderPipeline::run(Asset& asset) {
    if (!renderer_) {
        return nullptr;
    }

    SDL_Texture* base_frame = asset.get_current_frame();
    if (!base_frame) {
        return nullptr;
    }

    int    width       = asset.cached_w;
    int    height      = asset.cached_h;
    Uint32 base_format = SDL_PIXELFORMAT_UNKNOWN;

    if (width <= 0 || height <= 0) {
        SDL_QueryTexture(base_frame, nullptr, nullptr, &width, &height);
    }

    if (width <= 0 || height <= 0) {
        return nullptr;
    }

    StageContext context{};
    context.base_texture = base_frame;
    context.lighting     = &lighting_;
    context.width        = width;
    context.height       = height;
    context.reusable_final = asset.get_final_texture();
    if (renderer_) {
        SDL_GetRendererOutputSize(renderer_, &context.screen_width_px, &context.screen_height_px);
    }

    // Motion blur path removed: no previous-frame accumulation/blending.

    if (stages_.empty() || !stages_[0].stage) {
        return nullptr;
    }

    StageEntry& base_stage_entry = stages_[0];
    if (!base_stage_entry.stage->supports(asset)) {
        return nullptr;
    }

    SDL_Texture* final_texture = base_stage_entry.stage->run(renderer_, asset, context);
    if (!final_texture) {
        return nullptr;
    }

    context.final_texture     = final_texture;
    context.stage_destination = final_texture;
    context.stage_blend       = SDL_BLENDMODE_BLEND;
    context.stage_drew_to_destination = false;

    std::vector<SDL_Texture*> intermediates;
    intermediates.reserve(stages_.size());

    for (std::size_t i = 1; i < stages_.size(); ++i) {
        StageEntry& entry = stages_[i];
        if (!entry.stage || !entry.stage->supports(asset)) {
            continue;
        }
        if (low_quality_mode_ && entry.skip_in_low_quality) {
            continue;
        }
        context.final_texture     = final_texture;
        context.stage_destination = final_texture;
        context.stage_blend       = entry.blend;
        context.stage_drew_to_destination = false;
        SDL_Texture* stage_texture = entry.stage->run(renderer_, asset, context);
        if (context.stage_drew_to_destination) {
            continue;
        }
        if (!stage_texture || stage_texture == final_texture) {
            continue;
        }

        SDL_Texture* prev_target = SDL_GetRenderTarget(renderer_);
        SDL_SetRenderTarget(renderer_, final_texture);
        SDL_SetTextureBlendMode(stage_texture, entry.blend);
        SDL_RenderCopy(renderer_, stage_texture, nullptr, nullptr);
        SDL_SetRenderTarget(renderer_, prev_target);

        if (!entry.stage_manages_texture) {
            intermediates.push_back(stage_texture);
        }
    }

    // Motion blur blending removed.

    for (SDL_Texture* tex : intermediates) {
        SDL_DestroyTexture(tex);
    }

    asset.cached_w = context.width;
    asset.cached_h = context.height;

    return final_texture;
}

SDL_Texture* AssetRenderPipeline::regenerateFinalTexture(Asset* asset) {
    if (!asset) {
        return nullptr;
    }
    SDL_Texture* tex = run(*asset);
    return tex;
}

SDL_Texture* AssetRenderPipeline::texture_for_scale(Asset* asset,
                                                    SDL_Texture* base_tex,
                                                    int base_w,
                                                    int base_h,
                                                    int target_w,
                                                    int target_h,
                                                    float hysteresis_margin) {
    return render_asset_.texture_for_scale(asset, base_tex, base_w, base_h, target_w, target_h, hysteresis_margin);
}

void AssetRenderPipeline::set_low_quality_mode(bool enable) {
    low_quality_mode_ = enable;
}

SceneLighting& AssetRenderPipeline::lighting() {
    return lighting_;
}

const SceneLighting& AssetRenderPipeline::lighting() const {
    return lighting_;
}

void AssetRenderPipeline::set_player_asset(Asset* player) {
    lighting_.player = player;
}
