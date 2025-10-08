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
    manual_light_pos_ = SDL_Point{ sw / 2, sh / 3 };
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
    ray_intensity_buffer_.clear();
    ray_intensity_original_.clear();
    blur_work_buffer_.clear();
    alpha_buffer_.clear();
    screen_w_ = 0;
    screen_h_ = 0;
}

void LightRaysPass::set_screen_size(int sw, int sh) {
    if (sw <= 0 || sh <= 0) {
        return;
    }
    if (screen_w_ == sw && screen_h_ == sh) {
        return;
    }
    screen_w_ = sw;
    screen_h_ = sh;
    destroy_textures_();
}

void LightRaysPass::set_params(const LightRaysParams& p) {
    params_ = p;
    destroy_textures_();
}

void LightRaysPass::set_light_screen_pos(SDL_Point p) {
    manual_light_pos_ = p;
    manual_light_override_ = true;
}

void LightRaysPass::clear_light_override() {
    manual_light_override_ = false;
}
void LightRaysPass::set_enabled(bool v) { enabled_ = v; }

bool LightRaysPass::ensure_lowres_target_(int source_w, int source_h) {
    if (source_w <= 0 || source_h <= 0) {
        return false;
    }
    screen_w_ = source_w;
    screen_h_ = source_h;

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
    if (static_cast<int>(ray_intensity_buffer_.size()) < pixel_count) ray_intensity_buffer_.resize(pixel_count);
    if (static_cast<int>(ray_intensity_original_.size()) < pixel_count) ray_intensity_original_.resize(pixel_count);
    if (static_cast<int>(blur_work_buffer_.size()) < pixel_count) blur_work_buffer_.resize(pixel_count);
    if (static_cast<int>(alpha_buffer_.size()) < pixel_count)   alpha_buffer_.resize(pixel_count);
}

