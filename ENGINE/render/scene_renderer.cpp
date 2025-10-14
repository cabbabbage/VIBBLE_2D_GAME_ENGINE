#include "scene_renderer.hpp"
#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "light_map.hpp"
#include "render/camera.hpp"
#include "dev_mode/dev_ui_settings.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <tuple>
#include <vector>
#include <cstdint>
#include <string_view>
#include <unordered_set>

static constexpr SDL_Color SLATE_COLOR = {69, 101, 74, 255};
static constexpr float MIN_VISIBLE_SCREEN_RATIO = 0.015f;

namespace {
constexpr std::string_view kUpdateMapLightSettingKey = "dev_ui.lighting.map_panel.update_map_light";
} // namespace

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
  reactive_shadow_settings_(render_pipeline::shading::sanitize_reactive_shadow_settings({})),
  render_pipeline_(renderer,
                   SceneLighting{ assets->getView(),
                                  main_light_source_,
                                  assets->player,
                                  nullptr,
                                  &reactive_shadow_settings_ }),
  lens_flares_(renderer, screen_width, screen_height)
{
    fullscreen_light_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, screen_width_, screen_height_);
    if (fullscreen_light_tex_) {
        SDL_SetTextureBlendMode(fullscreen_light_tex_, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(fullscreen_light_tex_, SDL_ScaleModeNearest);
#endif
        update_fullscreen_light_texture();
    } else {
        std::cerr << "[SceneRenderer] Failed to create fullscreen light texture: " << SDL_GetError() << "\n";
    }

    z_light_pass_ = std::make_unique<LightMap>(renderer_, assets_, main_light_source_, screen_width_, screen_height_, fullscreen_light_tex_);
    if (z_light_pass_) {
        z_light_pass_->set_fullscreen_light_settings(screen_light_color_, screen_light_min_opacity_, screen_light_max_opacity_);
        z_light_pass_->update_virtual_light_map();
        render_pipeline_.lighting().virtual_light_map = &z_light_pass_->virtual_light_map();
    } else {
        render_pipeline_.lighting().virtual_light_map = nullptr;
    }
    render_pipeline_.lighting().reactive_shadow_settings = &reactive_shadow_settings_;
    main_light_source_.update();
    z_light_pass_->render(debugging, light_map_only_mode_);
}

SceneRenderer::~SceneRenderer(){
    if (fullscreen_light_tex_){ SDL_DestroyTexture(fullscreen_light_tex_); fullscreen_light_tex_=nullptr; }
}

SDL_Renderer* SceneRenderer::get_renderer() const { return renderer_; }

void SceneRenderer::set_low_quality_rendering(bool enabled){
    if (low_quality_rendering_==enabled) return;
    low_quality_rendering_=enabled;
}

void SceneRenderer::apply_map_light_config(const nlohmann::json& data){
    main_light_source_.apply_config(data);
    apply_screen_light_settings(data);
    update_fullscreen_light_texture();

    using namespace render_pipeline::shading;
    auto reactive_it = data.find("reactive_shadows");
    if (reactive_it != data.end()) {
        reactive_shadow_settings_ = reactive_shadow_settings_from_json(*reactive_it, reactive_shadow_settings_);
    } else {
        reactive_shadow_settings_ = sanitize_reactive_shadow_settings(reactive_shadow_settings_);
    }
    render_pipeline_.lighting().reactive_shadow_settings = &reactive_shadow_settings_;

    auto lens_it = data.find("lens_flare");
    if (lens_it != data.end()) {
        lens_flares_.apply_settings_from_json(*lens_it);
    } else {
        lens_flares_.apply_settings(LensFlareRenderer::default_settings());
    }
}

