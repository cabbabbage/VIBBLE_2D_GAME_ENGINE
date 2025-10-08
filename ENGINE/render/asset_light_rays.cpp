#include "asset_light_rays.hpp"

#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "render/gaussian_blur.hpp"
#include "utils/light_source.hpp"

#include <algorithm>
#include <cmath>

AssetLightRaysRenderer::AssetLightRaysRenderer(SDL_Renderer* renderer)
    : renderer_(renderer),
      blur_helper_(renderer ? std::make_unique<GaussianBlurHelper>(renderer) : nullptr) {}

void AssetLightRaysRenderer::set_renderer(SDL_Renderer* renderer) {
    renderer_ = renderer;
    if (blur_helper_) {
        blur_helper_->set_renderer(renderer);
    } else if (renderer) {
        blur_helper_ = std::make_unique<GaussianBlurHelper>(renderer);
    }
}

void AssetLightRaysRenderer::set_enabled(bool enabled) {
    enabled_ = enabled;
}

void AssetLightRaysRenderer::set_blur_settings(float radius, float mix) {
    blur_radius_ = radius;
    blur_mix_ = mix;
}

void AssetLightRaysRenderer::render_before_asset(Asset* asset,
                                                 const SDL_Rect& asset_screen_rect,
                                                 int base_width,
                                                 int base_height,
                                                 SDL_RendererFlip flip_mode) {
    if (!renderer_ || !enabled_ || !asset || !asset->info) {
        return;
    }
    if (asset_screen_rect.w <= 0 || asset_screen_rect.h <= 0) {
        return;
    }
    if (base_width <= 0 || base_height <= 0) {
        return;
    }
    if (asset->info->light_sources.empty()) {
        return;
    }

    const float scale_x = asset_screen_rect.w / static_cast<float>(base_width);
    const float scale_y = asset_screen_rect.h / static_cast<float>(base_height);
    if (!std::isfinite(scale_x) || !std::isfinite(scale_y)) {
        return;
    }

    for (auto& light : asset->info->light_sources) {
        if (!light.texture) {
            continue;
        }

        int light_w = light.cached_w;
        int light_h = light.cached_h;
        if (light_w <= 0 || light_h <= 0) {
            if (SDL_QueryTexture(light.texture, nullptr, nullptr, &light_w, &light_h) != 0) {
                continue;
            }
            light.cached_w = light_w;
            light.cached_h = light_h;
        }

        SDL_Texture* rays_texture = light.texture;
        if (blur_helper_ && blur_radius_ > 0.f && blur_mix_ > 0.f) {
            SDL_Texture* blurred = blur_helper_->apply(light.texture,
                                                       light_w,
                                                       light_h,
                                                       blur_radius_,
                                                       blur_mix_);
            if (blurred) {
                rays_texture = blurred;
            }
        }
        if (!rays_texture) {
            continue;
        }

        const int base_reach = std::max(0, light.radius) + std::max(0, light.flare);
        const float intensity_factor = std::clamp(light.intensity / 255.f, 0.f, 1.f);
        const float reach_scale = 0.35f + 0.65f * intensity_factor;
        int scaled_reach = static_cast<int>(std::ceil(base_reach * reach_scale));
        if (scaled_reach <= 0) {
            scaled_reach = std::max(light_w, light_h) / 2;
        }

        int local_offset_x = light.offset_x;
        if (asset->flipped) {
            local_offset_x = -local_offset_x;
        }
        const int local_offset_y = light.offset_y;

        const float local_center_x = static_cast<float>(base_width) * 0.5f + static_cast<float>(local_offset_x);
        const float local_center_y = static_cast<float>(base_height) + static_cast<float>(local_offset_y);

        const float center_screen_x = asset_screen_rect.x + scale_x * local_center_x;
        const float center_screen_y = asset_screen_rect.y + scale_y * local_center_y;

        const float dest_w = std::max(1.f, scale_x * static_cast<float>(light_w));
        const float dest_h = std::max(1.f, scale_y * static_cast<float>(light_h));

        const int expand_x = static_cast<int>(std::ceil(std::abs(scale_x) * static_cast<float>(scaled_reach)));
        const int expand_y = static_cast<int>(std::ceil(std::abs(scale_y) * static_cast<float>(scaled_reach)));

        SDL_Rect dst{
            static_cast<int>(std::round(center_screen_x - dest_w * 0.5f)) - expand_x,
            static_cast<int>(std::round(center_screen_y - dest_h * 0.5f)) - expand_y,
            static_cast<int>(std::round(dest_w)) + expand_x * 2,
            static_cast<int>(std::round(dest_h)) + expand_y * 2
        };

        SDL_SetTextureBlendMode(rays_texture, SDL_BLENDMODE_ADD);
        const Uint8 alpha_mod = static_cast<Uint8>(std::clamp(light.intensity, 0, 255));
        SDL_SetTextureAlphaMod(rays_texture, alpha_mod);
        SDL_RenderCopyEx(renderer_, rays_texture, nullptr, &dst, 0.0, nullptr, flip_mode);
    }
}
