#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"

#include "asset/Asset.hpp"
#include "render/global_light_source.hpp"
#include "render_pipeline/render_asset/IRenderStage.hpp"
#include "render_pipeline/render_asset/shading/RenderShadingStages.hpp"
#include "world/chunk.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float MOTION_BLUR_STRENGTH = 0.45f;

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

}  // namespace

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

const render_pipeline::shading::ReactiveShadowSettings* StageContext::reactive_shadow_settings() const {
    if (reactive_shadow_settings_override) {
        return reactive_shadow_settings_override;
    }
    return (lighting ? lighting->reactive_shadow_settings : nullptr);
}

void StageContext::update_projection(Asset& asset) {
    base_shadow_scale       = 1.0f;
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

    const camera::RenderEffects effects =
        cam.compute_render_effects(SDL_Point{ asset.pos.x, asset.pos.y }, base_sh, reference_height);

    const float scaled_sw = base_sw * effects.distance_scale;
    const float scaled_sh = base_sh * effects.distance_scale;
    const float final_visible_h = scaled_sh * effects.vertical_scale;

    if (!std::isfinite(scaled_sw) || !std::isfinite(final_visible_h) || scaled_sw <= 0.0f || final_visible_h <= 0.0f) {
        return;
    }

    const int sw = std::max(1, static_cast<int>(std::lround(scaled_sw)));
    const int sh = std::max(1, static_cast<int>(std::lround(final_visible_h)));
    const SDL_Point cp = effects.screen_position;
    screen_rect   = SDL_Rect{ cp.x - sw / 2, cp.y - sh, sw, sh };
    screen_center = SDL_FPoint{ static_cast<float>(cp.x), static_cast<float>(cp.y - sh / 2) };

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
    using render_pipeline::shading::RenderCastShadow;
    using render_pipeline::shading::RenderShadowMask;

    stages_.push_back(StageEntry{ std::make_unique<RenderAsset>(), SDL_BLENDMODE_BLEND, false, false });
    stages_.push_back(StageEntry{ std::make_unique<RenderCastShadow>(), SDL_BLENDMODE_BLEND, false, true });
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

    SDL_Texture* previous_final       = asset.get_final_texture();
    SDL_Texture* previous_final_copy  = nullptr;
    const float  clamped_blur_strength = std::clamp(MOTION_BLUR_STRENGTH, 0.0f, 1.0f);
    const bool   apply_motion_blur     = previous_final && clamped_blur_strength > 0.0f && !low_quality_mode_;
    if (apply_motion_blur) {
        int    prev_w      = 0;
        int    prev_h      = 0;
        Uint32 prev_format = SDL_PIXELFORMAT_UNKNOWN;
        if (SDL_QueryTexture(previous_final, &prev_format, nullptr, &prev_w, &prev_h) == 0 && prev_w == width && prev_h == height) {
            if (prev_format == SDL_PIXELFORMAT_UNKNOWN) {
                if (base_format == SDL_PIXELFORMAT_UNKNOWN) {
                    SDL_QueryTexture(base_frame, &base_format, nullptr, nullptr, nullptr);
                }
                prev_format = (base_format != SDL_PIXELFORMAT_UNKNOWN) ? base_format : SDL_PIXELFORMAT_RGBA8888;
            }

            Asset::RenderTextureCache& cache = asset.motion_blur_cache();
            if (cache.texture) {
                int tex_w = 0;
                int tex_h = 0;
                if (cache.width != prev_w || cache.height != prev_h) {
                    if (SDL_QueryTexture(cache.texture, nullptr, nullptr, &tex_w, &tex_h) != 0 || tex_w != prev_w || tex_h != prev_h) {
                        SDL_DestroyTexture(cache.texture);
                        cache.texture = nullptr;
                        cache.width   = 0;
                        cache.height  = 0;
                    } else {
                        cache.width  = tex_w;
                        cache.height = tex_h;
                    }
                }
            }

            if (!cache.texture) {
                cache.texture = SDL_CreateTexture(renderer_, prev_format, SDL_TEXTUREACCESS_TARGET, prev_w, prev_h);
                if (cache.texture) {
                    SDL_SetTextureBlendMode(cache.texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
                    SDL_SetTextureScaleMode(cache.texture, SDL_ScaleModeBest);
#endif
                    cache.width  = prev_w;
                    cache.height = prev_h;
                }
            }

            if (cache.texture) {
                SDL_Texture* prev_target = SDL_GetRenderTarget(renderer_);
                SDL_SetRenderTarget(renderer_, cache.texture);
                SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
                SDL_RenderClear(renderer_);
                SDL_RenderCopy(renderer_, previous_final, nullptr, nullptr);
                SDL_SetRenderTarget(renderer_, prev_target);
                previous_final_copy = cache.texture;
            }
        }
    }

    StageContext context{};
    context.base_texture = base_frame;
    context.lighting     = &lighting_;
    context.width        = width;
    context.height       = height;
    context.reusable_final = asset.get_final_texture();
    context.reactive_shadow_settings_override = low_quality_mode_ ? nullptr : lighting_.reactive_shadow_settings;
    if (renderer_) {
        SDL_GetRendererOutputSize(renderer_, &context.screen_width_px, &context.screen_height_px);
    }
    context.update_projection(asset);

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

    context.final_texture = final_texture;

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
        context.final_texture = final_texture;
        SDL_Texture* stage_texture = entry.stage->run(renderer_, asset, context);
        if (!stage_texture) {
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

    if (previous_final_copy && final_texture) {
        SDL_Texture* prev_target = SDL_GetRenderTarget(renderer_);
        SDL_SetRenderTarget(renderer_, final_texture);
        SDL_SetTextureBlendMode(previous_final_copy, SDL_BLENDMODE_BLEND);
        const Uint8 blur_alpha = static_cast<Uint8>(std::lround(clamped_blur_strength * 255.0f));
        SDL_SetTextureAlphaMod(previous_final_copy, blur_alpha);
        SDL_RenderCopy(renderer_, previous_final_copy, nullptr, nullptr);
        SDL_SetTextureAlphaMod(previous_final_copy, 255);
        SDL_SetRenderTarget(renderer_, prev_target);
    }

    for (SDL_Texture* tex : intermediates) {
        SDL_DestroyTexture(tex);
    }

    asset.cached_w = context.width;
    asset.cached_h = context.height;

    return final_texture;
}

SDL_Texture* AssetRenderPipeline::regenerateFinalTexture(Asset* asset) {
    return asset ? run(*asset) : nullptr;
}

SDL_Texture* AssetRenderPipeline::texture_for_scale(Asset* asset,
                                                    SDL_Texture* base_tex,
                                                    int base_w,
                                                    int base_h,
                                                    int target_w,
                                                    int target_h) {
    return render_asset_.texture_for_scale(asset, base_tex, base_w, base_h, target_w, target_h);
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

