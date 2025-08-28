
#include "light_map.hpp"
#include "utils/parallax.hpp"
#include <algorithm>
#include <random>
#include <vector>
#include <iostream>

LightMap::LightMap(SDL_Renderer* renderer,
                   Assets* assets,
                   Parallax& parallax,
                   Global_Light_Source& main_light,
                   int screen_width,
                   int screen_height,
                   SDL_Texture* fullscreen_light_tex)
    : renderer_(renderer),
      assets_(assets),
      parallax_(parallax),
      main_light_(main_light),
      screen_width_(screen_width),
      screen_height_(screen_height),
      fullscreen_light_tex_(fullscreen_light_tex)
{}

void LightMap::render(bool debugging) {
    if (debugging) std::cout << "[render_asset_lights_z] start\n";

    static std::mt19937 flicker_rng{ std::random_device{}() };
    static std::vector<LightEntry> z_lights;
    z_lights.clear();

    collect_layers(z_lights, flicker_rng);

    const int downscale = 4;
    const int low_w = screen_width_  / downscale;
    const int low_h = screen_height_ / downscale;

    SDL_Texture* lowres_mask = build_lowres_mask(z_lights, low_w, low_h, downscale);

    SDL_SetTextureBlendMode(lowres_mask, SDL_BLENDMODE_MOD);
    SDL_SetRenderTarget(renderer_, nullptr);
    SDL_RenderCopy(renderer_, lowres_mask, nullptr, nullptr);

    SDL_DestroyTexture(lowres_mask);

    if (debugging) std::cout << "[render_asset_lights_z] end\n";
}

void LightMap::collect_layers(std::vector<LightEntry>& out, std::mt19937& rng) {
    const float inv_scale = 1.0f / assets_->getView().get_scale();
    constexpr int min_visible_w = 1;
    constexpr int min_visible_h = 1;

    Uint8 main_alpha = main_light_.get_current_color().a;

    if (fullscreen_light_tex_) {
        out.push_back({ fullscreen_light_tex_, { 0, 0, screen_width_, screen_height_ },
                        static_cast<Uint8>(main_alpha / 2), SDL_FLIP_NONE, false });
    }

    if (SDL_Texture* map_tex = main_light_.get_texture()) {
        int lw = main_light_.get_cached_w();
        int lh = main_light_.get_cached_h();

        if (lw == 0 || lh == 0) SDL_QueryTexture(map_tex, nullptr, nullptr, &lw, &lh);

        SDL_Rect map_rect = get_scaled_position_rect(main_light_.get_position(),
                                                     lw, lh, inv_scale,
                                                     min_visible_w, min_visible_h);
        if (map_rect.w != 0 || map_rect.h != 0) {
            out.push_back({ map_tex, map_rect, main_alpha, SDL_FLIP_NONE, false });
        }
    }

    for (Asset* a : assets_->active_assets) {
        if (!a || !a->info || !a->info->has_light_source) continue;

        for (const auto& light : a->info->light_sources) {
            if (!light.texture) continue;

            int offX = a->flipped ? -light.offset_x : light.offset_x;
            int lw = light.cached_w, lh = light.cached_h;
            if (lw == 0 || lh == 0) SDL_QueryTexture(light.texture, nullptr, nullptr, &lw, &lh);

            SDL_Rect dst = get_scaled_position_rect({ a->pos_X + offX, a->pos_Y + light.offset_y },
                                                    lw, lh, inv_scale,
                                                    min_visible_w, min_visible_h);
            if (dst.w == 0 && dst.h == 0) continue;

            float alpha_f = static_cast<float>(main_light_.get_brightness());
            if (a == assets_->player) alpha_f *= 0.9f;

            if (light.flicker > 0) {
                float intensity_scale = std::clamp(light.intensity / 255.0f, 0.0f, 1.0f);
                float max_jitter = (light.flicker / 100.0f) * intensity_scale;
                alpha_f *= (1.0f + std::uniform_real_distribution<float>(-max_jitter, max_jitter)(rng));
            }

            Uint8 alpha = static_cast<Uint8>(std::clamp(alpha_f, 0.0f, 255.0f));
            out.push_back({ light.texture, dst, alpha,
                            a->flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE, true });
        }
    }
}

SDL_Texture* LightMap::build_lowres_mask(const std::vector<LightEntry>& layers,
                                         int low_w, int low_h, int downscale) {
    SDL_Texture* lowres_mask = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                                 SDL_TEXTUREACCESS_TARGET, low_w, low_h);
    SDL_SetTextureBlendMode(lowres_mask, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(renderer_, lowres_mask);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 200);
    SDL_RenderClear(renderer_);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);

    for (auto& e : layers) {
        SDL_SetTextureBlendMode(e.tex, SDL_BLENDMODE_ADD);
        SDL_SetTextureAlphaMod(e.tex, e.alpha);

        if (e.apply_tint) {
            SDL_Color tinted = main_light_.apply_tint_to_color({255, 255, 255, 220}, e.alpha);
            SDL_SetTextureColorMod(e.tex, tinted.r, tinted.g, tinted.b);
        } else {
            SDL_SetTextureColorMod(e.tex, 255, 255, 220);
        }

        SDL_Rect scaled_dst{
            e.dst.x / downscale,
            e.dst.y / downscale,
            e.dst.w / downscale,
            e.dst.h / downscale
        };
        SDL_RenderCopyEx(renderer_, e.tex, nullptr, &scaled_dst, 0, nullptr, e.flip);
    }

    return lowres_mask;
}

SDL_Rect LightMap::get_scaled_position_rect(const std::pair<int,int>& pos, int fw, int fh,
                                            float inv_scale, int min_w, int min_h) {
    int sw = static_cast<int>(fw * inv_scale);
    int sh = static_cast<int>(fh * inv_scale);
    if (sw < min_w && sh < min_h) {
        return {0, 0, 0, 0};
    }

    
    SDL_Point cp = parallax_.apply(pos.first, pos.second);
    cp.x = screen_width_ / 2 + static_cast<int>((cp.x - screen_width_ / 2) * inv_scale);
    cp.y = screen_height_ / 2 + static_cast<int>((cp.y - screen_height_ / 2) * inv_scale);

    return SDL_Rect{ cp.x - sw / 2, cp.y - sh / 2, sw, sh };
}
