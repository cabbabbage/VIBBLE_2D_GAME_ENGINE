#pragma once
#include <SDL.h>
#include "light_rays.hpp" // reuse LightRaysParams + BrightnessMetric

class Asset;
struct LightSource; // from utils/light_source.hpp

class AssetLightRaysRenderer {
public:
    explicit AssetLightRaysRenderer(SDL_Renderer* renderer);

    void set_renderer(SDL_Renderer* renderer);
    void set_enabled(bool enabled);
    void set_blur_settings(float radius, float mix);
    void set_params(const LightRaysParams& p) { params_ = p; }

    void render_before_asset(Asset* asset,
                             const SDL_Rect& asset_screen_rect,
                             int base_width,
                             int base_height,
                             SDL_RendererFlip flip_mode);

private:
    SDL_Renderer* renderer_ = nullptr;
    bool enabled_ = true;

    // optional post-blur on the rays texture
    float blur_radius_ = 0.f;
    float blur_mix_ = 0.f;

    LightRaysParams params_{}; // same knobs as full-screen pass
    static inline uint8_t clamp_u8_(float v) {
        if (v <= 0.f) return 0;
        if (v >= 255.f) return 255;
        return static_cast<uint8_t>(v + 0.5f);
    }
};
