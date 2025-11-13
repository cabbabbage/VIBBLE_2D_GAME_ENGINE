#include "render/depth_cue_effects.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
float mix(float a, float b, float t) {
    return a + (b - a) * t;
}

float compute_luma(float r, float g, float b) {
    return (0.2126f * r) + (0.7152f * g) + (0.0722f * b);
}

void apply_grayscale_mix(float& r, float& g, float& b, float strength) {
    if (strength <= 0.0f) {
        return;
    }
    const float gray = compute_luma(r, g, b);
    r = mix(r, gray, strength);
    g = mix(g, gray, strength);
    b = mix(b, gray, strength);
}

void apply_rby_channel_filter(float& r, float& g, float& b, float strength) {
    if (strength <= 0.0f) {
        return;
    }
    const float yellow_component = std::min(r, g);
    const float target_g = yellow_component;
    g = mix(g, target_g, strength);
}
}  // namespace

DepthCueEffects::DepthCueEffects(SDL_Renderer* renderer)
    : renderer_(renderer) {}

DepthCueEffects::~DepthCueEffects() {
    clear_cache();
}

void DepthCueEffects::set_renderer(SDL_Renderer* renderer) {
    renderer_ = renderer;
}

Uint8 DepthCueEffects::clamp_to_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return static_cast<Uint8>(v);
}

std::uint64_t DepthCueEffects::pack_color_key(int sat_percent, int primary_percent, int brightness_percent) {
    const std::uint64_t a = static_cast<std::uint64_t>(sat_percent + 100) & 0xFFFFu;
    const std::uint64_t b = static_cast<std::uint64_t>(primary_percent + 100) & 0xFFFFu;
    const std::uint64_t c = static_cast<std::uint64_t>(brightness_percent + 100) & 0xFFFFu;
    return (a << 32) | (b << 16) | c;
}

void DepthCueEffects::compute_rows(int height,
                                   float center_screen_y,
                                   float fg_plane_screen_y,
                                   float bg_plane_screen_y,
                                   const camera::RealismSettings& cam_settings,
                                   bool compute_blur,
                                   std::vector<Row>& rows_out) const {
    rows_out.assign(static_cast<std::size_t>(std::max(0, height)), {});
    const float fg_span_screen = std::max(0.0f, fg_plane_screen_y - center_screen_y);
    const float bg_span_screen = std::max(0.0f, center_screen_y - bg_plane_screen_y);
    for (int y = 0; y < height; ++y) {
        Row rc{};
        depth_cue::DepthSample sample;
        const float sy = static_cast<float>(y);
        if (std::fabs(sy - center_screen_y) > depth_cue::kDepthCueDeadzonePx) {
            if (sy > center_screen_y) {
                sample.side = depth_cue::DepthSide::Foreground;
                sample.t = (sy >= fg_plane_screen_y || fg_span_screen <= 0.0f)
                    ? 1.0f
                    : std::clamp((sy - center_screen_y) / fg_span_screen, 0.0f, 1.0f);
            } else {
                sample.side = depth_cue::DepthSide::Background;
                sample.t = (sy <= bg_plane_screen_y || bg_span_screen <= 0.0f)
                    ? 1.0f
                    : std::clamp((center_screen_y - sy) / bg_span_screen, 0.0f, 1.0f);
            }
        }

        const float bright_pct = depth_cue::sample_signed_effect(
            sample,
            cam_settings.foreground_brightness,
            cam_settings.background_brightness,
            cam_settings.brightness_falloff_method);
        rc.brightness_offset = std::clamp(bright_pct / 100.0f, -0.5f, 0.5f);

        const float combined_pct = depth_cue::sample_signed_effect(
            sample,
            cam_settings.saturation_foreground,
            cam_settings.saturation_background,
            cam_settings.saturation_falloff_method);
        const float combined_off = std::clamp(combined_pct / 100.0f, -0.5f, 0.5f);
        rc.saturation_offset = std::min(0.0f, combined_off);
        rc.primary_offset    = std::max(0.0f, combined_off);

        if (compute_blur) {
            const float br = depth_cue::evaluate_depth_curve(cam_settings.blur_falloff_method, sample.t) *
                (sample.is_foreground() ? std::max(0.0f, cam_settings.max_foreground_blur)
                                        : std::max(0.0f, cam_settings.max_background_blur));
            rc.blur_radius_px = std::clamp(br, 0.0f, 50.0f);
            rc.blur_mix = std::clamp(0.45f + (rc.blur_radius_px / 80.0f), 0.45f, 0.9f);
        }
        rows_out[static_cast<std::size_t>(y)] = rc;
    }
}

