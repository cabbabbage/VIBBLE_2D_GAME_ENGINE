#include "light_rays.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr float kEpsilon = 1e-6f;

inline float clamp01(float v) {
    if (v <= 0.0f) return 0.0f;
    if (v >= 1.0f) return 1.0f;
    return v;
}

inline Uint8 clamp_u8(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return static_cast<Uint8>(v + 0.5f);
}

float luma_rec709(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

float avg_rgb(float r, float g, float b) {
    return (r + g + b) / 3.0f;
}

float energy_rgb(float r, float g, float b) {
    return std::sqrt(std::max(0.0f, (r * r + g * g + b * b) / 3.0f));
}
}

LightRaysPass::LightRaysPass(SDL_Renderer* renderer, int screen_w, int screen_h)
    : renderer_(renderer), screen_w_(screen_w), screen_h_(screen_h) {
    pixel_format_ = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
    update_downsample_dimensions();
}

LightRaysPass::~LightRaysPass() {
    release_resources();
    if (pixel_format_) {
        SDL_FreeFormat(pixel_format_);
        pixel_format_ = nullptr;
    }
}

void LightRaysPass::set_renderer(SDL_Renderer* renderer) {
    if (renderer_ == renderer) {
        return;
    }
    renderer_ = renderer;
    release_resources();
}

void LightRaysPass::set_screen_size(int w, int h) {
    if (w == screen_w_ && h == screen_h_) {
        return;
    }
    screen_w_ = std::max(0, w);
    screen_h_ = std::max(0, h);
    update_downsample_dimensions();
    release_resources();
}

void LightRaysPass::set_enabled(bool enabled) {
    enabled_ = enabled;
}

void LightRaysPass::set_params(const LightRaysParams& params) {
    if (params_.downsample_log2 != params.downsample_log2) {
        params_ = params;
        update_downsample_dimensions();
        release_resources();
        return;
    }
    params_ = params;
}

float LightRaysPass::brightness_from_pixel(uint32_t pixel) const {
    float a = ((pixel >> 24) & 0xFF) / 255.0f;
    float r = ((pixel >> 16) & 0xFF) / 255.0f;
    float g = ((pixel >> 8) & 0xFF) / 255.0f;
    float b = ((pixel >> 0) & 0xFF) / 255.0f;

    float brightness = 0.0f;
    switch (params_.metric) {
        case BrightnessMetric::Luma709:  brightness = luma_rec709(r, g, b); break;
        case BrightnessMetric::MaxRGB:   brightness = std::max(std::max(r, g), b); break;
        case BrightnessMetric::AvgRGB:   brightness = avg_rgb(r, g, b); break;
        case BrightnessMetric::EnergyRGB:brightness = energy_rgb(r, g, b); break;
    }

    if (params_.use_alpha_in_mask) {
        brightness *= a;
    }
    brightness = std::pow(clamp01(brightness), std::max(0.01f, params_.gamma_comp));
    return clamp01(brightness);
}

float LightRaysPass::sample_brightness(float u, float v) const {
    if (downsample_w_ <= 0 || downsample_h_ <= 0 || downsampled_mask_.empty()) {
        return 0.0f;
    }

    float x = clamp01(u) * (static_cast<float>(downsample_w_) - 1.0f);
    float y = clamp01(v) * (static_cast<float>(downsample_h_) - 1.0f);

    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    int x1 = std::min(x0 + 1, downsample_w_ - 1);
    int y1 = std::min(y0 + 1, downsample_h_ - 1);

    float tx = x - static_cast<float>(x0);
    float ty = y - static_cast<float>(y0);

    const auto idx = [&](int px, int py) {
        px = std::clamp(px, 0, downsample_w_ - 1);
        py = std::clamp(py, 0, downsample_h_ - 1);
        return py * downsample_w_ + px;
    };

    float s00 = downsampled_mask_[idx(x0, y0)];
    float s10 = downsampled_mask_[idx(x1, y0)];
    float s01 = downsampled_mask_[idx(x0, y1)];
    float s11 = downsampled_mask_[idx(x1, y1)];

    float sx0 = s00 + (s10 - s00) * tx;
    float sx1 = s01 + (s11 - s01) * tx;
    return sx0 + (sx1 - sx0) * ty;
}

