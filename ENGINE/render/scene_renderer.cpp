#include "scene_renderer.hpp"
#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "world/chunk.hpp"
#include "render/camera.hpp"
#include "dev_mode/dev_ui_settings.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"
#include "render_pipeline/ScalingLogic.hpp"
#include "utils/log.hpp"
#include "utils/ranged_color.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>
#include <cstdlib>

static constexpr float kDefaultMinVisibleScreenRatio = 0.015f;

namespace {
constexpr std::string_view kUpdateMapLightSettingKey = "dev_ui.lighting.map_panel.update_map_light";

constexpr const char* kEnableChunkLightingEnv  = "VIBBLE_ENABLE_CHUNK_LIGHTING";
constexpr const char* kDisableChunkLightingEnv = "VIBBLE_DISABLE_CHUNK_LIGHTING";

bool env_truthy(const char* value) {
    if (!value || !value[0]) {
        return false;
    }
    const char c = value[0];
    return c == '1' || c == 'y' || c == 'Y' || c == 't' || c == 'T';
}

bool env_falsey(const char* value) {
    if (!value || !value[0]) {
        return false;
    }
    const char c = value[0];
    return c == '0' || c == 'n' || c == 'N' || c == 'f' || c == 'F';
}

bool chunk_lighting_suspended_flag() {
    if (env_truthy(std::getenv(kDisableChunkLightingEnv))) {
        return true;
    }
    if (const char* value = std::getenv(kEnableChunkLightingEnv)) {
        if (env_falsey(value)) {
            return true;
        }
        if (env_truthy(value)) {
            return false;
        }
    }
    return false;
}

SDL_BlendMode darkness_cutout_blend_mode() {
    static SDL_BlendMode cached = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    return cached;
}

}

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
                                  &reactive_shadow_settings_ }),
  update_map_light_enabled_(devmode::ui_settings::load_bool(kUpdateMapLightSettingKey, true))
{
    if (map_manifest.is_object()) {
        auto it = map_manifest.find("map_light_data");
        if (it != map_manifest.end() && it->is_object()) {
            map_clear_color_ = utils::color::resolve_ranged_color(
                it->value("map_color", nlohmann::json{}),
                SDL_Color{0, 0, 0, 255});
        }
    }
    main_light_source_.initialize_from_map_manifest(map_manifest, map_id);
    chunk_lighting_suspended_ = chunk_lighting_suspended_flag();
    light_map_ = std::make_unique<LightMap>(assets_, screen_width_, screen_height_);
    if (chunk_lighting_suspended_) {
        vibble::log::info("[SceneRenderer] Chunk lighting suspended; skipping light-map initialization.");
        render_pipeline_.lighting().light_map_sampler = nullptr;
    } else if (light_map_) {
        initialize_static_light_chunks();
        light_map_->rebuild(renderer_);
        render_pipeline_.lighting().light_map_sampler = light_map_.get();
    } else {
        render_pipeline_.lighting().light_map_sampler = nullptr;
    }
    render_pipeline_.lighting().reactive_shadow_settings = &reactive_shadow_settings_;
    runtime_lighting_sampler_ = std::make_unique<runtime_lighting::RuntimeLightingSampler>(assets_);
    main_light_source_.update(std::nullopt, std::nullopt);
}

SceneRenderer::~SceneRenderer() {
    destroy_darkness_overlay();
}

SDL_Renderer* SceneRenderer::get_renderer() const { return renderer_; }

void SceneRenderer::set_update_map_light_enabled(bool enabled) {
    update_map_light_enabled_ = enabled;
}

LightMap* SceneRenderer::light_map() {
    return light_map_ ? light_map_.get() : nullptr;
}

const LightMap* SceneRenderer::light_map() const {
    return const_cast<SceneRenderer*>(this)->light_map();
}

