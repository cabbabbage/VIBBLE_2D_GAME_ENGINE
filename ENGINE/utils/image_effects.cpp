#include "utils/image_effects.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {

inline float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

struct HSV {
    float h = 0.0f;
    float s = 0.0f;
    float v = 0.0f;
};

HSV rgb_to_hsv(float r, float g, float b) {
    HSV out{};
    const float maxc = std::max({r, g, b});
    const float minc = std::min({r, g, b});
    out.v = maxc;
    const float delta = maxc - minc;
    out.s = (maxc <= 1e-6f) ? 0.0f : (delta / maxc);
    if (delta <= 1e-6f) {
        out.h = 0.0f;
        return out;
    }
    if (maxc == r) {
        out.h = 60.0f * std::fmod(((g - b) / delta), 6.0f);
    } else if (maxc == g) {
        out.h = 60.0f * (((b - r) / delta) + 2.0f);
    } else {
        out.h = 60.0f * (((r - g) / delta) + 4.0f);
    }
    if (out.h < 0.0f) {
        out.h += 360.0f;
    }
    return out;
}

void hsv_to_rgb(float h, float s, float v, float& r_out, float& g_out, float& b_out) {
    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    const float m = v - c;

    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (h < 60.0f) {
        r = c; g = x; b = 0.0f;
    } else if (h < 120.0f) {
        r = x; g = c; b = 0.0f;
    } else if (h < 180.0f) {
        r = 0.0f; g = c; b = x;
    } else if (h < 240.0f) {
        r = 0.0f; g = x; b = c;
    } else if (h < 300.0f) {
        r = x; g = 0.0f; b = c;
    } else {
        r = c; g = 0.0f; b = x;
    }
    r_out = clamp01(r + m);
    g_out = clamp01(g + m);
    b_out = clamp01(b + m);
}

void blur_channel(std::vector<float>& channel, int width, int height, int radius) {
    if (radius <= 0) {
        return;
    }
    const int window = radius * 2 + 1;
    std::vector<float> temp(channel.size(), 0.0f);

    auto sample = [&](const std::vector<float>& data, int x, int y) -> float {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return data[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                sum += sample(channel, x + k, y);
            }
            temp[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] = sum / static_cast<float>(window);
        }
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                sum += sample(temp, x, y + k);
            }
            channel[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] = sum / static_cast<float>(window);
        }
    }
}

bool apply_color_pipeline(SDL_Surface* surface, const camera_effects::ImageEffectSettings& settings) {
    if (!surface || surface->w <= 0 || surface->h <= 0) {
        return false;
    }

    camera_effects::ImageEffectSettings clamped = settings;
    camera_effects::ClampImageEffectSettings(clamped);
    if (camera_effects::ImageEffectSettingsIsIdentity(clamped)) {
        return true;
    }

    if (SDL_LockSurface(surface) != 0) {
        return false;
    }

    const int width  = surface->w;
    const int height = surface->h;
    const int stride = surface->pitch / 4;
    SDL_PixelFormat* fmt = surface->format;
    auto* pixels = static_cast<std::uint32_t*>(surface->pixels);
    const std::size_t total = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    std::vector<float> base_r(total), base_g(total), base_b(total), base_a(total);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
            Uint8 r, g, b, a;
            SDL_GetRGBA(pixels[y * stride + x], fmt, &r, &g, &b, &a);
            base_r[idx] = static_cast<float>(r) / 255.0f;
            base_g[idx] = static_cast<float>(g) / 255.0f;
            base_b[idx] = static_cast<float>(b) / 255.0f;
            base_a[idx] = static_cast<float>(a) / 255.0f;
        }
    }

    std::vector<float> work_r = base_r;
    std::vector<float> work_g = base_g;
    std::vector<float> work_b = base_b;
    std::vector<float> work_a = base_a;

    if (std::fabs(clamped.blur) > 1e-4f) {
        const float blur_strength = std::clamp(std::fabs(clamped.blur), 0.0f, 1.0f);
        const int radius = std::max(1, static_cast<int>(std::round(blur_strength * 12.0f)));
        std::vector<float> blurred_r = work_r;
        std::vector<float> blurred_g = work_g;
        std::vector<float> blurred_b = work_b;
        std::vector<float> blurred_a = work_a;
        blur_channel(blurred_r, width, height, radius);
        blur_channel(blurred_g, width, height, radius);
        blur_channel(blurred_b, width, height, radius);
        blur_channel(blurred_a, width, height, radius);
        if (clamped.blur > 0.0f) {
            const float mix_amount = blur_strength;
            const float inv_amount = 1.0f - mix_amount;
            for (std::size_t i = 0; i < total; ++i) {
                work_r[i] = base_r[i] * inv_amount + blurred_r[i] * mix_amount;
                work_g[i] = base_g[i] * inv_amount + blurred_g[i] * mix_amount;
                work_b[i] = base_b[i] * inv_amount + blurred_b[i] * mix_amount;
                work_a[i] = base_a[i] * inv_amount + blurred_a[i] * mix_amount;
            }
        } else {
            const float sharpen_amount = blur_strength;
            for (std::size_t i = 0; i < total; ++i) {
                const float delta_r = base_r[i] - blurred_r[i];
                const float delta_g = base_g[i] - blurred_g[i];
                const float delta_b = base_b[i] - blurred_b[i];
                work_r[i] = clamp01(base_r[i] + sharpen_amount * delta_r);
                work_g[i] = clamp01(base_g[i] + sharpen_amount * delta_g);
                work_b[i] = clamp01(base_b[i] + sharpen_amount * delta_b);
            }
        }
    }

    const bool has_color_ops =
        std::fabs(clamped.brightness) > 1e-4f ||
        std::fabs(clamped.contrast) > 1e-4f ||
        std::fabs(clamped.saturation_red) > 1e-4f ||
        std::fabs(clamped.saturation_green) > 1e-4f ||
        std::fabs(clamped.saturation_blue) > 1e-4f ||
        std::fabs(clamped.hue) > 1e-4f ||
        std::fabs(clamped.rgb_boost) > 1e-4f;
    if (has_color_ops) {
        const float brightness = clamped.brightness;
        const float contrast   = clamped.contrast;
        const float contrast_factor = 1.0f + contrast;
        const float rgb_mult = 1.0f + clamped.rgb_boost;
        const bool has_sat_channels =
            std::fabs(clamped.saturation_red) > 1e-4f ||
            std::fabs(clamped.saturation_green) > 1e-4f ||
            std::fabs(clamped.saturation_blue) > 1e-4f;
        for (std::size_t i = 0; i < total; ++i) {
            float r = work_r[i];
            float g = work_g[i];
            float b = work_b[i];

            r = clamp01(r + brightness);
            g = clamp01(g + brightness);
            b = clamp01(b + brightness);

            r = clamp01((r - 0.5f) * contrast_factor + 0.5f);
            g = clamp01((g - 0.5f) * contrast_factor + 0.5f);
            b = clamp01((b - 0.5f) * contrast_factor + 0.5f);

            HSV hsv = rgb_to_hsv(r, g, b);
            if (has_sat_channels) {
                const float total_weight = std::max(r + g + b, 1e-5f);
                const float wr = r / total_weight;
                const float wg = g / total_weight;
                const float wb = b / total_weight;
                const float sat_delta =
                    wr * clamped.saturation_red +
                    wg * clamped.saturation_green +
                    wb * clamped.saturation_blue;
                hsv.s = clamp01(hsv.s + sat_delta);
            }
            hsv.h = std::fmod(hsv.h + clamped.hue + 360.0f, 360.0f);
            hsv_to_rgb(hsv.h, hsv.s, hsv.v, r, g, b);

            r = clamp01(r * rgb_mult);
            g = clamp01(g * rgb_mult);
            b = clamp01(b * rgb_mult);

            work_r[i] = r;
            work_g[i] = g;
            work_b[i] = b;
        }
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
            Uint8 r = static_cast<Uint8>(std::clamp(std::lround(work_r[idx] * 255.0f), 0L, 255L));
            Uint8 g = static_cast<Uint8>(std::clamp(std::lround(work_g[idx] * 255.0f), 0L, 255L));
            Uint8 b = static_cast<Uint8>(std::clamp(std::lround(work_b[idx] * 255.0f), 0L, 255L));
            Uint8 a = static_cast<Uint8>(std::clamp(std::lround(work_a[idx] * 255.0f), 0L, 255L));
            pixels[y * stride + x] = SDL_MapRGBA(fmt, r, g, b, a);
        }
    }

    SDL_UnlockSurface(surface);
    return true;
}

