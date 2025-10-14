#include "scene_renderer.hpp"
#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "light_map.hpp"
#include "render/camera.hpp"
#include "dev_mode/dev_ui_settings.hpp"
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

inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }
inline float lerp(float a,float b,float t){ return a+(b-a)*t; }

inline float pixel_luma_norm(uint32_t argb){
    const float r=((argb>>16)&0xFF)/255.f;
    const float g=((argb>> 8)&0xFF)/255.f;
    const float b=((argb>> 0)&0xFF)/255.f;
    return clamp01(0.2126f*r+0.7152f*g+0.0722f*b);
}
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
  render_pipeline_(renderer, SceneLighting{ assets->getView(), main_light_source_, assets->player })
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
    main_light_source_.update();
    z_light_pass_->render(debugging, light_map_only_mode_);
}

SceneRenderer::~SceneRenderer(){
    if (fullscreen_light_tex_){ SDL_DestroyTexture(fullscreen_light_tex_); fullscreen_light_tex_=nullptr; }
    destroy_flare_textures();
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

void SceneRenderer::update_shading_groups(){
    ++current_shading_group_;
    if (current_shading_group_>num_groups_) current_shading_group_=1;
}

bool SceneRenderer::shouldRegen(Asset* a){
    if (!a) return false;
    SDL_Texture* final_texture=a->get_final_texture();
    const int sg=a->get_shading_group();
    const bool group_due=sg>0 && sg==current_shading_group_;
    const bool locked=a->is_current_animation_locked_in_progress();
    const bool treat_static=a->static_frame||locked;
    return !final_texture||group_due||!treat_static;
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

    update_shading_groups();
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
    draw_lens_flares_after_light_map();

    SDL_SetRenderTarget(renderer_,nullptr);

    if (!light_map_only_mode_ && assets_){
        assets_->render_overlays(renderer_);
    }

    SDL_RenderPresent(renderer_);
}

// ------------------------ Lens Flare Implementation ------------------------

void SceneRenderer::ensure_flare_textures(){
    if (circle_tex_ && streak_tex_ && star_tex_) return;
    make_circle_tex();
    make_streak_tex();
    make_starburst_tex();
}

void SceneRenderer::destroy_flare_textures(){
    if (circle_tex_) { SDL_DestroyTexture(circle_tex_); circle_tex_=nullptr; }
    if (streak_tex_) { SDL_DestroyTexture(streak_tex_); streak_tex_=nullptr; }
    if (star_tex_)   { SDL_DestroyTexture(star_tex_);   star_tex_=nullptr; }
}

void SceneRenderer::make_circle_tex(){
    if (circle_tex_) return;
    const int SZ=256;
    circle_tex_=SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SZ, SZ);
    if (!circle_tex_) return;
    SDL_SetTextureBlendMode(circle_tex_, SDL_BLENDMODE_ADD);

    SDL_Texture* prev=SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, circle_tex_);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0,0,0,0);
    SDL_RenderClear(renderer_);

    const int cx=SZ/2, cy=SZ/2;
    const float R=SZ*0.48f;

    // soft bokeh: bright soft core + rapid feather
    for (int y=0;y<SZ;++y){
        for (int x=0;x<SZ;++x){
            float dx=(x-cx), dy=(y-cy);
            float r=std::sqrt(dx*dx+dy*dy);
            if (r>R) continue;
            float t=1.f - (r/R);                   // 1 center -> 0 edge
            float a = 0.9f*std::pow(t,0.55f) + 0.1f*std::pow(t,3.0f);
            Uint8 alpha=(Uint8)std::lround(255.f*clamp01(a));
            if (alpha){
                SDL_SetRenderDrawColor(renderer_,255,255,255,alpha);
                SDL_RenderDrawPoint(renderer_,x,y);
            }
        }
    }
    SDL_SetRenderTarget(renderer_, prev);
}

void SceneRenderer::make_streak_tex(){
    if (streak_tex_) return;
    const int W=640, H=96;
    streak_tex_=SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, W, H);
    if (!streak_tex_) return;
    SDL_SetTextureBlendMode(streak_tex_, SDL_BLENDMODE_ADD);

    SDL_Texture* prev=SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, streak_tex_);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_,0,0,0,0);
    SDL_RenderClear(renderer_);

    const float cx=W*0.5f, cy=H*0.5f;
    for (int y=0;y<H;++y){
        float ny=(y-cy)/(H*0.5f);
        float vy=ny*ny;
        for (int x=0;x<W;++x){
            float nx=(x-cx)/(W*0.5f);
            float d = nx*nx*0.12f + vy;       // long horizontal
            float t = std::exp(-4.0f*d);
            Uint8 a=(Uint8)std::lround(210.f*clamp01(t));
            if (a){
                SDL_SetRenderDrawColor(renderer_,255,255,255,a);
                SDL_RenderDrawPoint(renderer_,x,y);
            }
        }
    }
    SDL_SetRenderTarget(renderer_, prev);
}

