#include "scene_renderer.hpp"
#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "world/chunk.hpp"
#include "render/camera.hpp"
#include "render/asset_light_renderer.hpp"
#include "dev_mode/depth_cue_settings.hpp"
#include "dev_mode/dev_ui_settings.hpp"
#include "render_pipeline/ScalingLogic.hpp"
#include "utils/log.hpp"
#include "utils/ranged_color.hpp"
#include "util/grid.hpp"
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
#include <unordered_map>

static constexpr float kDefaultMinVisibleScreenRatio = 0.015f;

namespace {
constexpr std::string_view kUpdateMapLightSettingKey = "dev_ui.lighting.map_panel.update_map_light";

constexpr float kDepthCueDeadzonePx = 1.5f;

enum class DepthCuePlane {
    None = 0,
    Foreground,
    Background
};

struct DepthCueSample {
    DepthCuePlane plane = DepthCuePlane::None;
    float         t     = 0.0f;
};

float evaluate_depth_curve(camera::BlurFalloffMethod method, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (method) {
        case camera::BlurFalloffMethod::Quadratic:
            return t * t;
        case camera::BlurFalloffMethod::Cubic:
            return t * t * t;
        case camera::BlurFalloffMethod::Logarithmic: {
            const float k = 4.0f;
            const float num = std::log1p(k * t);
            const float den = std::log1p(k);
            return (den > 0.0f) ? (num / den) : t;
        }
        case camera::BlurFalloffMethod::Exponential: {
            const float k = 3.0f;
            const float num = std::exp(k * t) - 1.0f;
            const float den = std::exp(k) - 1.0f;
            return (den > 0.0f) ? (num / den) : t;
        }
        case camera::BlurFalloffMethod::Linear:
        default:
            return t;
    }
}

DepthCueSample make_depthcue_sample(float screen_y,
                                    float center_screen_y,
                                    float fg_plane_screen_y,
                                    float bg_plane_screen_y,
                                    float fg_span_screen,
                                    float bg_span_screen) {
    DepthCueSample sample;
    if (!std::isfinite(screen_y)) {
        return sample;
    }
    if (std::fabs(screen_y - center_screen_y) <= kDepthCueDeadzonePx) {
        return sample;
    }
    if (screen_y > center_screen_y) {
        sample.plane = DepthCuePlane::Foreground;
        sample.t = (screen_y >= fg_plane_screen_y || fg_span_screen <= 0.0f)
            ? 1.0f
            : std::clamp((screen_y - center_screen_y) / fg_span_screen, 0.0f, 1.0f);
    } else if (screen_y < center_screen_y) {
        sample.plane = DepthCuePlane::Background;
        sample.t = (screen_y <= bg_plane_screen_y || bg_span_screen <= 0.0f)
            ? 1.0f
            : std::clamp((center_screen_y - screen_y) / bg_span_screen, 0.0f, 1.0f);
    }
    return sample;
}

float compute_depthcue_opacity(const DepthCueSample& sample,
                               int max_opacity,
                               camera::BlurFalloffMethod method) {
    if (sample.plane == DepthCuePlane::None || max_opacity <= 0) {
        return 0.0f;
    }
    const float curve = evaluate_depth_curve(method, sample.t);
    return curve * static_cast<float>(std::clamp(max_opacity, 0, 255)) / 255.0f;
}

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

// Conservative float-rect intersection with small padding to avoid edge popping
static inline bool intersects_padded(const SDL_FRect& a,
                                     const SDL_FRect& b,
                                     float pad_px = 2.0f) {
    const float ax0 = a.x - pad_px;
    const float ay0 = a.y - pad_px;
    const float ax1 = a.x + a.w + pad_px;
    const float ay1 = a.y + a.h + pad_px;
    const float bx0 = b.x;
    const float by0 = b.y;
    const float bx1 = b.x + b.w;
    const float by1 = b.y + b.h;
    return !(ax1 <= bx0 || bx1 <= ax0 || ay1 <= by0 || by1 <= ay0);
}

// Ensure reusable render-target textures exist and match the screen size
static void ensure_scene_targets(SDL_Renderer* renderer,
                                 int width,
                                 int height,
                                 SDL_Texture*& composite,
                                 SDL_Texture*& postprocess,
                                 SDL_Texture*& blurtex) {
    auto ensure_target = [&](SDL_Texture*& tex) {
        int tw = 0, th = 0; Uint32 fmt = 0; int access = 0;
        if (tex && SDL_QueryTexture(tex, &fmt, &access, &tw, &th) == 0) {
            if (tw == width && th == height && access == SDL_TEXTUREACCESS_TARGET) {
                return; // ok
            }
        }
        if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
        tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
        if (tex) { SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND); }
    };
    auto ensure_streaming = [&](SDL_Texture*& tex) {
        int tw = 0, th = 0; Uint32 fmt = 0; int access = 0;
        if (tex && SDL_QueryTexture(tex, &fmt, &access, &tw, &th) == 0) {
            if (tw == width && th == height && access == SDL_TEXTUREACCESS_STREAMING) {
                return; // ok
            }
        }
        if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
        tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, width, height);
        if (tex) { SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND); }
    };
    ensure_target(composite);
    ensure_streaming(postprocess);
    ensure_streaming(blurtex);
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
  render_pipeline_(renderer,
                    SceneLighting{ assets->getView(),
                                   main_light_source_,
                                   assets->player,
                                   nullptr,
                                   &assets->world_grid() }),
  update_map_light_enabled_(devmode::ui_settings::load_bool(kUpdateMapLightSettingKey, true))
{
    // Allow override of warmup frames via env var (optional): VIBBLE_DEPTHCUE_WARMUP_FRAMES
    if (const char* override_frames = std::getenv("VIBBLE_DEPTHCUE_WARMUP_FRAMES")) {
        const int v = std::atoi(override_frames);
        if (v >= 0 && v <= 120) {
            depthcue_warmup_frames_ = static_cast<std::uint32_t>(v);
        }
    }
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
    tile_renderer_ = std::make_unique<GridTileRenderer>(assets_);
}

