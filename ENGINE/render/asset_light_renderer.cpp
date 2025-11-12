#include "asset_light_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"

namespace {
constexpr float kTwoPi       = 6.28318530718f;
constexpr int   kRadialSteps = 12;

SDL_Rect clamp_rect_to_bounds(const SDL_Rect& rect, int width, int height) {
    SDL_Rect clamped = rect;
    const int min_x  = std::max(rect.x, 0);
    const int min_y  = std::max(rect.y, 0);
    const int max_x  = std::min(rect.x + rect.w, width);
    const int max_y  = std::min(rect.y + rect.h, height);
    clamped.x        = min_x;
    clamped.y        = min_y;
    clamped.w        = std::max(0, max_x - min_x);
    clamped.h        = std::max(0, max_y - min_y);
    return clamped;
}

SDL_BlendMode mask_alpha_multiply_blend() {
    static SDL_BlendMode cached = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO,
                                                             SDL_BLENDFACTOR_SRC_ALPHA,
                                                             SDL_BLENDOPERATION_ADD,
                                                             SDL_BLENDFACTOR_ZERO,
                                                             SDL_BLENDFACTOR_SRC_ALPHA,
                                                             SDL_BLENDOPERATION_ADD);
    return cached;
}
}

AssetLightRenderer::AssetLightRenderer(SDL_Renderer* renderer,
                                       const runtime_lighting::AssetLight& source,
                                       std::vector<SDL_Vertex>& scratch_vertices,
                                       std::vector<int>& scratch_indices,
                                       float light_visibility,
                                       float flicker_time_seconds)
    : renderer_(renderer),
      source_(source),
      asset_(source.asset),
      scratch_vertices_(scratch_vertices),
      scratch_indices_(scratch_indices),
      overlay_visibility_(std::clamp(light_visibility, 0.0f, 1.0f)),
      flicker_time_seconds_(std::isfinite(flicker_time_seconds) ? flicker_time_seconds : 0.0f) {
    if (!renderer_ || !asset_ || !asset_->info) {
        return;
    }

    const auto& lights = asset_->info->light_sources;
    if (lights.empty()) {
        return;
    }
    lights_ = &lights;

    const float base_width  = static_cast<float>(std::max(1, source.base_width));
    const float base_height = static_cast<float>(std::max(1, source.base_height));
    scale_x_                = std::isfinite(static_cast<float>(source.asset_rect.w) / base_width)
                               ? static_cast<float>(source.asset_rect.w) / base_width
                               : 1.0f;
    const float scale_y_base = std::isfinite(static_cast<float>(source.asset_rect.h) / base_height)
                                   ? static_cast<float>(source.asset_rect.h) / base_height
                                   : scale_x_;
    scale_y_                = (source.base_height > 0) ? scale_y_base : scale_x_;
    if (!std::isfinite(scale_x_) || !std::isfinite(scale_y_)) {
        lights_ = nullptr;
        return;
    }

    const float safe_base_scale =
        (std::isfinite(source.asset_base_scale) && source.asset_base_scale > 0.0f)
            ? source.asset_base_scale
            : 1.0f;
    const float zoom_scale_x = scale_x_ / safe_base_scale;
    const float zoom_scale_y = scale_y_ / safe_base_scale;
    safe_zoom_scale_x_       = (std::isfinite(zoom_scale_x) && zoom_scale_x > 0.0f) ? zoom_scale_x : 1.0f;
    safe_zoom_scale_y_       = (std::isfinite(zoom_scale_y) && zoom_scale_y > 0.0f) ? zoom_scale_y : 1.0f;

    center_base_x_ = static_cast<float>(source.asset_rect.x) + static_cast<float>(source.asset_rect.w) * 0.5f;
    center_base_y_ = static_cast<float>(source.asset_rect.y + source.asset_rect.h);

    valid_ = true;
}