bool SceneRenderer::initialize_static_light_chunks() {
    if (!assets_) {
        vibble::log::debug("[SceneRenderer] Skipping light initialization (assets unavailable).");
        return false;
    }

    world::Grid& grid = assets_->world_grid();
    std::vector<world::Chunk*> chunks = grid.all_chunks();
    if (chunks.empty()) {
        vibble::log::info("[SceneRenderer] No map chunks detected; light initialization skipped.");
        return false;
    }

    bool initialized_chunks = false;
    for (world::Chunk* chunk : chunks) {
        if (!chunk) {
            continue;
        }
        chunk->releaseLightingArtifacts();
        chunk->lighting.static_strength          = 1.0f;
        chunk->lighting.dynamic_strength         = 1.0f;
        chunk->lighting.current_strength         = 1.0f;
        chunk->lighting.runtime_average_strength = 1.0f;
        chunk->lighting.runtime_average_color    = SDL_Color{255, 255, 255, 255};
        chunk->lighting.needs_update             = true;
        initialized_chunks                      = true;
    }

    return initialized_chunks;
}

void SceneRenderer::set_low_quality_rendering(bool enabled){
    if (low_quality_rendering_==enabled) return;
    low_quality_rendering_=enabled;
    render_pipeline_.set_low_quality_mode(enabled);
}

