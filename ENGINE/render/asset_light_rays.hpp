#pragma once

#include <SDL.h>
#include <memory>

class Asset;
class GaussianBlurHelper;

class AssetLightRaysRenderer {
public:
    explicit AssetLightRaysRenderer(SDL_Renderer* renderer = nullptr);

    void set_renderer(SDL_Renderer* renderer);
    void set_enabled(bool enabled);
    void set_blur_settings(float radius, float mix);

    void render_before_asset(Asset* asset,
                             const SDL_Rect& asset_screen_rect,
                             int base_width,
                             int base_height,
                             SDL_RendererFlip flip_mode);

private:
    SDL_Renderer* renderer_ = nullptr;
    bool enabled_ = false;
    float blur_radius_ = 0.f;
    float blur_mix_ = 0.f;
    std::unique_ptr<GaussianBlurHelper> blur_helper_;
};
