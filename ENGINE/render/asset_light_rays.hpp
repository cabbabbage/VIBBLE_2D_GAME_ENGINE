#pragma once

#include <SDL.h>

class Asset;
class LightRaysPass;

class AssetLightRaysRenderer {
public:
    AssetLightRaysRenderer(SDL_Renderer* renderer, LightRaysPass* pass = nullptr);

    void set_light_rays_pass(LightRaysPass* pass);

    void render_before_asset(Asset* asset,
                             const SDL_Rect& asset_screen_rect,
                             int base_width,
                             int base_height,
                             SDL_RendererFlip flip_mode);

private:
    SDL_Renderer* renderer_ = nullptr;
    LightRaysPass* light_rays_pass_ = nullptr;
};