void SceneRenderer::apply_map_light_config(const nlohmann::json& data){
    main_light_source_.apply_config(data);
    map_clear_color_ = utils::color::resolve_ranged_color(
        data.value("map_color", nlohmann::json{}),
        SDL_Color{0, 0, 0, 255});

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

SDL_FRect SceneRenderer::get_scaled_position_rect(Asset* a,int fw,int fh,float inv_scale,int min_w,int min_h,float ref_sh){
    float world_x = a ? a->smoothed_translation_x() : 0.0f;
    float world_y = a ? a->smoothed_translation_y() : 0.0f;

    // In dev mode, bypass translation smoothing so editor drags are visible immediately.
    if (assets_ && assets_->is_dev_mode()) {
        world_x = a ? static_cast<float>(a->pos.x) : 0.0f;
        world_y = a ? static_cast<float>(a->pos.y) : 0.0f;
    }

    float base_scale = 1.0f;
    if (a) {
        base_scale = a->smoothed_scale();
        if (!std::isfinite(base_scale) || base_scale <= 0.0f) {
            base_scale = 1.0f;
        }
    }
    const float scaled_fw = static_cast<float>(fw) * base_scale;
    const float scaled_fh = static_cast<float>(fh) * base_scale;
    const float base_sw   = scaled_fw * inv_scale;
    const float base_sh   = scaled_fh * inv_scale;

    camera& cam = assets_->getView();
    const camera::RenderSmoothingKey smoothing_key = a ?
        reinterpret_cast<camera::RenderSmoothingKey>(a) : 0;
    camera::RenderEffects ef=cam.compute_render_effects(
        SDL_Point{ static_cast<int>(std::lround(world_x)), static_cast<int>(std::lround(world_y)) },
        base_sh,
        ref_sh,
        smoothing_key);
    SDL_FPoint screen = cam.map_to_screen_f(SDL_FPoint{ world_x, world_y });
    ef.screen_position = screen;

    const float parallax_offset = (a && a->info && a->info->apply_parallax) ? ef.parallax_offset_x : 0.0f;
    const float distance_scale  = (a && a->info && a->info->apply_distance_scaling) ? ef.distance_scale : 1.0f;
    const float vertical_scale  = (a && a->info && a->info->apply_vertical_scaling) ? ef.vertical_scale : 1.0f;

    const float scaled_sw = base_sw * distance_scale;
    const float scaled_sh2 = base_sh * distance_scale;
    const float final_h = scaled_sh2 * vertical_scale;

    const float min_w_f = static_cast<float>(min_w);
    const float min_h_f = static_cast<float>(min_h);
    if (scaled_sw < min_w_f && final_h < min_h_f) {
        return SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f};
    }

    float width  = scaled_sw;
    float height = final_h;

    bool enforced_min = false;
    if (width < min_w_f) {
        width = min_w_f;
        enforced_min = true;
    }
    if (height < min_h_f) {
        height = min_h_f;
        enforced_min = true;
    }

    width  = std::max(width, 1.0f);
    height = std::max(height, 1.0f);

    const float center_x = ef.screen_position.x + parallax_offset;
    const float left     = center_x - width * 0.5f;
    const float top      = ef.screen_position.y - height;

    if (enforced_min) {
        width  = static_cast<float>(std::max(1, static_cast<int>(std::lround(width))));
        height = static_cast<float>(std::max(1, static_cast<int>(std::lround(height))));
    }

    return SDL_FRect{ left, top, width, height };
}
void SceneRenderer::render(){
    static int render_call_count=0; ++render_call_count;
    ++frame_counter_;

    SDL_Point orbit_center{ screen_width_ / 2, screen_height_ / 2 };
    main_light_source_.set_screen_orbit_center(orbit_center);

    const camera* camera_state_ptr = assets_ ? &assets_->getView() : nullptr;
    if (camera_state_ptr) {
        main_light_source_.set_direction_reference_world(camera_state_ptr->get_screen_center());
    }

    bool should_update_light = true;
    if (assets_ && assets_->is_dev_mode()) {
        should_update_light = update_map_light_enabled_;
    }

    if (light_map_ && !chunk_lighting_suspended_){
        render_pipeline_.lighting().light_map_sampler = light_map_.get();
    } else {
        render_pipeline_.lighting().light_map_sampler = nullptr;
    }

    render_pipeline_.lighting().reactive_shadow_settings = &reactive_shadow_settings_;

    const SDL_Color map_light_color = main_light_source_.get_current_color();
    const float map_light_opacity = std::clamp(static_cast<float>(map_light_color.a) / 255.0f, 0.0f, 1.0f);

    SDL_SetRenderTarget(renderer_,nullptr);
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
    const SDL_Color clear_color = light_map_only_mode_ ? SDL_Color{0,0,0,255} : map_clear_color_;
    SDL_SetRenderDrawColor(renderer_,clear_color.r,clear_color.g,clear_color.b,clear_color.a);
    SDL_RenderClear(renderer_);

    bool rendered_light_map = false;
    auto render_light_map = [&]() {
        if (!light_map_only_mode_) {
            return;
        }
        if (chunk_lighting_suspended_) {
            return;
        }
        if (!light_map_ || rendered_light_map) {
            return;
        }
        const float alpha_mult = map_light_opacity;

        SDL_Rect screen_view{0, 0, screen_width_, screen_height_};
        SDL_BlendMode previous_mode = SDL_BLENDMODE_BLEND;
        if (SDL_GetRenderDrawBlendMode(renderer_, &previous_mode) != 0) {
            previous_mode = SDL_BLENDMODE_BLEND;
        }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        light_map_->render_visible_chunks(renderer_, screen_view, alpha_mult, map_light_color);
        SDL_SetRenderDrawBlendMode(renderer_, previous_mode);
        rendered_light_map = true;
    };

    if (runtime_lighting_sampler_) {
        runtime_lighting_sampler_->set_assets(assets_);
        runtime_lighting_sampler_->begin_frame();
        inject_map_light_sample();
    }

    light_overlay_sources_.clear();

    if (!light_map_only_mode_){
        const camera* camera_state = assets_ ? &assets_->getView() : nullptr;
        const camera::RealismSettings cam_settings = camera_state
            ? camera_state->realism_settings()
            : camera::RealismSettings{};
        const int effective_quality_percent = assets_
                                                  ? assets_->effective_render_quality_percent() : cam_settings.render_quality_percent;
        const float quality_percent =
            std::clamp(static_cast<float>(effective_quality_percent), 10.0f, 100.0f);
        render_pipeline::ScalingLogic::SetQualityCap(quality_percent / 100.0f);

        float scale = camera_state ? camera_state->get_scale() : 1.0f;
        if (!std::isfinite(scale) || scale <= 0.0f) {
            scale = 1.0f;
        }
        float inv_scale = 1.0f / scale;
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
        texture_commands_.clear();
        texture_commands_.reserve(active.size());
        remaining_commands_.clear();
        remaining_commands_.reserve(active.size());
        light_overlay_sources_.reserve(active.size());

        auto enqueue_command = [&](Asset* asset,
                                   SDL_Texture* final_tex,
                                   SDL_Texture* draw_tex,
                                   const SDL_FRect& dst_rect) {
            AssetRenderCommand cmd;
            cmd.source_texture      = draw_tex ? draw_tex : final_tex;
            cmd.final_texture       = final_tex;
            cmd.dst                 = dst_rect;
            cmd.uses_scaled_texture = draw_tex && draw_tex != final_tex;
            cmd.highlighted         = asset->is_highlighted();
            cmd.selected            = asset->is_selected();
            cmd.flipped             = asset->flipped;
            cmd.alpha               = asset ? asset->smoothed_alpha() : 1.0f;
            if (!std::isfinite(cmd.alpha)) {
                cmd.alpha = 1.0f;
            }
            cmd.alpha = std::clamp(cmd.alpha, 0.0f, 1.0f);

            auto& target_commands = (asset->info->type == asset_types::texture) ? texture_commands_ : remaining_commands_;
            target_commands.push_back(std::move(cmd));
        };

        for (Asset* a : active) {
            if (!a || !a->info) {
                continue;
            }

            const bool is_new = a->last_render_frame_id != frame_counter_ - 1;
            if (is_new) {
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

            a->last_render_frame_id = frame_counter_;

            int fw = a->cached_w;
            int fh = a->cached_h;
            if (fw == 0 || fh == 0) {
                SDL_QueryTexture(final_tex, nullptr, nullptr, &fw, &fh);
                a->cached_w = fw;
                a->cached_h = fh;
            }

            SDL_FRect dst = get_scaled_position_rect(a, fw, fh, inv_scale, min_w, min_h, player_sh);
            if (dst.w <= 0.0f || dst.h <= 0.0f) {
                if (a->current_frame) {
                    last_rendered_frames_[a] = a->current_frame;
                } else {
                    last_rendered_frames_.erase(a);
                }
                continue;
            }

            const float hysteresis_margin = camera_state
                ? camera_state->realism_settings().scale_variant_hysteresis_margin
                : render_pipeline::ScalingLogic::kDefaultHysteresisMargin;
            SDL_Texture* draw_tex = render_pipeline_.texture_for_scale(
                a,
                final_tex,
                fw,
                fh,
                static_cast<int>(std::lround(dst.w)),
                static_cast<int>(std::lround(dst.h)),
                hysteresis_margin);
            enqueue_command(a, final_tex, draw_tex, dst);

            if (a->info && !a->info->light_sources.empty() && dst.w > 0.0f && dst.h > 0.0f && fw > 0 && fh > 0) {
                const std::string canonical_type = asset_types::canonicalize(a->info->type);
                const bool        punches_overlay =
                    (canonical_type == asset_types::object || canonical_type == asset_types::texture || canonical_type == asset_types::player);
                if (!punches_overlay) {
                    continue;
                }
                LightOverlaySource source;
                source.asset       = a;
                source.asset_rect  = SDL_Rect{
                    static_cast<int>(std::lround(dst.x)),
                    static_cast<int>(std::lround(dst.y)),
                    static_cast<int>(std::lround(dst.w)),
                    static_cast<int>(std::lround(dst.h))
                };
                source.base_width  = fw;
                source.base_height = fh;
                source.flipped     = a->flipped;
                float base_scale   = 1.0f;
                if (a->info && std::isfinite(a->info->scale_factor) && a->info->scale_factor > 0.0f) {
                    base_scale = a->info->scale_factor;
                }
                source.asset_base_scale = base_scale;
                light_overlay_sources_.push_back(source);
            }

            if (a->current_frame) {
                last_rendered_frames_[a] = a->current_frame;
            } else {
                last_rendered_frames_.erase(a);
            }
        }

    // ---- Modified: bold outline around non-transparent pixels (no interior fill) ----
    auto render_commands = [&](const std::vector<AssetRenderCommand>& commands) {
        const int outline_px = 3; // outline thickness in screen pixels

        // 8-direction offsets for a chunky outline; adjust if you want thinner/thicker
        const SDL_FPoint OFFS[] = {
            {  0, -1 }, {  0,  1 }, { -1,  0 }, {  1,  0 },
            { -1, -1 }, {  1, -1 }, { -1,  1 }, {  1,  1 },
            // add an extra “ring” for a bolder edge
            {  0, -2 }, {  0,  2 }, { -2,  0 }, {  2,  0 }
        };

        for (const AssetRenderCommand& cmd : commands) {
            if (!cmd.source_texture) continue;

            SDL_Texture* tex = cmd.source_texture;

            // Compute base alpha
            const Uint8 base_alpha_mod = static_cast<Uint8>(
                std::clamp(std::lround(cmd.alpha * 255.0f), 0L, 255L));

            // --------------------
            // 1) OUTLINE PASS (only if highlighted/selected)
            //    We draw the sprite as a colored mask at multiple small offsets,
            //    then later draw the real sprite on top (so the interior is not tinted).
            // --------------------
            if (cmd.highlighted || cmd.selected) {
                // Better/clearer colors
                Uint8 r = cmd.highlighted ? 255 : 0;   // yellow for highlighted
                Uint8 g = cmd.highlighted ? 220 : 220; // cyan for selected
                Uint8 b = cmd.highlighted ? 0   : 255;
                Uint8 a = 200; // fairly bold

                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
                SDL_SetTextureColorMod(tex, r, g, b);
                SDL_SetTextureAlphaMod(tex, a);

                // Draw multiple offset copies for a thick outline
                for (const SDL_FPoint& o : OFFS) {
                    SDL_FRect orect = cmd.dst;
                    orect.x += o.x * outline_px;
                    orect.y += o.y * outline_px;
                    SDL_RenderCopyExF(
                        renderer_,
                        tex,
                        nullptr,
                        &orect,
                        0.0,
                        nullptr,
                        cmd.flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE
                    );
                }

                // Restore defaults on the texture before the base pass
                SDL_SetTextureColorMod(tex, 255, 255, 255);
                SDL_SetTextureAlphaMod(tex, 255);
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            }

            // --------------------
            // 2) BASE SPRITE PASS (normal rendering covers the interior of the outline)
            // --------------------
            SDL_SetTextureColorMod(tex, 255, 255, 255);
            SDL_SetTextureAlphaMod(tex, base_alpha_mod);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_RenderCopyExF(
                renderer_,
                cmd.source_texture,
                nullptr,
                &cmd.dst,
                0.0,
                nullptr,
                cmd.flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE
            );

            // Cleanup any scaling-temp texture state
            if (cmd.uses_scaled_texture && cmd.final_texture) {
                SDL_SetTextureColorMod(cmd.final_texture, 255, 255, 255);
                SDL_SetTextureAlphaMod(cmd.final_texture, 255);
                SDL_SetTextureBlendMode(cmd.final_texture, SDL_BLENDMODE_BLEND);
            }
        }

        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    };
    // -------------------------------------------------------------------------------


        render_commands(texture_commands_);
        render_dynamic_darkness_overlay(map_light_opacity);
        runtime_lighting::RuntimeLightingFrame runtime_frame;
        if (runtime_lighting_sampler_ && assets_) {
            runtime_frame = runtime_lighting_sampler_->gather(light_overlay_sources_, assets_->getView());
        }

        if (should_update_light) {
            std::optional<SDL_FPoint> aggregated_target;
            std::optional<SDL_FPoint> aggregated_direction;
            if (runtime_frame.has_brightest_centroid) {
                aggregated_target = runtime_frame.brightest_centroid;
            } else if (runtime_frame.has_brightest_sample) {
                aggregated_target = runtime_frame.brightest_sample_position;
            }
            if (runtime_frame.has_brightest_direction) {
                aggregated_direction = runtime_frame.brightest_direction;
            }
            main_light_source_.update(aggregated_target, aggregated_direction);
        }

        if (light_map_ && !chunk_lighting_suspended_) {
            light_map_->ingest_runtime_samples(runtime_frame);
            light_map_->update(renderer_, 0u);
        }
        render_light_map();

        render_commands(remaining_commands_);

        for (auto it = last_rendered_frames_.begin(); it != last_rendered_frames_.end();) {
            Asset* asset = it->first;
            if (!asset || asset->last_render_frame_id != frame_counter_) {
                it = last_rendered_frames_.erase(it);
            } else {
                ++it;
            }
        }
    }

    SDL_SetRenderTarget(renderer_,nullptr);

    render_light_map();

    if (chunk_preview_enabled_ && light_map_) {
        SDL_Rect screen_view{0, 0, screen_width_, screen_height_};
        light_map_->render_chunk_preview(renderer_, screen_view);
    }

    if (!light_map_only_mode_ && assets_){
        assets_->render_overlays(renderer_);
    }

    SDL_RenderPresent(renderer_);
}

bool SceneRenderer::ensure_darkness_overlay() {
    if (!renderer_ || screen_width_ <= 0 || screen_height_ <= 0) {
        return false;
    }

    if (darkness_overlay_texture_ &&
        (darkness_overlay_width_ != screen_width_ || darkness_overlay_height_ != screen_height_)) {
        destroy_darkness_overlay();
    }

    if (!darkness_overlay_texture_) {
        SDL_Texture* texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, screen_width_, screen_height_);
        if (!texture) {
            vibble::log::warn(std::string{"[SceneRenderer] Failed to allocate darkness overlay: "} + SDL_GetError());
            return false;
        }
        darkness_overlay_texture_ = texture;
        darkness_overlay_width_   = screen_width_;
        darkness_overlay_height_  = screen_height_;
        SDL_SetTextureBlendMode(darkness_overlay_texture_, SDL_BLENDMODE_BLEND);
    }

    return darkness_overlay_texture_ != nullptr;
}