SDL_Texture* DepthCueEffects::apply_color_pass(SDL_Texture* source,
                                               int width,
                                               int height,
                                               const std::vector<Row>& rows,
                                               SDL_Texture* reusable_out) const {
    if (!renderer_ || !source || width <= 0 || height <= 0) {
        return nullptr;
    }
    if (static_cast<std::size_t>(height) > rows.size()) {
        return nullptr;
    }

    std::vector<Uint32> pixels(static_cast<std::size_t>(width) * height);
    Uint32 fmt = 0; int access = 0; int tw = 0; int th = 0;
    SDL_QueryTexture(source, &fmt, &access, &tw, &th);
    if (access == SDL_TEXTUREACCESS_STREAMING) {
        void* p = nullptr; int pitch = 0;
        if (SDL_LockTexture(source, nullptr, &p, &pitch) != 0) {
            return nullptr;
        }
        for (int y = 0; y < height; ++y) {
            std::memcpy(&pixels[static_cast<std::size_t>(y) * width],
                        reinterpret_cast<const Uint8*>(p) + static_cast<std::size_t>(y) * pitch,
                        static_cast<std::size_t>(width) * sizeof(Uint32));
        }
        SDL_UnlockTexture(source);
    } else {
        SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
        SDL_SetRenderTarget(renderer_, source);
        SDL_Rect rr{0, 0, width, height};
        if (SDL_RenderReadPixels(renderer_, &rr, SDL_PIXELFORMAT_RGBA8888,
                                 pixels.data(), width * static_cast<int>(sizeof(Uint32))) != 0) {
            SDL_SetRenderTarget(renderer_, prev);
            return nullptr;
        }
        SDL_SetRenderTarget(renderer_, prev);
    }

    for (int y = 0; y < height; ++y) {
        const Row& row = rows[static_cast<std::size_t>(y)];
        const float grayscale_mix = (row.saturation_offset < -1e-6f)
            ? std::clamp(-row.saturation_offset * 2.0f, 0.0f, 1.0f)
            : 0.0f;
        const float rby_mix = (row.primary_offset > 1e-6f)
            ? std::clamp(row.primary_offset * 2.0f, 0.0f, 1.0f)
            : 0.0f;
        const bool need_grayscale = grayscale_mix > 1e-6f;
        const bool need_rby = rby_mix > 1e-6f;
        const bool need_contrast = std::fabs(row.brightness_offset) > 1e-6f;
        if (!need_rby && !need_grayscale && !need_contrast) {
            continue;
        }
        Uint32* line = &pixels[static_cast<std::size_t>(y) * width];
        for (int x = 0; x < width; ++x) {
            Uint8* c = reinterpret_cast<Uint8*>(&line[x]);
            if (c[3] == 0) {
                c[0] = 0; c[1] = 0; c[2] = 0;
                continue;
            }
            const Uint8 orig_a = c[3];
            float r = static_cast<float>(c[0]) / 255.0f;
            float g = static_cast<float>(c[1]) / 255.0f;
            float b = static_cast<float>(c[2]) / 255.0f;
            if (need_rby) {
                apply_rby_channel_filter(r, g, b, rby_mix);
            }
            if (need_grayscale) {
                apply_grayscale_mix(r, g, b, grayscale_mix);
            }
            if (need_contrast) {
                const float coff = std::clamp(row.brightness_offset, -0.5f, 0.5f); // repurposed as contrast amount
                const float scale = std::max(0.0f, 1.0f + 2.0f * coff); // [-0.5..0.5] -> [0..2]
                r = (r - 0.5f) * scale + 0.5f;
                g = (g - 0.5f) * scale + 0.5f;
                b = (b - 0.5f) * scale + 0.5f;
            }
            c[0] = static_cast<Uint8>(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
            c[1] = static_cast<Uint8>(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f);
            c[2] = static_cast<Uint8>(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f);
            c[3] = orig_a;
        }
    }

    SDL_Texture* out = reusable_out;
    if (!out) {
        out = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!out) {
            return nullptr;
        }
        SDL_SetTextureBlendMode(out, SDL_BLENDMODE_BLEND);
    }
    if (SDL_UpdateTexture(out, nullptr, pixels.data(), width * static_cast<int>(sizeof(Uint32))) != 0) {
        return nullptr;
    }
    return out;
}

