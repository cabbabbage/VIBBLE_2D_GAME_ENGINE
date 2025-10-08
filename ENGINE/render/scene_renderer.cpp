#include "scene_renderer.hpp"

#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "render/camera.hpp"
#include "render_area.hpp"
#include "render/gaussian_blur.hpp"
#include "render/render_asset.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <tuple>
#include <vector>
#include <array>
#include <random>
#include <initializer_list>
#include <cstdint>

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
                     screen_width, SDL_Color{255, 255, 255, 255}, map_path) {

    low_quality_mode_ = assets_ && assets_->is_dev_mode();

    // Fullscreen light color buffer
    fullscreen_light_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                              SDL_TEXTUREACCESS_TARGET, screen_width_, screen_height_);
    if (fullscreen_light_tex_) {
        SDL_SetTextureBlendMode(fullscreen_light_tex_, SDL_BLENDMODE_BLEND);
        SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
        SDL_SetRenderTarget(renderer_, fullscreen_light_tex_);
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 0);
        SDL_RenderClear(renderer_);
        SDL_SetRenderTarget(renderer_, prev);
    } else {
        std::cerr << "[SceneRenderer] Failed to create fullscreen light texture: "
                  << SDL_GetError() << "\n";
    }

    // Z light pass
    z_light_pass_ = std::make_unique<LightMap>(renderer_, assets_, main_light_source_,
                                               screen_width_, screen_height_, fullscreen_light_tex_);
    main_light_source_.update();
    if (z_light_pass_) z_light_pass_->render(debugging);

    // Scene target. Always create so the ray pass can read pixels in any mode.
    scene_target_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                          SDL_TEXTUREACCESS_TARGET, screen_width_, screen_height_);
    if (!scene_target_tex_) {
        std::cerr << "[SceneRenderer] Failed to create scene target: " << SDL_GetError() << "\n";
    } else {
        SDL_SetTextureBlendMode(scene_target_tex_, SDL_BLENDMODE_BLEND);
        #if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(scene_target_tex_, low_quality_mode_ ? SDL_ScaleModeNearest : SDL_ScaleModeBest);
        #endif
    }

    // Asset renderer
    render_asset_ = std::make_unique<RenderAsset>(renderer_, assets_, assets->getView(), main_light_source_, assets->player);

    // Full-screen light rays pass
    light_rays_pass_ = std::make_unique<LightRaysPass>(renderer_, screen_width_, screen_height_);
    // Strong defaults that show clearly
    light_rays_params_.metric            = BrightnessMetric::MaxRGB;
    light_rays_params_.use_alpha_in_mask = false;
    light_rays_params_.gamma_comp        = 0.9f;

    light_rays_params_.min_luma_threshold = 0.35f;
    light_rays_params_.bright_percentile  = 0.90f;

    light_rays_params_.samples  = 112;
    light_rays_params_.density  = 1.4f;
    light_rays_params_.decay    = 0.985f;
    light_rays_params_.weight   = 1.35f;
    light_rays_params_.exposure = 2.2f;

    light_rays_params_.downsample_log2 = 1;
    light_rays_pass_->set_params(light_rays_params_);
    light_rays_pass_->set_enabled(true);

    // Final blur
    final_blur_helper_ = std::make_unique<GaussianBlurHelper>(renderer_);
    final_blur_radius_ = 2.5f;
    final_blur_mix_ = 0.85f;
    final_blur_requested_ = true;
    final_blur_enabled_ = final_blur_requested_;
}

SceneRenderer::~SceneRenderer() {
    if (fullscreen_light_tex_) { SDL_DestroyTexture(fullscreen_light_tex_); fullscreen_light_tex_ = nullptr; }
    if (scene_target_tex_)     { SDL_DestroyTexture(scene_target_tex_);     scene_target_tex_     = nullptr; }
}

SDL_Renderer* SceneRenderer::get_renderer() const { return renderer_; }

void SceneRenderer::set_low_quality_rendering(bool low_quality) {
    low_quality_mode_ = low_quality;
    refresh_blur_helpers();
    // Keep rays enabled in both modes
    if (light_rays_pass_) light_rays_pass_->set_enabled(true);
}

void SceneRenderer::apply_map_light_config(const nlohmann::json& data) {
    main_light_source_.apply_config(data);
    if (!renderer_ || !fullscreen_light_tex_) return;

    SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, fullscreen_light_tex_);
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 0);
    SDL_RenderClear(renderer_);
    SDL_SetRenderTarget(renderer_, prev);
}

