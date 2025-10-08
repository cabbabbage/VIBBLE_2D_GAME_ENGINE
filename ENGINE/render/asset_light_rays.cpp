#include "asset_light_rays.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "utils/light_source.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

// ------- brightness metric (same options as full-screen) -------
static inline float brightness_from_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                         const LightRaysParams& p) {
    float rf=r/255.f, gf=g/255.f, bf=b/255.f, v=0.f;
    switch (p.metric) {
        case BrightnessMetric::Luma709:
            v = 0.2126f*rf + 0.7152f*gf + 0.0722f*bf; break;
        case BrightnessMetric::MaxRGB:
            v = std::max(rf, std::max(gf, bf)); break;
        case BrightnessMetric::AvgRGB:
            v = (rf+gf+bf)/3.f; break;
        case BrightnessMetric::EnergyRGB:
            v = std::sqrt(rf*rf+gf*gf+bf*bf)/1.7320508f; break;
    }
    if (p.use_alpha_in_mask) v *= (a/255.f);
    if (p.gamma_comp != 1.f) v = std::pow(std::clamp(v,0.f,1.f), 1.f/p.gamma_comp);
    return std::clamp(v, 0.f, 1.f);
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
            int sx0 = x * ks, sy0 = y * ks;
            int rsum = 0, gsum = 0, bsum = 0, asum = 0, cnt = 0;
            for (int ky = 0; ky < ks; ++ky) {
                int sy = sy0 + ky; if (sy >= sh) break;
                const uint32_t* row = src + sy * sw;
                for (int kx = 0; kx < ks; ++kx) {
                    int sx = sx0 + kx; if (sx >= sw) break;
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

// ------- class -------
AssetLightRaysRenderer::AssetLightRaysRenderer(SDL_Renderer* renderer)
    : renderer_(renderer),
      blur_helper_(renderer ? std::make_unique<GaussianBlurHelper>(renderer) : nullptr) {}

void AssetLightRaysRenderer::set_renderer(SDL_Renderer* renderer) {
    renderer_ = renderer;
    if (blur_helper_) blur_helper_->set_renderer(renderer);
    else if (renderer) blur_helper_ = std::make_unique<GaussianBlurHelper>(renderer);
}

void AssetLightRaysRenderer::set_enabled(bool enabled) { enabled_ = enabled; }
void AssetLightRaysRenderer::set_blur_settings(float radius, float mix) { blur_radius_ = radius; blur_mix_ = mix; }

// Render the light to an RT so we can read pixels
SDL_Texture* AssetLightRaysRenderer::capture_light_to_rt_(SDL_Texture* src, int w, int h) {
    SDL_Texture* rt = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!rt) return nullptr;
    SDL_SetTextureBlendMode(rt, SDL_BLENDMODE_NONE);

    SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, rt);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
    SDL_RenderClear(renderer_);
    SDL_Rect dst{0,0,w,h};
    SDL_RenderCopy(renderer_, src, nullptr, &dst);
    #if SDL_VERSION_ATLEAST(2,0,10)
    SDL_RenderFlush(renderer_);
    #endif
    SDL_SetRenderTarget(renderer_, prev);
    return rt;
}

// Build a small rays texture from the light texture
SDL_Texture* AssetLightRaysRenderer::build_rays_from_light_(SDL_Texture* light_tex,
                                                            int light_w, int light_h,
                                                            float& out_avg_brightness)
{
    out_avg_brightness = 0.f;
    if (!renderer_ || !light_tex || light_w <= 0 || light_h <= 0) return nullptr;

    // 1) capture to RT and read pixels
    SDL_Texture* rt = capture_light_to_rt_(light_tex, light_w, light_h);
    if (!rt) return nullptr;

    SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, rt);
    std::vector<uint32_t> full_rgba(light_w * light_h);
    SDL_Rect r{0,0,light_w,light_h};
    if (SDL_RenderReadPixels(renderer_, &r, SDL_PIXELFORMAT_RGBA8888,
                             full_rgba.data(), light_w * int(sizeof(uint32_t))) != 0) {
        SDL_SetRenderTarget(renderer_, prev);
        SDL_DestroyTexture(rt);
        return nullptr;
    }
    SDL_SetRenderTarget(renderer_, prev);
    SDL_DestroyTexture(rt);

    // 2) downsample
    const int factor = 1 << std::max(0, params_.downsample_log2);
    std::vector<uint32_t> low_rgba;
    int dw=0, dh=0;
    downsample_box_rgba8888(full_rgba.data(), light_w, light_h, factor, low_rgba, dw, dh);

    // 3) compute brightness metric and histogram
    std::vector<float> metric(dw*dh, 0.f);
    std::array<int, 256> hist{}; hist.fill(0);
    double sum_b = 0.0;
    for (int i=0;i<dw*dh;++i) {
        uint32_t px = low_rgba[i];
        uint8_t a = (px>>24)&0xFF, r8=(px>>16)&0xFF, g8=(px>>8)&0xFF, b8=px&0xFF;
        float v = brightness_from_rgba(r8,g8,b8,a, params_);
        metric[i]=v; sum_b += v;
        hist[std::clamp(int(v*255.f + 0.5f),0,255)]++;
    }
    out_avg_brightness = float(sum_b / std::max(1, dw*dh));

    // 4) percentile threshold
    const int total = dw*dh;
    const float tail = 1.f - std::clamp(params_.bright_percentile, 0.f, 1.f);
    const int keep = std::max(1, int(total * tail));
    int running=0, thresh_bin=255;
    for (int b=255;b>=0;--b){ running+=hist[b]; if (running>=keep){ thresh_bin=b; break; } }
    float thr = std::max(params_.min_luma_threshold, thresh_bin / 255.f);

    // 5) bright mask
    const float denom = std::max(1e-5f, 1.f - thr);
    std::vector<float> bright(dw*dh, 0.f);
    for (int i=0;i<dw*dh;++i){
        float v = (metric[i] - thr) / denom;
        bright[i] = v>0.f ? v : 0.f;
    }

    // 6) ray march toward the light center (so rays emanate outward)
    const float lx = float(dw) * 0.5f;
    const float ly = float(dh) * 0.5f;
    const float max_dist = std::sqrt(float(dw*dw + dh*dh));

    std::vector<uint8_t> out_alpha(dw*dh, 0);
    const int   samples  = std::max(1, params_.samples);
    const float density  = params_.density / float(samples);
    const float decay    = params_.decay;
    const float weight   = params_.weight;
    const float exposure = params_.exposure;

    for (int y=0;y<dh;++y){
        for (int x=0;x<dw;++x){
            float dx = lx - float(x);
            float dy = ly - float(y);
            float px = float(x);
            float py = float(y);
            float stepx = dx * density;
            float stepy = dy * density;
            float illum_decay = 1.f;
            float sum = 0.f;

            for (int s=0; s<samples; ++s){
                px += stepx; py += stepy;
                int sx = int(px + 0.5f);
                int sy = int(py + 0.5f);
                if ((unsigned)sx >= (unsigned)dw || (unsigned)sy >= (unsigned)dh) break;
                float sample = bright[sy*dw + sx];
                sum += sample * illum_decay * weight;
                illum_decay *= decay;
            }
            float dx0 = float(x) - lx, dy0 = float(y) - ly;
            float dist = std::sqrt(dx0*dx0 + dy0*dy0);
            float falloff = max_dist>0.f ? std::clamp(1.f - (dist/max_dist), 0.f, 1.f) : 1.f;
            falloff = falloff * falloff;
            float val = std::min(1.f, sum * exposure * falloff);
            out_alpha[y*dw + x] = clamp_u8_(val * 255.f);
        }
    }

    // 7) make a streaming texture and write with correct format
    SDL_Texture* rays = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                          SDL_TEXTUREACCESS_STREAMING, dw, dh);
    if (!rays) return nullptr;
    SDL_SetTextureBlendMode(rays, SDL_BLENDMODE_ADD);

    void* pixels=nullptr; int pitch=0;
    if (SDL_LockTexture(rays, nullptr, &pixels, &pitch) != 0){
        SDL_DestroyTexture(rays); return nullptr;
    }
    Uint32 fmt=0; int access=0, tw=0, th=0;
    SDL_QueryTexture(rays, &fmt, &access, &tw, &th);
    SDL_PixelFormat* pf = SDL_AllocFormat(fmt);

    // white rays; tint at draw if desired
    for (int y=0;y<dh;++y){
        Uint32* row = reinterpret_cast<Uint32*>(static_cast<uint8_t*>(pixels) + y*pitch);
        for (int x=0;x<dw;++x){
            uint8_t a = out_alpha[y*dw + x];
            row[x] = SDL_MapRGBA(pf, 255, 255, 255, a);
        }
    }
    SDL_FreeFormat(pf);
    SDL_UnlockTexture(rays);

    return rays;
}