SDL_Texture* LightRaysPass::compute(SDL_Texture* source_render_target, int source_w, int source_h) {
    if (!enabled_ || !renderer_ || !source_render_target) return nullptr;
    if (source_w <= 0 || source_h <= 0) {
        SDL_QueryTexture(source_render_target, nullptr, nullptr, &source_w, &source_h);
    }
    if (source_w <= 0 || source_h <= 0) {
        return nullptr;
    }
    if (!ensure_lowres_target_(source_w, source_h)) return nullptr;

    const int factor = 1 << std::max(0, params_.downsample_log2);
    const int dw = lr_w_;
    const int dh = lr_h_;
    const int total_pixels = dw * dh;
    ensure_buffer_capacity_(total_pixels);
    float total_luma = 0.f;

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
        total_luma += L;
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
    float avg_luma = total_pixels > 0 ? total_luma / static_cast<float>(total_pixels) : 0.f;
    avg_luma = std::clamp(avg_luma, 0.f, 1.f);
    float darkness = 1.f - avg_luma;
    float percentile_thr = thresh_bin / 255.f;
    float dynamic_floor = params_.min_luma_threshold * (0.5f + 0.5f * avg_luma);
    float lowered_percentile = percentile_thr - darkness * 0.25f;
    float thr = std::max(dynamic_floor, lowered_percentile);
    thr = std::clamp(thr, 0.f, 1.f);

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
    analyze_brightness_distribution_(dw, dh);

    float lx = 0.5f * float(dw);
    float ly = 0.33f * float(dh);
    const float scale_x = source_w > 0 ? static_cast<float>(dw) / static_cast<float>(source_w) : 0.0f;
    const float scale_y = source_h > 0 ? static_cast<float>(dh) / static_cast<float>(source_h) : 0.0f;
    if (manual_light_override_) {
        lx = float(manual_light_pos_.x) * scale_x;
        ly = float(manual_light_pos_.y) * scale_y;
    } else if (has_detected_light_) {
        lx = detected_light_lowres_.x;
        ly = detected_light_lowres_.y;
    }
    const float max_dist = std::sqrt(float(dw * dw + dh * dh));

    const int samples = std::max(1, params_.samples);
    const float base_density = params_.density / float(samples);
    const float decay   = params_.decay;
    const float weight  = params_.weight;
    const float exposure = params_.exposure * (1.f + darkness * 1.25f);

    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            const int idx = y * dw + x;
            float dx = lx - float(x);
            float dy = ly - float(y);
            float px = float(x);
            float py = float(y);
            float base_strength = std::clamp(bright_buffer_[idx], 0.f, 1.f);
            float density_scale = 0.55f + 0.45f * base_strength;
            float stepx = dx * base_density * density_scale;
            float stepy = dy * base_density * density_scale;
            float illum_decay = 1.f;
            float sum = 0.f;
            float max_sample = base_strength;

            for (int s = 0; s < samples; ++s) {
                px += stepx;
                py += stepy;
                int sx = int(px + 0.5f);
                int sy = int(py + 0.5f);
                if ((unsigned)sx >= (unsigned)dw || (unsigned)sy >= (unsigned)dh) break;
                float sample = bright_buffer_[sy * dw + sx];
                if (sample > max_sample) max_sample = sample;
                sum += sample * illum_decay * weight;
                illum_decay *= decay;
                if (illum_decay < 1e-3f && max_sample < 0.25f) break;
            }

            float length_factor = std::max(base_strength, max_sample);
            length_factor = std::clamp(length_factor, 0.f, 1.f);
            const float min_length = 0.3f;
            float length_scale = min_length + (1.f - min_length) * length_factor;
            float darkness_bonus = darkness * 0.25f;
            length_scale = std::clamp(length_scale + (1.f - length_scale) * darkness_bonus,
                                      min_length * 0.5f, 1.f);

            float dx0 = float(x) - lx;
            float dy0 = float(y) - ly;
            float dist = std::sqrt(dx0 * dx0 + dy0 * dy0);
            float effective_max_dist = max_dist * length_scale;
            float falloff = effective_max_dist > 1e-5f
                                ? std::clamp(1.f - (dist / effective_max_dist), 0.f, 1.f)
                                : 1.f;
            falloff = falloff * falloff;

            float directional = 1.f;
            if (!manual_light_override_ && has_detected_light_) {
                float vx = float(x) - detected_light_lowres_.x;
                float vy = float(y) - detected_light_lowres_.y;
                float len = std::sqrt(vx * vx + vy * vy);
                if (len > 1e-4f) {
                    float norm_dot = (vx * dominant_axis_.x + vy * dominant_axis_.y) / len;
                    directional = 0.5f * (norm_dot + 1.f);
                    directional = directional * directional;
                }
            }

            float val = std::min(1.f, sum * exposure * falloff * directional);
            float gamma = std::clamp(0.85f - 0.15f * darkness, 0.65f, 0.9f);
            val = std::pow(std::max(0.f, val), gamma);
            ray_intensity_buffer_[idx] = val;
        }
    }

    // Final blur (performed at low resolution)
    const float blur_radius = std::clamp(params_.final_blur_radius, 0.f, 32.f);
    const float blur_mix = std::clamp(params_.final_blur_mix, 0.f, 1.f);
    if (blur_radius > 0.01f && blur_mix > 1e-3f) {
        const int radius_px = std::clamp(int(std::ceil(blur_radius)), 1, 64);
        const int kernel_size = radius_px * 2 + 1;
        std::vector<float> kernel(kernel_size);
        const float sigma = std::max(0.1f, blur_radius * 0.5f);
        const float inv_two_sigma_sq = 1.f / (2.f * sigma * sigma);
        float kernel_sum = 0.f;
        for (int i = -radius_px; i <= radius_px; ++i) {
            float w = std::exp(-(i * i) * inv_two_sigma_sq);
            kernel[i + radius_px] = w;
            kernel_sum += w;
        }
        if (kernel_sum <= 0.f) kernel_sum = 1.f;
        for (float& w : kernel) {
            w /= kernel_sum;
        }

        std::copy(ray_intensity_buffer_.begin(), ray_intensity_buffer_.begin() + total_pixels,
                  ray_intensity_original_.begin());

        for (int y = 0; y < dh; ++y) {
            for (int x = 0; x < dw; ++x) {
                float accum = 0.f;
                float weight_sum = 0.f;
                for (int k = -radius_px; k <= radius_px; ++k) {
                    int sx = std::clamp(x + k, 0, dw - 1);
                    float w = kernel[k + radius_px];
                    accum += w * ray_intensity_original_[y * dw + sx];
                    weight_sum += w;
                }
                blur_work_buffer_[y * dw + x] = weight_sum > 0.f ? accum / weight_sum : accum;
            }
        }

        for (int y = 0; y < dh; ++y) {
            for (int x = 0; x < dw; ++x) {
                float accum = 0.f;
                float weight_sum = 0.f;
                for (int k = -radius_px; k <= radius_px; ++k) {
                    int sy = std::clamp(y + k, 0, dh - 1);
                    float w = kernel[k + radius_px];
                    accum += w * blur_work_buffer_[sy * dw + x];
                    weight_sum += w;
                }
                float blurred = weight_sum > 0.f ? accum / weight_sum : accum;
                int idx = y * dw + x;
                float original = ray_intensity_original_[idx];
                float mixed = original + (blurred - original) * blur_mix;
                ray_intensity_buffer_[idx] = std::clamp(mixed, 0.f, 1.f);
            }
        }
    } else {
        for (int i = 0; i < total_pixels; ++i) {
            ray_intensity_buffer_[i] = std::clamp(ray_intensity_buffer_[i], 0.f, 1.f);
        }
    }

    for (int i = 0; i < total_pixels; ++i) {
        alpha_buffer_[i] = clamp_u8_(ray_intensity_buffer_[i] * 255.f);
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

void LightRaysPass::analyze_brightness_distribution_(int dw, int dh) {
    has_detected_light_ = false;
    dominant_axis_ = SDL_FPoint{1.f, 0.f};

    float total_weight = 0.f;
    float sum_x = 0.f;
    float sum_y = 0.f;

    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            float w = bright_buffer_[y * dw + x];
            if (w <= 0.f) continue;
            total_weight += w;
            sum_x += w * float(x);
            sum_y += w * float(y);
        }
    }

    if (total_weight <= 1e-4f) {
        return;
    }

    const float inv_w = 1.f / total_weight;
    float cx = sum_x * inv_w;
    float cy = sum_y * inv_w;

    detected_light_lowres_ = SDL_FPoint{cx, cy};
    has_detected_light_ = true;

    float mxx = 0.f, myy = 0.f, mxy = 0.f;
    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            float w = bright_buffer_[y * dw + x];
            if (w <= 0.f) continue;
            float dx = float(x) - cx;
            float dy = float(y) - cy;
            mxx += w * dx * dx;
            myy += w * dy * dy;
            mxy += w * dx * dy;
        }
    }

    mxx *= inv_w;
    myy *= inv_w;
    mxy *= inv_w;

    float diff = mxx - myy;
    float discr = std::sqrt(std::max(0.f, diff * diff * 0.25f + mxy * mxy));
    float lambda = 0.5f * (mxx + myy) + discr;

    float vx = 1.f;
    float vy = 0.f;
    if (std::abs(mxy) > 1e-6f) {
        vx = mxy;
        vy = lambda - mxx;
    } else if (diff < 0.f) {
        vx = 0.f;
        vy = 1.f;
    }

    float len = std::sqrt(vx * vx + vy * vy);
    if (len > 1e-6f) {
        dominant_axis_ = SDL_FPoint{vx / len, vy / len};
    } else {
        dominant_axis_ = SDL_FPoint{1.f, 0.f};
    }
}