void SceneRenderer::apply_screen_light_settings(const nlohmann::json& data){
    SDL_Color desired_color{255,255,255,255};
    int desired_min=0, desired_max=255;
    auto it=data.find("screen_light");
    if (it!=data.end() && it->is_object()){
        const auto& screen=*it;
        auto color_it=screen.find("color");
        if (color_it!=screen.end() && color_it->is_array()){
            try{
                if (color_it->size()>=3){
                    desired_color.r=(Uint8)std::clamp(color_it->at(0).get<int>(),0,255);
                    desired_color.g=(Uint8)std::clamp(color_it->at(1).get<int>(),0,255);
                    desired_color.b=(Uint8)std::clamp(color_it->at(2).get<int>(),0,255);
                }
            }catch(...){}
        }
        desired_min=screen.value("min_opacity",desired_min);
        desired_max=screen.value("max_opacity",desired_max);
    }

    int map_min=main_light_source_.min_opacity();
    int map_max=main_light_source_.max_opacity();
    desired_min=std::clamp(desired_min,map_min,map_max);
    desired_max=std::clamp(desired_max,map_min,map_max);
    if (desired_min>desired_max) std::swap(desired_min,desired_max);

    screen_light_color_=SDL_Color{desired_color.r,desired_color.g,desired_color.b,255};
    screen_light_min_opacity_=desired_min;
    screen_light_max_opacity_=desired_max;
    if (z_light_pass_){
        z_light_pass_->set_fullscreen_light_settings(screen_light_color_,screen_light_min_opacity_,screen_light_max_opacity_);
    }
}

void SceneRenderer::update_fullscreen_light_texture(){
    if (!renderer_||!fullscreen_light_tex_) return;
    SDL_Texture* prev=SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_,fullscreen_light_tex_);
    SDL_SetRenderDrawColor(renderer_,255,255,255,255);
    SDL_RenderClear(renderer_);
    SDL_SetRenderTarget(renderer_,prev);
}

bool SceneRenderer::shouldRegen(Asset* a){
    if (!a) return false;

    if (a->is_shaded || a->generate_rays || a->is_shading_group_set()) {
        return true;
    }

    SDL_Texture* final_texture=a->get_final_texture();
    if (!final_texture) {
        return true;
    }

    const bool locked=a->is_current_animation_locked_in_progress();
    const bool treat_static=a->static_frame||locked;
    return !treat_static;
}

SDL_Rect SceneRenderer::get_scaled_position_rect(Asset* a,int fw,int fh,float inv_scale,int min_w,int min_h,float ref_sh){
    float base_scale=1.f;
    if (a && a->info && std::isfinite(a->info->scale_factor) && a->info->scale_factor>=0.f) base_scale=a->info->scale_factor;
    float scaled_fw=(float)fw*base_scale;
    float scaled_fh=(float)fh*base_scale;
    float base_sw=scaled_fw*inv_scale;
    float base_sh=scaled_fh*inv_scale;

    const camera::RenderEffects ef=assets_->getView().compute_render_effects(SDL_Point{a->pos.x,a->pos.y}, base_sh, ref_sh);
    float scaled_sw=base_sw*ef.distance_scale;
    float scaled_sh2=base_sh*ef.distance_scale;
    float final_h=scaled_sh2*ef.vertical_scale;

    if (scaled_sw<min_w && final_h<min_h) return {0,0,0,0};

    int sw=std::max(1,(int)std::lround(scaled_sw));
    int sh=std::max(1,(int)std::lround(final_h));
    if (sw<min_w && sh<min_h) return {0,0,0,0};

    const SDL_Point& cp=ef.screen_position;
    return SDL_Rect{ cp.x - sw/2, cp.y - sh, sw, sh };
}

