#pragma once
#include <SDL.h>
#include <vector>
#include <array>
#include <cstdint>

struct LightRaysParams {
    // Bright-pass
    float min_luma_threshold = 0.60f;  // absolute floor in [0..1]
    float bright_percentile  = 0.9f; // keep top 0.5 percent

    // Ray march
    int   samples  = 94;
    float density  = 0.9f;   // fraction of vector per step across all samples
    float decay    = 0.5f;  // falloff per sample
    float weight   = 0.9f;  // per-sample contribution
    float exposure = 8.9f;   // final scale

    // Resolution control
    int   downsample_log2 = 2; // 2 => render at 1/4 size

    // Final blur (applied inside the pass)
    float final_blur_radius = 2.0f;  // gaussian radius in pixels (low-res space)
    float final_blur_mix    = 0.85f; // 0 => original, 1 => fully blurred
};

class LightRaysPass {
public:
    LightRaysPass(SDL_Renderer* r, int screen_w, int screen_h);
    ~LightRaysPass();

    void set_screen_size(int screen_w, int screen_h);
    void set_params(const LightRaysParams& p);
    void set_light_screen_pos(SDL_Point p); // screen pixels (manual override)
    void clear_light_override();            // re-enable automatic detection
    void set_enabled(bool v);

    // Returns low-res texture with white rays and alpha. Blend with ADD.
    // Returns nullptr if disabled or on failure.
    SDL_Texture* compute(SDL_Texture* source_render_target);

private:
    SDL_Renderer* renderer_ = nullptr;
    LightRaysParams params_{};
    SDL_Point manual_light_pos_{0, 0};
    bool enabled_ = true;
    bool manual_light_override_ = false;

    int screen_w_ = 0;
    int screen_h_ = 0;

    SDL_Texture* rays_tex_lowres_ = nullptr;
    SDL_Texture* capture_tex_lowres_ = nullptr;
    SDL_PixelFormat* rays_pixel_format_ = nullptr;
    int lr_w_ = 0, lr_h_ = 0;

    std::vector<uint32_t> capture_pixels_;
    std::vector<float>    luma_buffer_;
    std::vector<float>    bright_buffer_;
    std::vector<float>    ray_intensity_buffer_;
    std::vector<float>    ray_intensity_original_;
    std::vector<float>    blur_work_buffer_;
    std::vector<uint8_t>  alpha_buffer_;
    std::array<int, 256>  histogram_{};

    SDL_FPoint detected_light_lowres_{0.f, 0.f};
    SDL_FPoint dominant_axis_{1.f, 0.f};
    bool has_detected_light_ = false;

    void destroy_textures_();
    bool ensure_lowres_target_();
    void ensure_buffer_capacity_(int pixel_count);
    void analyze_brightness_distribution_(int dw, int dh);

    static inline uint8_t clamp_u8_(float v) {
        if (v <= 0.f) return 0;
        if (v >= 255.f) return 255;
        return static_cast<uint8_t>(v + 0.5f);
    }
};