void LightRaysPass::update_downsample_dimensions() {
    int shift = std::clamp(params_.downsample_log2, 0, 6);
    int divisor = 1 << shift;
    if (divisor <= 0) divisor = 1;

    auto ceil_div = [](int value, int d) {
        if (d <= 0) return value;
        return (value + d - 1) / d;
    };

    downsample_w_ = std::max(1, ceil_div(screen_w_, divisor));
    downsample_h_ = std::max(1, ceil_div(screen_h_, divisor));
}

bool LightRaysPass::ensure_resources() {
    if (!renderer_ || screen_w_ <= 0 || screen_h_ <= 0) {
        return false;
    }

    if (!pixel_format_) {
        pixel_format_ = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
        if (!pixel_format_) {
            return false;
        }
    }

    if (!capture_texture_) {
        capture_texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_TARGET, screen_w_, screen_h_);
        if (!capture_texture_) {
            return false;
        }
        SDL_SetTextureBlendMode(capture_texture_, SDL_BLENDMODE_NONE);
    }

    if (!rays_texture_) {
        rays_texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          downsample_w_, downsample_h_);
        if (!rays_texture_) {
            return false;
        }
        SDL_SetTextureBlendMode(rays_texture_, SDL_BLENDMODE_ADD);
    #if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(rays_texture_, SDL_ScaleModeLinear);
    #endif
    }

    capture_pixels_.assign(static_cast<size_t>(screen_w_) * static_cast<size_t>(screen_h_), 0u);
    downsampled_mask_.assign(static_cast<size_t>(downsample_w_) * static_cast<size_t>(downsample_h_), 0.0f);
    rays_pixels_.assign(static_cast<size_t>(downsample_w_) * static_cast<size_t>(downsample_h_), 0u);

    return true;
}

void LightRaysPass::release_resources() {
    if (capture_texture_) {
        SDL_DestroyTexture(capture_texture_);
        capture_texture_ = nullptr;
    }
    if (rays_texture_) {
        SDL_DestroyTexture(rays_texture_);
        rays_texture_ = nullptr;
    }
    capture_pixels_.clear();
    downsampled_mask_.clear();
    rays_pixels_.clear();
}