void AssetLightRaysRenderer::render_before_asset(Asset* asset,
                                                 const SDL_Rect& asset_screen_rect,
                                                 int base_width,
                                                 int base_height,
                                                 SDL_RendererFlip flip_mode)
{
    if (!renderer_ || !enabled_ || !asset || !asset->info) return;
    if (asset_screen_rect.w <= 0 || asset_screen_rect.h <= 0) return;
    if (base_width <= 0 || base_height <= 0) return;
    if (asset->info->light_sources.empty()) return;

    const float scale_x = asset_screen_rect.w / static_cast<float>(base_width);
    const float scale_y = asset_screen_rect.h / static_cast<float>(base_height);
    if (!std::isfinite(scale_x) || !std::isfinite(scale_y)) return;

    for (auto& light : asset->info->light_sources) {
        if (!light.texture) continue;

        int lw = light.cached_w, lh = light.cached_h;
        if (lw <= 0 || lh <= 0) {
            if (SDL_QueryTexture(light.texture, nullptr, nullptr, &lw, &lh) != 0) continue;
            light.cached_w = lw; light.cached_h = lh;
        }

        // Build per-light rays from its texture
        float avg_brightness = 0.f;
        SDL_Texture* rays_tex = build_rays_from_light_(light.texture, lw, lh, avg_brightness);
        if (!rays_tex) continue;

        // Optional blur on rays
        if (blur_helper_ && blur_radius_ > 0.f && blur_mix_ > 0.f) {
            if (SDL_Texture* blurred = blur_helper_->apply(rays_tex, lw, lh, blur_radius_, blur_mix_)) {
                SDL_DestroyTexture(rays_tex);
                rays_tex = blurred;
            }
        }

        // Base reach from light properties
        const int base_reach = std::max(0, light.radius) + std::max(0, light.flare);
        // Size factor from light area (normalized)
        const float size_norm = std::min(1.f, std::sqrt(float(lw*lh)) / 256.f);
        // Intensity factor from avg brightness and size
        const float light_intensity_norm = std::clamp(light.intensity / 255.f, 0.f, 1.f);
        const float intensity_factor = std::clamp(0.5f*avg_brightness + 0.5f*size_norm, 0.f, 1.f) * (0.5f + 0.5f*light_intensity_norm);

        // Length scales with intensity
        const float reach_scale = 0.5f + 1.8f * intensity_factor;
        int scaled_reach = int(std::ceil(std::max(1, base_reach) * reach_scale));
        if (scaled_reach <= 0) scaled_reach = std::max(lw, lh);

        // Compute screen center of the light
        int local_offset_x = light.offset_x;
        if (asset->flipped) local_offset_x = -local_offset_x;
        const int local_offset_y = light.offset_y;

        const float local_center_x = float(base_width) * 0.5f + float(local_offset_x);
        const float local_center_y = float(base_height) + float(local_offset_y);

        const float center_screen_x = asset_screen_rect.x + scale_x * local_center_x;
        const float center_screen_y = asset_screen_rect.y + scale_y * local_center_y;

        // Destination rect: scale light tex plus radial reach
        const float dest_w = std::max(1.f, scale_x * float(lw));
        const float dest_h = std::max(1.f, scale_y * float(lh));
        const int expand_x = int(std::ceil(std::abs(scale_x) * float(scaled_reach)));
        const int expand_y = int(std::ceil(std::abs(scale_y) * float(scaled_reach)));

        SDL_Rect dst{
            int(std::round(center_screen_x - dest_w*0.5f)) - expand_x,
            int(std::round(center_screen_y - dest_h*0.5f)) - expand_y,
            int(std::round(dest_w)) + expand_x*2,
            int(std::round(dest_h)) + expand_y*2
        };

        // Composite
        SDL_SetTextureBlendMode(rays_tex, SDL_BLENDMODE_ADD);
        const Uint8 alpha_mod = clamp_u8_(200.f + 55.f * intensity_factor); // 200..255
        SDL_SetTextureAlphaMod(rays_tex, alpha_mod);
        SDL_RenderCopyEx(renderer_, rays_tex, nullptr, &dst, 0.0, nullptr, flip_mode);

        SDL_DestroyTexture(rays_tex); // per-frame build; cache if needed
    }
}
