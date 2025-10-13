#include "render_pipeline/render_asset/shading/RenderShadowMask.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include "asset/Asset.hpp"
#include "render/global_light_source.hpp"
#include "render_pipeline/render_asset/AssetRenderPipeline.hpp"
#include "utils/light_source.hpp"
#include "utils/light_utils.hpp"

namespace render_pipeline::shading {

namespace {

bool render_static_lights(SDL_Renderer* renderer, const Asset& asset, StageContext& context, Uint8 alpha) {
    if (asset.static_lights.empty()) {
        return false;
    }

    bool any_rendered = false;
    static std::mt19937 flicker_rng{ std::random_device{}() };

    for (const auto& static_light : asset.static_lights) {
        if (!static_light.source || !static_light.source->texture) {
            continue;
        }
        if (static_light.source->behind) {
            continue;
        }

        int lw = static_light.source->cached_w;
        int lh = static_light.source->cached_h;
        if (lw == 0 || lh == 0) {
            SDL_QueryTexture(static_light.source->texture, nullptr, nullptr, &lw, &lh);
            static_light.source->cached_w = lw;
            static_light.source->cached_h = lh;
        }

        SDL_Rect dst = context.dest_from_world_offset(static_light.offset.x, static_light.offset.y, lw, lh);

        SDL_SetTextureBlendMode(static_light.source->texture, SDL_BLENDMODE_ADD);
        float base_alpha = static_cast<float>(alpha) * static_light.alpha_percentage;
        if (static_light.source->flicker > 0) {
            const float brightness_scale = std::clamp(static_light.source->intensity / 255.0f, 0.0f, 1.0f);
            const float max_jitter       = (static_light.source->flicker / 100.0f) * brightness_scale;
            std::uniform_real_distribution<float> dist(-max_jitter, max_jitter);
            base_alpha *= (1.0f + dist(flicker_rng));
        }
        Uint8 final_alpha = static_cast<Uint8>(std::clamp(base_alpha, 0.0f, 255.0f));
        SDL_SetTextureAlphaMod(static_light.source->texture, final_alpha);
        SDL_RenderCopy(renderer, static_light.source->texture, nullptr, &dst);
        SDL_SetTextureAlphaMod(static_light.source->texture, 255);
        any_rendered = true;
    }

    return any_rendered;
}

bool render_player_lights(SDL_Renderer* renderer, const Asset& asset, StageContext& context, Uint8 alpha) {
    Asset* player = context.player();
    if (!player || !player->info) {
        return false;
    }

    bool any_rendered = false;
    const double static_factor = LightUtils::calculate_static_alpha_percentage(&asset, player);
    const double base_alpha    = static_cast<double>(alpha) * static_factor;

    for (auto& light : player->info->light_sources) {
        if (!light.texture || light.behind) {
            continue;
        }

        int lw = light.cached_w;
        int lh = light.cached_h;
        if (lw == 0 || lh == 0) {
            SDL_QueryTexture(light.texture, nullptr, nullptr, &lw, &lh);
            light.cached_w = lw;
            light.cached_h = lh;
        }

        const int world_lx = player->pos.x + light.offset_x;
        const int world_ly = player->pos.y + light.offset_y;
        const int dx_world = world_lx - asset.pos.x;
        const int dy_world = world_ly - asset.pos.y;

        SDL_Rect dst = context.dest_from_world_offset(dx_world, dy_world, lw, lh);
        SDL_SetTextureBlendMode(light.texture, SDL_BLENDMODE_ADD);
        Uint8 inten = static_cast<Uint8>(std::clamp(base_alpha, 0.0, 255.0));
        SDL_SetTextureAlphaMod(light.texture, inten);
        SDL_RenderCopy(renderer, light.texture, nullptr, &dst);
        SDL_SetTextureAlphaMod(light.texture, 255);
        any_rendered = true;
    }

    return any_rendered;
}

bool render_orbital_lights(SDL_Renderer* renderer, const Asset& asset, StageContext& context, Uint8 alpha) {
    if (!asset.info) {
        return false;
    }

    bool any_rendered = false;
    const float base_angle = context.main_light().get_angle();

    const auto compute_angle = [&](const LightSource& light) {
        if (light.apex_speed_bias <= 0) {
            return base_angle;
        }
        constexpr float kPi     = 3.14159265358979323846f;
        constexpr float kHalfPi = kPi * 0.5f;
        constexpr float kTwoPi  = kPi * 2.0f;
        float normalized        = (kHalfPi - base_angle) / kTwoPi;
        normalized -= std::floor(normalized);
        const float bias      = std::clamp(light.apex_speed_bias, 0, 100) / 100.0f;
        const float exponent  = 1.0f + bias * 4.0f;
        float       adjusted  = normalized;
        if (normalized < 0.5f) {
            const float local = normalized * 2.0f;
            adjusted          = 0.5f * std::pow(local, exponent);
        } else {
            const float local = (normalized - 0.5f) * 2.0f;
            adjusted          = 0.5f + 0.5f * (1.0f - std::pow(1.0f - local, exponent));
        }
        return kHalfPi - adjusted * kTwoPi;
};

    for (auto& light : asset.info->orbital_light_sources) {
        if (!light.texture || light.behind || light.x_radius <= 0 || light.y_radius <= 0) {
            continue;
        }

        const bool  flipped   = asset.flipped;
        const float angle     = compute_angle(light);
        const float offset_x  = flipped ? -static_cast<float>(light.offset_x) : static_cast<float>(light.offset_x);
        float       orbit_x   = std::cos(angle) * light.x_radius;
        if (flipped) {
            orbit_x = -orbit_x;
        }
        const float lx = static_cast<float>(asset.pos.x) + offset_x + orbit_x;
        const float ly = static_cast<float>(asset.pos.y) + light.offset_y - std::sin(angle) * light.y_radius;

        int lw = light.cached_w;
        int lh = light.cached_h;
        if (lw == 0 || lh == 0) {
            SDL_QueryTexture(light.texture, nullptr, nullptr, &lw, &lh);
            light.cached_w = lw;
            light.cached_h = lh;
        }

        SDL_Rect dst = context.dest_from_world_offset(static_cast<int>(std::lround(lx)) - asset.pos.x, static_cast<int>(std::lround(ly)) - asset.pos.y, lw, lh);

        SDL_SetTextureBlendMode(light.texture, SDL_BLENDMODE_ADD);
        SDL_SetTextureAlphaMod(light.texture, alpha);
        SDL_RenderCopy(renderer, light.texture, nullptr, &dst);
        SDL_SetTextureAlphaMod(light.texture, 255);
        any_rendered = true;
    }

    return any_rendered;
}

}

bool RenderShadowMask::supports(const Asset& asset) const {
    return asset.is_shaded;
}

SDL_Texture* RenderShadowMask::run(SDL_Renderer* renderer, const Asset& asset, StageContext& context) {
    if (!renderer || !asset.is_shaded) {
        return nullptr;
    }

    int width = context.width;
    int height = context.height;
    if (width <= 0 || height <= 0) {
        if (SDL_Texture* base = context.base_texture) {
            SDL_QueryTexture(base, nullptr, nullptr, &width, &height);
            context.width  = width;
            context.height = height;
        }
    }

    if (width <= 0 || height <= 0) {
        return nullptr;
    }

    auto& cache = asset.shadow_mask_cache();
    if (cache.texture) {
        int tex_w = 0;
        int tex_h = 0;
        if (SDL_QueryTexture(cache.texture, nullptr, nullptr, &tex_w, &tex_h) != 0 || tex_w != width || tex_h != height) {
            SDL_DestroyTexture(cache.texture);
            cache.texture = nullptr;
            cache.width   = 0;
            cache.height  = 0;
        }
    }

    if (!cache.texture) {
        cache.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
        if (!cache.texture) {
            cache.width  = 0;
            cache.height = 0;
            return nullptr;
        }
    }
    SDL_SetTextureBlendMode(cache.texture, SDL_BLENDMODE_BLEND);

    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, cache.texture);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 0);
    SDL_RenderClear(renderer);

    if (SDL_Texture* base = context.base_texture) {
        SDL_SetTextureBlendMode(base, SDL_BLENDMODE_BLEND);
        SDL_SetTextureColorMod(base, 0, 0, 0);
        SDL_RenderCopy(renderer, base, nullptr, nullptr);
        SDL_SetTextureColorMod(base, 255, 255, 255);
    }

    const Uint8 light_alpha = context.main_light_brightness();
    const Uint8 main_alpha  = context.main_light_alpha();

    render_static_lights(renderer, asset, context, light_alpha);
    render_player_lights(renderer, asset, context, light_alpha);
    render_orbital_lights(renderer, asset, context, main_alpha);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_MOD);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 204);
    SDL_RenderFillRect(renderer, nullptr);

    SDL_SetRenderTarget(renderer, prev_target);

    cache.width  = width;
    cache.height = height;

    return cache.texture;
}

}