SDL_Texture* DepthCueEffects::apply_variable_blur(SDL_Texture* source,
                                                  int width,
                                                  int height,
                                                  const std::vector<Row>& rows,
                                                  SDL_Texture* reusable_blur_out) const {
    if (!renderer_ || !source || width <= 0 || height <= 0) {
        return nullptr;
    }
    if (static_cast<std::size_t>(height) > rows.size()) {
        return nullptr;
    }

    std::vector<Uint32> src(static_cast<std::size_t>(width) * height);
    Uint32 fmt = 0; int access = 0; int tw = 0; int th = 0;
    SDL_QueryTexture(source, &fmt, &access, &tw, &th);
    if (access == SDL_TEXTUREACCESS_STREAMING) {
        void* p = nullptr; int pitch = 0;
        if (SDL_LockTexture(source, nullptr, &p, &pitch) != 0) {
            return nullptr;
        }
        for (int y = 0; y < height; ++y) {
            std::memcpy(&src[static_cast<std::size_t>(y) * width],
                        reinterpret_cast<const Uint8*>(p) + static_cast<std::size_t>(y) * pitch,
                        static_cast<std::size_t>(width) * sizeof(Uint32));
        }
        SDL_UnlockTexture(source);
    } else {
        SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
        SDL_SetRenderTarget(renderer_, source);
        SDL_Rect rr{0, 0, width, height};
        if (SDL_RenderReadPixels(renderer_, &rr, SDL_PIXELFORMAT_RGBA8888,
                                 src.data(), width * static_cast<int>(sizeof(Uint32))) != 0) {
            SDL_SetRenderTarget(renderer_, prev);
            return nullptr;
        }
        SDL_SetRenderTarget(renderer_, prev);
    }

    // Fast separable triangular blur (two box blurs) ignoring transparency.
    // We approximate a Gaussian by convolving two variable-radius box filters per axis.

    // Horizontal pass 1 (radius r1 per row)
    std::vector<Uint32> h1(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        const float r_in = std::max(0.0f, rows[static_cast<std::size_t>(y)].blur_radius_px);
        const int R = static_cast<int>(std::round(r_in));
        const int r1 = R > 0 ? (R / 2) : 0;
        const Uint32* line = &src[static_cast<std::size_t>(y) * width];
        Uint32* out = &h1[static_cast<std::size_t>(y) * width];
        if (r1 <= 0) {
            std::memcpy(out, line, sizeof(Uint32) * static_cast<std::size_t>(width));
            continue;
        }
        std::vector<int> pr(width + 1, 0), pg(width + 1, 0), pb(width + 1, 0);
        for (int x = 0; x < width; ++x) {
            const Uint8* c = reinterpret_cast<const Uint8*>(&line[x]);
            pr[x + 1] = pr[x] + c[0];
            pg[x + 1] = pg[x] + c[1];
            pb[x + 1] = pb[x] + c[2];
        }
        for (int x = 0; x < width; ++x) {
            const int x0 = std::max(0, x - r1);
            const int x1 = std::min(width - 1, x + r1);
            const int n  = (x1 - x0 + 1);
            Uint8* d = reinterpret_cast<Uint8*>(&out[x]);
            const int sr = pr[x1 + 1] - pr[x0];
            const int sg = pg[x1 + 1] - pg[x0];
            const int sb = pb[x1 + 1] - pb[x0];
            d[0] = clamp_to_u8((sr + n / 2) / n);
            d[1] = clamp_to_u8((sg + n / 2) / n);
            d[2] = clamp_to_u8((sb + n / 2) / n);
            d[3] = reinterpret_cast<const Uint8*>(&line[x])[3];
        }
    }

    // Horizontal pass 2 (radius r2 per row)
    std::vector<Uint32> h2(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        const float r_in = std::max(0.0f, rows[static_cast<std::size_t>(y)].blur_radius_px);
        const int R = static_cast<int>(std::round(r_in));
        const int r1 = R > 0 ? (R / 2) : 0;
        const int r2 = std::max(0, R - r1);
        const Uint32* line = &h1[static_cast<std::size_t>(y) * width];
        Uint32* out = &h2[static_cast<std::size_t>(y) * width];
        if (r2 <= 0) {
            std::memcpy(out, line, sizeof(Uint32) * static_cast<std::size_t>(width));
            continue;
        }
        std::vector<int> pr(width + 1, 0), pg(width + 1, 0), pb(width + 1, 0);
        for (int x = 0; x < width; ++x) {
            const Uint8* c = reinterpret_cast<const Uint8*>(&line[x]);
            pr[x + 1] = pr[x] + c[0];
            pg[x + 1] = pg[x] + c[1];
            pb[x + 1] = pb[x] + c[2];
        }
        for (int x = 0; x < width; ++x) {
            const int x0 = std::max(0, x - r2);
            const int x1 = std::min(width - 1, x + r2);
            const int n  = (x1 - x0 + 1);
            Uint8* d = reinterpret_cast<Uint8*>(&out[x]);
            const int sr = pr[x1 + 1] - pr[x0];
            const int sg = pg[x1 + 1] - pg[x0];
            const int sb = pb[x1 + 1] - pb[x0];
            d[0] = clamp_to_u8((sr + n / 2) / n);
            d[1] = clamp_to_u8((sg + n / 2) / n);
            d[2] = clamp_to_u8((sb + n / 2) / n);
            d[3] = reinterpret_cast<const Uint8*>(&line[x])[3];
        }
    }

    // Vertical pass 1 (radius r1(y) per destination row)
    std::vector<Uint32> v1(static_cast<std::size_t>(width) * height);
    {
        std::vector<int> pr(height + 1), pg(height + 1), pb(height + 1);
        for (int x = 0; x < width; ++x) {
            pr[0] = pg[0] = pb[0] = 0;
            for (int y = 0; y < height; ++y) {
                const Uint8* c = reinterpret_cast<const Uint8*>(&h2[static_cast<std::size_t>(y) * width + x]);
                pr[y + 1] = pr[y] + c[0];
                pg[y + 1] = pg[y] + c[1];
                pb[y + 1] = pb[y] + c[2];
            }
            for (int y = 0; y < height; ++y) {
                const float r_in = std::max(0.0f, rows[static_cast<std::size_t>(y)].blur_radius_px);
                const int R = static_cast<int>(std::round(r_in));
                const int r1 = R > 0 ? (R / 2) : 0;
                Uint8* d = reinterpret_cast<Uint8*>(&v1[static_cast<std::size_t>(y) * width + x]);
                if (r1 <= 0) {
                    const Uint8* s = reinterpret_cast<const Uint8*>(&h2[static_cast<std::size_t>(y) * width + x]);
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                    continue;
                }
                const int y0 = std::max(0, y - r1);
                const int y1 = std::min(height - 1, y + r1);
                const int n  = (y1 - y0 + 1);
                const int sr = pr[y1 + 1] - pr[y0];
                const int sg = pg[y1 + 1] - pg[y0];
                const int sb = pb[y1 + 1] - pb[y0];
                d[0] = clamp_to_u8((sr + n / 2) / n);
                d[1] = clamp_to_u8((sg + n / 2) / n);
                d[2] = clamp_to_u8((sb + n / 2) / n);
                d[3] = reinterpret_cast<const Uint8*>(&h2[static_cast<std::size_t>(y) * width + x])[3];
            }
        }
    }

    // Vertical pass 2 (radius r2(y) per destination row)
    std::vector<Uint32> blurred(static_cast<std::size_t>(width) * height);
    {
        std::vector<int> pr(height + 1), pg(height + 1), pb(height + 1);
        for (int x = 0; x < width; ++x) {
            pr[0] = pg[0] = pb[0] = 0;
            for (int y = 0; y < height; ++y) {
                const Uint8* c = reinterpret_cast<const Uint8*>(&v1[static_cast<std::size_t>(y) * width + x]);
                pr[y + 1] = pr[y] + c[0];
                pg[y + 1] = pg[y] + c[1];
                pb[y + 1] = pb[y] + c[2];
            }
            for (int y = 0; y < height; ++y) {
                const float r_in = std::max(0.0f, rows[static_cast<std::size_t>(y)].blur_radius_px);
                const int R = static_cast<int>(std::round(r_in));
                const int r1 = R > 0 ? (R / 2) : 0;
                const int r2 = std::max(0, R - r1);
                Uint8* d = reinterpret_cast<Uint8*>(&blurred[static_cast<std::size_t>(y) * width + x]);
                if (r2 <= 0) {
                    const Uint8* s = reinterpret_cast<const Uint8*>(&v1[static_cast<std::size_t>(y) * width + x]);
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                    continue;
                }
                const int y0 = std::max(0, y - r2);
                const int y1 = std::min(height - 1, y + r2);
                const int n  = (y1 - y0 + 1);
                const int sr = pr[y1 + 1] - pr[y0];
                const int sg = pg[y1 + 1] - pg[y0];
                const int sb = pb[y1 + 1] - pb[y0];
                d[0] = clamp_to_u8((sr + n / 2) / n);
                d[1] = clamp_to_u8((sg + n / 2) / n);
                d[2] = clamp_to_u8((sb + n / 2) / n);
                d[3] = reinterpret_cast<const Uint8*>(&v1[static_cast<std::size_t>(y) * width + x])[3];
            }
        }
    }

    std::vector<Uint32> final_px(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        const float mixv = std::clamp(rows[static_cast<std::size_t>(y)].blur_mix, 0.0f, 1.0f);
        const int w_mix = static_cast<int>(std::round(mixv * 256.0f));
        for (int x = 0; x < width; ++x) {
            const Uint8* s = reinterpret_cast<const Uint8*>(&src[static_cast<std::size_t>(y) * width + x]);
            const Uint8* b = reinterpret_cast<const Uint8*>(&blurred[static_cast<std::size_t>(y) * width + x]);
            Uint8* d = reinterpret_cast<Uint8*>(&final_px[static_cast<std::size_t>(y) * width + x]);
            if (s[3] == 0) {
                d[0] = 0; d[1] = 0; d[2] = 0; d[3] = 0;
                continue;
            }
            d[0] = clamp_to_u8(((256 - w_mix) * s[0] + w_mix * b[0]) >> 8);
            d[1] = clamp_to_u8(((256 - w_mix) * s[1] + w_mix * b[1]) >> 8);
            d[2] = clamp_to_u8(((256 - w_mix) * s[2] + w_mix * b[2]) >> 8);
            d[3] = s[3];
        }
    }

    SDL_Texture* out = reusable_blur_out;
    if (!out) {
        out = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!out) {
            return nullptr;
        }
        SDL_SetTextureBlendMode(out, SDL_BLENDMODE_BLEND);
    }
    if (SDL_UpdateTexture(out, nullptr, final_px.data(), width * static_cast<int>(sizeof(Uint32))) != 0) {
        return nullptr;
    }
    return out;
}

