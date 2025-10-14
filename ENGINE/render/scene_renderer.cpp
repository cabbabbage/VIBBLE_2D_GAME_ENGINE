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
                std::cerr << "[SceneRenderer] Failed to create fullscreen light texture: "
                          << SDL_GetError() << "\n";
        }

        z_light_pass_ = std::make_unique<LightMap>(renderer_, assets_, main_light_source_, screen_width_, screen_height_, fullscreen_light_tex_);
        if (z_light_pass_) {
                z_light_pass_->set_fullscreen_light_settings(screen_light_color_, screen_light_min_opacity_, screen_light_max_opacity_);
        }
        light_rays_config_ = LightRaysConfig::defaults();
        light_rays_params_ = light_rays_config_.to_light_rays_params();
        light_rays_pass_ = std::make_unique<LightRaysPass>(renderer_, screen_width_, screen_height_);
        light_rays_enabled_ = false;
        if (light_rays_pass_) {
                light_rays_pass_->set_screen_size(screen_width_, screen_height_);
                light_rays_pass_->set_params(light_rays_params_);
                light_rays_pass_->set_enabled(light_rays_enabled_ && !fullscreen_light_rays_disabled_);
        }
        main_light_source_.update();
        z_light_pass_->render(debugging);
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
        if (post_small_tex_a_) {
                SDL_DestroyTexture(post_small_tex_a_);
                post_small_tex_a_ = nullptr;
        }
        if (post_small_tex_b_) {
                SDL_DestroyTexture(post_small_tex_b_);
                post_small_tex_b_ = nullptr;
        }

}

SDL_Renderer* SceneRenderer::get_renderer() const {
    return renderer_;
}

void SceneRenderer::set_low_quality_rendering(bool enabled) {
        if (low_quality_rendering_ == enabled) {
                return;
        }
        low_quality_rendering_ = enabled;
        if (enabled) {
                if (scene_target_tex_) {
                        SDL_DestroyTexture(scene_target_tex_);
                        scene_target_tex_ = nullptr;
                }
                if (post_small_tex_a_) {
                        SDL_DestroyTexture(post_small_tex_a_);
                        post_small_tex_a_ = nullptr;
                }
                if (post_small_tex_b_) {
                        SDL_DestroyTexture(post_small_tex_b_);
                        post_small_tex_b_ = nullptr;
                }
        }
}

void SceneRenderer::apply_map_light_config(const nlohmann::json& data) {
        main_light_source_.apply_config(data);
        apply_screen_light_settings(data);
        update_fullscreen_light_texture();
}

void SceneRenderer::apply_light_rays_config(const nlohmann::json& data) {
        light_rays_config_ = LightRaysConfig::from_json(data);
        light_rays_params_ = light_rays_config_.to_light_rays_params();
        light_rays_enabled_ = false;
        if (light_rays_pass_) {
                light_rays_pass_->set_screen_size(screen_width_, screen_height_);
                light_rays_pass_->set_params(light_rays_params_);
                light_rays_pass_->set_enabled(light_rays_enabled_ && !fullscreen_light_rays_disabled_);
        }
}

void SceneRenderer::apply_screen_light_settings(const nlohmann::json& data) {
        SDL_Color desired_color{255, 255, 255, 255};
        int desired_min = 0;
        int desired_max = 255;
        auto screen_it = data.find("screen_light");
        if (screen_it != data.end() && screen_it->is_object()) {
                const auto& screen = *screen_it;
                auto color_it = screen.find("color");
                if (color_it != screen.end() && color_it->is_array()) {
                        try {
                                if (color_it->size() >= 3) {
                                        desired_color.r = static_cast<Uint8>(std::clamp(color_it->at(0).get<int>(), 0, 255));
                                        desired_color.g = static_cast<Uint8>(std::clamp(color_it->at(1).get<int>(), 0, 255));
                                        desired_color.b = static_cast<Uint8>(std::clamp(color_it->at(2).get<int>(), 0, 255));
                                }
                        } catch (...) {}
                }
                desired_min = screen.value("min_opacity", desired_min);
                desired_max = screen.value("max_opacity", desired_max);
        }

        int map_min = main_light_source_.min_opacity();
        int map_max = main_light_source_.max_opacity();
        desired_min = std::clamp(desired_min, map_min, map_max);
        desired_max = std::clamp(desired_max, map_min, map_max);
        if (desired_min > desired_max) std::swap(desired_min, desired_max);

        screen_light_color_ = SDL_Color{desired_color.r, desired_color.g, desired_color.b, 255};
        screen_light_min_opacity_ = desired_min;
        screen_light_max_opacity_ = desired_max;
        if (z_light_pass_) {
                z_light_pass_->set_fullscreen_light_settings(screen_light_color_, screen_light_min_opacity_, screen_light_max_opacity_);
        }
}