AssetLightRenderer::~AssetLightRenderer() {
    if (mask_composite_texture_) {
        SDL_DestroyTexture(mask_composite_texture_);
        mask_composite_texture_ = nullptr;
    }
}

bool AssetLightRenderer::prepare_light(const LightSource& light, ComputedLight& out) const {
    const int raw_radius = light.radius;
    if (raw_radius <= 0) {
        return false;
    }

    int intensity = std::clamp(light.intensity, 0, 255);
    if (intensity <= 0) {
        return false;
    }

    const float flicker_multiplier = compute_flicker_multiplier(light);
    intensity = static_cast<int>(std::lround(static_cast<float>(intensity) * flicker_multiplier));
    intensity = std::clamp(intensity, 0, 255);
    if (intensity <= 0) {
        return false;
    }

    const float radius_base = static_cast<float>(std::max(1, raw_radius));
    const float radius_x    = std::max(1.0f, radius_base * safe_zoom_scale_x_);
    const float radius_y    = std::max(1.0f, radius_base * safe_zoom_scale_y_);
    if (!std::isfinite(radius_x) || !std::isfinite(radius_y)) {
        return false;
    }

    const float offset_x = static_cast<float>(source_.flipped ? -light.offset_x : light.offset_x);
    const float offset_y = static_cast<float>(light.offset_y);
    const float center_x = center_base_x_ + offset_x * scale_x_;
    const float center_y = center_base_y_ + offset_y * scale_y_;

    SDL_Rect dst{};
    dst.w = std::max(1, static_cast<int>(std::lround(radius_x * 2.0f)));
    dst.h = std::max(1, static_cast<int>(std::lround(radius_y * 2.0f)));
    dst.x = static_cast<int>(std::lround(center_x - static_cast<float>(dst.w) * 0.5f));
    dst.y = static_cast<int>(std::lround(center_y - static_cast<float>(dst.h) * 0.5f));

    const float falloff_norm  = std::clamp(static_cast<float>(light.fall_off) / 100.0f, 0.0f, 1.0f);
    const float fade_exponent = 0.6f + 3.4f * falloff_norm;

    out.source        = &light;
    out.intensity     = intensity;
    out.center_x      = center_x;
    out.center_y      = center_y;
    out.radius_x      = radius_x;
    out.radius_y      = radius_y;
    out.bounds        = dst;
    out.fade_exponent = fade_exponent;
    out.textured      = false;
    out.texture_dst   = SDL_Rect{0, 0, 0, 0};

    const float width_f  = static_cast<float>(std::max(1, source_.asset_rect.w));
    const float height_f = static_cast<float>(std::max(1, source_.asset_rect.h));
    out.center_ratio_x   = (center_x - static_cast<float>(source_.asset_rect.x)) / width_f;
    out.center_ratio_y   = (center_y - static_cast<float>(source_.asset_rect.y)) / height_f;
    out.radius_ratio_x   = radius_x / width_f;
    out.radius_ratio_y   = radius_y / height_f;

    if (light.texture) {
        int base_w = light.cached_w;
        int base_h = light.cached_h;
        if (base_w <= 0 || base_h <= 0) {
            SDL_QueryTexture(light.texture, nullptr, nullptr, &base_w, &base_h);
        }
        if (base_w <= 0 || base_h <= 0) {
            base_w = static_cast<int>(std::lround(radius_base * 2.0f));
            base_h = static_cast<int>(std::lround(radius_base * 2.0f));
        }

        const int scaled_w = std::max(1, static_cast<int>(std::lround(static_cast<float>(base_w) * safe_zoom_scale_x_)));
        const int scaled_h = std::max(1, static_cast<int>(std::lround(static_cast<float>(base_h) * safe_zoom_scale_y_)));

        SDL_Rect tex_dst{};
        tex_dst.w = scaled_w;
        tex_dst.h = scaled_h;
        tex_dst.x = static_cast<int>(std::lround(center_x - static_cast<float>(tex_dst.w) * 0.5f));
        tex_dst.y = static_cast<int>(std::lround(center_y - static_cast<float>(tex_dst.h) * 0.5f));

        out.textured        = true;
        out.texture_dst     = tex_dst;
        out.texture_ratio_x = (static_cast<float>(tex_dst.x) - static_cast<float>(source_.asset_rect.x)) / width_f;
        out.texture_ratio_y = (static_cast<float>(tex_dst.y) - static_cast<float>(source_.asset_rect.y)) / height_f;
        out.texture_ratio_w = static_cast<float>(tex_dst.w) / width_f;
        out.texture_ratio_h = static_cast<float>(tex_dst.h) / height_f;
    }

    return true;
}