SceneRenderer::~SceneRenderer() {
    destroy_darkness_overlay();
    if (scene_composite_tex_) { SDL_DestroyTexture(scene_composite_tex_); scene_composite_tex_ = nullptr; }
    if (postprocess_tex_)     { SDL_DestroyTexture(postprocess_tex_);     postprocess_tex_     = nullptr; }
    if (blur_tex_)            { SDL_DestroyTexture(blur_tex_);            blur_tex_            = nullptr; }
}

SDL_Renderer* SceneRenderer::get_renderer() const { return renderer_; }

void SceneRenderer::set_update_map_light_enabled(bool enabled) {
    update_map_light_enabled_ = enabled;
}

void SceneRenderer::set_dark_mask_enabled(bool enabled) {
    if (dark_mask_enabled_ == enabled) {
        return;
    }
    dark_mask_enabled_ = enabled;
    if (!dark_mask_enabled_) {
        destroy_darkness_overlay();
    }
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
    const SDL_Point world_point{
        static_cast<int>(std::lround(world_x)),
        static_cast<int>(std::lround(world_y))
    };
    camera::RenderEffects ef = cam.compute_render_effects(
        world_point,
        base_sh,
        ref_sh,
        smoothing_key);
    SDL_FPoint screen = cam.map_to_screen_f(SDL_FPoint{ world_x, world_y });
    ef.screen_position = screen;

    float center_x = ef.screen_position.x;
    if (assets_) {
        // Do not apply grid parallax to the player asset
        if (!(a && assets_->player == a)) {
            world::Grid& grid = assets_->world_grid();
            center_x = grid.parallax_adjusted_screen_x(world_point, center_x);
        }
    }
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

    const float left     = center_x - width * 0.5f;
    const float top      = ef.screen_position.y - height;

    if (enforced_min) {
        width  = static_cast<float>(std::max(1, static_cast<int>(std::lround(width))));
        height = static_cast<float>(std::max(1, static_cast<int>(std::lround(height))));
    }

    return SDL_FRect{ left, top, width, height };
}

