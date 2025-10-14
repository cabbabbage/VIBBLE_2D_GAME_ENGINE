#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"

#include "asset/Asset.hpp"
#include "render/global_light_source.hpp"
#include "render_pipeline/render_asset/IRenderStage.hpp"
#include "render_pipeline/render_asset/lighting/RenderLightBehind.hpp"
#include "render_pipeline/render_asset/lighting/RenderLightFront.hpp"
#include "render_pipeline/render_asset/shading/RenderShadingStages.hpp"

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

AssetRenderPipeline::AssetRenderPipeline(SDL_Renderer* renderer, const SceneLighting& lighting)
: renderer_(renderer)
, lighting_(lighting)
, render_asset_(renderer) {
    using render_pipeline::lighting::RenderLightBehind;
    using render_pipeline::lighting::RenderLightFront;
    using render_pipeline::shading::RenderAsset;
    using render_pipeline::shading::RenderCastShadow;
    using render_pipeline::shading::RenderShadowMask;

    stages_.push_back(StageEntry{ std::make_unique<RenderAsset>(), SDL_BLENDMODE_BLEND, false });
    stages_.push_back(StageEntry{ std::make_unique<RenderLightBehind>(), SDL_BLENDMODE_ADD, true });
    stages_.push_back(StageEntry{ std::make_unique<RenderLightFront>(), SDL_BLENDMODE_ADD, true });
    stages_.push_back(StageEntry{ std::make_unique<RenderCastShadow>(), SDL_BLENDMODE_BLEND, false });
    stages_.push_back(StageEntry{ std::make_unique<RenderShadowMask>(), SDL_BLENDMODE_MOD, true });
}

SDL_Texture* AssetRenderPipeline::run(Asset& asset) {
    if (!renderer_) {
        return nullptr;
    }

    SDL_Texture* base_frame = asset.get_current_frame();
    if (!base_frame) {
        return nullptr;
    }

    int width = asset.cached_w;
    int height = asset.cached_h;
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

SceneLighting& AssetRenderPipeline::lighting() {
    return lighting_;
}

const SceneLighting& AssetRenderPipeline::lighting() const {
    return lighting_;
}

void AssetRenderPipeline::set_player_asset(Asset* player) {
    lighting_.player = player;
}