void SceneRenderer::make_starburst_tex(){
    if (star_tex_) return;
    const int SZ=256;
    star_tex_=SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SZ, SZ);
    if (!star_tex_) return;
    SDL_SetTextureBlendMode(star_tex_, SDL_BLENDMODE_ADD);

    SDL_Texture* prev=SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, star_tex_);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_,0,0,0,0);
    SDL_RenderClear(renderer_);

    const int cx=SZ/2, cy=SZ/2;
    const float R=SZ*0.48f;
    // radial spikes (thin rays) + soft core
    for (int y=0;y<SZ;++y){
        for (int x=0;x<SZ;++x){
            float dx=(x-cx), dy=(y-cy);
            float r=std::sqrt(dx*dx+dy*dy);
            if (r>R) continue;
            float ang = std::atan2(dy,dx);
            float rays = std::pow(std::fabs(std::cos(ang*6.0f)), 18.0f); // 12 spikes
            float core = std::pow(1.f - (r/R), 0.35f);
            float a = clamp01(0.8f*core + 0.4f*rays*(1.f - r/R));
            Uint8 alpha=(Uint8)std::lround(255.f*a);
            if (alpha){
                SDL_SetRenderDrawColor(renderer_,255,255,255,alpha);
                SDL_RenderDrawPoint(renderer_,x,y);
            }
        }
    }
    SDL_SetRenderTarget(renderer_, prev);
}

SDL_Color SceneRenderer::warm_tint(float hue_deg, float intensity_scale) const{
    // small warm tint around orange; simple HSV->RGB (S=0.5,V=1)
    float H=fmodf(hue_deg,360.f)/60.f; int i=(int)std::floor(H);
    float f=H - i;
    float V=1.f, S=0.5f;
    float p=V*(1.f-S);
    float q=V*(1.f-S*f);
    float t=V*(1.f-S*(1.f-f));
    float r,g,b;
    switch (i){
        default:
        case 0: r=V; g=t; b=p; break;
        case 1: r=q; g=V; b=p; break;
        case 2: r=p; g=V; b=t; break;
        case 3: r=p; g=q; b=V; break;
        case 4: r=t; g=p; b=V; break;
        case 5: r=V; g=p; b=q; break;
    }
    SDL_Color c{ (Uint8)std::lround(r*255.f), (Uint8)std::lround(g*255.f), (Uint8)std::lround(b*255.f), (Uint8)std::lround(clamp01(intensity_scale)*255.f) };
    return c;
}

void SceneRenderer::render_sprite(SDL_Texture* tex, float cx, float cy, float intensity, float base_px, float angle_deg, SDL_Color tint){
    if (!renderer_||!tex) return;
    int tw=0,th=0; SDL_QueryTexture(tex,nullptr,nullptr,&tw,&th); if (tw<=0||th<=0) return;
    float scale = base_px/(float)std::max(tw,th);
    int dw = std::max(1,(int)std::lround(tw*scale));
    int dh = std::max(1,(int)std::lround(th*scale));

    SDL_Rect dst{ (int)std::lround(cx - dw/2.f), (int)std::lround(cy - dh/2.f), dw, dh };

    const float a = clamp01(intensity) * ghost_alpha_cap_;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
    SDL_SetTextureAlphaMod(tex, (Uint8)std::lround(a*255.f));
    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);

    if (std::fabs(angle_deg)>0.01f) SDL_RenderCopyEx(renderer_,tex,nullptr,&dst,angle_deg,nullptr,SDL_FLIP_NONE);
    else                            SDL_RenderCopy(renderer_,tex,nullptr,&dst);

    SDL_SetTextureAlphaMod(tex,255);
    SDL_SetTextureColorMod(tex,255,255,255);
}

