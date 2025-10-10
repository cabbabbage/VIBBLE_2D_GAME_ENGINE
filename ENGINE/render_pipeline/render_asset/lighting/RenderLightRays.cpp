#include "render_pipeline/render_asset/lighting/RenderLightRays.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <vector>

#include "asset/Asset.hpp"
#include "render/light_rays.hpp"
#include "render/light_rays_config.hpp"
#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"

namespace render_pipeline::lighting {

namespace {

inline float luma_u8(uint8_t r, uint8_t g, uint8_t b) {
    return (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.f;
}

inline float max_rgb_u8(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<float>(std::max({r, g, b})) / 255.f;
}

inline float avg_rgb_u8(uint8_t r, uint8_t g, uint8_t b) {
    return (static_cast<float>(r) + static_cast<float>(g) + static_cast<float>(b)) / (3.f * 255.f);
}

inline float energy_rgb_u8(uint8_t r, uint8_t g, uint8_t b) {
    const float rf = static_cast<float>(r) / 255.f;
    const float gf = static_cast<float>(g) / 255.f;
    const float bf = static_cast<float>(b) / 255.f;
    return std::sqrt((rf * rf + gf * gf + bf * bf) / 3.f);
}

inline float brightness_from_metric(BrightnessMetric metric, uint8_t r, uint8_t g, uint8_t b) {
    switch (metric) {
        case BrightnessMetric::Luma709:  return luma_u8(r, g, b);
        case BrightnessMetric::MaxRGB:   return max_rgb_u8(r, g, b);
        case BrightnessMetric::AvgRGB:   return avg_rgb_u8(r, g, b);
        case BrightnessMetric::EnergyRGB:return energy_rgb_u8(r, g, b);
    }
    return max_rgb_u8(r, g, b);
}

inline float apply_gamma(float value, float gamma) {
    const float v = std::clamp(value, 0.f, 1.f);
    if (gamma <= 0.f) {
        return v;
    }
    const float inv_gamma = 1.f / std::max(1e-4f, gamma);
    return std::pow(v, inv_gamma);
}

static void downsample_box_rgba8888(const uint32_t* src,
                                    int sw,
                                    int sh,
                                    int factor,
                                    std::vector<uint32_t>& dst,
                                    int& dw,
                                    int& dh) {
    dw = std::max(1, sw / factor);
    dh = std::max(1, sh / factor);
    dst.assign(dw * dh, 0u);

    const int ks = factor;
    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            const int sx0 = x * ks;
            const int sy0 = y * ks;
            int rsum = 0, gsum = 0, bsum = 0, asum = 0, cnt = 0;
            for (int ky = 0; ky < ks; ++ky) {
                const int sy = sy0 + ky;
                if (sy >= sh) break;
                const uint32_t* row = src + sy * sw;
                for (int kx = 0; kx < ks; ++kx) {
                    const int sx = sx0 + kx;
                    if (sx >= sw) break;
                    const uint32_t px = row[sx];
                    const uint8_t a = static_cast<uint8_t>((px >> 24) & 0xFF);
                    const uint8_t r = static_cast<uint8_t>((px >> 16) & 0xFF);
                    const uint8_t g = static_cast<uint8_t>((px >> 8) & 0xFF);
                    const uint8_t b = static_cast<uint8_t>((px >> 0) & 0xFF);
                    rsum += r; gsum += g; bsum += b; asum += a; ++cnt;
                }
            }
            if (cnt == 0) cnt = 1;
            const uint8_t r = static_cast<uint8_t>(rsum / cnt);
            const uint8_t g = static_cast<uint8_t>(gsum / cnt);
            const uint8_t b = static_cast<uint8_t>(bsum / cnt);
            const uint8_t a = static_cast<uint8_t>(asum / cnt);
            dst[y * dw + x] = (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
        }
    }
}

inline uint8_t clamp_u8(float v) {
    if (v <= 0.f) return 0;
    if (v >= 255.f) return 255;
    return static_cast<uint8_t>(v + 0.5f);
}

inline int asset_ray_strength(const Asset& asset) {
    if (asset.info) {
        return std::clamp(asset.info->ray_strength, 0, 100);
    }
    return std::clamp(asset.ray_strength, 0, 100);
}

} // namespace

bool RenderLightRays::supports(const Asset& asset) const {
    return asset.generate_rays && asset_ray_strength(asset) > 0;
}

SDL_Texture* RenderLightRays::run(SDL_Renderer* renderer, const Asset& asset, StageContext& context) {
    const int ray_strength = asset_ray_strength(asset);
    if (!renderer || !asset.generate_rays || ray_strength <= 0) {
        return nullptr;
    }

    const LightRaysConfig* config = context.light_rays_config();
    if (config && (!config->enabled || !config->per_light_enabled)) {
        return nullptr;
    }

    LightRaysParams params{};
    if (const LightRaysParams* from_context = context.light_rays_params()) {
        params = *from_context;
    } else if (config) {
        params = config->to_light_rays_params();
    } else {
        params = LightRaysConfig::defaults().to_light_rays_params();
    }

    SDL_Texture* source = context.final_texture ? context.final_texture : context.base_texture;
    if (!source) {
        return nullptr;
    }

    int width = context.width;
    int height = context.height;
    if (width <= 0 || height <= 0) {
        SDL_QueryTexture(source, nullptr, nullptr, &width, &height);
        context.width  = width;
        context.height = height;
    }
    if (width <= 0 || height <= 0) {
        return nullptr;
    }

    Uint32 fmt = SDL_PIXELFORMAT_RGBA8888;
    int    access = SDL_TEXTUREACCESS_TARGET;
    SDL_QueryTexture(source, &fmt, &access, nullptr, nullptr);

    SDL_Texture* readable = source;
    SDL_Texture* scratch  = nullptr;
    if (access != SDL_TEXTUREACCESS_TARGET) {
        scratch = SDL_CreateTexture(renderer, fmt, SDL_TEXTUREACCESS_TARGET, width, height);
        if (!scratch) {
            return nullptr;
        }
        SDL_SetTextureBlendMode(scratch, SDL_BLENDMODE_NONE);
        SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
        SDL_SetRenderTarget(renderer, scratch);
        SDL_RenderCopy(renderer, source, nullptr, nullptr);
        SDL_SetRenderTarget(renderer, prev_target);
        readable = scratch;
    }

    std::vector<uint32_t> full_rgba(static_cast<size_t>(width) * static_cast<size_t>(height));
    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, readable);
    SDL_Rect rect{0, 0, width, height};
    if (SDL_RenderReadPixels(renderer,
                             &rect,
                             SDL_PIXELFORMAT_RGBA8888,
                             full_rgba.data(),
                             width * static_cast<int>(sizeof(uint32_t))) != 0) {
        SDL_SetRenderTarget(renderer, prev_target);
        if (scratch) SDL_DestroyTexture(scratch);
        return nullptr;
    }
    SDL_SetRenderTarget(renderer, prev_target);
    if (scratch) {
        SDL_DestroyTexture(scratch);
        scratch = nullptr;
    }