void SceneRenderer::apply_light_rays_config(const nlohmann::json& data) {
    auto f = [&](const char* k, double d, double lo, double hi){
        double v=d; try{ v=data.at(k).get<double>(); }catch(...){} return float(std::clamp(v,lo,hi));
    };

    // Blur
    final_blur_radius_    = f("final_blur_radius", final_blur_radius_, 0.0, 32.0);
    final_blur_mix_       = f("final_blur_mix",    final_blur_mix_,    0.0, 1.0);
    final_blur_requested_ = data.value("enabled", true);
    refresh_blur_helpers();

    // Rays
    if (light_rays_pass_) {
        LightRaysParams p = light_rays_params_;

        try {
            std::string m = data.at("metric").get<std::string>();
            if (m == "Luma709")   p.metric = BrightnessMetric::Luma709;
            if (m == "MaxRGB")    p.metric = BrightnessMetric::MaxRGB;
            if (m == "AvgRGB")    p.metric = BrightnessMetric::AvgRGB;
            if (m == "EnergyRGB") p.metric = BrightnessMetric::EnergyRGB;
        } catch (...) {}

        p.use_alpha_in_mask = data.value("use_alpha_in_mask", p.use_alpha_in_mask);
        p.gamma_comp        = f("gamma_comp", p.gamma_comp, 0.1, 4.0);

        p.min_luma_threshold = f("min_luma_threshold", p.min_luma_threshold, 0.0, 1.0);
        p.bright_percentile  = f("bright_percentile",  p.bright_percentile,  0.0, 1.0);

        p.samples  = int(std::clamp(data.value("samples",  p.samples),  1, 512));
        p.density  = f("density",  p.density,  0.01, 4.0);
        p.decay    = f("decay",    p.decay,    0.5,  0.9999);
        p.weight   = f("weight",   p.weight,   0.0,  8.0);
        p.exposure = f("exposure", p.exposure, 0.0,  8.0);

        p.downsample_log2 = std::clamp(data.value("downsample_log2", p.downsample_log2), 0, 4);

        light_rays_params_ = p;
        light_rays_pass_->set_params(p);
        light_rays_pass_->set_enabled(true);
    }
}

void SceneRenderer::refresh_blur_helpers() {
    final_blur_enabled_ = final_blur_requested_ && !low_quality_mode_;
    if (renderer_) {
        if (final_blur_helper_) final_blur_helper_->set_renderer(renderer_);
        else final_blur_helper_ = std::make_unique<GaussianBlurHelper>(renderer_);
    } else {
        final_blur_helper_.reset();
    }
}

void SceneRenderer::apply_final_blur_pass() {
    if (!final_blur_enabled_ || final_blur_radius_ <= 0.f || final_blur_mix_ <= 0.f) return;
    if (!renderer_ || !scene_target_tex_ || !final_blur_helper_) return;

    SDL_Texture* blurred = final_blur_helper_->apply(scene_target_tex_, screen_width_, screen_height_,
                                                     final_blur_radius_, final_blur_mix_);
    if (!blurred) return;

    SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, scene_target_tex_);
    SDL_SetTextureBlendMode(blurred, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(blurred, 255);
    SDL_Rect dst{0, 0, screen_width_, screen_height_};
    SDL_RenderCopy(renderer_, blurred, nullptr, &dst);
    SDL_SetRenderTarget(renderer_, prev);
}

void SceneRenderer::update_shading_groups() {
    ++current_shading_group_;
    if (current_shading_group_ > num_groups_) current_shading_group_ = 1;
}

bool SceneRenderer::shouldRegen(Asset* a) {
    if (!a->get_final_texture()) return true;
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

    const camera::RenderEffects effects =
        assets_->getView().compute_render_effects(SDL_Point{a->pos.x, a->pos.y}, base_sh, reference_screen_height);

    float scaled_sw = base_sw * effects.distance_scale;
    float scaled_sh = base_sh * effects.distance_scale;
    float final_visible_h = scaled_sh * effects.vertical_scale;

    if (scaled_sw < min_w && final_visible_h < min_h) return SDL_Rect{0,0,0,0};

    int sw = std::max(1, int(std::round(scaled_sw)));
    int sh = std::max(1, int(std::round(final_visible_h)));

    if (sw < min_w && sh < min_h) return SDL_Rect{0,0,0,0};

    const SDL_Point& cp = effects.screen_position;
    return SDL_Rect{ cp.x - sw / 2, cp.y - sh, sw, sh };
}

