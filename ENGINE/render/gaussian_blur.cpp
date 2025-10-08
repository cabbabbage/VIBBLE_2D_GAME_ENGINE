#include "gaussian_blur.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr float kRadiusEpsilon = 0.01f;
constexpr float kMixEpsilon = 1e-3f;

inline uint8_t clamp_u8(float v) {
    if (v <= 0.f) return 0;
    if (v >= 255.f) return 255;
    return static_cast<uint8_t>(v + 0.5f);
}
}

GaussianBlurHelper::GaussianBlurHelper(SDL_Renderer* renderer)
    : renderer_(renderer) {
    pixel_format_ = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
}

GaussianBlurHelper::~GaussianBlurHelper() {
    destroy_textures();
    if (pixel_format_) {
        SDL_FreeFormat(pixel_format_);
        pixel_format_ = nullptr;
    }
}

void GaussianBlurHelper::set_renderer(SDL_Renderer* renderer) {
    if (renderer_ == renderer) {
        return;
    }
    renderer_ = renderer;
    destroy_textures();
}

bool GaussianBlurHelper::ensure_resources(int width, int height) {
    if (!renderer_ || width <= 0 || height <= 0) {
        return false;
    }

    if (!pixel_format_) {
        pixel_format_ = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
        if (!pixel_format_) {
            return false;
        }
    }

    if (width != tex_w_ || height != tex_h_) {
        destroy_textures();
    }

    if (!capture_tex_) {
        capture_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET, width, height);
        if (!capture_tex_) {
            return false;
        }
        SDL_SetTextureBlendMode(capture_tex_, SDL_BLENDMODE_NONE);
    }

    if (!upload_tex_) {
        upload_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                        SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!upload_tex_) {
            return false;
        }
        SDL_SetTextureBlendMode(upload_tex_, SDL_BLENDMODE_BLEND);
    }

    tex_w_ = width;
    tex_h_ = height;

    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    pixel_buffer_.assign(pixel_count, 0u);
    output_pixels_.assign(pixel_count, 0u);
    channel_r_.assign(pixel_count, 0.f);
    channel_g_.assign(pixel_count, 0.f);
    channel_b_.assign(pixel_count, 0.f);
    channel_a_.assign(pixel_count, 0.f);
    original_r_.assign(pixel_count, 0.f);
    original_g_.assign(pixel_count, 0.f);
    original_b_.assign(pixel_count, 0.f);
    original_a_.assign(pixel_count, 0.f);
    temp_buffer_.assign(pixel_count, 0.f);

    return true;
}

void GaussianBlurHelper::destroy_textures() {
    if (capture_tex_) {
        SDL_DestroyTexture(capture_tex_);
        capture_tex_ = nullptr;
    }
    if (upload_tex_) {
        SDL_DestroyTexture(upload_tex_);
        upload_tex_ = nullptr;
    }
    tex_w_ = 0;
    tex_h_ = 0;
    pixel_buffer_.clear();
    output_pixels_.clear();
    channel_r_.clear();
    channel_g_.clear();
    channel_b_.clear();
    channel_a_.clear();
    original_r_.clear();
    original_g_.clear();
    original_b_.clear();
    original_a_.clear();
    temp_buffer_.clear();
}

