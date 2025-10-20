#include "scene_renderer.hpp"
#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "light_map.hpp"
#include "render/camera.hpp"
#include "dev_mode/dev_ui_settings.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"
#include "render_pipeline/ScalingLogic.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include <tuple>
#include <vector>
#include <cstdint>
#include <string_view>
#include <unordered_set>
#include <utility>

static constexpr SDL_Color SLATE_COLOR = {69, 101, 74, 255};
static constexpr float kDefaultMinVisibleScreenRatio = 0.015f;

namespace {
constexpr std::string_view kUpdateMapLightSettingKey = "dev_ui.lighting.map_panel.update_map_light";
} // namespace

SceneRenderer::SceneRenderer(SDL_Renderer* renderer,
                             Assets* assets,
                             int screen_width,
                             int screen_height,
                             const nlohmann::json& map_manifest,
                             const std::string& map_id,
                             std::unique_ptr<PrecomputedLightMap> precomputed_light_map)
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
                                  &reactive_shadow_settings_,
                                  nullptr })
{
    main_light_source_.initialize_from_map_manifest(map_manifest, map_id);
    light_map_ = std::make_unique<LightMap>(assets_,
                                            screen_width_,
                                            screen_height_,
                                            std::move(precomputed_light_map));
    if (light_map_) {
        // Use 2 grid-spaces per quadrant by default
        light_map_->set_cells_per_quadrant(2);
        light_map_->rebuild(renderer_);
        render_pipeline_.lighting().light_map_sampler = light_map_.get();
    } else {
        render_pipeline_.lighting().light_map_sampler = nullptr;
    }
    render_pipeline_.lighting().reactive_shadow_settings = &reactive_shadow_settings_;
    main_light_source_.update();
}

SDL_Renderer* SceneRenderer::get_renderer() const { return renderer_; }

LightMap* SceneRenderer::light_map() {
    return light_map_ ? light_map_.get() : nullptr;
}

const LightMap* SceneRenderer::light_map() const {
    return const_cast<SceneRenderer*>(this)->light_map();
}

void SceneRenderer::set_virtual_light_map_quadrants(int quadrants) {
    if (light_map_) {
        light_map_->set_virtual_light_map_quadrants(quadrants);
    }
    force_virtual_light_map_refresh();
}

void SceneRenderer::set_virtual_light_map_quadrant_size(int size_px) {
    if (light_map_) {
        light_map_->set_virtual_light_map_quadrant_size(size_px);
    }
    force_virtual_light_map_refresh();
}

