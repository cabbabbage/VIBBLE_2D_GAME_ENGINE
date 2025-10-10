#include "light_rays.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <array>

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

static void downsample_box_rgba8888(
    const uint32_t* src, int sw, int sh, int factor,
    std::vector<uint32_t>& dst, int& dw, int& dh)
{
    dw = std::max(1, sw / factor);
    dh = std::max(1, sh / factor);
    dst.assign(dw * dh, 0u);

    const int ks = factor;
    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            int sx0 = x * ks;
            int sy0 = y * ks;
            int rsum = 0, gsum = 0, bsum = 0, asum = 0, cnt = 0;
            for (int ky = 0; ky < ks; ++ky) {
                int sy = sy0 + ky;
                if (sy >= sh) break;
                const uint32_t* row = src + sy * sw;
                for (int kx = 0; kx < ks; ++kx) {
                    int sx = sx0 + kx;
                    if (sx >= sw) break;
                    uint32_t px = row[sx];
                    uint8_t a = (px >> 24) & 0xFF;
                    uint8_t r = (px >> 16) & 0xFF;
                    uint8_t g = (px >> 8)  & 0xFF;
                    uint8_t b = (px >> 0)  & 0xFF;
                    rsum += r; gsum += g; bsum += b; asum += a; ++cnt;
                }
            }
            if (cnt == 0) cnt = 1;
            uint8_t r = static_cast<uint8_t>(rsum / cnt);
            uint8_t g = static_cast<uint8_t>(gsum / cnt);
            uint8_t b = static_cast<uint8_t>(bsum / cnt);
            uint8_t a = static_cast<uint8_t>(asum / cnt);
            dst[y * dw + x] = (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
        }
    }
}
}

LightRaysPass::LightRaysPass(SDL_Renderer* r, int sw, int sh)
: renderer_(r), screen_w_(sw), screen_h_(sh) {
    light_pos_ = SDL_Point{ sw / 2, sh / 3 };
}

LightRaysPass::~LightRaysPass() {
    destroy_textures_();
}

void LightRaysPass::destroy_textures_() {
    if (rays_tex_lowres_) {
        SDL_DestroyTexture(rays_tex_lowres_);
        rays_tex_lowres_ = nullptr;
    }
    lr_w_ = lr_h_ = 0;
}

void LightRaysPass::set_screen_size(int sw, int sh) {
    screen_w_ = sw; screen_h_ = sh;
    destroy_textures_();
}

void LightRaysPass::set_params(const LightRaysParams& p) {
    params_ = p;
    destroy_textures_();
}

void LightRaysPass::set_light_screen_pos(SDL_Point p) { light_pos_ = p; }
void LightRaysPass::set_enabled(bool v) { enabled_ = v; }

bool LightRaysPass::ensure_lowres_target_() {
    const int factor = 1 << std::max(0, params_.downsample_log2);
    int want_w = std::max(1, screen_w_ / factor);
    int want_h = std::max(1, screen_h_ / factor);
    if (rays_tex_lowres_) {
        int w = 0, h = 0; Uint32 fmt = 0; int access = 0;
        if (SDL_QueryTexture(rays_tex_lowres_, &fmt, &access, &w, &h) == 0 && w == want_w && h == want_h) {
            lr_w_ = w; lr_h_ = h; return true;
        }
        SDL_DestroyTexture(rays_tex_lowres_);
        rays_tex_lowres_ = nullptr;
    }
    rays_tex_lowres_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, want_w, want_h);
    if (!rays_tex_lowres_) {
        std::cerr << "[LightRaysPass] Failed to create lowres rays texture: " << SDL_GetError() << "\n";
        return false;
    }
    SDL_SetTextureBlendMode(rays_tex_lowres_, SDL_BLENDMODE_ADD);
#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(rays_tex_lowres_, SDL_ScaleModeBest);
#endif
    lr_w_ = want_w; lr_h_ = want_h;
    return true;
}

