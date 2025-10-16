#include "scene_renderer.hpp"
#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "light_map.hpp"
#include "render/camera.hpp"
#include "dev_mode/dev_ui_settings.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"
#include <algorithm>
#include <cmath>
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
                             const nlohmann::json& map_manifest,
                             const std::string& map_id)
: renderer_(renderer),
  assets_(assets),
  screen_width_(screen_width),
  screen_height_(screen_height),
  main_light_source_(renderer, SDL_Point{ screen_width / 2, screen_height / 2 },
                     screen_width, SDL_Color{255, 255, 255, 255}),
  reactive_shadow_settings_(render_pipeline::shading::sanitize_reactive_shadow_settings({})),
  render_pipeline_(renderer,
                   SceneLighting{ assets->getView(),
                                  main_light_source_,
                                  assets->player,
                                  nullptr,
                                  &reactive_shadow_settings_ })
{
    main_light_source_.initialize_from_map_manifest(map_manifest, map_id);
    z_light_pass_ = std::make_unique<LightMap>(assets_, screen_width_, screen_height_);
    if (z_light_pass_) {
        z_light_pass_->prepare_fullscreen_light_map(renderer_);
        z_light_pass_->update_virtual_light_map(renderer_);
        render_pipeline_.lighting().virtual_light_map = &z_light_pass_->virtual_light_map();
    } else {
        render_pipeline_.lighting().virtual_light_map = nullptr;
    }
    render_pipeline_.lighting().reactive_shadow_settings = &reactive_shadow_settings_;
    main_light_source_.update();
}

SDL_Renderer* SceneRenderer::get_renderer() const { return renderer_; }

const VirtualLightMap* SceneRenderer::virtual_light_map() const {
    return z_light_pass_ ? &z_light_pass_->virtual_light_map() : nullptr;
}

void SceneRenderer::set_low_quality_rendering(bool enabled){
    if (low_quality_rendering_==enabled) return;
    low_quality_rendering_=enabled;
}

void SceneRenderer::apply_map_light_config(const nlohmann::json& data){
    main_light_source_.apply_config(data);

    using namespace render_pipeline::shading;
    auto reactive_it = data.find("reactive_shadows");
    if (reactive_it != data.end()) {
        reactive_shadow_settings_ = reactive_shadow_settings_from_json(*reactive_it, reactive_shadow_settings_);
    } else {
        reactive_shadow_settings_ = sanitize_reactive_shadow_settings(reactive_shadow_settings_);
    }
    render_pipeline_.lighting().reactive_shadow_settings = &reactive_shadow_settings_;

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
        z_light_pass_->prepare_fullscreen_light_map(renderer_);
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

        const auto& active = assets_->getActive();
        std::unordered_set<Asset*> cur_active;
        cur_active.reserve(active.size());

        struct AssetRenderCommand {
            SDL_Texture* source_texture        = nullptr;
            SDL_Texture* final_texture         = nullptr;
            SDL_Rect     dst                   { 0, 0, 0, 0 };
            bool         uses_scaled_texture   = false;
            bool         highlighted           = false;
            bool         selected              = false;
            bool         flipped               = false;
        };

        std::vector<AssetRenderCommand> texture_commands;
        std::vector<AssetRenderCommand> remaining_commands;
        texture_commands.reserve(active.size());
        remaining_commands.reserve(active.size());

        auto enqueue_command = [&](Asset* asset,
                                   SDL_Texture* final_tex,
                                   SDL_Texture* draw_tex,
                                   const SDL_Rect& dst_rect) {
            AssetRenderCommand cmd;
            cmd.source_texture      = draw_tex ? draw_tex : final_tex;
            cmd.final_texture       = final_tex;
            cmd.dst                 = dst_rect;
            cmd.uses_scaled_texture = draw_tex && draw_tex != final_tex;
            cmd.highlighted         = asset->is_highlighted();
            cmd.selected            = asset->is_selected();
            cmd.flipped             = asset->flipped;

            if (asset->info->type == asset_types::texture) {
                texture_commands.push_back(cmd);
            } else {
                remaining_commands.push_back(cmd);
            }
        };

        for (Asset* a : active) {
            if (!a || !a->info) {
                continue;
            }

            cur_active.insert(a);
            const bool newly = last_active_assets_.find(a) == last_active_assets_.end();
            if (newly) {
                SDL_Texture* tex = render_pipeline_.regenerateFinalTexture(a);
                a->set_final_texture(tex);
            } else if (shouldRegen(a)) {
                SDL_Texture* tex = render_pipeline_.regenerateFinalTexture(a);
                a->set_final_texture(tex);
            }

            SDL_Texture* final_tex = a->get_final_texture();
            if (!final_tex) {
                continue;
            }

            int fw = a->cached_w;
            int fh = a->cached_h;
            if (fw == 0 || fh == 0) {
                SDL_QueryTexture(final_tex, nullptr, nullptr, &fw, &fh);
                a->cached_w = fw;
                a->cached_h = fh;
            }

            SDL_Rect dst = get_scaled_position_rect(a, fw, fh, inv_scale, min_w, min_h, player_sh);
            if (dst.w == 0 && dst.h == 0) {
                continue;
            }

            SDL_Texture* draw_tex = render_pipeline_.texture_for_scale(a, final_tex, fw, fh, dst.w, dst.h);
            enqueue_command(a, final_tex, draw_tex, dst);
        }

        auto render_commands = [&](const std::vector<AssetRenderCommand>& commands) {
            for (const AssetRenderCommand& cmd : commands) {
                if (!cmd.source_texture) {
                    continue;
                }

                SDL_Texture* mod_target = cmd.source_texture;
                if (cmd.highlighted) {
                    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
                    SDL_SetRenderDrawColor(renderer_, 200, 5, 5, 100);
                    SDL_Rect outline = cmd.dst;
                    outline.x -= 2;
                    outline.y -= 2;
                    outline.w += 4;
                    outline.h += 4;
                    SDL_RenderFillRect(renderer_, &outline);
                    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureColorMod(mod_target, 255, 200, 200);
                } else if (cmd.selected) {
                    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
                    SDL_SetRenderDrawColor(renderer_, 5, 5, 200, 100);
                    SDL_Rect outline = cmd.dst;
                    outline.x -= 2;
                    outline.y -= 2;
                    outline.w += 4;
                    outline.h += 4;
                    SDL_RenderFillRect(renderer_, &outline);
                    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureColorMod(mod_target, 255, 200, 200);
                } else {
                    SDL_SetTextureColorMod(mod_target, 255, 255, 255);
                }

                SDL_RenderCopyEx(renderer_, cmd.source_texture, nullptr, &cmd.dst, 0, nullptr,
                                 cmd.flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
                SDL_SetTextureColorMod(mod_target, 255, 255, 255);
                if (cmd.uses_scaled_texture && cmd.final_texture) {
                    SDL_SetTextureColorMod(cmd.final_texture, 255, 255, 255);
                }
            }
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        };

        render_commands(texture_commands);

        if (z_light_pass_) {
            z_light_pass_->render_fullscreen_light_map(renderer_);
        }

        render_commands(remaining_commands);

        last_active_assets_ = std::move(cur_active);
    }

    SDL_SetRenderTarget(renderer_,nullptr);

    if (z_light_pass_) {
        z_light_pass_->update_virtual_light_map(renderer_);
    }

    if (!light_map_only_mode_ && assets_){
        assets_->render_overlays(renderer_);
    }

    SDL_RenderPresent(renderer_);
}