void AssetLightRenderer::draw_pass(Pass pass) {
    if (!valid_ || !lights_) {
        return;
    }

    if (overlay_visibility_ <= 0.0f) {
        return;
    }

    SDL_Texture* original_target = SDL_GetRenderTarget(renderer_);

    for (const LightSource& light : *lights_) {
        // Render lights only on passes they are configured for.
        // Mask-only lights with no explicit front/behind flags default to the behind pass.
        const bool mask_only_light = light.render_front_and_back_to_asset_alpha_mask &&
                                     !light.in_front && !light.behind;
        const bool should_render_in_behind_pass = light.behind || mask_only_light;
        if ((pass == Pass::kBehind && !should_render_in_behind_pass) ||
            (pass == Pass::kFront  && !light.in_front)) {
            continue;
        }

        ComputedLight computed{};
        if (!prepare_light(light, computed)) {
            continue;
        }

        computed.intensity = static_cast<int>(
            std::lround(static_cast<float>(computed.intensity) * overlay_visibility_));
        computed.intensity = std::clamp(computed.intensity, 0, 255);
        if (computed.intensity <= 0) {
            continue;
        }

        SDL_Color base_color = computed.source ? computed.source->color : SDL_Color{255, 255, 255, 255};

        bool handled_with_mask = false;
        if (light.render_front_and_back_to_asset_alpha_mask) {
            handled_with_mask = render_light_with_asset_mask(light, computed, base_color);
        }

        if (handled_with_mask) {
            if (original_target != SDL_GetRenderTarget(renderer_)) {
                SDL_SetRenderTarget(renderer_, original_target);
            }
            continue;
        }

        if (original_target != SDL_GetRenderTarget(renderer_)) {
            SDL_SetRenderTarget(renderer_, original_target);
        }

        if (computed.textured && computed.source && computed.source->texture) {
            render_textured_light(computed, computed.texture_dst);
            continue;
        }

        render_radial_light(computed,
                            base_color,
                            1.0f,
                            computed.center_x,
                            computed.center_y,
                            computed.radius_x,
                            computed.radius_y,
                            computed.bounds);
    }

    if (original_target != SDL_GetRenderTarget(renderer_)) {
        SDL_SetRenderTarget(renderer_, original_target);
    }
}

void AssetLightRenderer::render_textured_light(const ComputedLight& info, const SDL_Rect& dst) {
    if (!info.source || !info.source->texture || dst.w <= 0 || dst.h <= 0) {
        return;
    }

    SDL_Texture* tex = info.source->texture;
    Uint8        prev_a = 255;
    Uint8        prev_r = 255;
    Uint8        prev_g = 255;
    Uint8        prev_b = 255;
    SDL_BlendMode prev_blend = SDL_BLENDMODE_BLEND;
    SDL_GetTextureAlphaMod(tex, &prev_a);
    SDL_GetTextureColorMod(tex, &prev_r, &prev_g, &prev_b);
    SDL_GetTextureBlendMode(tex, &prev_blend);

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(info.intensity));
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    SDL_SetTextureAlphaMod(tex, prev_a);
    SDL_SetTextureColorMod(tex, prev_r, prev_g, prev_b);
    SDL_SetTextureBlendMode(tex, prev_blend);
}