SDL_Texture* LightRaysPass::compute(SDL_Texture* source_render_target) {
    if (!enabled_ || !renderer_ || !source_render_target) return nullptr;
    if (!ensure_lowres_target_()) return nullptr;

    SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, source_render_target);

    std::vector<uint32_t> full_rgba(screen_w_ * screen_h_);
    SDL_Rect rect{0, 0, screen_w_, screen_h_};
    if (SDL_RenderReadPixels(renderer_, &rect, SDL_PIXELFORMAT_RGBA8888,
                             full_rgba.data(), screen_w_ * int(sizeof(uint32_t))) != 0) {
        SDL_SetRenderTarget(renderer_, prev);
        std::cerr << "[LightRaysPass] SDL_RenderReadPixels failed: " << SDL_GetError() << "\n";
        return nullptr;
    }

    SDL_SetRenderTarget(renderer_, prev);

    const int factor = 1 << std::max(0, params_.downsample_log2);
    std::vector<uint32_t> low_rgba;
    int dw = 0, dh = 0;
    downsample_box_rgba8888(full_rgba.data(), screen_w_, screen_h_, factor, low_rgba, dw, dh);

    std::vector<float> brightness(dw * dh, 0.f);
    std::array<int, 256> hist{}; hist.fill(0);
    for (int i = 0; i < dw * dh; ++i) {
        uint32_t px = low_rgba[i];
        uint8_t a = (px >> 24) & 0xFF;
        uint8_t r = (px >> 16) & 0xFF;
        uint8_t g = (px >> 8)  & 0xFF;
        uint8_t b = (px >> 0)  & 0xFF;
        float L = brightness_from_metric(params_.metric, r, g, b);
        if (params_.use_alpha_in_mask) {
            L *= (a / 255.f);
        }
        L = apply_gamma(L, params_.gamma_comp);
        L = std::clamp(L, 0.f, 1.f);
        brightness[i] = L;
        int bin = std::clamp(int(L * 255.f + 0.5f), 0, 255);
        hist[bin] += 1;
    }

    const int total = dw * dh;
    const float tail = 1.f - std::clamp(params_.bright_percentile, 0.f, 1.f);
    const int keep = std::max(1, int(total * tail));
    int running = 0;
    int thresh_bin = 255;
    for (int b = 255; b >= 0; --b) {
        running += hist[b];
        if (running >= keep) { thresh_bin = b; break; }
    }
    float thr = std::max(params_.min_luma_threshold, thresh_bin / 255.f);

    if (keep > total * 0.25f) {
        return nullptr;
    }

    std::vector<float> bright(dw * dh, 0.f);
    const float denom = std::max(1e-5f, 1.f - thr);
    for (int i = 0; i < dw * dh; ++i) {
        float v = (brightness[i] - thr) / denom;
        bright[i] = v > 0.f ? v : 0.f;
    }

    const float lx = float(light_pos_.x) / float(factor);
    const float ly = float(light_pos_.y) / float(factor);
    const float max_dist = std::sqrt(float(dw * dw + dh * dh));

    std::vector<uint8_t> out_alpha(dw * dh, 0);
    const int samples = std::max(1, params_.samples);
    const float density = params_.density / float(samples);
    const float decay   = params_.decay;
    const float weight  = params_.weight;
    const float exposure = params_.exposure;

    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            float dx = lx - float(x);
            float dy = ly - float(y);
            float px = float(x);
            float py = float(y);
            float stepx = dx * density;
            float stepy = dy * density;
            float illum_decay = 1.f;
            float sum = 0.f;

            for (int s = 0; s < samples; ++s) {
                px += stepx;
                py += stepy;
                int sx = int(px + 0.5f);
                int sy = int(py + 0.5f);
                if ((unsigned)sx >= (unsigned)dw || (unsigned)sy >= (unsigned)dh) break;
                float sample = bright[sy * dw + sx];
                sum += sample * illum_decay * weight;
                illum_decay *= decay;
            }
            float dx0 = float(x) - lx;
            float dy0 = float(y) - ly;
            float dist = std::sqrt(dx0 * dx0 + dy0 * dy0);
            float falloff = max_dist > 0.f ? std::clamp(1.f - (dist / max_dist), 0.f, 1.f) : 1.f;
            falloff = falloff * falloff;
            float val = std::min(1.f, sum * exposure * falloff);
            out_alpha[y * dw + x] = clamp_u8_(val * 255.f);
        }
    }

    void* pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(rays_tex_lowres_, nullptr, &pixels, &pitch) != 0) {
        std::cerr << "[LightRaysPass] LockTexture failed: " << SDL_GetError() << "\n";
        return nullptr;
    }

    Uint32 fmt = 0; int access = 0, tw = 0, th = 0;
    SDL_QueryTexture(rays_tex_lowres_, &fmt, &access, &tw, &th);
    SDL_PixelFormat* pf = SDL_AllocFormat(fmt);

    for (int y = 0; y < dh; ++y) {
        uint8_t* row = static_cast<uint8_t*>(pixels) + y * pitch;
        Uint32* p32 = reinterpret_cast<Uint32*>(row);
        for (int x = 0; x < dw; ++x) {
            uint8_t a = out_alpha[y * dw + x];
            p32[x] = SDL_MapRGBA(pf, 255, 255, 255, a);
        }
    }

    SDL_FreeFormat(pf);
    SDL_UnlockTexture(rays_tex_lowres_);
    return rays_tex_lowres_;
}