SDL_Texture* DepthCueEffects::build_color_texture(SDL_Texture* source,
                                                  float saturation_percent,
                                                  float primary_percent,
                                                  float brightness_percent) const {
    if (!renderer_) {
        return nullptr;
    }
    return build_color_texture_internal(source, saturation_percent, primary_percent, brightness_percent);
}

SDL_Texture* DepthCueEffects::build_color_texture_internal(SDL_Texture* source,
                                                           float saturation_percent,
                                                           float primary_percent,
                                                           float brightness_percent) const {
    if (!renderer_ || !source) {
        return nullptr;
    }
    saturation_percent = std::clamp(saturation_percent, -50.0f, 50.0f);
    primary_percent    = std::clamp(primary_percent, -50.0f, 50.0f);
    brightness_percent = std::clamp(brightness_percent, -50.0f, 50.0f);
    const float sat_offset     = saturation_percent / 100.0f;
    const float primary_offset = primary_percent / 100.0f;
    const float contrast_offset = brightness_percent / 100.0f; // repurposed input value
    constexpr float kEffectEpsilon = 0.0001f;
    const float grayscale_mix = (sat_offset < 0.0f)
        ? std::clamp(-sat_offset * 2.0f, 0.0f, 1.0f)
        : 0.0f;
    const float rby_mix_from_sat = (sat_offset > 0.0f)
        ? std::clamp(sat_offset * 2.0f, 0.0f, 1.0f)
        : 0.0f;
    const float rby_mix_from_primary = (primary_offset > 0.0f)
        ? std::clamp(primary_offset * 2.0f, 0.0f, 1.0f)
        : 0.0f;
    const float rby_mix = std::max(rby_mix_from_sat, rby_mix_from_primary);
    const bool grayscale_needed = grayscale_mix > kEffectEpsilon;
    const bool rby_needed = rby_mix > kEffectEpsilon;
    const bool contrast_needed = std::fabs(contrast_offset) > kEffectEpsilon;
    if (!grayscale_needed && !rby_needed && !contrast_needed) {
        return nullptr;
    }
    int tex_w = 0;
    int tex_h = 0;
    Uint32 format = 0;
    int access = 0;
    if (SDL_QueryTexture(source, &format, &access, &tex_w, &tex_h) != 0) {
        return nullptr;
    }
    if (tex_w <= 0 || tex_h <= 0) {
        return nullptr;
    }
    SDL_Texture* readable = source;
    SDL_Texture* temp_target = nullptr;
    SDL_Texture* prev_target = nullptr;
    SDL_BlendMode saved_blend = SDL_BLENDMODE_NONE;
    if (access != SDL_TEXTUREACCESS_TARGET) {
        temp_target = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, tex_w, tex_h);
        if (!temp_target) {
            return nullptr;
        }
        prev_target = SDL_GetRenderTarget(renderer_);
        SDL_SetRenderTarget(renderer_, temp_target);
        SDL_GetTextureBlendMode(source, &saved_blend);
        SDL_SetTextureBlendMode(source, SDL_BLENDMODE_NONE);
        SDL_RenderCopy(renderer_, source, nullptr, nullptr);
        SDL_SetTextureBlendMode(source, saved_blend);
        SDL_SetRenderTarget(renderer_, prev_target);
        readable = temp_target;
    }
    std::vector<Uint32> pixels(static_cast<std::size_t>(tex_w) * tex_h);
    prev_target = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, readable);
    SDL_Rect read_rect{ 0, 0, tex_w, tex_h };
    if (SDL_RenderReadPixels(renderer_, &read_rect, SDL_PIXELFORMAT_RGBA8888, pixels.data(), tex_w * static_cast<int>(sizeof(Uint32))) != 0) {
        SDL_SetRenderTarget(renderer_, prev_target);
        if (temp_target) SDL_DestroyTexture(temp_target);
        return nullptr;
    }
    SDL_SetRenderTarget(renderer_, prev_target);
    if (temp_target) {
        SDL_DestroyTexture(temp_target);
    }

    for (Uint32& pixel : pixels) {
        Uint8* c = reinterpret_cast<Uint8*>(&pixel);
        if (c[3] == 0) {
            c[0] = 0; c[1] = 0; c[2] = 0;
            continue;
        }
        const Uint8 orig_a = c[3];
        float r = static_cast<float>(c[0]) / 255.0f;
        float g = static_cast<float>(c[1]) / 255.0f;
        float b = static_cast<float>(c[2]) / 255.0f;
        if (rby_needed) {
            apply_rby_channel_filter(r, g, b, rby_mix);
        }
        if (grayscale_needed) {
            apply_grayscale_mix(r, g, b, grayscale_mix);
        }
        if (contrast_needed) {
            const float coff = std::clamp(contrast_offset, -0.5f, 0.5f);
            const float scale = std::max(0.0f, 1.0f + 2.0f * coff);
            r = (r - 0.5f) * scale + 0.5f;
            g = (g - 0.5f) * scale + 0.5f;
            b = (b - 0.5f) * scale + 0.5f;
        }
        c[0] = static_cast<Uint8>(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
        c[1] = static_cast<Uint8>(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f);
        c[2] = static_cast<Uint8>(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f);
        c[3] = orig_a;
    }

    SDL_Texture* result = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, tex_w, tex_h);
    if (!result) {
        return nullptr;
    }
    if (SDL_UpdateTexture(result, nullptr, pixels.data(), tex_w * static_cast<int>(sizeof(Uint32))) != 0) {
        SDL_DestroyTexture(result);
        return nullptr;
    }
    SDL_SetTextureBlendMode(result, SDL_BLENDMODE_BLEND);
    return result;
}