void AssetLightRenderer::render_radial_light(const ComputedLight& info,
                                             const SDL_Color&     base_color,
                                             float                alpha_scale,
                                             float                center_x,
                                             float                center_y,
                                             float                radius_x,
                                             float                radius_y,
                                             const SDL_Rect&      fallback_rect) {
    if (!(radius_x > 0.0f) || !(radius_y > 0.0f)) {
        return;
    }

    const float radius_hint   = std::max(radius_x, radius_y);
    const int   angular_steps = std::clamp(static_cast<int>(std::ceil(radius_hint / 6.0f)), 16, 64);

    const std::size_t desired_vertex_capacity =
        static_cast<std::size_t>((kRadialSteps + 1) * (angular_steps + 1));
    const std::size_t desired_index_capacity =
        static_cast<std::size_t>(kRadialSteps * angular_steps * 6);

    scratch_vertices_.clear();
    scratch_indices_.clear();
    if (desired_vertex_capacity > scratch_vertices_.capacity()) {
        scratch_vertices_.reserve(desired_vertex_capacity);
    }
    if (desired_index_capacity > scratch_indices_.capacity()) {
        scratch_indices_.reserve(desired_index_capacity);
    }

    for (int ring = 0; ring <= kRadialSteps; ++ring) {
        const float ring_ratio = static_cast<float>(ring) / static_cast<float>(kRadialSteps);
        const float base       = std::max(0.0f, 1.0f - ring_ratio);
        float       alpha_ratio = std::pow(base, info.fade_exponent);
        alpha_ratio             = std::clamp(alpha_ratio, 0.0f, 1.0f);
        const float scaled_alpha = std::min(255.0f, static_cast<float>(info.intensity) * alpha_ratio * alpha_scale);
        const Uint8 alpha        = static_cast<Uint8>(std::clamp(std::lround(scaled_alpha), 0L, 255L));

        for (int step = 0; step <= angular_steps; ++step) {
            const float angle = (static_cast<float>(step) / static_cast<float>(angular_steps)) * kTwoPi;
            const float px    = center_x + std::cos(angle) * radius_x * ring_ratio;
            const float py    = center_y + std::sin(angle) * radius_y * ring_ratio;

            SDL_Vertex vertex{};
            vertex.position.x = px;
            vertex.position.y = py;
            vertex.color      = SDL_Color{ base_color.r, base_color.g, base_color.b, alpha };
            vertex.tex_coord  = SDL_FPoint{ 0.0f, 0.0f };
            scratch_vertices_.push_back(vertex);
        }
    }

    const int stride = angular_steps + 1;
    for (int ring = 0; ring < kRadialSteps; ++ring) {
        for (int step = 0; step < angular_steps; ++step) {
            const int current = ring * stride + step;
            const int next    = current + stride;

            scratch_indices_.push_back(current);
            scratch_indices_.push_back(next);
            scratch_indices_.push_back(current + 1);

            scratch_indices_.push_back(current + 1);
            scratch_indices_.push_back(next);
            scratch_indices_.push_back(next + 1);
        }
    }

    if (SDL_RenderGeometry(renderer_, nullptr,
                           scratch_vertices_.data(), static_cast<int>(scratch_vertices_.size()),
                           scratch_indices_.data(), static_cast<int>(scratch_indices_.size())) != 0) {
        const Uint8 fallback_alpha = static_cast<Uint8>(std::clamp(
            std::lround(static_cast<float>(info.intensity) * alpha_scale), 0L, 255L));
        SDL_SetRenderDrawColor(renderer_, base_color.r, base_color.g, base_color.b, fallback_alpha);
        SDL_RenderFillRect(renderer_, &fallback_rect);
    }
}

