#include "scene_renderer.hpp"
#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "light_map.hpp"
#include "render/camera.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <tuple>
#include <vector>

static constexpr SDL_Color SLATE_COLOR = {69, 101, 74, 255};
static constexpr float MIN_VISIBLE_SCREEN_RATIO = 0.015f;

// Motion blur disabled

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
  render_asset_(renderer, assets->getView(), main_light_source_, assets->player)
{
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

	// No accumulation texture; render directly to default target

        z_light_pass_ = std::make_unique<LightMap>(renderer_, assets_, main_light_source_, screen_width_, screen_height_, fullscreen_light_tex_);
        main_light_source_.update();
        z_light_pass_->render(debugging);
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
	        a->get_shading_group() == current_shading_group_) ||
	       (!a->get_final_texture() || !a->static_frame || a->get_render_player_light());
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

    // ====== Tunable post-process params (hook to your UI) ======
    static int   kPostBlurRadiusPx = 4;   // blur radius at full-res
    static Uint8 kPostOverlayAlpha = 80;  // 0..255
    // 0=BLEND, 1=ADD, 2=MOD, 3=MUL(if supported)
    static int   kPostBlendModeSel = 0;
    // Downscale factor: 1=no scale, 2=quarter pixels, 3=~1/9 pixels, etc.
    static int   kPostDownscale    = 2;   // try 2..4 for a good speed/quality tradeoff
    // ===========================================================

    SDL_BlendMode kPostBlendMode = SDL_BLENDMODE_BLEND;
    switch (kPostBlendModeSel) {
        case 1: kPostBlendMode = SDL_BLENDMODE_ADD; break;
        case 2: kPostBlendMode = SDL_BLENDMODE_MOD; break;
        #ifdef SDL_BLENDMODE_MUL
        case 3: kPostBlendMode = SDL_BLENDMODE_MUL; break;
        #endif
        default: kPostBlendMode = SDL_BLENDMODE_BLEND; break;
    }
    const int blur_radius_full = std::max(0, kPostBlurRadiusPx);
    const int ds               = std::max(1, kPostDownscale);
    const int small_w          = std::max(1, screen_width_  / ds);
    const int small_h          = std::max(1, screen_height_ / ds);
    // Scale blur radius into the downsampled space so the look roughly matches
    const int blur_radius_small = std::max(0, blur_radius_full / ds);

    // ----- CLEAR BACKBUFFER -----
    SDL_SetRenderTarget(renderer_, nullptr);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, SLATE_COLOR.r, SLATE_COLOR.g, SLATE_COLOR.b, 255);
    SDL_RenderClear(renderer_);

    // ----- WORLD RENDER -----
    const auto& camera_state = assets_->getView();
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
    for (Asset* a : active_assets) {
        if (!a || !a->info) continue;

        if (shouldRegen(a)) {
            SDL_Texture* tex = render_asset_.regenerateFinalTexture(a);
            a->set_final_texture(tex);
        }

        SDL_Texture* final_tex = a->get_final_texture();
        if (!final_tex) continue;

        int fw = a->cached_w, fh = a->cached_h;
        if (fw == 0 || fh == 0) {
            SDL_QueryTexture(final_tex, nullptr, nullptr, &fw, &fh);
            a->cached_w = fw; a->cached_h = fh;
        }

        SDL_Rect fb = get_scaled_position_rect(a, fw, fh, inv_scale, min_visible_w, min_visible_h, player_screen_height);
        if (fb.w == 0 && fb.h == 0) continue;

        if (a->is_highlighted()) {
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
            SDL_SetRenderDrawColor(renderer_, 200, 5, 5, 100);
            SDL_Rect outline = fb; outline.x -= 2; outline.y -= 2; outline.w += 4; outline.h += 4;
            SDL_RenderFillRect(renderer_, &outline);
            SDL_SetTextureColorMod(final_tex, 255, 200, 200);
        } else if (a->is_selected()) {
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
            SDL_SetRenderDrawColor(renderer_, 5, 5, 200, 100);
            SDL_Rect outline = fb; outline.x -= 2; outline.y -= 2; outline.w += 4; outline.h += 4;
            SDL_RenderFillRect(renderer_, &outline);
            SDL_SetTextureColorMod(final_tex, 255, 200, 200);
        } else {
            SDL_SetTextureColorMod(final_tex, 255, 255, 255);
        }

        SDL_RenderCopyEx(renderer_, final_tex, nullptr, &fb, 0, nullptr, a->flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        SDL_SetTextureColorMod(final_tex, 255, 255, 255);
    }

    // ----- LIGHTS / OVERLAYS -----
    SDL_SetRenderTarget(renderer_, nullptr);
    z_light_pass_->render(debugging);
    if (assets_) assets_->render_overlays(renderer_);

    // ----- POST: DOWNSCALE -> BLUR (SMALL) -> UPSCALE OVERLAY -----
    if ((blur_radius_full > 0 || kPostOverlayAlpha > 0) && (kPostOverlayAlpha > 0)) {
        // 1) Read back full backbuffer once
        SDL_Surface* full = SDL_CreateRGBSurfaceWithFormat(0, screen_width_, screen_height_, 32, SDL_PIXELFORMAT_RGBA8888);
        if (full && SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_RGBA8888, full->pixels, full->pitch) == 0) {
            Uint8* full_px = static_cast<Uint8*>(full->pixels);
            const int full_pitch = full->pitch;
            SDL_PixelFormat* full_fmt = full->format;

            // 2) Downscale on CPU (nearest-neighbor for speed)
            SDL_Surface* small = SDL_CreateRGBSurfaceWithFormat(0, small_w, small_h, 32, SDL_PIXELFORMAT_RGBA8888);
            if (small) {
                Uint8* small_px = static_cast<Uint8*>(small->pixels);
                const int small_pitch = small->pitch;
                SDL_PixelFormat* small_fmt = small->format;

                auto full_at = [&](int x, int y) -> Uint32 {
                    return *reinterpret_cast<Uint32*>(full_px + y * full_pitch + x * 4);
                };
                auto small_set = [&](int x, int y, Uint32 v) {
                    *reinterpret_cast<Uint32*>(small_px + y * small_pitch + x * 4) = v;
                };

                for (int y = 0; y < small_h; ++y) {
                    int sy = std::min(screen_height_ - 1, y * ds);
                    for (int x = 0; x < small_w; ++x) {
                        int sx = std::min(screen_width_ - 1, x * ds);
                        small_set(x, y, full_at(sx, sy));
                    }
                }

                // 3) Blur in the downsampled space (separable box blur)
                if (blur_radius_small > 0) {
                    // Horizontal
                    std::vector<Uint32> row(small_w);
                    for (int y = 0; y < small_h; ++y) {
                        for (int x = 0; x < small_w; ++x)
                            row[x] = *reinterpret_cast<Uint32*>(small_px + y * small_pitch + x * 4);
                        for (int x = 0; x < small_w; ++x) {
                            int x0 = std::max(0, x - blur_radius_small);
                            int x1 = std::min(small_w - 1, x + blur_radius_small);
                            int cnt = (x1 - x0 + 1);
                            uint32_t r=0,g=0,b=0,a=0;
                            for (int xi = x0; xi <= x1; ++xi) {
                                Uint8 rr,gg,bb,aa;
                                SDL_GetRGBA(row[xi], small_fmt, &rr,&gg,&bb,&aa);
                                r+=rr; g+=gg; b+=bb; a+=aa;
                            }
                            Uint8 R = static_cast<Uint8>(r / cnt);
                            Uint8 G = static_cast<Uint8>(g / cnt);
                            Uint8 B = static_cast<Uint8>(b / cnt);
                            Uint8 A = static_cast<Uint8>(a / cnt);
                            small_set(x, y, SDL_MapRGBA(small_fmt, R,G,B,A));
                        }
                    }
                    // Vertical
                    std::vector<Uint32> col(small_h);
                    for (int x = 0; x < small_w; ++x) {
                        for (int y = 0; y < small_h; ++y)
                            col[y] = *reinterpret_cast<Uint32*>(small_px + y * small_pitch + x * 4);
                        for (int y = 0; y < small_h; ++y) {
                            int y0 = std::max(0, y - blur_radius_small);
                            int y1 = std::min(small_h - 1, y + blur_radius_small);
                            int cnt = (y1 - y0 + 1);
                            uint32_t r=0,g=0,b=0,a=0;
                            for (int yi = y0; yi <= y1; ++yi) {
                                Uint8 rr,gg,bb,aa;
                                SDL_GetRGBA(col[yi], small_fmt, &rr,&gg,&bb,&aa);
                                r+=rr; g+=gg; b+=bb; a+=aa;
                            }
                            Uint8 R = static_cast<Uint8>(r / cnt);
                            Uint8 G = static_cast<Uint8>(g / cnt);
                            Uint8 B = static_cast<Uint8>(b / cnt);
                            Uint8 A = static_cast<Uint8>(a / cnt);
                            small_set(x, y, SDL_MapRGBA(small_fmt, R,G,B,A));
                        }
                    }
                }

                // 4) Create texture from small surface and composite scaled-up
                SDL_Texture* blur_small_tex = SDL_CreateTextureFromSurface(renderer_, small);
                if (blur_small_tex) {
                    SDL_SetTextureBlendMode(blur_small_tex, kPostBlendMode);
                    SDL_SetTextureAlphaMod(blur_small_tex, kPostOverlayAlpha);

                    // Upscale to full-screen; rely on renderer scaling
                    // (Optionally enable linear scaling once at init: SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");)
                    SDL_Rect dst{0, 0, screen_width_, screen_height_};
                    SDL_RenderCopy(renderer_, blur_small_tex, nullptr, &dst);
                    SDL_DestroyTexture(blur_small_tex);
                }
                SDL_FreeSurface(small);
            }
        }
        if (full) SDL_FreeSurface(full);
    }

    // ----- PRESENT -----
    SDL_RenderPresent(renderer_);
}