SDL_Texture* DepthCueEffects::get_or_build_tinted_texture(SDL_Texture* source,
                                                          float saturation_percent,
                                                          float primary_percent,
                                                          float brightness_percent,
                                                          std::uint64_t frame_counter) {
    if (!renderer_ || !source) {
        return nullptr;
    }
    const int sat_q = std::clamp(static_cast<int>(std::round(saturation_percent)), -50, 50);
    const int pri_q = std::clamp(static_cast<int>(std::round(primary_percent)), -50, 50);
    const int bri_q = std::clamp(static_cast<int>(std::round(brightness_percent)), -50, 50);
    if (sat_q == 0 && pri_q == 0 && bri_q == 0) {
        return nullptr;
    }
    const std::uint64_t key = pack_color_key(sat_q, pri_q, bri_q);
    auto& variants = tinted_texture_cache_[source];
    if (auto it = variants.find(key); it != variants.end() && it->second.texture) {
        it->second.last_used_frame = frame_counter;
        return it->second.texture;
    }
    SDL_Texture* built = build_color_texture_internal(
        source,
        static_cast<float>(sat_q),
        static_cast<float>(pri_q),
        static_cast<float>(bri_q));
    if (!built) {
        return nullptr;
    }
    variants[key] = TintedTextureEntry{ built, frame_counter };
    return built;
}

void DepthCueEffects::prune_tinted_cache(std::uint64_t current_frame) {
    static constexpr std::uint64_t kTTLFrames = 240;
    for (auto it_src = tinted_texture_cache_.begin(); it_src != tinted_texture_cache_.end();) {
        auto& variants = it_src->second;
        for (auto it = variants.begin(); it != variants.end();) {
            if (current_frame > it->second.last_used_frame + kTTLFrames) {
                if (it->second.texture) {
                    SDL_DestroyTexture(it->second.texture);
                    it->second.texture = nullptr;
                }
                it = variants.erase(it);
            } else {
                ++it;
            }
        }
        if (variants.empty()) {
            it_src = tinted_texture_cache_.erase(it_src);
        } else {
            ++it_src;
        }
    }
}

void DepthCueEffects::clear_cache() {
    for (auto& by_source : tinted_texture_cache_) {
        for (auto& kv : by_source.second) {
            if (kv.second.texture) {
                SDL_DestroyTexture(kv.second.texture);
                kv.second.texture = nullptr;
            }
        }
    }
    tinted_texture_cache_.clear();
}