void SceneRenderer::destroy_darkness_overlay() {
    if (darkness_overlay_texture_) {
        SDL_DestroyTexture(darkness_overlay_texture_);
        darkness_overlay_texture_ = nullptr;
        darkness_overlay_width_   = 0;
        darkness_overlay_height_  = 0;
    }
}

void SceneRenderer::inject_map_light_sample() {
    if (!runtime_lighting_sampler_ || !assets_) {
        return;
    }

    const Global_Light_Source* map_light = assets_->map_light_source();
    if (!map_light) {
        return;
    }

    const SDL_Color light_color = map_light->get_current_color();
    const float     opacity     = std::clamp(static_cast<float>(light_color.a) / 255.0f, 0.0f, 1.0f);
    const float     brightness  = std::clamp(1.0f - opacity, 0.0f, 1.0f);
    const float     strength    = std::clamp( reactive_shadow_settings_.virtual_light_map.map_light_dir_offset_strength, 0.0f, 1.0f);
    const float effective_intensity = brightness * strength;
    if (effective_intensity <= 1e-4f) {
        return;
    }

    const camera& cam = assets_->getView();
    const SDL_Point screen_pos = map_light->get_position();
    const SDL_FPoint world_pos  = cam.screen_to_map(screen_pos);

    runtime_lighting::ExternalLightSample sample{};
    sample.position.x = world_pos.x;
    sample.position.y = world_pos.y;
    sample.intensity  = std::clamp(effective_intensity, 0.0f, 1.0f);
    sample.color      = light_color;

    SDL_FPoint world00 = cam.screen_to_map({0, 0});
    SDL_FPoint worldX  = cam.screen_to_map({screen_width_, 0});
    SDL_FPoint worldY  = cam.screen_to_map({0, screen_height_});
    const float span_x = std::abs(worldX.x - world00.x);
    const float span_y = std::abs(worldY.y - world00.y);
    float       dominant_span = std::max(span_x, span_y);
    if (!(dominant_span > 1e-3f)) {
        dominant_span = static_cast<float>(std::max(screen_width_, screen_height_));
    }
    sample.radius = std::max(dominant_span * 0.75f, 1000.0f);

    SDL_Point reference = map_light->get_direction_reference();
    SDL_FPoint default_dir{
        static_cast<float>(reference.x) - sample.position.x, static_cast<float>(reference.y) - sample.position.y};
    const float dir_len = std::sqrt(default_dir.x * default_dir.x + default_dir.y * default_dir.y);
    if (dir_len > 1e-4f) {
        const float inv = 1.0f / dir_len;
        default_dir.x *= inv;
        default_dir.y *= inv;
        sample.direction     = default_dir;
        sample.has_direction = true;
    }

    runtime_lighting_sampler_->add_external_sample(sample);
}