SDL_Surface* capture_texture_surface(SDL_Renderer* renderer,
                                     SDL_Texture* texture,
                                     int width,
                                     int height) {
    if (!renderer || !texture || width <= 0 || height <= 0) {
        return nullptr;
    }
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA8888);
    if (!surface) {
        return nullptr;
    }
    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, texture) != 0) {
        SDL_FreeSurface(surface);
        SDL_SetRenderTarget(renderer, prev_target);
        return nullptr;
    }
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA8888, surface->pixels, surface->pitch) != 0) {
        SDL_SetRenderTarget(renderer, prev_target);
        SDL_FreeSurface(surface);
        return nullptr;
    }
    SDL_SetRenderTarget(renderer, prev_target);
    return surface;
}

} // namespace

namespace image_effects {

bool ApplyImageEffectsToSurface(SDL_Surface* surface, const camera_effects::ImageEffectSettings& settings) {
    return apply_color_pipeline(surface, settings);
}

bool ApplyImageEffectsToTexture(SDL_Renderer* renderer,
                                SDL_Texture*& texture,
                                int width,
                                int height,
                                const camera_effects::ImageEffectSettings& settings) {
    if (!renderer || !texture) {
        return false;
    }
    camera_effects::ImageEffectSettings clamped = settings;
    camera_effects::ClampImageEffectSettings(clamped);
    if (camera_effects::ImageEffectSettingsIsIdentity(clamped)) {
        return true;
    }
    SDL_Surface* surface = capture_texture_surface(renderer, texture, width, height);
    if (!surface) {
        return false;
    }
    if (!ApplyImageEffectsToSurface(surface, clamped)) {
        SDL_FreeSurface(surface);
        return false;
    }
    SDL_Texture* processed = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (!processed) {
        return false;
    }
    SDL_SetTextureBlendMode(processed, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(processed, SDL_ScaleModeBest);
#endif
    SDL_DestroyTexture(texture);
    texture = processed;
    return true;
}

SDL_Texture* BakeImageEffectTexture(SDL_Renderer* renderer,
                                    SDL_Texture* source,
                                    int width,
                                    int height,
                                    const camera_effects::ImageEffectSettings& settings) {
    if (!renderer || !source || width <= 0 || height <= 0) {
        return nullptr;
    }
    SDL_Texture* staging = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_TARGET,
                                             width,
                                             height);
    if (!staging) {
        return nullptr;
    }
    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, staging) != 0) {
        SDL_DestroyTexture(staging);
        SDL_SetRenderTarget(renderer, prev_target);
        return nullptr;
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, source, nullptr, nullptr);
    SDL_SetRenderTarget(renderer, prev_target);

    SDL_Texture* processed = staging;
    if (!ApplyImageEffectsToTexture(renderer, processed, width, height, settings)) {
        SDL_DestroyTexture(staging);
        return nullptr;
    }
    return processed;
}

} // namespace image_effects