    const int factor = 1 << std::max(0, params.downsample_log2);
    std::vector<uint32_t> low_rgba;
    int dw = 0;
    int dh = 0;
    downsample_box_rgba8888(full_rgba.data(), width, height, std::max(1, factor), low_rgba, dw, dh);

    if (dw <= 0 || dh <= 0) {
        return nullptr;
    }

    std::vector<float> brightness(static_cast<size_t>(dw) * static_cast<size_t>(dh), 0.f);
    std::array<int, 256> histogram{};
    histogram.fill(0);
    double brightness_sum = 0.0;

    for (int i = 0; i < dw * dh; ++i) {
        const uint32_t px = low_rgba[i];
        const uint8_t a = static_cast<uint8_t>((px >> 24) & 0xFF);
        const uint8_t r = static_cast<uint8_t>((px >> 16) & 0xFF);
        const uint8_t g = static_cast<uint8_t>((px >> 8) & 0xFF);
        const uint8_t b = static_cast<uint8_t>((px >> 0) & 0xFF);
        float L = brightness_from_metric(params.metric, r, g, b);
        if (params.use_alpha_in_mask) {
            L *= (a / 255.f);
        }
        L = apply_gamma(L, params.gamma_comp);
        L = std::clamp(L, 0.f, 1.f);
        brightness[i] = L;
        brightness_sum += static_cast<double>(L);
        const int bin = std::clamp(static_cast<int>(L * 255.f + 0.5f), 0, 255);
        histogram[bin] += 1;
    }

    const int total_pixels = dw * dh;
    const float avg_brightness = total_pixels > 0 ? static_cast<float>(brightness_sum / total_pixels) : 0.f;

    const float tail = 1.f - std::clamp(params.bright_percentile, 0.f, 1.f);
    int keep = std::max(1, static_cast<int>(std::round(total_pixels * tail)));
    int running = 0;
    int threshold_bin = 255;
    for (int b = 255; b >= 0; --b) {
        running += histogram[b];
        if (running >= keep) { threshold_bin = b; break; }
    }
    float threshold = std::max(params.min_luma_threshold, threshold_bin / 255.f);

    // Encourage tighter highlights when the bright portion is very large
    if (keep > total_pixels * 0.35f) {
        threshold = std::max(threshold, 0.92f);
    }

    std::vector<float> bright(static_cast<size_t>(dw) * static_cast<size_t>(dh), 0.f);
    const float denom = std::max(1e-5f, 1.f - threshold);
    for (int i = 0; i < dw * dh; ++i) {
        const float v = (brightness[i] - threshold) / denom;
        bright[i] = v > 0.f ? v : 0.f;
    }

    SDL_Point anchor = context.anchor_bottom_center();
    SDL_Point asset_screen = context.camera_view().map_to_screen(asset.pos);
    SDL_Point light_world = context.main_light().get_position();
    SDL_Point light_screen = context.camera_view().map_to_screen(light_world);