SDL_Texture* LightRaysPass::compute(SDL_Texture* source_texture) {
    if (!enabled_ || !renderer_ || !source_texture) {
        return nullptr;
    }
    if (screen_w_ <= 0 || screen_h_ <= 0) {
        return nullptr;
    }
    if (params_.samples <= 0) {
        return nullptr;
    }

    update_downsample_dimensions();
    if (!ensure_resources()) {
        return nullptr;
    }

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, capture_texture_);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
    SDL_RenderClear(renderer_);

    SDL_BlendMode prev_blend = SDL_BLENDMODE_INVALID;
    const bool restore_blend = SDL_GetTextureBlendMode(source_texture, &prev_blend) == 0;
    Uint8 prev_r = 255, prev_g = 255, prev_b = 255, prev_a = 255;
    const bool restore_color = SDL_GetTextureColorMod(source_texture, &prev_r, &prev_g, &prev_b) == 0;
    const bool restore_alpha = SDL_GetTextureAlphaMod(source_texture, &prev_a) == 0;

    SDL_SetTextureBlendMode(source_texture, SDL_BLENDMODE_NONE);
    SDL_SetTextureColorMod(source_texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(source_texture, 255);

    SDL_Rect dst{0, 0, screen_w_, screen_h_};
    SDL_RenderCopy(renderer_, source_texture, nullptr, &dst);

    if (restore_blend) SDL_SetTextureBlendMode(source_texture, prev_blend);
    if (restore_color) SDL_SetTextureColorMod(source_texture, prev_r, prev_g, prev_b);
    if (restore_alpha) SDL_SetTextureAlphaMod(source_texture, prev_a);

    SDL_Rect rect{0, 0, screen_w_, screen_h_};
    if (SDL_RenderReadPixels(renderer_, &rect, SDL_PIXELFORMAT_RGBA8888,
                             capture_pixels_.data(), screen_w_ * static_cast<int>(sizeof(uint32_t))) != 0) {
        SDL_SetRenderTarget(renderer_, previous_target);
        return nullptr;
    }

    SDL_SetRenderTarget(renderer_, previous_target);

    const int block_shift = std::clamp(params_.downsample_log2, 0, 6);

    std::vector<int> contribution_counts(static_cast<size_t>(downsample_w_) * static_cast<size_t>(downsample_h_), 0);

    for (int y = 0; y < screen_h_; ++y) {
        int mask_y = std::min(y >> block_shift, downsample_h_ - 1);
        for (int x = 0; x < screen_w_; ++x) {
            int mask_x = std::min(x >> block_shift, downsample_w_ - 1);
            size_t mask_idx = static_cast<size_t>(mask_y) * static_cast<size_t>(downsample_w_) + static_cast<size_t>(mask_x);
            float brightness = brightness_from_pixel(capture_pixels_[static_cast<size_t>(y) * static_cast<size_t>(screen_w_) + static_cast<size_t>(x)]);
            downsampled_mask_[mask_idx] += brightness;
            contribution_counts[mask_idx] += 1;
        }
    }

    std::vector<float> mask_values = downsampled_mask_;
    for (size_t idx = 0; idx < mask_values.size(); ++idx) {
        int count = contribution_counts[idx];
        if (count > 0) {
            mask_values[idx] = downsampled_mask_[idx] / static_cast<float>(count);
        } else {
            mask_values[idx] = 0.0f;
        }
    }

    float bright_cut = params_.min_luma_threshold;
    if (!mask_values.empty()) {
        std::vector<float> temp = mask_values;
        size_t percentile_index = static_cast<size_t>(std::clamp(params_.bright_percentile, 0.0f, 1.0f) * (temp.size() - 1));
        std::nth_element(temp.begin(), temp.begin() + static_cast<long>(percentile_index), temp.end());
        bright_cut = std::max(bright_cut, temp[percentile_index]);
    }

    for (size_t i = 0; i < mask_values.size(); ++i) {
        float v = mask_values[i];
        if (v <= bright_cut) {
            v = 0.0f;
        } else {
            float denom = std::max(kEpsilon, 1.0f - bright_cut);
            v = (v - bright_cut) / denom;
        }
        downsampled_mask_[i] = clamp01(v);
    }

    const float inv_mask_w = 1.0f / static_cast<float>(downsample_w_);
    const float inv_mask_h = 1.0f / static_cast<float>(downsample_h_);

    float light_u = 0.5f;
    float light_v = 0.5f;
    if (screen_w_ > 0) {
        light_u = (static_cast<float>(light_screen_pos_.x) + 0.5f) / static_cast<float>(screen_w_);
    }
    if (screen_h_ > 0) {
        light_v = (static_cast<float>(light_screen_pos_.y) + 0.5f) / static_cast<float>(screen_h_);
    }

    const int samples = std::clamp(params_.samples, 1, 1024);
    const float density = std::max(0.0f, params_.density);
    const float decay = std::clamp(params_.decay, 0.0f, 0.9999f);
    const float weight = std::max(0.0f, params_.weight);
    const float exposure = std::max(0.0f, params_.exposure);

    for (int y = 0; y < downsample_h_; ++y) {
        for (int x = 0; x < downsample_w_; ++x) {
            float u = (static_cast<float>(x) + 0.5f) * inv_mask_w;
            float v = (static_cast<float>(y) + 0.5f) * inv_mask_h;

            float delta_u = (u - light_u) * density / static_cast<float>(samples);
            float delta_v = (v - light_v) * density / static_cast<float>(samples);

            float sample_u = u;
            float sample_v = v;
            float illumination_decay = 1.0f;
            float accum = 0.0f;

            for (int i = 0; i < samples; ++i) {
                sample_u -= delta_u;
                sample_v -= delta_v;
                if (sample_u < 0.0f || sample_u > 1.0f || sample_v < 0.0f || sample_v > 1.0f) {
                    continue;
                }
                float sample_value = sample_brightness(sample_u, sample_v);
                accum += sample_value * illumination_decay * weight;
                illumination_decay *= decay;
            }

            float intensity = accum * exposure;
            Uint8 value = clamp_u8(intensity * 255.0f);
            rays_pixels_[static_cast<size_t>(y) * static_cast<size_t>(downsample_w_) + static_cast<size_t>(x)] =
                SDL_MapRGBA(pixel_format_, value, value, value, value);
        }
    }

    if (SDL_UpdateTexture(rays_texture_, nullptr, rays_pixels_.data(),
                          downsample_w_ * static_cast<int>(sizeof(uint32_t))) != 0) {
        return nullptr;
    }

    return rays_texture_;
}
