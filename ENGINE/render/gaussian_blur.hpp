#pragma once

#include <SDL.h>
#include <vector>

class GaussianBlurHelper {
public:
    explicit GaussianBlurHelper(SDL_Renderer* renderer = nullptr);
    ~GaussianBlurHelper();

    void set_renderer(SDL_Renderer* renderer);

    SDL_Texture* apply(SDL_Texture* source,
                       int source_w,
                       int source_h,
                       float radius,
                       float mix);

private:
    bool ensure_resources(int width, int height);
    void destroy_textures();

    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* capture_tex_ = nullptr;   // render target copy of the source
    SDL_Texture* upload_tex_ = nullptr;    // streaming texture holding blurred result
    SDL_PixelFormat* pixel_format_ = nullptr;
    int tex_w_ = 0;
    int tex_h_ = 0;

    std::vector<uint32_t> pixel_buffer_;
    std::vector<uint32_t> output_pixels_;
    std::vector<float> channel_r_;
    std::vector<float> channel_g_;
    std::vector<float> channel_b_;
    std::vector<float> channel_a_;
    std::vector<float> original_r_;
    std::vector<float> original_g_;
    std::vector<float> original_b_;
    std::vector<float> original_a_;
    std::vector<float> temp_buffer_;
};
