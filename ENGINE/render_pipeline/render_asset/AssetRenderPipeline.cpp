#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"

#include "render_pipeline/render_asset/IRenderStage.hpp"

AssetRenderPipeline::AssetRenderPipeline(SDL_Renderer* renderer, const SceneLighting& lighting)
: renderer_(renderer)
, lighting_(lighting)
, render_asset_(renderer, lighting.camera, lighting.main_light, lighting.player) {}

SDL_Texture* AssetRenderPipeline::regenerateFinalTexture(Asset* asset) {
    return render_asset_.regenerateFinalTexture(asset);
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
    render_asset_.set_player_asset(player);
}