bool AssetLightRenderer::render_light_with_asset_mask(const LightSource& light,
                                                      const ComputedLight& computed,
                                                      const SDL_Color& base_color) {
    (void)light;
    if (!renderer_ || !asset_) {
        return false;
    }

    SDL_Texture* mask = asset_->get_current_mask_texture();
    if (!mask) {
        return false;
    }

    Uint32 mask_format = 0;
    int    mask_w      = 0;
    int    mask_h      = 0;
    if (SDL_QueryTexture(mask, &mask_format, nullptr, &mask_w, &mask_h) != 0 || mask_w <= 0 || mask_h <= 0) {
        return false;
    }

    SDL_Texture* composite = ensure_mask_composite_texture(mask_w, mask_h, mask_format);
    if (!composite) {
        return false;
    }

    SDL_Texture* saved_target = SDL_GetRenderTarget(renderer_);
    if (SDL_SetRenderTarget(renderer_, composite) != 0) {
        SDL_SetRenderTarget(renderer_, saved_target);
        return false;
    }

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
    SDL_RenderClear(renderer_);

    SDL_Rect mask_space_rect{0, 0, mask_w, mask_h};
    SDL_Rect light_rect{0, 0, 0, 0};

    if (computed.textured && computed.source && computed.source->texture) {
        SDL_Rect dst = computed.texture_dst;
        dst.x = static_cast<int>(std::lround(computed.texture_ratio_x * static_cast<float>(mask_space_rect.w)));
        dst.y = static_cast<int>(std::lround(computed.texture_ratio_y * static_cast<float>(mask_space_rect.h)));
        dst.w = std::max(1, static_cast<int>(std::lround(computed.texture_ratio_w * static_cast<float>(mask_space_rect.w))));
        dst.h = std::max(1, static_cast<int>(std::lround(computed.texture_ratio_h * static_cast<float>(mask_space_rect.h))));
        render_textured_light(computed, dst);
        light_rect = dst;
    } else {
        const float center_x = computed.center_ratio_x * static_cast<float>(mask_space_rect.w);
        const float center_y = computed.center_ratio_y * static_cast<float>(mask_space_rect.h);
        const float radius_x = computed.radius_ratio_x * static_cast<float>(mask_space_rect.w);
        const float radius_y = computed.radius_ratio_y * static_cast<float>(mask_space_rect.h);

        SDL_Rect bounds{};
        bounds.w = std::max(1, static_cast<int>(std::lround(radius_x * 2.0f)));
        bounds.h = std::max(1, static_cast<int>(std::lround(radius_y * 2.0f)));
        bounds.x = static_cast<int>(std::lround(center_x - static_cast<float>(bounds.w) * 0.5f));
        bounds.y = static_cast<int>(std::lround(center_y - static_cast<float>(bounds.h) * 0.5f));

        render_radial_light(computed,
                            base_color,
                            1.0f,
                            center_x,
                            center_y,
                            radius_x,
                            radius_y,
                            bounds);
        light_rect = bounds;
    }

    SDL_Rect clipped_src = clamp_rect_to_bounds(light_rect, mask_space_rect.w, mask_space_rect.h);

    if (clipped_src.w <= 0 || clipped_src.h <= 0) {
        SDL_SetRenderTarget(renderer_, saved_target);
        return true;
    }

    SDL_BlendMode prev_mask_blend = SDL_BLENDMODE_BLEND;
    SDL_GetTextureBlendMode(mask, &prev_mask_blend);
    Uint8 prev_r = 255;
    Uint8 prev_g = 255;
    Uint8 prev_b = 255;
    Uint8 prev_a = 255;
    SDL_GetTextureColorMod(mask, &prev_r, &prev_g, &prev_b);
    SDL_GetTextureAlphaMod(mask, &prev_a);

    SDL_SetTextureColorMod(mask, 255, 255, 255);
    SDL_SetTextureAlphaMod(mask, 255);
    SDL_SetTextureBlendMode(mask, mask_alpha_multiply_blend());
    SDL_RenderCopy(renderer_, mask, &clipped_src, &clipped_src);
    SDL_SetTextureBlendMode(mask, prev_mask_blend);
    SDL_SetTextureColorMod(mask, prev_r, prev_g, prev_b);
    SDL_SetTextureAlphaMod(mask, prev_a);

    SDL_SetRenderTarget(renderer_, saved_target);

    SDL_Rect dst_rect = scale_mask_rect_to_asset(clipped_src, mask_space_rect.w, mask_space_rect.h);
    if (dst_rect.w <= 0 || dst_rect.h <= 0) {
        return true;
    }

    SDL_RenderCopy(renderer_, composite, &clipped_src, &dst_rect);
    return true;
}