bool SceneRenderer::detect_bright_seeds(std::vector<FlareSeed>& out, int stride_px, float threshold_norm){
    out.clear();
    if (!renderer_||screen_width_<=0||screen_height_<=0) return false;

    std::vector<uint32_t> pixels((size_t)screen_width_*(size_t)screen_height_);
    if (SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888,
                             pixels.data(), screen_width_*(int)sizeof(uint32_t))!=0){
        return false;
    }

    // coarse scan + local max
    for (int y=stride_px/2; y<screen_height_; y+=stride_px){
        const uint32_t* row=&pixels[(size_t)y*(size_t)screen_width_];
        for (int x=stride_px/2; x<screen_width_; x+=stride_px){
            float lum=pixel_luma_norm(row[x]);
            if (lum<threshold_norm) continue;

            bool is_max=true;
            for (int oy=-1; oy<=1 && is_max; ++oy){
                int yy=y+oy*stride_px; if (yy<0||yy>=screen_height_) continue;
                const uint32_t* prow=&pixels[(size_t)yy*(size_t)screen_width_];
                for (int ox=-1; ox<=1; ++ox){
                    int xx=x+ox*stride_px; if (xx<0||xx>=screen_width_) continue;
                    if (pixel_luma_norm(prow[xx]) > lum + 1e-5f){ is_max=false; break; }
                }
            }
            if (!is_max) continue;

            FlareSeed s; s.x=(float)x; s.y=(float)y; s.sx=s.x; s.sy=s.y; s.strength=clamp01(lum); s.valid=true;
            out.push_back(s);
        }
    }
    std::sort(out.begin(), out.end(), [](const FlareSeed&a,const FlareSeed&b){ return a.strength>b.strength; });
    return !out.empty();
}

void SceneRenderer::smooth_and_track_seeds(std::vector<FlareSeed>& seeds){
    const float match2 = 160.f*160.f;
    std::vector<bool> matched(seeds.size(), false);

    for (auto& prev : last_seeds_){
        float best_d2=match2; int best=-1;
        for (int i=0;i<(int)seeds.size();++i){
            if (matched[i]||!seeds[i].valid) continue;
            float dx=seeds[i].x - prev.sx;
            float dy=seeds[i].y - prev.sy;
            float d2=dx*dx+dy*dy;
            if (d2<best_d2){ best_d2=d2; best=i; }
        }
        if (best>=0){
            seeds[best].sx = lerp(prev.sx, seeds[best].x, seed_pos_ema_);
            seeds[best].sy = lerp(prev.sy, seeds[best].y, seed_pos_ema_);
            matched[best]=true;
        }
    }
    for (int i=0;i<(int)seeds.size();++i){
        if (!matched[i]){ seeds[i].sx=seeds[i].x; seeds[i].sy=seeds[i].y; }
    }
    last_seeds_=seeds;
}

void SceneRenderer::axis_cascade_points(const FlareSeed& seed, std::vector<SDL_FPoint>& out) const{
    out.clear();
    const SDL_FPoint c=screen_center();
    float vx=seed.sx - c.x, vy=seed.sy - c.y;
    float L=std::sqrt(vx*vx+vy*vy) + 1e-6f;
    float ux=vx/L, uy=vy/L;

    for (float f : axis_factors_){
        out.push_back( SDL_FPoint{ c.x + ux*L*f, c.y + uy*L*f } );
    }
}

bool SceneRenderer::on_screen(float x,float y,int m) const{
    return x>=-m && x<=screen_width_+m && y>=-m && y<=screen_height_+m;
}

