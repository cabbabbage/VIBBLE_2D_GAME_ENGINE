#include "asset_light_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"

namespace {
constexpr float kTwoPi = 6.28318530718f;
constexpr int   kRadialSteps = 12;
}

AssetLightRenderer::AssetLightRenderer(SDL_Renderer* renderer,
                                       const runtime_lighting::AssetLight& source,
                                       std::vector<SDL_Vertex>& scratch_vertices,
                                       std::vector<int>& scratch_indices)
    : renderer_(renderer),
      source_(source),
      asset_(source.asset),
      scratch_vertices_(scratch_vertices),
      scratch_indices_(scratch_indices) {
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

bool AssetLightRenderer::prepare_light(const LightSource& light, ComputedLight& out) const {
    const int raw_radius = light.radius;
    if (raw_radius <= 0) {
        return false;
    }

    const int intensity = std::clamp(light.intensity, 0, 255);
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

    SDL_Texture* original_target = SDL_GetRenderTarget(renderer_);
    SDL_Texture* current_target  = original_target;

    for (const LightSource& light : *lights_) {
        if (pass == Pass::kBehind) {
            if (!light.behind) {
                continue;
            }
        } else {
            if (!light.in_front) {
                continue;
            }
        }

        ComputedLight computed{};
        if (!prepare_light(light, computed)) {
            continue;
        }

        SDL_Texture* desired_target = resolve_target_for_light(light, original_target);
        const bool   using_mask     = (desired_target != original_target && desired_target != nullptr);
        if (desired_target != current_target) {
            if (!desired_target || SDL_SetRenderTarget(renderer_, desired_target) != 0) {
                SDL_SetRenderTarget(renderer_, original_target);
                current_target = original_target;
            } else {
                current_target = desired_target;
            }
        }

        SDL_Color base_color = computed.source ? computed.source->color : SDL_Color{255, 255, 255, 255};

        if (computed.textured && computed.source && computed.source->texture) {
            SDL_Rect dst = computed.texture_dst;
            if (using_mask && mask_width_ > 0 && mask_height_ > 0) {
                dst.x = static_cast<int>(std::lround(computed.texture_ratio_x * static_cast<float>(mask_width_)));
                dst.y = static_cast<int>(std::lround(computed.texture_ratio_y * static_cast<float>(mask_height_)));
                dst.w = std::max(1, static_cast<int>(std::lround(computed.texture_ratio_w * static_cast<float>(mask_width_))));
                dst.h = std::max(1, static_cast<int>(std::lround(computed.texture_ratio_h * static_cast<float>(mask_height_))));
            }
            render_textured_light(computed, dst);
            continue;
        }

        float    center_x = computed.center_x;
        float    center_y = computed.center_y;
        float    radius_x = computed.radius_x;
        float    radius_y = computed.radius_y;
        SDL_Rect bounds   = computed.bounds;
        if (using_mask && mask_width_ > 0 && mask_height_ > 0) {
            center_x = computed.center_ratio_x * static_cast<float>(mask_width_);
            center_y = computed.center_ratio_y * static_cast<float>(mask_height_);
            radius_x = computed.radius_ratio_x * static_cast<float>(mask_width_);
            radius_y = computed.radius_ratio_y * static_cast<float>(mask_height_);
            bounds.w = std::max(1, static_cast<int>(std::lround(radius_x * 2.0f)));
            bounds.h = std::max(1, static_cast<int>(std::lround(radius_y * 2.0f)));
            bounds.x = static_cast<int>(std::lround(center_x - static_cast<float>(bounds.w) * 0.5f));
            bounds.y = static_cast<int>(std::lround(center_y - static_cast<float>(bounds.h) * 0.5f));
        }

        render_radial_light(computed, base_color, 1.0f, center_x, center_y, radius_x, radius_y, bounds);
    }

    if (current_target != original_target) {
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

SDL_Texture* AssetLightRenderer::resolve_target_for_light(const LightSource& light, SDL_Texture* fallback_target) {
    if (!light.render_front_and_back_to_asset_alpha_mask) {
        return fallback_target;
    }
    SDL_Texture* mask = acquire_asset_mask_texture();
    return mask ? mask : fallback_target;
}

SDL_Texture* AssetLightRenderer::acquire_asset_mask_texture() {
    if (mask_target_resolved_) {
        return cached_mask_target_;
    }
    mask_target_resolved_ = true;
    mask_width_           = 0;
    mask_height_          = 0;
    cached_mask_target_   = nullptr;

    if (!asset_) {
        return nullptr;
    }

    SDL_Texture* texture = asset_->get_current_mask_texture();
    if (!texture) {
        return nullptr;
    }

    int access = 0;
    int tex_w  = 0;
    int tex_h  = 0;
    if (SDL_QueryTexture(texture, nullptr, &access, &tex_w, &tex_h) != 0) {
        return nullptr;
    }
    if (access != SDL_TEXTUREACCESS_TARGET) {
        return nullptr;
    }

    cached_mask_target_ = texture;
    mask_width_         = tex_w;
    mask_height_        = tex_h;
    return cached_mask_target_;
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