SDL_Texture* GaussianBlurHelper::apply(SDL_Texture* source,
                                       int source_w,
                                       int source_h,
                                       float radius,
                                       float mix) {
    if (!renderer_ || !source) {
        return nullptr;
    }

    if (source_w <= 0 || source_h <= 0) {
        SDL_QueryTexture(source, nullptr, nullptr, &source_w, &source_h);
    }
    if (source_w <= 0 || source_h <= 0) {
        return nullptr;
    }

    if (!ensure_resources(source_w, source_h)) {
        return nullptr;
    }

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, capture_tex_);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
    SDL_RenderClear(renderer_);

    SDL_BlendMode prev_blend = SDL_BLENDMODE_INVALID;
    const bool restore_blend = SDL_GetTextureBlendMode(source, &prev_blend) == 0;
    Uint8 prev_r = 255, prev_g = 255, prev_b = 255, prev_a = 255;
    const bool restore_color = SDL_GetTextureColorMod(source, &prev_r, &prev_g, &prev_b) == 0;
    const bool restore_alpha = SDL_GetTextureAlphaMod(source, &prev_a) == 0;

    SDL_SetTextureBlendMode(source, SDL_BLENDMODE_NONE);
    SDL_SetTextureColorMod(source, 255, 255, 255);
    SDL_SetTextureAlphaMod(source, 255);

    SDL_Rect dst{0, 0, source_w, source_h};
    SDL_RenderCopy(renderer_, source, nullptr, &dst);

    if (restore_blend) {
        SDL_SetTextureBlendMode(source, prev_blend);
    } else {
        SDL_SetTextureBlendMode(source, SDL_BLENDMODE_BLEND);
    }
    if (restore_color) SDL_SetTextureColorMod(source, prev_r, prev_g, prev_b);
    if (restore_alpha) SDL_SetTextureAlphaMod(source, prev_a);

    SDL_Rect rect{0, 0, source_w, source_h};
    if (SDL_RenderReadPixels(renderer_, &rect, SDL_PIXELFORMAT_RGBA8888,
                             pixel_buffer_.data(), source_w * static_cast<int>(sizeof(uint32_t))) != 0) {
        SDL_SetRenderTarget(renderer_, previous_target);
        return nullptr;
    }

    SDL_SetRenderTarget(renderer_, previous_target);

    const int total_pixels = source_w * source_h;
    for (int i = 0; i < total_pixels; ++i) {
        uint32_t px = pixel_buffer_[i];
        float a = ((px >> 24) & 0xFF) / 255.f;
        float r = ((px >> 16) & 0xFF) / 255.f;
        float g = ((px >> 8) & 0xFF) / 255.f;
        float b = ((px >> 0) & 0xFF) / 255.f;

        channel_a_[i] = original_a_[i] = std::clamp(a, 0.f, 1.f);
        channel_r_[i] = original_r_[i] = std::clamp(r, 0.f, 1.f);
        channel_g_[i] = original_g_[i] = std::clamp(g, 0.f, 1.f);
        channel_b_[i] = original_b_[i] = std::clamp(b, 0.f, 1.f);
    }

    const float radius_clamped = std::clamp(radius, 0.f, 64.f);
    const float mix_clamped = std::clamp(mix, 0.f, 1.f);
    const bool apply_blur = radius_clamped > kRadiusEpsilon && mix_clamped > kMixEpsilon;

    if (apply_blur) {
        const int radius_px = std::clamp(static_cast<int>(std::ceil(radius_clamped)), 1, 64);
        const int kernel_size = radius_px * 2 + 1;
        std::vector<float> kernel(static_cast<size_t>(kernel_size));
        const float sigma = std::max(0.1f, radius_clamped * 0.5f);
        const float inv_two_sigma_sq = 1.f / (2.f * sigma * sigma);
        float kernel_sum = 0.f;
        for (int i = -radius_px; i <= radius_px; ++i) {
            float w = std::exp(-(i * i) * inv_two_sigma_sq);
            kernel[static_cast<size_t>(i + radius_px)] = w;
            kernel_sum += w;
        }
        if (kernel_sum <= 0.f) kernel_sum = 1.f;
        for (float& w : kernel) {
            w /= kernel_sum;
        }

        auto blur_channel = [&](std::vector<float>& channel, const std::vector<float>& original) {
            for (int y = 0; y < source_h; ++y) {
                for (int x = 0; x < source_w; ++x) {
                    float accum = 0.f;
                    float weight_sum = 0.f;
                    for (int k = -radius_px; k <= radius_px; ++k) {
                        int sx = std::clamp(x + k, 0, source_w - 1);
                        float w = kernel[static_cast<size_t>(k + radius_px)];
                        accum += w * channel[static_cast<size_t>(y * source_w + sx)];
                        weight_sum += w;
                    }
                    temp_buffer_[static_cast<size_t>(y * source_w + x)] =
                        weight_sum > 0.f ? accum / weight_sum : accum;
                }
            }

            for (int y = 0; y < source_h; ++y) {
                for (int x = 0; x < source_w; ++x) {
                    float accum = 0.f;
                    float weight_sum = 0.f;
                    for (int k = -radius_px; k <= radius_px; ++k) {
                        int sy = std::clamp(y + k, 0, source_h - 1);
                        float w = kernel[static_cast<size_t>(k + radius_px)];
                        accum += w * temp_buffer_[static_cast<size_t>(sy * source_w + x)];
                        weight_sum += w;
                    }
                    const size_t idx = static_cast<size_t>(y * source_w + x);
                    float blurred = weight_sum > 0.f ? accum / weight_sum : accum;
                    float original_value = original[idx];
                    float mixed = original_value + (blurred - original_value) * mix_clamped;
                    channel[idx] = std::clamp(mixed, 0.f, 1.f);
                }
            }
        };

        blur_channel(channel_r_, original_r_);
        blur_channel(channel_g_, original_g_);
        blur_channel(channel_b_, original_b_);
        blur_channel(channel_a_, original_a_);
    } else {
        for (int i = 0; i < total_pixels; ++i) {
            channel_r_[i] = std::clamp(channel_r_[i], 0.f, 1.f);
            channel_g_[i] = std::clamp(channel_g_[i], 0.f, 1.f);
            channel_b_[i] = std::clamp(channel_b_[i], 0.f, 1.f);
            channel_a_[i] = std::clamp(channel_a_[i], 0.f, 1.f);
        }
    }

    for (int i = 0; i < total_pixels; ++i) {
        uint8_t r = clamp_u8(channel_r_[i] * 255.f);
        uint8_t g = clamp_u8(channel_g_[i] * 255.f);
        uint8_t b = clamp_u8(channel_b_[i] * 255.f);
        uint8_t a = clamp_u8(channel_a_[i] * 255.f);
        output_pixels_[static_cast<size_t>(i)] = SDL_MapRGBA(pixel_format_, r, g, b, a);
    }

    if (SDL_UpdateTexture(upload_tex_, nullptr, output_pixels_.data(),
                          source_w * static_cast<int>(sizeof(uint32_t))) != 0) {
        return nullptr;
    }

    return upload_tex_;
}