SDL_FRect SceneRenderer::get_child_position_rect(const Asset* parent,
                                                 SDL_Point world_point,
                                                 int fw,
                                                 int fh,
                                                 float inv_scale,
                                                 int min_w,
                                                 int min_h,
                                                 float reference_screen_height) {
    if (!parent || fw <= 0 || fh <= 0) {
        return SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f};
    }
    float base_scale = parent->smoothed_scale();
    if (!std::isfinite(base_scale) || base_scale <= 0.0f) {
        base_scale = 1.0f;
    }
    const float scaled_fw = static_cast<float>(fw) * base_scale;
    const float scaled_fh = static_cast<float>(fh) * base_scale;
    const float base_sw   = scaled_fw * inv_scale;
    const float base_sh   = scaled_fh * inv_scale;

    camera& cam = assets_->getView();
    const camera::RenderSmoothingKey smoothing_key =
        reinterpret_cast<camera::RenderSmoothingKey>(parent);
    camera::RenderEffects ef = cam.compute_render_effects(
        world_point,
        base_sh,
        reference_screen_height,
        smoothing_key);
    SDL_FPoint screen = cam.map_to_screen_f(SDL_FPoint{
        static_cast<float>(world_point.x),
        static_cast<float>(world_point.y)
    });
    ef.screen_position = screen;

    float center_x = ef.screen_position.x;
    if (assets_ && assets_->player != parent) {
        world::Grid& grid = assets_->world_grid();
        center_x = grid.parallax_adjusted_screen_x(world_point, center_x);
    }

    const bool apply_distance = parent->info && parent->info->apply_distance_scaling;
    const bool apply_vertical = parent->info && parent->info->apply_vertical_scaling;
    const float distance_scale = apply_distance ? ef.distance_scale : 1.0f;
    const float vertical_scale = apply_vertical ? ef.vertical_scale : 1.0f;

    float width  = base_sw * distance_scale;
    float height = (base_sh * distance_scale) * vertical_scale;

    const float min_w_f = static_cast<float>(min_w);
    const float min_h_f = static_cast<float>(min_h);
    if (width < min_w_f && height < min_h_f) {
        return SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f};
    }

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

    const float left = center_x - width * 0.5f;
    const float top  = ef.screen_position.y - height;

    if (enforced_min) {
        width  = static_cast<float>(std::max(1, static_cast<int>(std::lround(width))));
        height = static_cast<float>(std::max(1, static_cast<int>(std::lround(height))));
    }

    return SDL_FRect{ left, top, width, height };
}
void SceneRenderer::render(){
    ++frame_counter_;
    // Opportunistically prune tinted texture cache to prevent stale growth


    if (light_map_ && !chunk_lighting_suspended_){
        render_pipeline_.lighting().light_map_sampler = light_map_.get();
    } else {
        render_pipeline_.lighting().light_map_sampler = nullptr;
    }

    const SDL_Color map_light_color = main_light_source_.get_current_color();
    const float     map_light_opacity =
        std::clamp(static_cast<float>(map_light_color.a) / 255.0f, 0.0f, 1.0f);
    const float light_overlay_visibility = map_light_opacity;
    const float frame_flicker_time_seconds =
        static_cast<float>(SDL_GetTicks64() % 1000000ULL) * 0.001f;

    // Apply depth-cue per-asset only; disable full-screen post
    SDL_SetRenderTarget(renderer_, nullptr);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const SDL_Color clear_color = light_map_only_mode_ ? SDL_Color{0,0,0,255} : map_clear_color_;
    SDL_SetRenderDrawColor(renderer_, clear_color.r, clear_color.g, clear_color.b, clear_color.a);
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

    light_overlay_sources_.clear();

    if (!light_map_only_mode_){
        const camera* camera_state = assets_ ? &assets_->getView() : nullptr;
        const camera::RealismSettings cam_settings = camera_state
            ? camera_state->realism_settings()
            : camera::RealismSettings{};
        const bool depthcue_setting_enabled = devmode::camera_prefs::load_depthcue_enabled();
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

        // Draw grid tiles first
        if (tile_renderer_) {
            tile_renderer_->render(renderer_, assets_->getView(), assets_->world_grid());
        }

        const int fg_max_opacity = std::clamp(cam_settings.foreground_texture_max_opacity, 0, 255);
        const int bg_max_opacity = std::clamp(cam_settings.background_texture_max_opacity, 0, 255);
        const bool cold_start = frame_counter_ <= static_cast<std::uint64_t>(depthcue_warmup_frames_);
        const bool depthcue_values_active = (fg_max_opacity > 0 || bg_max_opacity > 0);

        float center_screen_y = static_cast<float>(screen_height_) * 0.5f;
        float fg_plane_screen_y = std::clamp(cam_settings.foreground_plane_screen_y, 0.0f, static_cast<float>(screen_height_));
        float bg_plane_screen_y = std::clamp(cam_settings.background_plane_screen_y, 0.0f, static_cast<float>(screen_height_));
        if (camera_state) {
            SDL_FPoint center_world_f = camera_state->get_view_center_f();
            SDL_FPoint center_screen_f = camera_state->map_to_screen_f(center_world_f);
            if (std::isfinite(center_screen_f.y)) {
                center_screen_y = std::clamp(center_screen_f.y, 0.0f, static_cast<float>(screen_height_));
            }
        }
        const float fg_span_screen = std::max(0.0f, fg_plane_screen_y - center_screen_y);
        const float bg_span_screen = std::max(0.0f, center_screen_y - bg_plane_screen_y);
        auto depth_sample_for = [&](float screen_y) -> DepthCueSample {
            return make_depthcue_sample(screen_y,
                                        center_screen_y,
                                        fg_plane_screen_y,
                                        bg_plane_screen_y,
                                        fg_span_screen,
                                        bg_span_screen);
        };

        const auto& active = assets_->getActive();
        // Screen-space culling rect (float) used to filter off-screen assets and attachments
        const SDL_FRect screen_rect_f{ 0.0f, 0.0f,
                                       static_cast<float>(screen_width_),
                                       static_cast<float>(screen_height_) };
        texture_commands_.clear();
        texture_commands_.reserve(active.size());
        remaining_commands_.clear();
        remaining_commands_.reserve(active.size());
        light_overlay_sources_.reserve(active.size());

        auto enqueue_command = [&](Asset* asset,
                                   SDL_Texture* final_tex,
                                   SDL_Texture* draw_tex,
                                   const SDL_FRect& dst_rect,
                                   bool suppress_sprite_draw = false) {
            AssetRenderCommand cmd;
            cmd.asset              = asset;
            cmd.final_texture       = final_tex;
            cmd.dst                 = dst_rect;
            if (!suppress_sprite_draw) {
                cmd.source_texture      = draw_tex ? draw_tex : final_tex;
                cmd.uses_scaled_texture = draw_tex && draw_tex != final_tex;
            } else {
                cmd.source_texture      = nullptr;
                cmd.uses_scaled_texture = false;
            }
            cmd.highlighted         = asset->is_highlighted();
            cmd.selected            = asset->is_selected();
            cmd.flipped             = asset->flipped;
            cmd.alpha               = asset ? asset->smoothed_alpha() : 1.0f;
            if (!std::isfinite(cmd.alpha)) {
                cmd.alpha = 1.0f;
            }
            cmd.alpha = std::clamp(cmd.alpha, 0.0f, 1.0f);

            cmd.depthcue_foreground_texture = nullptr;
            cmd.depthcue_background_texture = nullptr;
            cmd.depthcue_foreground_alpha   = 0;
            cmd.depthcue_background_alpha   = 0;

            const bool is_texture_asset = asset && asset->info && asset->info->type == asset_types::texture;
            bool is_chunk_tiled = false;
            if (asset) {
                const auto& tiling_opt = asset->tiling_info();
                is_chunk_tiled = (tiling_opt && tiling_opt->is_valid());
            }
            const bool is_tillable_asset = asset && asset->info && asset->info->tillable;
            const bool depthcue_allowed = depthcue_setting_enabled && depthcue_values_active &&
                !is_texture_asset && !is_chunk_tiled && !is_tillable_asset &&
                camera_state && camera_state->realism_enabled() && !cold_start;
            if (depthcue_allowed) {
                float wx = asset ? asset->smoothed_translation_x() : 0.0f;
                float wy = asset ? asset->smoothed_translation_y() : 0.0f;
                if (assets_ && assets_->is_dev_mode() && asset) {
                    wx = static_cast<float>(asset->pos.x);
                    wy = static_cast<float>(asset->pos.y);
                }
                SDL_FPoint screen_pos = camera_state->map_to_screen_f(SDL_FPoint{ wx, wy });
                const float screen_y = screen_pos.y;
                const AnimationFrame* frame_ptr = asset ? asset->current_animation_frame() : nullptr;
                const Animation* current_anim = nullptr;
                if (asset && asset->info) {
                    auto anim_it = asset->info->animations.find(asset->current_animation);
                    if (anim_it != asset->info->animations.end()) {
                        current_anim = &anim_it->second;
                    }
                }
                SDL_Texture* fg_overlay = nullptr;
                SDL_Texture* bg_overlay = nullptr;
                if (current_anim && frame_ptr) {
                    fg_overlay = current_anim->depthcue_foreground_texture(frame_ptr);
                    bg_overlay = current_anim->depthcue_background_texture(frame_ptr);
                }
                const DepthCueSample depth_sample = depth_sample_for(screen_y);
                if (depth_sample.plane != DepthCuePlane::None) {
                    if (depth_sample.plane == DepthCuePlane::Foreground && fg_overlay && fg_max_opacity > 0) {
                        const float normalized = compute_depthcue_opacity(
                            depth_sample,
                            fg_max_opacity,
                            cam_settings.texture_opacity_falloff_method);
                        const int alpha_value = static_cast<int>(std::round(normalized * 255.0f));
                        cmd.depthcue_foreground_alpha = static_cast<Uint8>(std::clamp(alpha_value, 0, 255));
                        if (cmd.depthcue_foreground_alpha > 0) {
                            cmd.depthcue_foreground_texture = fg_overlay;
                        }
                    } else if (depth_sample.plane == DepthCuePlane::Background && bg_overlay && bg_max_opacity > 0) {
                        const float normalized = compute_depthcue_opacity(
                            depth_sample,
                            bg_max_opacity,
                            cam_settings.texture_opacity_falloff_method);
                        const int alpha_value = static_cast<int>(std::round(normalized * 255.0f));
                        cmd.depthcue_background_alpha = static_cast<Uint8>(std::clamp(alpha_value, 0, 255));
                        if (cmd.depthcue_background_alpha > 0) {
                            cmd.depthcue_background_texture = bg_overlay;
                        }
                }
            }
            }

            auto& target_commands = (asset->info->type == asset_types::texture) ? texture_commands_ : remaining_commands_;
            target_commands.push_back(std::move(cmd));
        };

        for (Asset* a : active) {
            if (!a || !a->info) {
                continue;
            }

            // Tileable assets are composited into grid tiles and should skip the normal sprite pass.
            const auto& tiling_opt      = a->tiling_info();
            const bool  is_chunk_tiled  = tiling_opt && tiling_opt->is_valid();
            const bool  has_light_sources = !a->info->light_sources.empty();

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

            // Skip assets fully outside the viewport to avoid texture selection and draw work
            if (!intersects_padded(dst, screen_rect_f)) {
                if (a->current_frame) {
                    last_rendered_frames_[a] = a->current_frame;
                } else {
                    last_rendered_frames_.erase(a);
                }
                continue;
            }

            if (!is_chunk_tiled) {
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
            } else if (has_light_sources) {
                // Keep a command placeholder so the lighting system can still sample this asset.
                enqueue_command(a, final_tex, nullptr, dst, /*suppress_sprite_draw=*/true);
            }

            if (!a->animation_children().empty()) {
                const auto& child_slots = a->animation_children();
                for (std::size_t child_index = 0; child_index < child_slots.size(); ++child_index) {
                    const auto& attachment = child_slots[child_index];
                    if (!attachment.visible || !attachment.animation || !attachment.current_frame) {
                        continue;
                    }
                    SDL_Texture* child_tex = attachment.animation->get_frame(attachment.current_frame);
                    if (!child_tex) {
                        continue;
                    }
                    int child_fw = attachment.cached_w;
                    int child_fh = attachment.cached_h;
                    if ((child_fw == 0 || child_fh == 0)) {
                        SDL_QueryTexture(child_tex, nullptr, nullptr, &child_fw, &child_fh);
                        if (child_fw > 0 && child_fh > 0) {
                            auto& mutable_slot = const_cast<Asset::AnimationChildAttachment&>(child_slots[child_index]);
                            mutable_slot.cached_w = child_fw;
                            mutable_slot.cached_h = child_fh;
                        }
                    }
                    SDL_FRect child_rect = get_child_position_rect(
                        a,
                        attachment.world_pos,
                        child_fw,
                        child_fh,
                        inv_scale,
                        min_w,
                        min_h,
                        player_sh);
                    if (child_rect.w <= 0.0f || child_rect.h <= 0.0f) {
                        continue;
                    }
                    // Cull child attachments fully off-screen
                    if (!intersects_padded(child_rect, screen_rect_f)) {
                        continue;
                    }
                    AssetRenderCommand child_cmd;
                    child_cmd.asset = a;
                    child_cmd.final_texture = child_tex;
                    child_cmd.source_texture = child_tex;
                    child_cmd.dst = child_rect;
                    child_cmd.highlighted = a->is_highlighted();
                    child_cmd.selected = a->is_selected();
                    child_cmd.flipped = a->flipped;
                    child_cmd.alpha = a ? a->smoothed_alpha() : 1.0f;
                    if (!std::isfinite(child_cmd.alpha)) {
                        child_cmd.alpha = 1.0f;
                    }
                    child_cmd.alpha = std::clamp(child_cmd.alpha, 0.0f, 1.0f);
                    child_cmd.rotation_degrees = attachment.rotation_degrees;
                    if (std::fabs(attachment.rotation_degrees) > std::numeric_limits<float>::epsilon()) {
                        child_cmd.has_custom_pivot = true;
                        child_cmd.rotation_pivot = SDL_FPoint{ child_rect.w * 0.5f, child_rect.h };
                    }
                    remaining_commands_.push_back(std::move(child_cmd));
                }
            }

            if (has_light_sources && dst.w > 0.0f && dst.h > 0.0f && fw > 0 && fh > 0) {
                // Skip creating overlay sources for off-screen assets
                if (!intersects_padded(dst, screen_rect_f)) {
                    if (a->current_frame) {
                        last_rendered_frames_[a] = a->current_frame;
                    } else {
                        last_rendered_frames_.erase(a);
                    }
                    continue;
                }
                bool has_front_lights         = false;
                bool has_back_lights          = false;
                bool has_dark_mask_lights     = false;
                bool has_alpha_mask_only_lights = false;
                for (const LightSource& light : a->info->light_sources) {
                    has_front_lights         |= light.in_front;
                    has_back_lights          |= light.behind;
                    has_dark_mask_lights     |= light.render_to_dark_mask;
                    has_alpha_mask_only_lights |=
                        light.render_front_and_back_to_asset_alpha_mask && !light.in_front &&
                        !light.behind;
                    if (has_front_lights && has_back_lights && has_dark_mask_lights && has_alpha_mask_only_lights) {
                        break;
                    }
                }
                if (!(has_front_lights || has_back_lights || has_dark_mask_lights || has_alpha_mask_only_lights)) {
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
                source.asset_base_scale     = base_scale;
                source.has_front_lights     = has_front_lights;
                // Treat alpha-mask-only lights as a behind-pass so they get processed.
                source.has_back_lights      = has_back_lights || has_alpha_mask_only_lights;
                source.has_dark_mask_lights = has_dark_mask_lights;
                light_overlay_sources_.push_back(std::move(source));
            }

            if (a->current_frame) {
                last_rendered_frames_[a] = a->current_frame;
            } else {
                last_rendered_frames_.erase(a);
            }
        }

    // ---- Modified: bold outline around non-transparent pixels (no interior fill) ----
    std::unordered_map<const Asset*, const LightOverlaySource*> overlay_lookup;
    overlay_lookup.reserve(light_overlay_sources_.size());
    for (const auto& source : light_overlay_sources_) {
        if (!source.asset) {
            continue;
        }
        overlay_lookup.emplace(source.asset, &source);
    }

    std::vector<const LightOverlaySource*> pending_front_lights;

    auto render_commands = [&](const std::vector<AssetRenderCommand>& commands, bool overlay_passed) {
        const int outline_px = 3; // outline thickness in screen pixels

        // 8-direction offsets for a chunky outline; adjust if you want thinner/thicker
        const SDL_FPoint OFFS[] = {
            {  0, -1 }, {  0,  1 }, { -1,  0 }, {  1,  0 },
            { -1, -1 }, {  1, -1 }, { -1,  1 }, {  1,  1 },
            // add an extra "ring" for a bolder edge
            {  0, -2 }, {  0,  2 }, { -2,  0 }, {  2,  0 }
        };

        for (const AssetRenderCommand& cmd : commands) {
            const LightOverlaySource* overlay_source = nullptr;
            bool has_front_light = false;
            if (cmd.asset) {
                if (const auto it = overlay_lookup.find(cmd.asset); it != overlay_lookup.end()) {
                    overlay_source = it->second;
                    if (overlay_source->has_back_lights && light_overlay_visibility > 0.0f) {
                        // Per-asset depth-cue compositing may render lights into a local target.
                        // We only draw behind-lights directly when not compositing for this asset.
                    }
                    has_front_light = light_overlay_visibility > 0.0f && overlay_source->has_front_lights;
                }
            }
            SDL_Texture* tex = cmd.source_texture;

            // Draw behind-lights directly in non-composited path
            if (overlay_source && overlay_source->has_back_lights && light_overlay_visibility > 0.0f) {
                AssetLightRenderer light_renderer(renderer_,
                                                  *overlay_source,
                                                  darkness_overlay_vertices_,
                                                  darkness_overlay_indices_,
                                                  light_overlay_visibility,
                                                  frame_flicker_time_seconds);
                light_renderer.draw_behind();
            }
            if (!tex) {
                if (overlay_source && has_front_light) {
                    pending_front_lights.push_back(overlay_source);
                }
                // Tinted textures are cached and owned by the renderer cache; do not destroy here.
                continue;
            }

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
                    cmd.rotation_degrees,
                    cmd.has_custom_pivot ? &cmd.rotation_pivot : nullptr,
                    cmd.flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE
                );
                }

                // Restore defaults on the texture before the base pass
                SDL_SetTextureColorMod(tex, 255, 255, 255);
                SDL_SetTextureAlphaMod(tex, 255);
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            }

            // --------------------
            // 2) BASE SPRITE PASS (grid-sliced trapezoids when large relative to grid)
            //    Always draw base pass so blur never reduces opacity
            // --------------------
            bool drew_grid_sliced = false;
            if (assets_ && cmd.asset && tex) {
                // Only apply when not already handled by loader-composed grid tiles
                const auto& tiling_opt = cmd.asset->tiling_info();
                const bool allow_grid_sliced = (cmd.asset->info && cmd.asset->info->tillable);
                const bool tiling_managed_by_chunk = (tiling_opt && tiling_opt->is_valid());
                if (allow_grid_sliced && !tiling_managed_by_chunk) {
                    // Compute grid step (world units)
                    const int grid_step = std::max(1, 1 << std::clamp(cmd.asset->grid_resolution, 0, vibble::grid::kMaxResolution));

                    // Camera parameters
                    camera& cam = assets_->getView();
                    const float scale = std::max(1e-6f, cam.get_scale());
                    const float inv_scale_local = 1.0f / scale;

                    // Asset base dimensions and scale
                    int fw = cmd.asset->cached_w;
                    int fh = cmd.asset->cached_h;
                    if ((fw <= 0 || fh <= 0) && cmd.final_texture) {
                        SDL_QueryTexture(cmd.final_texture, nullptr, nullptr, &fw, &fh);
                    }
                    float base_scale = cmd.asset->smoothed_scale();
                    if (!std::isfinite(base_scale) || base_scale <= 0.0f) base_scale = 1.0f;
                    const float scaled_fw = static_cast<float>(fw) * base_scale;
                    const float scaled_fh = static_cast<float>(fh) * base_scale;
                    const float base_sh   = scaled_fh * inv_scale_local;

                    // Reference screen height (player) for realism effects
                    float ref_sh = player_sh;
                    if (!std::isfinite(ref_sh) || ref_sh <= 0.0f) ref_sh = 1.0f;

                    // Effects (distance + vertical squash)
                    const SDL_Point world_point{
                        static_cast<int>(std::lround(cmd.asset->smoothed_translation_x())),
                        static_cast<int>(std::lround(cmd.asset->smoothed_translation_y()))
                    };
                    camera::RenderEffects ef = cam.compute_render_effects(world_point, base_sh, ref_sh, reinterpret_cast<camera::RenderSmoothingKey>(cmd.asset));
                    const float distance_scale = (cmd.asset->info && cmd.asset->info->apply_distance_scaling) ? ef.distance_scale : 1.0f;
                    const float vertical_scale = (cmd.asset->info && cmd.asset->info->apply_vertical_scaling) ? ef.vertical_scale : 1.0f;

                    // Determine world extents covered by this sprite so we can slice on world grid
                    const float world_width  = cmd.dst.w * scale / std::max(1e-6f, distance_scale);
                    const float world_height = cmd.dst.h * scale / std::max(1e-6f, (distance_scale * vertical_scale));

                    if (std::isfinite(world_width) && std::isfinite(world_height) && world_width > 0.0f && world_height > 0.0f) {
                        // Only slice when larger than a single grid cell in at least one dimension
                        if (world_width > static_cast<float>(grid_step) || world_height > static_cast<float>(grid_step)) {
                            // Anchor world and screen positions
                            SDL_FPoint anchor_screen = cam.map_to_screen_f(SDL_FPoint{ static_cast<float>(world_point.x), static_cast<float>(world_point.y) });

                            const double left_world   = static_cast<double>(world_point.x) - static_cast<double>(world_width) * 0.5;
                            const double top_world    = static_cast<double>(world_point.y) - static_cast<double>(world_height);
                            const double right_world  = left_world + static_cast<double>(world_width);
                            const double bottom_world = static_cast<double>(world_point.y);

                            auto align_down = [](double v, int step) {
                                if (step <= 0) return v;
                                const double s = static_cast<double>(step);
                                return std::floor(v / s) * s;
                            };
                            auto align_up = [](double v, int step) {
                                if (step <= 0) return v;
                                const double s = static_cast<double>(step);
                                return std::ceil(v / s) * s;
                            };

                            const double start_x = align_down(left_world, grid_step);
                            const double end_x   = align_up(right_world, grid_step);
                            const double start_y = align_down(top_world, grid_step);
                            const double end_y   = align_up(bottom_world, grid_step);

                            // Normalize helpers for UVs (relative to entire texture)
                            const double inv_w = (world_width > 0.0f) ? (1.0 / static_cast<double>(world_width)) : 0.0;
                            const double inv_h = (world_height > 0.0f) ? (1.0 / static_cast<double>(world_height)) : 0.0;

                            const SDL_Color white{255,255,255,255};
                            int indices[6] = {0, 1, 2, 0, 2, 3};

                            // Ensure the texture is in a neutral mod state; use vertex alpha/color
                            SDL_SetTextureColorMod(tex, 255, 255, 255);
                            SDL_SetTextureAlphaMod(tex, 255);
                            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

                            for (double wy = start_y; wy < end_y; wy += grid_step) {
                                const double cell_top    = std::max(wy, top_world);
                                const double cell_bottom = std::min(wy + grid_step, end_y);
                                if (cell_bottom <= cell_top) continue;
                                for (double wx = start_x; wx < end_x; wx += grid_step) {
                                    const double cell_left  = std::max(wx, left_world);
                                    const double cell_right = std::min(wx + grid_step, end_x);
                                    if (cell_right <= cell_left) continue;

                                    // World corners of this patch (TL, TR, BR, BL)
                                    SDL_FPoint w_tl{ static_cast<float>(cell_left),  static_cast<float>(cell_top) };
                                    SDL_FPoint w_tr{ static_cast<float>(cell_right), static_cast<float>(cell_top) };
                                    SDL_FPoint w_br{ static_cast<float>(cell_right), static_cast<float>(cell_bottom) };
                                    SDL_FPoint w_bl{ static_cast<float>(cell_left),  static_cast<float>(cell_bottom) };

                                    auto to_screen = [&](const SDL_FPoint& w)->SDL_FPoint {
                                        // Offsets from anchor in world units
                                        const double dx_world = static_cast<double>(w.x) - static_cast<double>(world_point.x);
                                        const double dy_world = static_cast<double>(world_point.y) - static_cast<double>(w.y);
                                        double sx = static_cast<double>(anchor_screen.x) + (dx_world * inv_scale_local * distance_scale);
                                        double sy = static_cast<double>(anchor_screen.y) - (dy_world * inv_scale_local * distance_scale * vertical_scale);
                                        // Parallax-adjusted X
                                        if (!(assets_ && assets_->player == cmd.asset)) {
                                            sx = static_cast<double>(assets_->world_grid().parallax_adjusted_screen_x(
                                                SDL_Point{ static_cast<int>(std::lround(w.x)), static_cast<int>(std::lround(w.y)) },
                                                static_cast<float>(sx)));
                                        }
                                        return SDL_FPoint{ static_cast<float>(sx), static_cast<float>(sy) };
                                    };

                                    SDL_FPoint s_tl = to_screen(w_tl);
                                    SDL_FPoint s_tr = to_screen(w_tr);
                                    SDL_FPoint s_br = to_screen(w_br);
                                    SDL_FPoint s_bl = to_screen(w_bl);

                                    // UVs normalized to 0..1 over the full sprite
                                    double u0 = (static_cast<double>(w_tl.x) - left_world) * inv_w;
                                    double u1 = (static_cast<double>(w_tr.x) - left_world) * inv_w;
                                    double v0 = (static_cast<double>(w_tl.y) - top_world)  * inv_h;
                                    double v1 = (static_cast<double>(w_br.y) - top_world)  * inv_h;

                                    // Horizontal flip
                                    if (cmd.flipped) {
                                        u0 = 1.0 - u0;
                                        u1 = 1.0 - u1;
                                        std::swap(u0, u1);
                                    }

                                    const Uint8 a_mod = base_alpha_mod;
                                    SDL_Vertex verts[4]{};
                                    verts[0].position = s_tl; verts[0].color = SDL_Color{255,255,255,a_mod}; verts[0].tex_coord = SDL_FPoint{ static_cast<float>(u0), static_cast<float>(v0) };
                                    verts[1].position = s_tr; verts[1].color = SDL_Color{255,255,255,a_mod}; verts[1].tex_coord = SDL_FPoint{ static_cast<float>(u1), static_cast<float>(v0) };
                                    verts[2].position = s_br; verts[2].color = SDL_Color{255,255,255,a_mod}; verts[2].tex_coord = SDL_FPoint{ static_cast<float>(u1), static_cast<float>(v1) };
                                    verts[3].position = s_bl; verts[3].color = SDL_Color{255,255,255,a_mod}; verts[3].tex_coord = SDL_FPoint{ static_cast<float>(u0), static_cast<float>(v1) };

                                    SDL_RenderGeometry(renderer_, tex, verts, 4, indices, 6);
                                }
                            }

                            drew_grid_sliced = true;
                        }
                    }
                }
            }

            if (!drew_grid_sliced) {
                // Fallback: draw normally as a rect
                SDL_SetTextureColorMod(tex, 255, 255, 255);
                SDL_SetTextureAlphaMod(tex, base_alpha_mod);
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                SDL_RenderCopyExF(
                    renderer_,
                    tex,
                    nullptr,
                    &cmd.dst,
                    cmd.rotation_degrees,
                    cmd.has_custom_pivot ? &cmd.rotation_pivot : nullptr,
                    cmd.flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE
                );
            }
            auto draw_depthcue_overlay = [&](SDL_Texture* overlay_tex, Uint8 overlay_alpha) {
                if (!overlay_tex || overlay_alpha == 0) {
                    return;
                }
                SDL_SetTextureBlendMode(overlay_tex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureColorMod(overlay_tex, 255, 255, 255);
                SDL_SetTextureAlphaMod(overlay_tex, overlay_alpha);
                SDL_RenderCopyExF(
                    renderer_,
                    overlay_tex,
                    nullptr,
                    &cmd.dst,
                    cmd.rotation_degrees,
                    cmd.has_custom_pivot ? &cmd.rotation_pivot : nullptr,
                    cmd.flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE
                );
                SDL_SetTextureAlphaMod(overlay_tex, 255);
            };
            draw_depthcue_overlay(cmd.depthcue_background_texture, cmd.depthcue_background_alpha);
            draw_depthcue_overlay(cmd.depthcue_foreground_texture, cmd.depthcue_foreground_alpha);

            if (cmd.uses_scaled_texture && cmd.final_texture) {
                SDL_SetTextureColorMod(cmd.final_texture, 255, 255, 255);
                SDL_SetTextureAlphaMod(cmd.final_texture, 255);
                SDL_SetTextureBlendMode(cmd.final_texture, SDL_BLENDMODE_BLEND);
            }

            if (overlay_source && has_front_light) {
                pending_front_lights.push_back(overlay_source);
            }
            // Tinted textures are cached and owned by the renderer cache; do not destroy here.
        }

        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    };
    // -------------------------------------------------------------------------------


        render_commands(texture_commands_, /*overlay_passed=*/false);
        if (light_map_ && !chunk_lighting_suspended_) {
            light_map_->update(renderer_, 0u);
        }
        render_commands(remaining_commands_, /*overlay_passed=*/false);

        // After all base sprites are drawn, apply the dynamic darkness overlay once
        if (dark_mask_enabled_) {
            render_dynamic_darkness_overlay(map_light_opacity, frame_flicker_time_seconds);
        }
        render_light_map();

        // Draw all deferred front-lights above the darkness overlay
        if (light_overlay_visibility > 0.0f) {
            for (const LightOverlaySource* src : pending_front_lights) {
                if (src && src->has_front_lights) {
                    AssetLightRenderer light_renderer(renderer_,
                                                      *src,
                                                      darkness_overlay_vertices_,
                                                      darkness_overlay_indices_,
                                                      light_overlay_visibility,
                                                      frame_flicker_time_seconds);
                    light_renderer.draw_in_front();
                }
            }
        }
        pending_front_lights.clear();

        for (auto it = last_rendered_frames_.begin(); it != last_rendered_frames_.end();) {
            Asset* asset = it->first;
            if (!asset || asset->last_render_frame_id != frame_counter_) {
                it = last_rendered_frames_.erase(it);
            } else {
                ++it;
            }
        }
    } else {
        // Light-map only mode: keep original behavior
        SDL_SetRenderTarget(renderer_, nullptr);
        render_light_map();
        if (chunk_preview_enabled_ && light_map_) {
            SDL_Rect screen_view{0, 0, screen_width_, screen_height_};
            light_map_->render_chunk_preview(renderer_, screen_view);
        }
        if (assets_) {
            assets_->render_overlays(renderer_);
        }
        SDL_RenderPresent(renderer_);
    }
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

void SceneRenderer::render_dynamic_darkness_overlay(float map_light_opacity, float flicker_time_seconds) {
    if (!renderer_) {
        return;
    }

    const float overlay_alpha             = std::clamp(map_light_opacity, 0.0f, 1.0f);
    const float light_overlay_visibility = overlay_alpha;
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
        if (!source.has_dark_mask_lights) {
            continue;
        }
        AssetLightRenderer light_renderer(renderer_,
                                          source,
                                          darkness_overlay_vertices_,
                                          darkness_overlay_indices_,
                                          light_overlay_visibility,
                                          flicker_time_seconds);
        auto               result = light_renderer.accumulate_dark_mask();
        frame_max_vertices        = std::max(frame_max_vertices, result.max_vertices);
        frame_max_indices         = std::max(frame_max_indices, result.max_indices);
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