float AssetLightRenderer::compute_flicker_multiplier(const LightSource& light) const {
    const float speed_setting =
        std::clamp(static_cast<float>(light.flicker_speed), 0.0f, 100.0f) / 100.0f;
    const float smooth_setting =
        std::clamp(static_cast<float>(light.flicker_smoothness), 0.0f, 100.0f) / 100.0f;

    if (speed_setting <= 0.001f) {
        return 1.0f;
    }

    auto mix = [](std::uint32_t seed, int value) {
        seed ^= static_cast<std::uint32_t>(value) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        return seed;
    };

    std::uint32_t hash = 0x811C9DC5u;
    hash = mix(hash, light.offset_x);
    hash = mix(hash, light.offset_y);
    hash = mix(hash, light.radius);
    hash = mix(hash, light.intensity);
    hash = mix(hash, light.fall_off);
    hash = mix(hash, static_cast<int>(reinterpret_cast<std::uintptr_t>(light.texture) & 0xFFFFu));

    const float phase  = static_cast<float>(hash & 0xFFFFu) / 65535.0f * kTwoPi;
    const float wobble = static_cast<float>((hash >> 16) & 0xFFu) / 255.0f;
    const float slow_speed =
        (0.6f + 3.4f * speed_setting) * (0.7f + 0.3f * (1.0f - smooth_setting));
    const float fast_speed =
        slow_speed * (0.6f + 0.8f * (1.0f - smooth_setting)) + 0.35f * (1.0f + wobble);

    const float time_a = flicker_time_seconds_ * slow_speed + phase;
    const float time_b =
        flicker_time_seconds_ * fast_speed + phase * 0.61f + wobble * (kTwoPi * 0.75f);

    float smooth_component = 0.7f * std::sin(time_a) + 0.3f * std::sin(time_b);

    auto random_value = [&](int idx) {
        const std::uint32_t jitter_hash = mix(hash, idx);
        return static_cast<float>(jitter_hash & 0xFFFFu) / 32767.5f - 1.0f;
    };

    const float jitter_rate = (90.0f + 260.0f * speed_setting);
    const float jitter_time =
        flicker_time_seconds_ * jitter_rate + phase * 0.37f + wobble * (kTwoPi * 0.8f);
    const int jitter_index = static_cast<int>(std::floor(jitter_time));
    const float jitter_frac = jitter_time - static_cast<float>(jitter_index);

    const float rand_a = random_value(jitter_index);
    const float rand_b = random_value(jitter_index + 1);

    float interpolation = 0.0f;
    if (smooth_setting > 0.0f) {
        const float eased =
            std::pow(std::clamp(jitter_frac, 0.0f, 1.0f), 0.35f + 0.65f * smooth_setting);
        interpolation = std::clamp(eased, 0.0f, 1.0f);
    }

    const float jitter_component =
        (smooth_setting <= 0.0f) ? rand_a : (rand_a + (rand_b - rand_a) * interpolation);

    const float random_mix = 1.0f - smooth_setting;
    const float smooth_mix = 0.35f + 0.65f * smooth_setting;
    float noise            = smooth_component * smooth_mix + jitter_component * random_mix;
    noise                  = std::clamp(noise, -1.0f, 1.0f);

    const float amplitude   = 0.12f + 0.45f * speed_setting;
    const float multiplier  = 1.0f + noise * amplitude;
    return std::clamp(multiplier, 0.2f, 1.0f + amplitude);
}