void SceneRenderer::render(){
    static int render_call_count=0; ++render_call_count;

    bool should_update_light=true;
    if (assets_ && assets_->is_dev_mode()){
        should_update_light=devmode::ui_settings::load_bool(kUpdateMapLightSettingKey, false);
    }
    if (should_update_light){ main_light_source_.update(); }

    if (z_light_pass_){
        z_light_pass_->update_virtual_light_map();
        render_pipeline_.lighting().virtual_light_map=&z_light_pass_->virtual_light_map();
    } else {
        render_pipeline_.lighting().virtual_light_map=nullptr;
    }
    render_pipeline_.lighting().reactive_shadow_settings = &reactive_shadow_settings_;

    SDL_SetRenderTarget(renderer_,nullptr);
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
    const SDL_Color clear_color = light_map_only_mode_ ? SDL_Color{0,0,0,255} : SLATE_COLOR;
    SDL_SetRenderDrawColor(renderer_,clear_color.r,clear_color.g,clear_color.b,clear_color.a);
    SDL_RenderClear(renderer_);

    if (!light_map_only_mode_){
        const auto& camera_state=assets_->getView();
        float scale=camera_state.get_scale();
        float inv_scale=1.f/scale;
        int min_w=(int)(screen_width_*MIN_VISIBLE_SCREEN_RATIO);
        int min_h=(int)(screen_height_*MIN_VISIBLE_SCREEN_RATIO);

        float player_sh=1.f;
        Asset* player=assets_?assets_->player:nullptr;
        if (player){
            SDL_Texture* tf=player->get_final_texture();
            SDL_Texture* fr=player->get_current_frame();
            int pw=player->cached_w, ph=player->cached_h;
            if ((pw==0||ph==0) && tf) SDL_QueryTexture(tf,nullptr,nullptr,&pw,&ph);
            if ((pw==0||ph==0) && fr) SDL_QueryTexture(fr,nullptr,nullptr,&pw,&ph);
            if (pw!=0) player->cached_w=pw;
            if (ph!=0) player->cached_h=ph;
            const float pscale=(player->info && std::isfinite(player->info->scale_factor) && player->info->scale_factor>=0.f) ? player->info->scale_factor : 1.f;
            if (ph>0) player_sh=(float)ph*pscale*inv_scale;
        }
        if (player_sh<=0.f) player_sh=1.f;

        const auto& active=assets_->getActive();
        std::unordered_set<Asset*> cur_active;
        cur_active.reserve(active.size());
        for (Asset* a: active){
            if (!a || !a->info) continue;

            cur_active.insert(a);
            const bool newly=last_active_assets_.find(a)==last_active_assets_.end();
            if (newly){
                SDL_Texture* tex=render_pipeline_.regenerateFinalTexture(a);
                a->set_final_texture(tex);
            } else if (shouldRegen(a)){
                SDL_Texture* tex=render_pipeline_.regenerateFinalTexture(a);
                a->set_final_texture(tex);
            }

            SDL_Texture* final_tex=a->get_final_texture();
            if (!final_tex) continue;

            int fw=a->cached_w, fh=a->cached_h;
            if (fw==0||fh==0){
                SDL_QueryTexture(final_tex,nullptr,nullptr,&fw,&fh);
                a->cached_w=fw; a->cached_h=fh;
            }

            SDL_Rect fb=get_scaled_position_rect(a,fw,fh,inv_scale,min_w,min_h,player_sh);
            if (fb.w==0 && fb.h==0) continue;

            SDL_Texture* draw_tex=render_pipeline_.texture_for_scale(a, final_tex, fw, fh, fb.w, fb.h);
            SDL_Texture* mod_target=draw_tex ? draw_tex : final_tex;

            if (a->is_highlighted()){
                SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_ADD);
                SDL_SetRenderDrawColor(renderer_,200,5,5,100);
                SDL_Rect outline=fb; outline.x-=2; outline.y-=2; outline.w+=4; outline.h+=4;
                SDL_RenderFillRect(renderer_,&outline);
                SDL_SetTextureColorMod(mod_target,255,200,200);
            } else if (a->is_selected()){
                SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_ADD);
                SDL_SetRenderDrawColor(renderer_,5,5,200,100);
                SDL_Rect outline=fb; outline.x-=2; outline.y-=2; outline.w+=4; outline.h+=4;
                SDL_RenderFillRect(renderer_,&outline);
                SDL_SetTextureColorMod(mod_target,255,200,200);
            } else {
                SDL_SetTextureColorMod(mod_target,255,255,255);
            }

            SDL_RenderCopyEx(renderer_, draw_tex?draw_tex:final_tex, nullptr, &fb, 0, nullptr, a->flipped?SDL_FLIP_HORIZONTAL:SDL_FLIP_NONE);
            SDL_SetTextureColorMod(mod_target,255,255,255);
            if (draw_tex && draw_tex!=final_tex){
                SDL_SetTextureColorMod(final_tex,255,255,255);
            }
        }

        last_active_assets_=std::move(cur_active);
    }

    SDL_SetRenderTarget(renderer_,nullptr);
    // Light map composites directly to backbuffer
    z_light_pass_->render(debugging, light_map_only_mode_);

    // ---- Cinematic lens flares (camera-axis, smoothed, lifecycle, subtle) ----
    lens_flares_.draw_after_light_map();

    SDL_SetRenderTarget(renderer_,nullptr);

    if (!light_map_only_mode_ && assets_){
        assets_->render_overlays(renderer_);
    }

    SDL_RenderPresent(renderer_);
}

