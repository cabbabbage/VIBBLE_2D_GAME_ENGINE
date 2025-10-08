#pragma once
#include <SDL.h>
#include <vector>
#include <cstdint>

enum class BrightnessMetric {
    Luma709,
    MaxRGB,
    AvgRGB,
    EnergyRGB,
};

struct LightRaysParams {
    // Bright mask options
    bool  use_alpha_in_mask = true;
    BrightnessMetric metric = BrightnessMetric::MaxRGB;
    float gamma_comp = 1.0f;   // >1 brightens after compensation

    // Bright-pass
    float min_luma_threshold = 0.90f;  // absolute floor in [0..1]
    float bright_percentile  = 0.995f; // keep top 0.5 percent

    // Ray march
    int   samples  = 64;
    float density  = 0.9f;   // fraction of vector per step across all samples
    float decay    = 0.97f;  // falloff per sample
    float weight   = 0.75f;  // per-sample contribution
    float exposure = 0.9f;   // final scale

    // Resolution control
    int   downsample_log2 = 2; // 2 => render at 1/4 size
};

class LightRaysPass {
public:
    LightRaysPass(SDL_Renderer* r, int screen_w, int screen_h);
    ~LightRaysPass();

    void set_screen_size(int screen_w, int screen_h);
    void set_params(const LightRaysParams& p);
    void set_light_screen_pos(SDL_Point p); // screen pixels
    void set_enabled(bool v);

    // Returns low-res texture with white rays and alpha. Blend with ADD.
    // Returns nullptr if disabled or on failure.
    SDL_Texture* compute(SDL_Texture* source_render_target);

private:
    SDL_Renderer* renderer_ = nullptr;
    LightRaysParams params_{};
    SDL_Point light_pos_{0, 0};
    bool enabled_ = true;

    int screen_w_ = 0;
    int screen_h_ = 0;

    SDL_Texture* rays_tex_lowres_ = nullptr;
    int lr_w_ = 0, lr_h_ = 0;

    void destroy_textures_();
    bool ensure_lowres_target_();

    static inline uint8_t clamp_u8_(float v) {
        if (v <= 0.f) return 0;
        if (v >= 255.f) return 255;
        return static_cast<uint8_t>(v + 0.5f);
    }
};