SDL_Texture* AssetLightRenderer::ensure_mask_composite_texture(int width, int height, Uint32 format_hint) {
    if (width <= 0 || height <= 0 || !renderer_) {
        return nullptr;
    }

    if (mask_composite_texture_) {
        if (mask_composite_w_ != width || mask_composite_h_ != height ||
            (format_hint != 0 && mask_composite_format_ != format_hint)) {
            SDL_DestroyTexture(mask_composite_texture_);
            mask_composite_texture_ = nullptr;
            mask_composite_w_       = 0;
            mask_composite_h_       = 0;
            mask_composite_format_  = 0;
        }
    }

    if (!mask_composite_texture_) {
        Uint32 format = format_hint ? format_hint : SDL_PIXELFORMAT_RGBA8888;
        mask_composite_texture_ = SDL_CreateTexture(renderer_, format, SDL_TEXTUREACCESS_TARGET, width, height);
        if (!mask_composite_texture_ && format != SDL_PIXELFORMAT_RGBA8888) {
            format = SDL_PIXELFORMAT_RGBA8888;
            mask_composite_texture_ =
                SDL_CreateTexture(renderer_, format, SDL_TEXTUREACCESS_TARGET, width, height);
        }
        if (!mask_composite_texture_) {
            return nullptr;
        }
        SDL_SetTextureBlendMode(mask_composite_texture_, SDL_BLENDMODE_BLEND);
        mask_composite_w_      = width;
        mask_composite_h_      = height;
        mask_composite_format_ = format;
    }

    return mask_composite_texture_;
}

SDL_Rect AssetLightRenderer::scale_mask_rect_to_asset(const SDL_Rect& rect, int mask_width, int mask_height) const {
    SDL_Rect result{0, 0, 0, 0};
    if (mask_width <= 0 || mask_height <= 0) {
        return result;
    }

    SDL_Rect asset_rect = source_.asset_rect;
    const float scale_x = static_cast<float>(asset_rect.w) / static_cast<float>(mask_width);
    const float scale_y = static_cast<float>(asset_rect.h) / static_cast<float>(mask_height);

    result.x = asset_rect.x + static_cast<int>(std::lround(static_cast<float>(rect.x) * scale_x));
    result.y = asset_rect.y + static_cast<int>(std::lround(static_cast<float>(rect.y) * scale_y));
    result.w = std::max(0, static_cast<int>(std::lround(static_cast<float>(rect.w) * scale_x)));
    result.h = std::max(0, static_cast<int>(std::lround(static_cast<float>(rect.h) * scale_y)));
    return result;
}

AssetLightRenderer::DarkMaskResult AssetLightRenderer::accumulate_dark_mask() {
    DarkMaskResult result{};
    if (!valid_ || !lights_) {
        return result;
    }

    for (const LightSource& light : *lights_) {
        if (!light.render_to_dark_mask) {
            continue;
        }
        ComputedLight computed{};
        if (!prepare_light(light, computed)) {
            continue;
        }

        const float radius_hint   = std::max(computed.radius_x, computed.radius_y);
        const int   angular_steps = std::clamp(static_cast<int>(std::ceil(radius_hint / 6.0f)), 16, 64);
        const std::size_t desired_vertex_capacity =
            static_cast<std::size_t>((kRadialSteps + 1) * (angular_steps + 1));
        const std::size_t desired_index_capacity =
            static_cast<std::size_t>(kRadialSteps * angular_steps * 6);
        result.max_vertices = std::max(result.max_vertices, desired_vertex_capacity);
        result.max_indices  = std::max(result.max_indices, desired_index_capacity);

        SDL_Color base_color{0, 0, 0, 255};
        render_radial_light(computed, base_color, 1.6f,
                            computed.center_x, computed.center_y,
                            computed.radius_x, computed.radius_y,
                            computed.bounds);
    }

    return result;
}

void AssetLightRenderer::draw_behind() { draw_pass(Pass::kBehind); }

void AssetLightRenderer::draw_in_front() { draw_pass(Pass::kFront); }

