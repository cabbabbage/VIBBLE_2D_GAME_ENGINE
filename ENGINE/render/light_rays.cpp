#include "light_rays.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <array>

namespace {
inline float luma_u8(uint8_t r, uint8_t g, uint8_t b) {
    // Rec. 709
    return (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.f;
}

} // anon

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
    if (capture_tex_lowres_) {
        SDL_DestroyTexture(capture_tex_lowres_);
        capture_tex_lowres_ = nullptr;
    }
    if (rays_pixel_format_) {
        SDL_FreeFormat(rays_pixel_format_);
        rays_pixel_format_ = nullptr;
    }
    lr_w_ = lr_h_ = 0;
    capture_pixels_.clear();
    luma_buffer_.clear();
    bright_buffer_.clear();
    alpha_buffer_.clear();
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
    bool recreated_rays = false;

    if (rays_tex_lowres_) {
        int w = 0, h = 0; Uint32 fmt = 0; int access = 0;
        if (SDL_QueryTexture(rays_tex_lowres_, &fmt, &access, &w, &h) != 0 || w != want_w || h != want_h) {
            SDL_DestroyTexture(rays_tex_lowres_);
            rays_tex_lowres_ = nullptr;
        }
    }
    if (!rays_tex_lowres_) {
        rays_tex_lowres_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_STREAMING, want_w, want_h);
        if (!rays_tex_lowres_) {
            std::cerr << "[LightRaysPass] Failed to create lowres rays texture: " << SDL_GetError() << "\n";
            return false;
        }
        SDL_SetTextureBlendMode(rays_tex_lowres_, SDL_BLENDMODE_ADD); // default to ADD
        recreated_rays = true;
    }

    if (capture_tex_lowres_) {
        int w = 0, h = 0; Uint32 fmt = 0; int access = 0;
        if (SDL_QueryTexture(capture_tex_lowres_, &fmt, &access, &w, &h) != 0 || w != want_w || h != want_h) {
            SDL_DestroyTexture(capture_tex_lowres_);
            capture_tex_lowres_ = nullptr;
        }
    }
    if (!capture_tex_lowres_) {
        capture_tex_lowres_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                                SDL_TEXTUREACCESS_TARGET, want_w, want_h);
        if (!capture_tex_lowres_) {
            std::cerr << "[LightRaysPass] Failed to create capture texture: " << SDL_GetError() << "\n";
            SDL_DestroyTexture(rays_tex_lowres_);
            rays_tex_lowres_ = nullptr;
            return false;
        }
        SDL_SetTextureBlendMode(capture_tex_lowres_, SDL_BLENDMODE_NONE);
    }

    if (recreated_rays || !rays_pixel_format_) {
        if (rays_pixel_format_) {
            SDL_FreeFormat(rays_pixel_format_);
        }
        Uint32 fmt = 0; int access = 0;
        SDL_QueryTexture(rays_tex_lowres_, &fmt, &access, nullptr, nullptr);
        rays_pixel_format_ = SDL_AllocFormat(fmt);
        if (!rays_pixel_format_) {
            std::cerr << "[LightRaysPass] Failed to allocate pixel format info: " << SDL_GetError() << "\n";
            return false;
        }
    }

    lr_w_ = want_w; lr_h_ = want_h;
    return true;
}

void LightRaysPass::ensure_buffer_capacity_(int pixel_count) {
    if (pixel_count <= 0) return;
    if (static_cast<int>(capture_pixels_.size()) < pixel_count) capture_pixels_.resize(pixel_count);
    if (static_cast<int>(luma_buffer_.size()) < pixel_count)    luma_buffer_.resize(pixel_count);
    if (static_cast<int>(bright_buffer_.size()) < pixel_count)  bright_buffer_.resize(pixel_count);
    if (static_cast<int>(alpha_buffer_.size()) < pixel_count)   alpha_buffer_.resize(pixel_count);
}

