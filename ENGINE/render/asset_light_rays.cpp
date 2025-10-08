#include "asset_light_rays.hpp"

#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "utils/light_source.hpp"

#include <algorithm>
#include <cmath>

namespace {
inline float safe_div(float numerator, float denominator) {
    if (denominator == 0.0f) return 0.0f;
    return numerator / denominator;
}
}

AssetLightRaysRenderer::AssetLightRaysRenderer(SDL_Renderer* renderer)
    : renderer_(renderer) {}

void AssetLightRaysRenderer::set_renderer(SDL_Renderer* renderer) {
    renderer_ = renderer;
}

void AssetLightRaysRenderer::set_enabled(bool enabled) { enabled_ = enabled; }
void AssetLightRaysRenderer::set_blur_settings(float radius, float mix) {
    blur_radius_ = radius;
    blur_mix_ = mix;
}

void AssetLightRaysRenderer::render_before_asset(Asset* asset,
                                                 const SDL_Rect& asset_screen_rect,
                                                 int base_width,
                                                 int base_height,
                                                 SDL_RendererFlip flip_mode) {
    (void)blur_radius_;
    (void)blur_mix_;

    if (!renderer_ || !enabled_) return;
    if (!asset || !asset->info) return;
    if (asset_screen_rect.w <= 0 || asset_screen_rect.h <= 0) return;
    if (base_width <= 0 || base_height <= 0) return;
    if (asset->info->light_sources.empty()) return;

    const float scale_x = safe_div(static_cast<float>(asset_screen_rect.w), static_cast<float>(base_width));
    const float scale_y = safe_div(static_cast<float>(asset_screen_rect.h), static_cast<float>(base_height));

    for (auto& light : asset->info->light_sources) {
        if (!light.texture) continue;

        int lw = light.cached_w;
        int lh = light.cached_h;
        if (lw <= 0 || lh <= 0) {
            if (SDL_QueryTexture(light.texture, nullptr, nullptr, &lw, &lh) != 0) {
                continue;
            }
            light.cached_w = lw;
            light.cached_h = lh;
        }

        const int local_offset_x = asset->flipped ? -light.offset_x : light.offset_x;
        const int local_offset_y = light.offset_y;

        const float center_local_x = static_cast<float>(base_width) * 0.5f + static_cast<float>(local_offset_x);
        const float center_local_y = static_cast<float>(base_height) + static_cast<float>(local_offset_y);

        const float screen_center_x = static_cast<float>(asset_screen_rect.x) + scale_x * center_local_x;
        const float screen_center_y = static_cast<float>(asset_screen_rect.y) + scale_y * center_local_y;

        const float scale_abs_x = std::abs(scale_x);
        const float scale_abs_y = std::abs(scale_y);
        const float dst_w_f = std::max(1.0f, scale_abs_x * static_cast<float>(lw));
        const float dst_h_f = std::max(1.0f, scale_abs_y * static_cast<float>(lh));

        SDL_Rect dst{
            static_cast<int>(std::round(screen_center_x - dst_w_f * 0.5f)),
            static_cast<int>(std::round(screen_center_y - dst_h_f * 0.5f)),
            static_cast<int>(std::round(dst_w_f)),
            static_cast<int>(std::round(dst_h_f))
        };

        Uint8 prev_alpha = 255;
        Uint8 prev_r = 255, prev_g = 255, prev_b = 255;
        SDL_BlendMode prev_blend = SDL_BLENDMODE_BLEND;

        SDL_GetTextureAlphaMod(light.texture, &prev_alpha);
        SDL_GetTextureColorMod(light.texture, &prev_r, &prev_g, &prev_b);
        SDL_GetTextureBlendMode(light.texture, &prev_blend);

        const Uint8 alpha_mod = clamp_u8_(static_cast<float>(std::clamp(light.intensity, 0, 255)));
        SDL_SetTextureBlendMode(light.texture, SDL_BLENDMODE_ADD);
        SDL_SetTextureColorMod(light.texture, 255, 255, 255);
        SDL_SetTextureAlphaMod(light.texture, alpha_mod);

        SDL_RenderCopyEx(renderer_, light.texture, nullptr, &dst, 0.0, nullptr, flip_mode);

        SDL_SetTextureAlphaMod(light.texture, prev_alpha);
        SDL_SetTextureColorMod(light.texture, prev_r, prev_g, prev_b);
        SDL_SetTextureBlendMode(light.texture, prev_blend);
    }
}