void SceneRenderer::spawn_or_update_ghosts(const std::vector<FlareSeed>& seeds){
    // mark all ghosts to fade unless retargeted this frame
    for (auto& g: ghosts_){ g.dying=true; g.target_alpha=0.f; }

    std::vector<SDL_FPoint> pts;
    int new_budget = max_new_per_frame_;

    for (const auto& s : seeds){
        if (!s.valid) continue;

        axis_cascade_points(s, pts);
        const float seed_a = clamp01(s.strength * ghost_intensity_gain_);
        const float base_sz = lerp(ghost_size_min_, ghost_size_max_, clamp01(0.15f + 0.85f*s.strength));

        // 1) small starburst near the brightest seed position (first factor near 1.0)
        // 2) multiple circles along axis + one faint streak

        for (size_t i=0;i<pts.size();++i){
            const SDL_FPoint p = pts[i];

            int desired_kind;
            if (i==4) desired_kind = 2;            // starburst near the light
            else if (i==1 || i==2 || i==5) desired_kind = 0; // circles
            else desired_kind = 1;                 // occasional streak

            // find nearest ghost of this kind to retarget
            int best=-1; float best_d2=52.f*52.f;
            for (int gi=0; gi<(int)ghosts_.size(); ++gi){
                auto& g=ghosts_[gi];
                if (g.kind!=desired_kind) continue;
                float dx=g.x-p.x, dy=g.y-p.y;
                float d2=dx*dx+dy*dy;
                if (d2<best_d2){ best_d2=d2; best=gi; }
            }

            float kind_alpha = seed_a * (desired_kind==0 ? 1.0f : desired_kind==1 ? 0.70f : 0.85f);
            float kind_size  = base_sz * (desired_kind==0 ? 1.0f : desired_kind==1 ? 1.4f : 1.15f);

            if (best>=0){
                auto& g=ghosts_[best];
                g.dying=false;
                g.tx=p.x; g.ty=p.y;
                g.target_alpha = std::max(g.target_alpha, kind_alpha);
                g.size_px = lerp(g.size_px, kind_size, 0.25f);
                g.life = std::min(g.life+1.f, g.max_life);
            } else if (new_budget>0){
                // spawn off-screen along axis
                SDL_FPoint c=screen_center();
                float ax=p.x - c.x, ay=p.y - c.y;
                float L=std::sqrt(ax*ax+ay*ay) + 1e-6f; ax/=L; ay/=L;

                FlareGhost g;
                g.kind = desired_kind;
                float spawn_dist = std::max<float>(std::max(screen_width_,screen_height_), L) + offscreen_spawn_bias_;
                g.x = c.x + ax*spawn_dist; g.y = c.y + ay*spawn_dist;
                g.tx=p.x; g.ty=p.y;
                g.vx = -ax * ghost_spawn_speed_;
                g.vy = -ay * ghost_spawn_speed_;
                g.alpha = 0.f; g.target_alpha=kind_alpha;
                g.size_px = kind_size;
                g.hue = 28.f + (desired_kind==0 ? (i*8.f) : desired_kind==2 ? 10.f : 5.f); // subtle warm shifts
                g.life=0.f; g.dying=false;

                ghosts_.push_back(g);
                --new_budget;
                if (new_budget<=0) break;
            }
        }
        if (new_budget<=0) break;
    }
}

void SceneRenderer::step_and_render_ghosts(){
    ensure_flare_textures();
    if (!circle_tex_ && !streak_tex_ && !star_tex_) return;

    std::vector<FlareGhost> keep;
    keep.reserve(ghosts_.size());

    for (auto& g : ghosts_){
        // follow + drift
        g.x = lerp(g.x, g.tx, ghost_follow_ema_) + g.vx * ghost_drift_;
        g.y = lerp(g.y, g.ty, ghost_follow_ema_) + g.vy * ghost_drift_;

        // fade
        if (!g.dying) g.alpha = clamp01(g.alpha + ghost_alpha_rise_);
        else          g.alpha = clamp01(g.alpha - ghost_alpha_fall_);

        // expire softly
        if (!g.dying && g.life > g.max_life){ g.dying=true; g.target_alpha=0.f; }
        g.life += 1.f;

        // approach target alpha but never exceed cap
        float target = std::min(g.target_alpha, ghost_alpha_cap_);
        g.alpha = std::min(g.alpha, target);

        // cull if fully transparent and off-screen
        if (g.dying && g.alpha <= 0.001f && !on_screen(g.x, g.y, 24)) continue;

        // choose sprite + tint
        SDL_Texture* tex = (g.kind==0 ? circle_tex_ : g.kind==1 ? streak_tex_ : star_tex_);
        float angle = (g.kind==1 ? (g.x >= screen_width_*0.5f ? +streak_angle_lean_ : -streak_angle_lean_) : 0.f);
        SDL_Color tint = warm_tint(g.hue, 1.f);

        render_sprite(tex, g.x, g.y, g.alpha, g.size_px, angle, tint);
        keep.push_back(g);
    }

    ghosts_.swap(keep);
}

void SceneRenderer::draw_lens_flares_after_light_map(){
    std::vector<FlareSeed> seeds;
    const bool found = detect_bright_seeds(seeds, seed_stride_px_, seed_threshold_norm_);
    if (found) smooth_and_track_seeds(seeds); // smoothing stabilizes over frames

    // spawn/update (if none found, existing ghosts will simply fade out)
    spawn_or_update_ghosts(found ? seeds : std::vector<FlareSeed>{});

    // step & render all ghosts (handles fade-in/out and culling)
    step_and_render_ghosts();
}