SDL_Texture* LightRaysPass::compute(SDL_Texture* source_render_target) {
    if (!enabled_ || !renderer_ || !source_render_target) return nullptr;
    if (!ensure_lowres_target_()) return nullptr;

    const int factor = 1 << std::max(0, params_.downsample_log2);
    const int dw = lr_w_;
    const int dh = lr_h_;
    const int total_pixels = dw * dh;
    ensure_buffer_capacity_(total_pixels);

    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, capture_tex_lowres_);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

    SDL_BlendMode prev_blend = SDL_BLENDMODE_INVALID;
    bool restore_blend = SDL_GetTextureBlendMode(source_render_target, &prev_blend) == 0;
    Uint8 prev_r = 255, prev_g = 255, prev_b = 255;
    Uint8 prev_a = 255;
    bool restore_color = SDL_GetTextureColorMod(source_render_target, &prev_r, &prev_g, &prev_b) == 0;
    bool restore_alpha = SDL_GetTextureAlphaMod(source_render_target, &prev_a) == 0;
    SDL_SetTextureBlendMode(source_render_target, SDL_BLENDMODE_NONE);
    SDL_SetTextureColorMod(source_render_target, 255, 255, 255);
    SDL_SetTextureAlphaMod(source_render_target, 255);

    SDL_Rect dst{0, 0, dw, dh};
    SDL_RenderCopy(renderer_, source_render_target, nullptr, &dst);
    if (restore_blend) {
        SDL_SetTextureBlendMode(source_render_target, prev_blend);
    } else {
        SDL_SetTextureBlendMode(source_render_target, SDL_BLENDMODE_BLEND);
    }
    if (restore_color) SDL_SetTextureColorMod(source_render_target, prev_r, prev_g, prev_b);
    if (restore_alpha) SDL_SetTextureAlphaMod(source_render_target, prev_a);

    SDL_Rect rect{0, 0, dw, dh};
    if (SDL_RenderReadPixels(renderer_, &rect, SDL_PIXELFORMAT_RGBA8888,
                             capture_pixels_.data(), dw * int(sizeof(uint32_t))) != 0) {
        SDL_SetRenderTarget(renderer_, prev_target);
        std::cerr << "[LightRaysPass] SDL_RenderReadPixels failed: " << SDL_GetError() << "\n";
        return nullptr;
    }

    SDL_SetRenderTarget(renderer_, prev_target);

    // Build luma buffer and histogram
    std::fill(histogram_.begin(), histogram_.end(), 0);
    for (int i = 0; i < total_pixels; ++i) {
        uint32_t px = capture_pixels_[i];
        uint8_t a = (px >> 24) & 0xFF;
        uint8_t r = (px >> 16) & 0xFF;
        uint8_t g = (px >> 8)  & 0xFF;
        uint8_t b = (px >> 0)  & 0xFF;
        float L = luma_u8(r, g, b) * (a / 255.f);
        L = std::min(1.f, std::max(0.f, L));
        luma_buffer_[i] = L;
        int bin = std::clamp(int(L * 255.f + 0.5f), 0, 255);
        histogram_[bin] += 1;
    }

    // Percentile threshold
    const float tail = 1.f - std::clamp(params_.bright_percentile, 0.f, 1.f);
    const int keep = std::max(1, int(total_pixels * tail));
    int running = 0;
    int thresh_bin = 255;
    for (int b = 255; b >= 0; --b) {
        running += histogram_[b];
        if (running >= keep) { thresh_bin = b; break; }
    }
    float thr = std::max(params_.min_luma_threshold, thresh_bin / 255.f);

    // If too many pixels are considered bright, skip to avoid washing the screen
    if (keep > total_pixels * 0.45f) {
        return nullptr;
    }

    // Bright mask in [0..1]
    const float denom = std::max(1e-5f, 1.f - thr);
    for (int i = 0; i < total_pixels; ++i) {
        float v = (luma_buffer_[i] - thr) / denom;
        bright_buffer_[i] = v > 0.f ? v : 0.f;
    }

    // Ray march toward light
    const float lx = float(light_pos_.x) / float(factor);
    const float ly = float(light_pos_.y) / float(factor);
    const float max_dist = std::sqrt(float(dw * dw + dh * dh));

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
                float sample = bright_buffer_[sy * dw + sx];
                sum += sample * illum_decay * weight;
                illum_decay *= decay;
            }
            float dx0 = float(x) - lx;
            float dy0 = float(y) - ly;
            float dist = std::sqrt(dx0 * dx0 + dy0 * dy0);
            float falloff = max_dist > 0.f ? std::clamp(1.f - (dist / max_dist), 0.f, 1.f) : 1.f;
            falloff = falloff * falloff;
            float val = std::min(1.f, sum * exposure * falloff);
            // slight gamma curve to sharpen streaks
            val = std::pow(std::max(0.f, val), 0.85f);
            alpha_buffer_[y * dw + x] = clamp_u8_(val * 255.f);
        }
    }

    // Map to low-res texture using the texture's real format
    void* pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(rays_tex_lowres_, nullptr, &pixels, &pitch) != 0) {
        std::cerr << "[LightRaysPass] LockTexture failed: " << SDL_GetError() << "\n";
        return nullptr;
    }

    if (!rays_pixel_format_) {
        Uint32 fmt_local = 0; int access = 0;
        SDL_QueryTexture(rays_tex_lowres_, &fmt_local, &access, nullptr, nullptr);
        rays_pixel_format_ = SDL_AllocFormat(fmt_local);
        if (!rays_pixel_format_) {
            SDL_UnlockTexture(rays_tex_lowres_);
            std::cerr << "[LightRaysPass] Failed to allocate pixel format info for writeback\n";
            return nullptr;
        }
    }

    for (int y = 0; y < dh; ++y) {
        uint8_t* row = static_cast<uint8_t*>(pixels) + y * pitch;
        Uint32* p32 = reinterpret_cast<Uint32*>(row);
        for (int x = 0; x < dw; ++x) {
            uint8_t a = alpha_buffer_[y * dw + x];
            p32[x] = SDL_MapRGBA(rays_pixel_format_, 255, 255, 255, a); // correct packing for this texture format
        }
    }

    SDL_UnlockTexture(rays_tex_lowres_);
    return rays_tex_lowres_;
}
