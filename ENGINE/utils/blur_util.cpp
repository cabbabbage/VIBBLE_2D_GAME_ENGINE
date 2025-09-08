#include "blur_util.hpp"
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <random>
BlurUtil::BlurUtil(SDL_Renderer* renderer,
                   int downscale,
                   int blur_radius,
                   float weight_min,
                   float weight_max)
    : renderer_(renderer),
      downscale_(downscale),
      blur_radius_(blur_radius),
      weight_min_(weight_min),
      weight_max_(weight_max)
{}

SDL_Texture* BlurUtil::blur_core(SDL_Texture* source_tex,
                                 int override_w,
                                 int override_h,
                                 int override_blur_radius,
                                 std::function<float(std::mt19937&)> weight_func)
{
    if (!source_tex) throw std::runtime_error("blur_core: source_tex is null");
    int tex_w = 0, tex_h = 0;
    SDL_QueryTexture(source_tex, nullptr, nullptr, &tex_w, &tex_h);
    int w = (override_w > 0) ? override_w : tex_w;
    int h = (override_h > 0) ? override_h : tex_h;
    int radius = (override_blur_radius > 0) ? override_blur_radius : blur_radius_;
    int small_w = std::max(1, w / downscale_);
    int small_h = std::max(1, h / downscale_);
    SDL_Texture* downscaled = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                                SDL_TEXTUREACCESS_TARGET, small_w, small_h);
    SDL_SetTextureBlendMode(downscaled, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(renderer_, downscaled);
    SDL_RenderCopy(renderer_, source_tex, nullptr, nullptr);
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, small_w, small_h, 32, SDL_PIXELFORMAT_RGBA8888);
    if (!surf) throw std::runtime_error("blur_core: failed to create surface");
    if (SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_RGBA8888,
                             surf->pixels, surf->pitch) != 0)
    {
        SDL_FreeSurface(surf);
        SDL_DestroyTexture(downscaled);
        throw std::runtime_error("blur_core: SDL_RenderReadPixels failed");
    }
    Uint32* pixels = static_cast<Uint32*>(surf->pixels);
    std::vector<Uint32> temp(pixels, pixels + small_w * small_h);
    std::mt19937 rng{ std::random_device{}() };
    for (int y = 0; y < small_h; ++y) {
        for (int x = 0; x < small_w; ++x) {
            float r = 0, g = 0, b = 0, a = 0, total_weight = 0;
            for (int k = -radius; k <= radius; ++k) {
                int nx = std::clamp(x + k, 0, small_w - 1);
                Uint8 pr, pg, pb, pa;
                SDL_GetRGBA(temp[y * small_w + nx], surf->format, &pr, &pg, &pb, &pa);
                float weight = weight_func(rng);
                r += pr * weight; g += pg * weight; b += pb * weight; a += pa * weight;
                total_weight += weight;
            }
            pixels[y * small_w + x] = SDL_MapRGBA(surf->format,
                                                  static_cast<Uint8>(r / total_weight),
                                                  static_cast<Uint8>(g / total_weight),
                                                  static_cast<Uint8>(b / total_weight),
                                                  static_cast<Uint8>(a / total_weight));
        }
    }
    temp.assign(pixels, pixels + small_w * small_h);
    for (int y = 0; y < small_h; ++y) {
        for (int x = 0; x < small_w; ++x) {
            float r = 0, g = 0, b = 0, a = 0, total_weight = 0;
            for (int k = -radius; k <= radius; ++k) {
                int ny = std::clamp(y + k, 0, small_h - 1);
                Uint8 pr, pg, pb, pa;
                SDL_GetRGBA(temp[ny * small_w + x], surf->format, &pr, &pg, &pb, &pa);
                float weight = weight_func(rng);
                r += pr * weight; g += pg * weight; b += pb * weight; a += pa * weight;
                total_weight += weight;
            }
            pixels[y * small_w + x] = SDL_MapRGBA(surf->format,
                                                  static_cast<Uint8>(r / total_weight),
                                                  static_cast<Uint8>(g / total_weight),
                                                  static_cast<Uint8>(b / total_weight),
                                                  static_cast<Uint8>(a / total_weight));
        }
    }
    SDL_Texture* blurred_small = SDL_CreateTextureFromSurface(renderer_, surf);
    SDL_SetTextureBlendMode(blurred_small, SDL_BLENDMODE_MOD);
    SDL_FreeSurface(surf);
    SDL_DestroyTexture(downscaled);
    SDL_Texture* blurred_full = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                                  SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_SetTextureBlendMode(blurred_full, SDL_BLENDMODE_MOD);
    SDL_SetRenderTarget(renderer_, blurred_full);
    SDL_RenderCopy(renderer_, blurred_small, nullptr, nullptr);
    SDL_DestroyTexture(blurred_small);
    SDL_SetRenderTarget(renderer_, nullptr);
    return blurred_full;
}

SDL_Texture* BlurUtil::blur_texture(SDL_Texture* source_tex,
                                    int override_w,
                                    int override_h,
                                    int override_blur_radius)
{
    return blur_core(source_tex, override_w, override_h, override_blur_radius,
                     [](std::mt19937&) { return 1.0f; });
}

SDL_Texture* BlurUtil::blur_texture_random(SDL_Texture* source_tex,
                                           int override_w,
                                           int override_h,
                                           int override_blur_radius)
{
    return blur_core(source_tex, override_w, override_h, override_blur_radius,
                     [this](std::mt19937& rng) {
                         std::uniform_real_distribution<float> dist(weight_min_, weight_max_);
                         return dist(rng);
                     });
}