    const float light_local_x = static_cast<float>(anchor.x + (light_screen.x - asset_screen.x));
    const float light_local_y = static_cast<float>(anchor.y + (light_screen.y - asset_screen.y));
    const float lx = light_local_x / static_cast<float>(factor);
    const float ly = light_local_y / static_cast<float>(factor);

    const float max_dist = std::sqrt(static_cast<float>(dw * dw + dh * dh)) * 1.35f;
    const int base_samples = std::max(8, params.samples);

    std::vector<uint8_t> out_alpha(static_cast<size_t>(dw) * static_cast<size_t>(dh), 0u);

    const float map_opacity = context.main_light_alpha() / 255.f;
    const float map_intensity_boost = 1.f + (1.f - map_opacity) * 0.85f;
    const float darkness_boost = 1.f + std::clamp(0.55f - avg_brightness, 0.f, 0.55f) * 1.35f;
    const float strength_scale = static_cast<float>(ray_strength) / 100.f;
    const float exposure_base = params.exposure * map_intensity_boost * darkness_boost * strength_scale;

    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            const float seed = std::clamp(bright[y * dw + x], 0.f, 1.f);

            const float length_scale = 0.75f + seed * 0.85f;
            const float ray_count_scale = 1.0f + (1.f - seed) * 0.3f;
            const int   dynamic_samples = std::clamp(static_cast<int>(std::round(base_samples * ray_count_scale)), 4, base_samples * 2);
            const float total_density = params.density * length_scale;
            const float step_density = total_density / static_cast<float>(dynamic_samples);
            const float local_decay = std::pow(params.decay, 1.f / std::max(1.f, length_scale));
            const float local_weight = params.weight * (0.8f + seed * 0.7f);

            float dx = lx - static_cast<float>(x);
            float dy = ly - static_cast<float>(y);
            float px = static_cast<float>(x);
            float py = static_cast<float>(y);
            float stepx = dx * step_density;
            float stepy = dy * step_density;
            float illum_decay = 1.f;
            float sum = (1.f - seed) * 0.08f;
            float previous_sample = seed;

            for (int s = 0; s < dynamic_samples; ++s) {
                px += stepx;
                py += stepy;
                const int sx = static_cast<int>(std::lround(px));
                const int sy = static_cast<int>(std::lround(py));

                float sample = 0.f;
                if ((unsigned)sx < static_cast<unsigned>(dw) && (unsigned)sy < static_cast<unsigned>(dh)) {
                    sample = bright[sy * dw + sx];
                    previous_sample = std::max(previous_sample, sample);
                } else {
                    sample = previous_sample * 0.6f;
                    previous_sample *= 0.6f;
                }

                sum += sample * illum_decay * local_weight;
                illum_decay *= local_decay;
            }

            const float dx0 = static_cast<float>(x) - lx;
            const float dy0 = static_cast<float>(y) - ly;
            const float dist = std::sqrt(dx0 * dx0 + dy0 * dy0);
            float falloff = max_dist > 0.f ? std::clamp(1.f - (dist / max_dist), 0.f, 1.f) : 1.f;
            falloff = std::pow(falloff, 1.6f);

            const float value = std::min(1.f, sum * exposure_base * falloff);
            out_alpha[y * dw + x] = clamp_u8(value * 255.f);
        }
    }

    SDL_Texture* lowres_tex = SDL_CreateTexture(renderer,
                                                SDL_PIXELFORMAT_RGBA8888,
                                                SDL_TEXTUREACCESS_STREAMING,
                                                dw,
                                                dh);
    if (!lowres_tex) {
        return nullptr;
    }
    SDL_SetTextureBlendMode(lowres_tex, SDL_BLENDMODE_ADD);
#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(lowres_tex, SDL_ScaleModeBest);
#endif

    void* pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(lowres_tex, nullptr, &pixels, &pitch) != 0) {
        SDL_DestroyTexture(lowres_tex);
        return nullptr;
    }

    for (int y = 0; y < dh; ++y) {
        auto* row = static_cast<uint8_t*>(pixels) + y * pitch;
        auto* row_px = reinterpret_cast<uint32_t*>(row);
        for (int x = 0; x < dw; ++x) {
            const uint8_t a = out_alpha[y * dw + x];
            row_px[x] = (uint32_t(a) << 24) | 0x00FFFFFFu;
        }
    }

    SDL_UnlockTexture(lowres_tex);

    SDL_Texture* texture = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_TARGET,
                                             width,
                                             height);
    if (!texture) {
        SDL_DestroyTexture(lowres_tex);
        return nullptr;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_ADD);
#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeBest);
#endif

    SDL_Texture* prev = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, lowres_tex, nullptr, nullptr);
    SDL_SetRenderTarget(renderer, prev);

    SDL_DestroyTexture(lowres_tex);
    return texture;
}

} // namespace render_pipeline::lighting