void SceneRenderer::force_virtual_light_map_refresh() {
    if (!light_map_ || !renderer_) {
        return;
    }
    light_map_->rebuild(renderer_);
    render_pipeline_.lighting().light_map_sampler = light_map_.get();
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

    if (a->is_shaded || a->is_shading_group_set()) {
        return true;
    }

    SDL_Texture* final_texture = a->get_final_texture();
    if (!final_texture) {
        return true;
    }

    const bool locked = a->is_current_animation_locked_in_progress();
    const bool treat_static = a->static_frame || locked;
    if (treat_static) {
        return false;
    }

    const AnimationFrame* current_frame = a->current_frame;
    auto it = last_rendered_frames_.find(a);
    if (it == last_rendered_frames_.end()) {
        return true;
    }

    return it->second != current_frame;
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

    // Per-chunk shadow data is updated inside LightMap::update

    if (light_map_){
        // Keep the sampler available for the pipeline.
        render_pipeline_.lighting().light_map_sampler = light_map_.get();
        // Update quadrant tile masks so moving lights affecting them are reflected this frame.
        light_map_->update(renderer_, 0u);
    } else {
        render_pipeline_.lighting().light_map_sampler = nullptr;
    }
    render_pipeline_.lighting().reactive_shadow_settings = &reactive_shadow_settings_;

    SDL_SetRenderTarget(renderer_,nullptr);
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
    // In quadrant debug mode we explicitly keep the normal background.
    const SDL_Color clear_color = quadrant_debug_mode_ ? SLATE_COLOR : (light_map_only_mode_ ? SDL_Color{0,0,0,255} : SLATE_COLOR);
    SDL_SetRenderDrawColor(renderer_,clear_color.r,clear_color.g,clear_color.b,clear_color.a);
    SDL_RenderClear(renderer_);

    bool rendered_light_map = false;
    auto render_light_map = [&]() {
        if (!light_map_ || rendered_light_map) {
            return;
        }
        // Compute a global alpha multiplier from the map light's current opacity window
        // (same logic as shading stages). Darker overlays should result from a higher
        // map-light opacity, so invert the normalized value before applying it.
        float alpha_mult = 1.0f;
        if (!quadrant_debug_mode_) {
            const int min_opacity = main_light_source_.min_opacity();
            const int max_opacity = main_light_source_.max_opacity();
            const int cur_a       = std::clamp(static_cast<int>(main_light_source_.get_current_color().a), min_opacity, max_opacity);
            const int range       = std::max(1, max_opacity - min_opacity);
            const float normalized = std::clamp(static_cast<float>(cur_a - min_opacity) / static_cast<float>(range), 0.0f, 1.0f);
            alpha_mult            = std::clamp(1.0f - normalized, 0.0f, 1.0f);
        }

        SDL_Rect screen_view{0,0,screen_width_,screen_height_};
        SDL_BlendMode previous_mode = SDL_BLENDMODE_BLEND;
        if (SDL_GetRenderDrawBlendMode(renderer_,&previous_mode) != 0) {
            previous_mode = SDL_BLENDMODE_BLEND;
        }
        SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
        if (quadrant_debug_mode_) {
            // Draw quadrants with additive blending + red outlines
            light_map_->render_visible_quadrants_debug(renderer_, screen_view, 1.0f);
        } else {
            light_map_->render_visible_quadrants(renderer_,screen_view, alpha_mult);
        }
        SDL_SetRenderDrawBlendMode(renderer_,previous_mode);
        rendered_light_map = true;
    };

    if (!light_map_only_mode_ && !quadrant_debug_mode_){
        const auto& camera_state=assets_->getView();
        const camera::RealismSettings& cam_settings = camera_state.realism_settings();
        const float quality_percent = std::clamp(static_cast<float>(cam_settings.render_quality_percent), 10.0f, 100.0f);
        render_pipeline::ScalingLogic::SetQualityCap(quality_percent / 100.0f);

        float scale=camera_state.get_scale();
        float inv_scale=1.f/scale;
        float min_ratio = cam_settings.min_visible_screen_ratio;
        if (!std::isfinite(min_ratio) || min_ratio < 0.0f) {
            min_ratio = kDefaultMinVisibleScreenRatio;
        }
        min_ratio = std::clamp(min_ratio, 0.0f, 0.5f);
        int min_w=(int)std::lround(static_cast<double>(screen_width_)*min_ratio);
        int min_h=(int)std::lround(static_cast<double>(screen_height_)*min_ratio);

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
        current_active_assets_.clear();
        current_active_assets_.reserve(active.size());
        texture_commands_.clear();
        texture_commands_.reserve(active.size());
        remaining_commands_.clear();
        remaining_commands_.reserve(active.size());

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

            auto& target_commands = (asset->info->type == asset_types::texture)
                                        ? texture_commands_
                                        : remaining_commands_;
            target_commands.push_back(std::move(cmd));
        };

        for (Asset* a : active) {
            if (!a || !a->info) {
                continue;
            }

            current_active_assets_.insert(a);
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
                last_rendered_frames_.erase(a);
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
                if (a->current_frame) {
                    last_rendered_frames_[a] = a->current_frame;
                } else {
                    last_rendered_frames_.erase(a);
                }
                continue;
            }

            SDL_Texture* draw_tex = render_pipeline_.texture_for_scale(a, final_tex, fw, fh, dst.w, dst.h);
            enqueue_command(a, final_tex, draw_tex, dst);

            if (a->current_frame) {
                last_rendered_frames_[a] = a->current_frame;
            } else {
                last_rendered_frames_.erase(a);
            }
        }

        // Dynamic light-ray stamping removed.

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

        render_commands(texture_commands_);
        render_light_map();

        render_commands(remaining_commands_);

        last_active_assets_.swap(current_active_assets_);
        for (auto it = last_rendered_frames_.begin(); it != last_rendered_frames_.end();) {
            if (last_active_assets_.find(it->first) == last_active_assets_.end()) {
                it = last_rendered_frames_.erase(it);
            } else {
                ++it;
            }
        }
        current_active_assets_.clear();
    }

    SDL_SetRenderTarget(renderer_,nullptr);

    render_light_map();

    if (!light_map_only_mode_ && !quadrant_debug_mode_ && assets_){
        assets_->render_overlays(renderer_);
    }

    SDL_RenderPresent(renderer_);
}