void SceneRenderer::resize_render_targets_if_needed() {
    if (!renderer_) return;

    int output_w = 0, output_h = 0;
    if (SDL_GetRendererOutputSize(renderer_, &output_w, &output_h) != 0) return;
    if (output_w <= 0 || output_h <= 0) return;
    if (output_w == screen_width_ && output_h == screen_height_) return;

    screen_width_ = output_w;
    screen_height_ = output_h;
    main_light_source_.set_screen_size(SDL_Point{ screen_width_ / 2, screen_height_ / 2 }, screen_width_);

    if (scene_target_tex_) { SDL_DestroyTexture(scene_target_tex_); scene_target_tex_ = nullptr; }
    recreate_fullscreen_light_texture();

    // Recreate scene target at new size
    scene_target_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                          SDL_TEXTUREACCESS_TARGET, screen_width_, screen_height_);
    if (scene_target_tex_) {
        SDL_SetTextureBlendMode(scene_target_tex_, SDL_BLENDMODE_BLEND);
        #if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(scene_target_tex_, low_quality_mode_ ? SDL_ScaleModeNearest : SDL_ScaleModeBest);
        #endif
    }

    if (z_light_pass_) {
        z_light_pass_->set_screen_dimensions(screen_width_, screen_height_, fullscreen_light_tex_);
    }
    if (light_rays_pass_) {
        light_rays_pass_->set_screen_size(screen_width_, screen_height_);
    }
    refresh_blur_helpers();
}

void SceneRenderer::recreate_fullscreen_light_texture() {
    if (!renderer_) return;

    if (fullscreen_light_tex_) { SDL_DestroyTexture(fullscreen_light_tex_); fullscreen_light_tex_ = nullptr; }
    fullscreen_light_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                              SDL_TEXTUREACCESS_TARGET, screen_width_, screen_height_);
    if (!fullscreen_light_tex_) {
        std::cerr << "[SceneRenderer] Failed to recreate fullscreen light texture: " << SDL_GetError() << "\n";
        return;
    }
    SDL_SetTextureBlendMode(fullscreen_light_tex_, SDL_BLENDMODE_BLEND);
    SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, fullscreen_light_tex_);
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 0);
    SDL_RenderClear(renderer_);
    SDL_SetRenderTarget(renderer_, prev);
}

