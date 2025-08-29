
#include "scene_renderer.hpp"
#include "core/Assets.hpp"
#include "asset/Asset.hpp"
#include "light_map.hpp"
#include "utils/parallax.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <tuple>
#include <vector>

static constexpr SDL_Color SLATE_COLOR = {69, 101, 74, 255};
static constexpr float MIN_VISIBLE_SCREEN_RATIO = 0.015f;


static int MOTION_BLUR_STRENGTH   = 150; 
static int MOTION_BLUR_PERSISTENCE = 200; 

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
      parallax_(screen_width, screen_height),   
      main_light_source_(renderer, screen_width / 2, screen_height / 2,
                         screen_width, SDL_Color{255, 255, 255, 255}, map_path),
      fullscreen_light_tex_(nullptr),
      render_asset_(renderer, parallax_, main_light_source_, assets->player), 
      accumulation_tex_(nullptr)
{
    fullscreen_light_tex_ = SDL_CreateTexture(renderer_,
                                              SDL_PIXELFORMAT_RGBA8888,
                                              SDL_TEXTUREACCESS_TARGET,
                                              screen_width_,
                                              screen_height_);
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

    accumulation_tex_ = SDL_CreateTexture(renderer_,
                                          SDL_PIXELFORMAT_RGBA8888,
                                          SDL_TEXTUREACCESS_TARGET,
                                          screen_width_,
                                          screen_height_);
    if (!accumulation_tex_) {
        std::cerr << "[SceneRenderer] Failed to create accumulation texture: "
                  << SDL_GetError() << "\n";
    } else {
        SDL_SetTextureBlendMode(accumulation_tex_, SDL_BLENDMODE_BLEND);
    }

    z_light_pass_ = std::make_unique<LightMap>(renderer_,
                                            assets_,
                                            parallax_,
                                            main_light_source_,
                                            screen_width_,
                                            screen_height_,
                                            fullscreen_light_tex_);


    main_light_source_.update();
    z_light_pass_->render(debugging);
}

void SceneRenderer::update_shading_groups() {
    ++current_shading_group_;
    if (current_shading_group_ > num_groups_)
        current_shading_group_ = 1;
}

bool SceneRenderer::shouldRegen(Asset* a) {
    if (!a->get_final_texture()){return true;}
    return (a->get_shading_group() > 0 &&
            a->get_shading_group() == current_shading_group_) ||
           (!a->get_final_texture() ||
            !a->static_frame ||
            a->get_render_player_light());
}

SDL_Rect SceneRenderer::get_scaled_position_rect(Asset* a, int fw, int fh,
                                                 float inv_scale, int min_w, int min_h) {
    static float smooth_inv_scale = 1.0f;      
    constexpr float lerp_speed = 0.08f;        

    
    smooth_inv_scale += (inv_scale - smooth_inv_scale) * lerp_speed;

    int sw = static_cast<int>(fw * smooth_inv_scale);
    int sh = static_cast<int>(fh * smooth_inv_scale);
    if (sw < min_w && sh < min_h) {
        return {0, 0, 0, 0};
    }

    SDL_Point cp{ a->screen_X, a->screen_Y }; 
    cp.x = screen_width_ / 2 + static_cast<int>((cp.x - screen_width_ / 2) * smooth_inv_scale);
    cp.y = screen_height_ / 2 + static_cast<int>((cp.y - screen_height_ / 2) * smooth_inv_scale);

    return SDL_Rect{ cp.x - sw / 2, cp.y - sh, sw, sh };
}

void SceneRenderer::render() {
    static int render_call_count = 0;
    ++render_call_count;

    update_shading_groups();

    int px = assets_->player ? assets_->player->pos_X : 0;
    int py = assets_->player ? assets_->player->pos_Y : 0;
    parallax_.setReference(px, py); 

    main_light_source_.update();

    // Update screen positions for all active assets based on current parallax
    for (Asset* a : assets_->active_assets) {
        if (!a) continue;
        parallax_.update_screen_position(*a);
    }

    
    SDL_SetRenderTarget(renderer_, accumulation_tex_);

    
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(accumulation_tex_, MOTION_BLUR_STRENGTH);
    SDL_RenderCopy(renderer_, accumulation_tex_, nullptr, nullptr);
    SDL_SetTextureAlphaMod(accumulation_tex_, 255);

    
    SDL_SetRenderDrawColor(renderer_, SLATE_COLOR.r, SLATE_COLOR.g, SLATE_COLOR.b, MOTION_BLUR_PERSISTENCE);
    SDL_RenderFillRect(renderer_, nullptr);

    const auto& view_state = assets_->getView();
    float scale = view_state.get_scale();
    float inv_scale = 1.0f / scale;

    int min_visible_w = static_cast<int>(screen_width_  * MIN_VISIBLE_SCREEN_RATIO);
    int min_visible_h = static_cast<int>(screen_height_ * MIN_VISIBLE_SCREEN_RATIO);

    
    for (Asset* a : assets_->active_assets) {
        if (!a || !a->info) continue;
        if (shouldRegen(a)) {
            SDL_Texture* tex = render_asset_.regenerateFinalTexture(a);
            a->set_final_texture(tex);
            if (tex) SDL_QueryTexture(tex, nullptr, nullptr, &a->cached_w, &a->cached_h);
        }

        SDL_Texture* final_tex = a->get_final_texture();
        if (!final_tex) continue;

        int fw = a->cached_w;
        int fh = a->cached_h;
        if (fw == 0 || fh == 0) SDL_QueryTexture(final_tex, nullptr, nullptr, &fw, &fh);

        SDL_Rect fb = get_scaled_position_rect(a, fw, fh, inv_scale,
                                               min_visible_w, min_visible_h);
        if (fb.w == 0 && fb.h == 0) continue;

    if (a->is_highlighted()) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
        SDL_SetRenderDrawColor(renderer_, 200, 5, 5, 100); 

        SDL_Rect outline = fb;
        outline.x -= 2; outline.y -= 2;
        outline.w += 4; outline.h += 4;
        SDL_RenderFillRect(renderer_, &outline);

        SDL_SetTextureColorMod(final_tex, 255, 200, 200); 
    }
    else if (a->is_selected()) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
        SDL_SetRenderDrawColor(renderer_, 5, 5, 200, 100); 

        SDL_Rect outline = fb;
        outline.x -= 2; outline.y -= 2;
        outline.w += 4; outline.h += 4;
        SDL_RenderFillRect(renderer_, &outline);

        SDL_SetTextureColorMod(final_tex, 255, 200, 200); 
    }
    else {
        SDL_SetTextureColorMod(final_tex, 255, 255, 255); 
    }

        SDL_RenderCopyEx(renderer_,
                         final_tex,
                         nullptr,
                         &fb,
                         0,
                         nullptr,
                         a->flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);

        SDL_SetTextureColorMod(final_tex, 255, 255, 255); 
    }

    
    SDL_SetRenderTarget(renderer_, nullptr);
    SDL_RenderCopy(renderer_, accumulation_tex_, nullptr, nullptr);

    
    z_light_pass_->render(debugging);

    
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_MOD);
    SDL_Color tint = main_light_source_.apply_tint_to_color({255, 255, 255, 255}, 255);
    SDL_SetRenderDrawColor(renderer_, tint.r, tint.g, tint.b, tint.a);
    SDL_Rect screenRect{0, 0, screen_width_, screen_height_};
    SDL_RenderFillRect(renderer_, &screenRect);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);

    // Draw overlays (e.g., asset library panel) before presenting
    if (assets_) {
        assets_->render_overlays(renderer_);
    }

    SDL_RenderPresent(renderer_);
}
