#include "scene_renderer.hpp"
#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "light_map.hpp"
#include "render/camera.hpp"
#include "render_area.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <tuple>
#include <vector>
#include <cstdint>
#include <initializer_list>
#include <array>

static constexpr SDL_Color SLATE_COLOR = {69, 101, 74, 255};
static constexpr float MIN_VISIBLE_SCREEN_RATIO = 0.015f;

namespace {
constexpr std::array<SDL_Point, 9> HIGHLIGHT_OFFSETS = {
        SDL_Point{ 0,  0}, SDL_Point{ 1,  0}, SDL_Point{-1,  0},
        SDL_Point{ 0,  1}, SDL_Point{ 0, -1}, SDL_Point{ 1,  1},
        SDL_Point{-1,  1}, SDL_Point{ 1, -1}, SDL_Point{-1, -1}
};
}

SceneRenderer::SceneRenderer(SDL_Renderer* renderer,
                             Assets* assets,
                             int screen_width,
                             int screen_height,
                             const std::string& map_path)
: map_path_(map_path),
  renderer_(renderer),
  assets_(assets),
  screen_width_(screen_width),
  screen_height_(screen_height),
  main_light_source_(renderer, SDL_Point{ screen_width / 2, screen_height / 2 },
                     screen_width, SDL_Color{255, 255, 255, 255}, map_path),
  fullscreen_light_tex_(nullptr),
  render_asset_(renderer, assets, assets->getView(), main_light_source_, assets->player)
{
        low_quality_mode_ = assets_ && assets_->is_dev_mode();
	fullscreen_light_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, screen_width_, screen_height_);
	if (fullscreen_light_tex_) {
		SDL_SetTextureBlendMode(fullscreen_light_tex_, SDL_BLENDMODE_BLEND);
		SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
		SDL_SetRenderTarget(renderer_, fullscreen_light_tex_);
		SDL_Color color = main_light_source_.get_current_color();
		SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
		SDL_RenderClear(renderer_);
		SDL_SetRenderTarget(renderer_, prev);
	} else {
		std::cerr << "[SceneRenderer] Failed to create fullscreen light texture: "
		          << SDL_GetError() << "\n";
	}

        z_light_pass_ = std::make_unique<LightMap>(renderer_, assets_, main_light_source_, screen_width_, screen_height_, fullscreen_light_tex_);
        main_light_source_.update();
        if (!low_quality_mode_ && z_light_pass_) {
                z_light_pass_->render(debugging);
        }
}

SceneRenderer::~SceneRenderer() {
        if (fullscreen_light_tex_) {
                SDL_DestroyTexture(fullscreen_light_tex_);
                fullscreen_light_tex_ = nullptr;
        }
        if (scene_target_tex_) {
                SDL_DestroyTexture(scene_target_tex_);
                scene_target_tex_ = nullptr;
        }

}

SDL_Renderer* SceneRenderer::get_renderer() const {
    return renderer_;
}

void SceneRenderer::set_low_quality_rendering(bool low_quality) {
        low_quality_mode_ = low_quality;
}

void SceneRenderer::apply_map_light_config(const nlohmann::json& data) {
        main_light_source_.apply_config(data);
        if (!renderer_ || !fullscreen_light_tex_) {
                return;
        }
        SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
        SDL_SetRenderTarget(renderer_, fullscreen_light_tex_);
        SDL_Color color = main_light_source_.get_current_color();
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        SDL_RenderClear(renderer_);
        SDL_SetRenderTarget(renderer_, prev);
}

void SceneRenderer::update_shading_groups() {
        ++current_shading_group_;
        if (current_shading_group_ > num_groups_)
                current_shading_group_ = 1;
}

bool SceneRenderer::shouldRegen(Asset* a) {
	if (!a->get_final_texture()) { return true; }
	return (a->get_shading_group() > 0 &&
	        a->get_shading_group() == current_shading_group_) || (!a->get_final_texture() || !a->static_frame || a->get_render_player_light());
}