void SceneRenderer::render() {
    static int render_call_count = 0;
    ++render_call_count;

    resize_render_targets_if_needed();

    update_shading_groups();
    main_light_source_.update();

    // Always render into the scene target so ray pass can read pixels
    SDL_SetRenderTarget(renderer_, scene_target_tex_);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, SLATE_COLOR.r, SLATE_COLOR.g, SLATE_COLOR.b, 255);
    SDL_RenderClear(renderer_);

    const auto& camera_state = assets_->getView();
    const bool debug_render_areas = camera_state.render_areas_enabled();
    float scale = camera_state.get_scale();
    float inv_scale = 1.0f / scale;
    int min_visible_w = int(screen_width_  * MIN_VISIBLE_SCREEN_RATIO);
    int min_visible_h = int(screen_height_ * MIN_VISIBLE_SCREEN_RATIO);

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
        if (ph > 0) player_screen_height = float(ph) * inv_scale;
    }
    if (player_screen_height <= 0.0f) player_screen_height = 1.0f;

    static const std::vector<Asset*> kEmpty{};
    const auto& active_assets = assets_ ? assets_->getFilteredActiveAssets() : kEmpty;
    const float pulse = 0.45f + 0.55f * std::sin(render_call_count * 0.18f);

    struct AreaOverlayRequest { Asset* asset; float asset_screen_height; };
    std::vector<AreaOverlayRequest> area_requests;
    if (debug_render_areas) area_requests.reserve(active_assets.size());

    for (Asset* a : active_assets) {
        if (!a || !a->info) continue;

        SDL_Texture* final_tex = a->get_final_texture();
        if (shouldRegen(a)) {
            SDL_Texture* previous_final = final_tex;
            final_tex = render_asset_->regenerateFinalTexture(a);
            if (!final_tex) final_tex = previous_final;
            else if (final_tex != previous_final) a->set_final_texture(final_tex);
        }
        if (!final_tex) continue;

        int fw = a->cached_w, fh = a->cached_h;
        if (fw == 0 || fh == 0) {
            SDL_QueryTexture(final_tex, nullptr, nullptr, &fw, &fh);
            a->cached_w = fw; a->cached_h = fh;
        }

        SDL_Rect fb = get_scaled_position_rect(a, fw, fh, inv_scale, min_visible_w, min_visible_h, player_screen_height);
        if (fb.w == 0 && fb.h == 0) continue;

        SDL_Texture* draw_tex = render_asset_->texture_for_scale(a, final_tex, fw, fh, fb.w, fb.h, scale);
        SDL_Texture* mod_target = draw_tex ? draw_tex : final_tex;

        const bool is_highlighted = a->is_highlighted();
        const bool is_selected   = a->is_selected();
        const SDL_RendererFlip flip_mode = a->flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;

        if (is_highlighted || is_selected) {
            SDL_BlendMode prev_blend = SDL_BLENDMODE_BLEND;
            SDL_GetTextureBlendMode(mod_target, &prev_blend);

            Uint8 pr=255, pg=255, pb=255, pa=255;
            SDL_GetTextureColorMod(mod_target, &pr, &pg, &pb);
            SDL_GetTextureAlphaMod(mod_target, &pa);

            SDL_Rect glow_rect = fb;
            const int min_dim = std::max(1, std::min(fb.w, fb.h));
            const int glow_margin = std::max(8, int(std::round(min_dim * 0.2f)));
            glow_rect.x -= glow_margin; glow_rect.y -= glow_margin;
            glow_rect.w += glow_margin*2; glow_rect.h += glow_margin*2;

            auto apply_tinted_copy = [&](const SDL_Color& color, SDL_Rect rect) {
                SDL_SetTextureColorMod(mod_target, color.r, color.g, color.b);
                SDL_SetTextureAlphaMod(mod_target, color.a);
                SDL_RenderCopyEx(renderer_, mod_target, nullptr, &rect, 0, nullptr, flip_mode);
            };

            SDL_SetTextureBlendMode(mod_target, SDL_BLENDMODE_ADD);

            const Uint8 base_alpha = Uint8(std::clamp(160.f + 70.f * pulse, 0.f, 255.f));
            SDL_Color outer_color = is_highlighted ? SDL_Color{90, 220, 255, base_alpha}
                                                   : SDL_Color{255, 185, 60, base_alpha};
            if (is_highlighted && is_selected) {
                outer_color = SDL_Color{255, 255, 255, Uint8(std::clamp(190.f + 60.f * pulse, 0.f, 255.f))};
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
                Uint8 inner_alpha = Uint8(std::clamp(150.f + 80.f * pulse, 0.f, 255.f));
                SDL_Color inner_color = is_highlighted ? SDL_Color{255, 245, 200, inner_alpha}
                                                       : SDL_Color{255, 215, 120, inner_alpha};
                apply_tinted_copy(inner_color, fb);
            }

            SDL_SetTextureColorMod(mod_target, pr, pg, pb);
            SDL_SetTextureAlphaMod(mod_target, pa);
            SDL_SetTextureBlendMode(mod_target, prev_blend);
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
            area_requests.push_back(AreaOverlayRequest{ a, float(fb.h) });
        }
    }

    // Z light pass over the scene target
    if (z_light_pass_) z_light_pass_->render(debugging);

    // Full-screen light rays right before final blur
    if (light_rays_pass_ && scene_target_tex_) {
        // Pick a screen-space light origin. Replace if you have a tracked light position.
        SDL_Point light_screen = SDL_Point{ screen_width_ / 2, screen_height_ / 3 };
        light_rays_pass_->set_light_screen_pos(light_screen);

        SDL_Texture* rays_lowres = light_rays_pass_->compute(scene_target_tex_);
        if (rays_lowres) {
            SDL_SetRenderTarget(renderer_, scene_target_tex_);
            SDL_SetTextureBlendMode(rays_lowres, SDL_BLENDMODE_ADD);
            SDL_SetTextureAlphaMod(rays_lowres, 255);
            SDL_Rect full{ 0, 0, screen_width_, screen_height_ };
            SDL_RenderCopy(renderer_, rays_lowres, nullptr, &full);
        }
    }

    // Final blur over the scene target
    apply_final_blur_pass();

    // Present
    SDL_SetRenderTarget(renderer_, nullptr);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, SLATE_COLOR.r, SLATE_COLOR.g, SLATE_COLOR.b, 255);
    SDL_RenderClear(renderer_);

    SDL_SetTextureBlendMode(scene_target_tex_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(scene_target_tex_, 255);
    SDL_Rect dst{ 0, 0, screen_width_, screen_height_ };
    SDL_RenderCopy(renderer_, scene_target_tex_, nullptr, &dst);

    // Dev overlays after present
    SDL_SetRenderTarget(renderer_, nullptr);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    if (assets_) assets_->render_overlays(renderer_);
    if (debug_render_areas) {
        for (const auto& request : area_requests) {
            if (!request.asset) continue;
            render_asset_debug_areas(renderer_, camera_state, *request.asset,
                                     request.asset_screen_height, player_screen_height);
        }
    }
}
