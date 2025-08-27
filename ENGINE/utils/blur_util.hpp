// === File: blur_util.hpp ===
#pragma once
#include <SDL.h>
#include <functional>
#include <random>

class BlurUtil {
public:
    BlurUtil(SDL_Renderer* renderer,
             int downscale = 2,
             int blur_radius = 4,
             float weight_min = 0.8f,
             float weight_max = 1.2f);

    // Uniform blur
    SDL_Texture* blur_texture(SDL_Texture* source_tex,
                              int override_w = 0,
                              int override_h = 0,
                              int override_blur_radius = 0);

    // Random-weight blur
    SDL_Texture* blur_texture_random(SDL_Texture* source_tex,
                                     int override_w = 0,
                                     int override_h = 0,
                                     int override_blur_radius = 0);

private:
    SDL_Renderer* renderer_;
    int downscale_;
    int blur_radius_;
    float weight_min_;
    float weight_max_;

    // Shared core blur function
    SDL_Texture* blur_core(SDL_Texture* source_tex,
                           int override_w,
                           int override_h,
                           int override_blur_radius,
                           std::function<float(std::mt19937&)> weight_func);
};