void SceneRenderer::update_fullscreen_light_texture() {
        if (!renderer_ || !fullscreen_light_tex_) {
                return;
        }
        SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
        SDL_SetRenderTarget(renderer_, fullscreen_light_tex_);
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
        SDL_RenderClear(renderer_);
        SDL_SetRenderTarget(renderer_, prev);
}

void SceneRenderer::update_shading_groups() {
        ++current_shading_group_;
        if (current_shading_group_ > num_groups_)
                current_shading_group_ = 1;
}

bool SceneRenderer::shouldRegen(Asset* a) {
        if (!a) {
                return false;
        }

        SDL_Texture* final_texture = a->get_final_texture();
        const int shading_group = a->get_shading_group();
        const bool shading_group_due = shading_group > 0 && shading_group == current_shading_group_;
        const bool locked_animation = a->is_current_animation_locked_in_progress();
        const bool treat_as_static = a->static_frame || locked_animation;

        return !final_texture || shading_group_due || !treat_as_static || a->get_render_player_light();
}

SDL_Rect SceneRenderer::get_scaled_position_rect(Asset* a,
                                                 int fw,
                                                 int fh,
                                                 float inv_scale,
                                                 int min_w,
                                                 int min_h,
                                                 float reference_screen_height) {
        float base_scale = 1.0f;
        if (a && a->info && std::isfinite(a->info->scale_factor) && a->info->scale_factor >= 0.0f) {
                base_scale = a->info->scale_factor;
        }
        float scaled_fw = static_cast<float>(fw) * base_scale;
        float scaled_fh = static_cast<float>(fh) * base_scale;
        float base_sw = scaled_fw * inv_scale;
        float base_sh = scaled_fh * inv_scale;

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
    bool should_update_light = true;
    if (assets_ && assets_->is_dev_mode()) {
        should_update_light = devmode::ui_settings::load_bool(kUpdateMapLightSettingKey, false);
    }
    if (should_update_light) {
        main_light_source_.update();
    }

    static Uint8 kPostOverlayAlpha = 255;

    const bool use_postprocess = !low_quality_rendering_;

    auto ensure_target = [&](SDL_Texture*& tex, int w, int h) {
        int tw = 0, th = 0; Uint32 fmt = 0; int access = 0;
        if (tex && SDL_QueryTexture(tex, &fmt, &access, &tw, &th) == 0) {
            if (tw == w && th == h && access == SDL_TEXTUREACCESS_TARGET) return true;
            SDL_DestroyTexture(tex); tex = nullptr;
        }
        tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
        if (!tex) return false;
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        #if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeBest);
        #endif
        return true;
};
    if (use_postprocess) {
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
    } else {
        if (scene_target_tex_) {
            SDL_DestroyTexture(scene_target_tex_);
            scene_target_tex_ = nullptr;
        }
        if (post_small_tex_a_) {
            SDL_DestroyTexture(post_small_tex_a_);
            post_small_tex_a_ = nullptr;
        }
        if (post_small_tex_b_) {
            SDL_DestroyTexture(post_small_tex_b_);
            post_small_tex_b_ = nullptr;
        }
        SDL_SetRenderTarget(renderer_, nullptr);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, SLATE_COLOR.r, SLATE_COLOR.g, SLATE_COLOR.b, 255);
        SDL_RenderClear(renderer_);
    }

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
        const float player_scale = (player_asset->info && std::isfinite(player_asset->info->scale_factor) &&
                                    player_asset->info->scale_factor >= 0.0f)
                                       ? player_asset->info->scale_factor
                                       : 1.0f;
        if (ph > 0) player_screen_height = static_cast<float>(ph) * player_scale * inv_scale;
    }
    if (player_screen_height <= 0.0f) player_screen_height = 1.0f;

    const auto& active_assets = assets_->getActive();
    std::unordered_set<Asset*> current_active_assets;
    current_active_assets.reserve(active_assets.size());
    for (Asset* a : active_assets) {
        if (!a || !a->info) continue;

        current_active_assets.insert(a);
        const bool newly_active = last_active_assets_.find(a) == last_active_assets_.end();
        if (newly_active) {
            SDL_Texture* tex = render_pipeline_.regenerateFinalTexture(a);
            a->set_final_texture(tex);
        } else if (shouldRegen(a)) {
            SDL_Texture* tex = render_pipeline_.regenerateFinalTexture(a);
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

        SDL_Texture* draw_tex = render_pipeline_.texture_for_scale(a, final_tex, fw, fh, fb.w, fb.h);
        SDL_Texture* mod_target = draw_tex ? draw_tex : final_tex;

        if (a->is_highlighted()) {
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
            SDL_SetRenderDrawColor(renderer_, 200, 5, 5, 100);
            SDL_Rect outline = fb; outline.x -= 2; outline.y -= 2; outline.w += 4; outline.h += 4;
            SDL_RenderFillRect(renderer_, &outline);
            SDL_SetTextureColorMod(mod_target, 255, 200, 200);
        } else if (a->is_selected()) {
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
            SDL_SetRenderDrawColor(renderer_, 5, 5, 200, 100);
            SDL_Rect outline = fb; outline.x -= 2; outline.y -= 2; outline.w += 4; outline.h += 4;
            SDL_RenderFillRect(renderer_, &outline);
            SDL_SetTextureColorMod(mod_target, 255, 200, 200);
        } else {
            SDL_SetTextureColorMod(mod_target, 255, 255, 255);
        }

        SDL_RenderCopyEx(renderer_, draw_tex ? draw_tex : final_tex, nullptr, &fb, 0, nullptr, a->flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        SDL_SetTextureColorMod(mod_target, 255, 255, 255);
        if (draw_tex && draw_tex != final_tex) {
            SDL_SetTextureColorMod(final_tex, 255, 255, 255);
        }
    }

    last_active_assets_ = std::move(current_active_assets);

    SDL_SetRenderTarget(renderer_, use_postprocess ? scene_target_tex_ : nullptr);
    z_light_pass_->render(debugging);

    SDL_Texture* light_rays_texture = nullptr;
    if (use_postprocess && scene_target_tex_ && light_rays_pass_ && light_rays_enabled_ &&
        !fullscreen_light_rays_disabled_) {
        light_rays_pass_->set_screen_size(screen_width_, screen_height_);
        SDL_Point light_screen_pos = main_light_source_.get_position();
        if (assets_) {
            light_screen_pos = assets_->getView().map_to_screen(light_screen_pos);
        }
        light_rays_pass_->set_light_screen_pos(light_screen_pos);
        light_rays_texture = light_rays_pass_->compute(scene_target_tex_);
    }

    if (use_postprocess && scene_target_tex_) {
        SDL_SetRenderTarget(renderer_, nullptr);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);

        Uint8 base_alpha = static_cast<Uint8>(255 - kPostOverlayAlpha);
        SDL_SetTextureBlendMode(scene_target_tex_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(scene_target_tex_, base_alpha);
        SDL_RenderCopy(renderer_, scene_target_tex_, nullptr, nullptr);

        if (kPostOverlayAlpha > 0) {
            SDL_SetTextureAlphaMod(scene_target_tex_, kPostOverlayAlpha);
            SDL_RenderCopy(renderer_, scene_target_tex_, nullptr, nullptr);
            SDL_SetTextureAlphaMod(scene_target_tex_, 255);
        } else {
            SDL_SetTextureAlphaMod(scene_target_tex_, 255);
        }

        if (light_rays_texture) {
            SDL_Rect dest{0, 0, screen_width_, screen_height_};
            SDL_RenderCopy(renderer_, light_rays_texture, nullptr, &dest);
        }
    } else {
        SDL_SetRenderTarget(renderer_, nullptr);
    }

    if (assets_) {
        assets_->render_overlays(renderer_);
    }

    SDL_RenderPresent(renderer_);
}