SDL_Rect SceneRenderer::get_scaled_position_rect(Asset* a,
                                                 int fw,
                                                 int fh,
                                                 float inv_scale,
                                                 int min_w,
                                                 int min_h,
                                                 float reference_screen_height) {
        float base_sw = static_cast<float>(fw) * inv_scale;
        float base_sh = static_cast<float>(fh) * inv_scale;

        const camera::RenderEffects effects = assets_->getView().compute_render_effects(
            SDL_Point{a->pos.x, a->pos.y}, base_sh, reference_screen_height);

        float scaled_sw = base_sw * effects.distance_scale;
        float scaled_sh = base_sh * effects.distance_scale;
        float final_visible_h = scaled_sh * effects.vertical_scale;

        if (scaled_sw < min_w && final_visible_h < min_h) {
                return {0, 0, 0, 0};
        }

        int sw = static_cast<int>(std::round(scaled_sw));
        int sh = static_cast<int>(std::round(final_visible_h));
        sw = std::max(sw, 1);
        sh = std::max(sh, 1);

        if (sw < min_w && sh < min_h) {
                return {0, 0, 0, 0};
        }

        const SDL_Point& cp = effects.screen_position;
        return SDL_Rect{ cp.x - sw / 2, cp.y - sh, sw, sh };
}
void SceneRenderer::render() {
    static int render_call_count = 0;
    ++render_call_count;

    update_shading_groups();
    main_light_source_.update();

    auto ensure_target = [&](SDL_Texture*& tex, int w, int h) {
        if (low_quality_mode_) {
            if (tex) {
                SDL_DestroyTexture(tex);
                tex = nullptr;
            }
            return false;
        }
        int tw = 0, th = 0; Uint32 fmt = 0; int access = 0;
        if (tex && SDL_QueryTexture(tex, &fmt, &access, &tw, &th) == 0) {
            if (tw == w && th == h && access == SDL_TEXTUREACCESS_TARGET) return true;
            SDL_DestroyTexture(tex); tex = nullptr;
        }
        tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
        if (!tex) return false;
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        #if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(tex, low_quality_mode_ ? SDL_ScaleModeNearest : SDL_ScaleModeBest);
        #endif
        return true;
};
    if (!ensure_target(scene_target_tex_, screen_width_, screen_height_)) {

        SDL_SetRenderTarget(renderer_, nullptr);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, SLATE_COLOR.r, SLATE_COLOR.g, SLATE_COLOR.b, 255);
        SDL_RenderClear(renderer_);
    } else {
        SDL_SetRenderTarget(renderer_, scene_target_tex_);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, SLATE_COLOR.r, SLATE_COLOR.g, SLATE_COLOR.b, 255);
        SDL_RenderClear(renderer_);
    }

    const auto& camera_state = assets_->getView();
    const bool debug_render_areas = camera_state.render_areas_enabled();
    float scale = camera_state.get_scale();
    float inv_scale = 1.0f / scale;
    int min_visible_w = static_cast<int>(screen_width_  * MIN_VISIBLE_SCREEN_RATIO);
    int min_visible_h = static_cast<int>(screen_height_ * MIN_VISIBLE_SCREEN_RATIO);

    float player_screen_height = 1.0f;
    Asset* player_asset = assets_ ? assets_->player : nullptr;
    if (player_asset) {
        SDL_Texture* player_final = player_asset->get_final_texture();
        SDL_Texture* player_frame = player_asset->get_current_frame();
        int pw = player_asset->cached_w, ph = player_asset->cached_h;
        if ((pw == 0 || ph == 0) && player_final) SDL_QueryTexture(player_final, nullptr, nullptr, &pw, &ph);
        if ((pw == 0 || ph == 0) && player_frame) SDL_QueryTexture(player_frame, nullptr, nullptr, &pw, &ph);
        if (pw != 0) player_asset->cached_w = pw;
        if (ph != 0) player_asset->cached_h = ph;
        if (ph > 0) player_screen_height = static_cast<float>(ph) * inv_scale;
    }
    if (player_screen_height <= 0.0f) player_screen_height = 1.0f;

    const auto& active_assets = assets_->getActive();
    const float highlight_pulse = 0.45f + 0.55f * std::sin(render_call_count * 0.18f);

    struct AreaOverlayRequest {
        Asset* asset = nullptr;
        float asset_screen_height = 0.0f;
    };
    std::vector<AreaOverlayRequest> area_requests;
    if (debug_render_areas) {
        area_requests.reserve(active_assets.size());
    }

    for (Asset* a : active_assets) {
        if (!a || !a->info) continue;

        SDL_Texture* final_tex = a->get_final_texture();
        if (shouldRegen(a)) {
            SDL_Texture* previous_final = final_tex;
            final_tex = render_asset_.regenerateFinalTexture(a);
            if (!final_tex) {
                final_tex = previous_final;
            } else if (final_tex != previous_final) {
                a->set_final_texture(final_tex);
            }
        }
        if (!final_tex) continue;

        int fw = a->cached_w, fh = a->cached_h;
        if (fw == 0 || fh == 0) {
            SDL_QueryTexture(final_tex, nullptr, nullptr, &fw, &fh);
            a->cached_w = fw; a->cached_h = fh;
        }

        SDL_Rect fb = get_scaled_position_rect(a, fw, fh, inv_scale, min_visible_w, min_visible_h, player_screen_height);
        if (fb.w == 0 && fb.h == 0) continue;

        SDL_Texture* draw_tex = render_asset_.texture_for_scale(a, final_tex, fw, fh, fb.w, fb.h, scale);
        SDL_Texture* mod_target = draw_tex ? draw_tex : final_tex;

        const bool is_highlighted = a->is_highlighted();
        const bool is_selected   = a->is_selected();
        const SDL_RendererFlip flip_mode = a->flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;

        if (is_highlighted || is_selected) {
            SDL_BlendMode previous_blend_mode = SDL_BLENDMODE_BLEND;
            SDL_GetTextureBlendMode(mod_target, &previous_blend_mode);

            Uint8 prev_r = 255, prev_g = 255, prev_b = 255, prev_a = 255;
            SDL_GetTextureColorMod(mod_target, &prev_r, &prev_g, &prev_b);
            SDL_GetTextureAlphaMod(mod_target, &prev_a);

            SDL_Rect glow_rect = fb;
            const int min_dimension = std::max(1, std::min(fb.w, fb.h));
            const int glow_margin = std::max( 8, static_cast<int>(std::round(min_dimension * 0.2f)));
            glow_rect.x -= glow_margin;
            glow_rect.y -= glow_margin;
            glow_rect.w += glow_margin * 2;
            glow_rect.h += glow_margin * 2;

            auto apply_tinted_copy = [&](const SDL_Color& color, SDL_Rect rect) {
                SDL_SetTextureColorMod(mod_target, color.r, color.g, color.b);
                SDL_SetTextureAlphaMod(mod_target, color.a);
                SDL_RenderCopyEx(renderer_, mod_target, nullptr, &rect, 0, nullptr, flip_mode);
};

            SDL_SetTextureBlendMode(mod_target, SDL_BLENDMODE_ADD);

            const Uint8 base_alpha = static_cast<Uint8>(std::clamp(160.f + 70.f * highlight_pulse, 0.f, 255.f));
            SDL_Color outer_color = is_highlighted
                                        ? SDL_Color{90, 220, 255, base_alpha}
                                        : SDL_Color{255, 185, 60, base_alpha};
            if (is_highlighted && is_selected) {
                outer_color = SDL_Color{255, 255, 255,
                                        static_cast<Uint8>(std::clamp(190.f + 60.f * highlight_pulse, 0.f, 255.f))};
            }

            const int offset = glow_margin / 2;
            for (const SDL_Point& pt : HIGHLIGHT_OFFSETS) {
                SDL_Rect rect = glow_rect;
                rect.x += pt.x * offset;
                rect.y += pt.y * offset;
                apply_tinted_copy(outer_color, rect);
            }

            if (is_selected) {
                SDL_SetTextureBlendMode(mod_target, SDL_BLENDMODE_BLEND);
                Uint8 inner_alpha = static_cast<Uint8>(std::clamp(150.f + 80.f * highlight_pulse, 0.f, 255.f));
                SDL_Color inner_color = is_highlighted
                                            ? SDL_Color{255, 245, 200, inner_alpha}
                                            : SDL_Color{255, 215, 120, inner_alpha};
                apply_tinted_copy(inner_color, fb);
            }

            SDL_SetTextureColorMod(mod_target, prev_r, prev_g, prev_b);
            SDL_SetTextureAlphaMod(mod_target, prev_a);
            SDL_SetTextureBlendMode(mod_target, previous_blend_mode);
        } else {
            SDL_SetTextureColorMod(mod_target, 255, 255, 255);
        }

        SDL_RenderCopyEx(renderer_, mod_target, nullptr, &fb, 0, nullptr, flip_mode);
        SDL_SetTextureColorMod(mod_target, 255, 255, 255);
        SDL_SetTextureAlphaMod(mod_target, 255);
        if (draw_tex && draw_tex != final_tex) {
            SDL_SetTextureColorMod(final_tex, 255, 255, 255);
            SDL_SetTextureAlphaMod(final_tex, 255);
        }

        if (debug_render_areas && fb.w > 0 && fb.h > 0) {
            area_requests.push_back(AreaOverlayRequest{ a, static_cast<float>(fb.h) });
        }
    }

    SDL_SetRenderTarget(renderer_, scene_target_tex_);
    if (!low_quality_mode_ && z_light_pass_) {
        z_light_pass_->render(debugging);
    }
    if (assets_) assets_->render_overlays(renderer_);

    if (debug_render_areas) {
        for (const auto& request : area_requests) {
            if (!request.asset) continue;
            render_asset_debug_areas(renderer_, camera_state, *request.asset, request.asset_screen_height, player_screen_height);
        }
    }

    if (scene_target_tex_) {
        SDL_SetRenderTarget(renderer_, nullptr);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, SLATE_COLOR.r, SLATE_COLOR.g, SLATE_COLOR.b, 255);
        SDL_RenderClear(renderer_);

        SDL_SetTextureBlendMode(scene_target_tex_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(scene_target_tex_, 255);
        SDL_RenderCopy(renderer_, scene_target_tex_, nullptr, nullptr);
    }

}