void SceneRenderer::render_dynamic_darkness_overlay(float map_light_opacity) {
    if (!renderer_) {
        return;
    }

    const float overlay_alpha = std::clamp(map_light_opacity, 0.0f, 1.0f);
    if (!ensure_darkness_overlay()) {
        return;
    }

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, darkness_overlay_texture_);
    SDL_SetTextureAlphaMod(darkness_overlay_texture_, 255);
    SDL_SetTextureColorMod(darkness_overlay_texture_, 255, 255, 255);

    SDL_BlendMode previous_draw_blend = SDL_BLENDMODE_BLEND;
    if (SDL_GetRenderDrawBlendMode(renderer_, &previous_draw_blend) != 0) {
        previous_draw_blend = SDL_BLENDMODE_BLEND;
    }

    const SDL_BlendMode cutout_blend = darkness_cutout_blend_mode();
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_SetRenderDrawBlendMode(renderer_, cutout_blend);

    if (darkness_overlay_vertex_capacity_hint_ > darkness_overlay_vertices_.capacity()) {
        darkness_overlay_vertices_.reserve(darkness_overlay_vertex_capacity_hint_);
    }
    if (darkness_overlay_index_capacity_hint_ > darkness_overlay_indices_.capacity()) {
        darkness_overlay_indices_.reserve(darkness_overlay_index_capacity_hint_);
    }

    std::size_t frame_max_vertices = 0;
    std::size_t frame_max_indices  = 0;

    for (const LightOverlaySource& source : light_overlay_sources_) {
        Asset* asset = source.asset;
        if (!asset || !asset->info) {
            continue;
        }

        const auto& lights = asset->info->light_sources;
        if (lights.empty()) {
            continue;
        }

        const float base_width  = static_cast<float>(std::max(1, source.base_width));
        const float base_height = static_cast<float>(std::max(1, source.base_height));
        const float scale_x     = std::isfinite(static_cast<float>(source.asset_rect.w) / base_width) ? static_cast<float>(source.asset_rect.w) / base_width : 1.0f;
        const float scale_y_base = std::isfinite(static_cast<float>(source.asset_rect.h) / base_height) ? static_cast<float>(source.asset_rect.h) / base_height : scale_x;
        const float scale_y = (source.base_height > 0) ? scale_y_base : scale_x;
        if (!std::isfinite(scale_x) || !std::isfinite(scale_y)) {
            continue;
        }

        const float safe_base_scale = (std::isfinite(source.asset_base_scale) && source.asset_base_scale > 0.0f) ? source.asset_base_scale : 1.0f;
        const float zoom_scale_x = scale_x / safe_base_scale;
        const float zoom_scale_y = scale_y / safe_base_scale;
        const float safe_zoom_scale_x = (std::isfinite(zoom_scale_x) && zoom_scale_x > 0.0f) ? zoom_scale_x : 1.0f;
        const float safe_zoom_scale_y = (std::isfinite(zoom_scale_y) && zoom_scale_y > 0.0f) ? zoom_scale_y : 1.0f;

        const float center_base_x = static_cast<float>(source.asset_rect.x) + static_cast<float>(source.asset_rect.w) * 0.5f;
        const float center_base_y = static_cast<float>(source.asset_rect.y + source.asset_rect.h);

        for (const LightSource& light : lights) {
            if (light.behind) {
                continue;
            }
            const int raw_radius = light.radius;
            if (raw_radius <= 0) {
                continue;
            }

            const Uint8 intensity = static_cast<Uint8>(std::clamp(light.intensity, 0, 255));
            if (intensity == 0) {
                continue;
            }

            const float radius_base = static_cast<float>(std::max(1, raw_radius));
            const float radius_x    = std::max(1.0f, radius_base * safe_zoom_scale_x);
            const float radius_y    = std::max(1.0f, radius_base * safe_zoom_scale_y);

            if (!std::isfinite(radius_x) || !std::isfinite(radius_y)) {
                continue;
            }

            const float offset_x = static_cast<float>(source.flipped ? -light.offset_x : light.offset_x);
            const float offset_y = static_cast<float>(light.offset_y);

            const float center_x = center_base_x + offset_x * scale_x;
            const float center_y = center_base_y + offset_y * scale_y;

            SDL_Rect dst{};
            dst.w = std::max(1, static_cast<int>(std::lround(radius_x * 2.0f)));
            dst.h = std::max(1, static_cast<int>(std::lround(radius_y * 2.0f)));
            dst.x = static_cast<int>(std::lround(center_x - static_cast<float>(dst.w) * 0.5f));
            dst.y = static_cast<int>(std::lround(center_y - static_cast<float>(dst.h) * 0.5f));

            const float falloff_norm  = std::clamp(static_cast<float>(light.fall_off) / 100.0f, 0.0f, 1.0f);
            const float fade_exponent = 0.6f + 3.4f * falloff_norm;

            const float radius_hint   = std::max(radius_x, radius_y);
            const int   angular_steps = std::clamp(static_cast<int>(std::ceil(radius_hint / 6.0f)), 16, 64);
            const int   radial_steps  = 12;
            const float two_pi        = 6.28318530718f;

            const std::size_t desired_vertex_capacity = static_cast<std::size_t>((radial_steps + 1) * (angular_steps + 1));
            const std::size_t desired_index_capacity  = static_cast<std::size_t>(radial_steps * angular_steps * 6);

            auto& light_vertices = darkness_overlay_vertices_;
            auto& light_indices  = darkness_overlay_indices_;

            light_vertices.clear();
            light_indices.clear();

            if (desired_vertex_capacity > light_vertices.capacity()) {
                light_vertices.reserve(desired_vertex_capacity);
            }
            if (desired_index_capacity > light_indices.capacity()) {
                light_indices.reserve(desired_index_capacity);
            }

            frame_max_vertices = std::max(frame_max_vertices, desired_vertex_capacity);
            frame_max_indices  = std::max(frame_max_indices, desired_index_capacity);

            for (int ring = 0; ring <= radial_steps; ++ring) {
                const float ring_ratio = static_cast<float>(ring) / static_cast<float>(radial_steps);
                const float base       = std::max(0.0f, 1.0f - ring_ratio);
                float       alpha_ratio = std::pow(base, fade_exponent);
                alpha_ratio             = std::clamp(alpha_ratio, 0.0f, 1.0f);
                const float scaled_alpha = std::min(255.0f, static_cast<float>(intensity) * alpha_ratio * 1.6f);
                const Uint8 alpha        = static_cast<Uint8>(std::clamp(std::lround(scaled_alpha), 0L, 255L));

                for (int step = 0; step <= angular_steps; ++step) {
                    const float angle = (static_cast<float>(step) / static_cast<float>(angular_steps)) * two_pi;
                    const float px    = center_x + std::cos(angle) * radius_x * ring_ratio;
                    const float py    = center_y + std::sin(angle) * radius_y * ring_ratio;

                    SDL_Vertex vertex{};
                    vertex.position.x = px;
                    vertex.position.y = py;
                    vertex.color      = SDL_Color{0, 0, 0, alpha};
                    vertex.tex_coord  = SDL_FPoint{0.0f, 0.0f};
                    light_vertices.push_back(vertex);
                }
            }

            const int stride = angular_steps + 1;
            for (int ring = 0; ring < radial_steps; ++ring) {
                for (int step = 0; step < angular_steps; ++step) {
                    const int current = ring * stride + step;
                    const int next    = current + stride;

                    light_indices.push_back(current);
                    light_indices.push_back(next);
                    light_indices.push_back(current + 1);

                    light_indices.push_back(current + 1);
                    light_indices.push_back(next);
                    light_indices.push_back(next + 1);
                }
            }

            if (SDL_RenderGeometry(renderer_, nullptr,
                                   light_vertices.data(), static_cast<int>(light_vertices.size()),
                                   light_indices.data(), static_cast<int>(light_indices.size())) != 0) {
                SDL_SetRenderDrawColor(renderer_, 0, 0, 0, intensity);
                SDL_RenderFillRect(renderer_, &dst);
            }
        }
    }

    if (frame_max_vertices > darkness_overlay_vertex_capacity_hint_) {
        darkness_overlay_vertex_capacity_hint_ = frame_max_vertices;
    }
    if (frame_max_indices > darkness_overlay_index_capacity_hint_) {
        darkness_overlay_index_capacity_hint_ = frame_max_indices;
    }

    SDL_SetRenderDrawBlendMode(renderer_, previous_draw_blend);
    SDL_SetRenderTarget(renderer_, previous_target);

    if (overlay_alpha <= 0.0f) {
        return;
    }

    SDL_SetTextureAlphaMod(darkness_overlay_texture_, static_cast<Uint8>(std::clamp(std::lround(overlay_alpha * 255.0f), 0L, 255L)));
    SDL_SetTextureColorMod(darkness_overlay_texture_, 0, 0, 0);

    SDL_Rect screen_dst{0, 0, screen_width_, screen_height_};
    SDL_RenderCopy(renderer_, darkness_overlay_texture_, nullptr, &screen_dst);
}
