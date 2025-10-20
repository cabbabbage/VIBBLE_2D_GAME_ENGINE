#include "scene_renderer.hpp"
#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "world/chunk.hpp"
#include "render/camera.hpp"
#include "dev_mode/dev_ui_settings.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"
#include "render_pipeline/ScalingLogic.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>
#include <cstdlib>

static constexpr SDL_Color SLATE_COLOR = {69, 101, 74, 255};
static constexpr float kDefaultMinVisibleScreenRatio = 0.015f;

namespace {
constexpr std::string_view kUpdateMapLightSettingKey = "dev_ui.lighting.map_panel.update_map_light";
constexpr std::int64_t kMaxStaticLightTextureBytes   = 512LL * 1024LL * 1024LL; // 512 MiB ceiling
constexpr std::int64_t kBytesPerPixel                = static_cast<std::int64_t>(sizeof(std::uint32_t));
constexpr std::int64_t kMaxStaticLightPixels         =
    kMaxStaticLightTextureBytes / (kBytesPerPixel > 0 ? kBytesPerPixel : 1);
static_assert(kMaxStaticLightPixels > 0, "Max static light pixels must be positive.");

bool safe_loading_enabled() {
    const char* value = std::getenv("VIBBLE_SAFE_LOADING");
    if (!value) {
        return false;
    }
    return value[0] == '1' || value[0] == 'y' || value[0] == 'Y' || value[0] == 't' || value[0] == 'T';
}

// Simple helper to limit verbose logs.
struct LogLimiter {
    int first_n = 10;
    int every_k = 100;
    bool operator()(int index) const {
        if (index < first_n) {
            return true;
        }
        return (index % every_k) == 0;
    }
};

std::string rect_str(const SDL_Rect& r) {
    return std::string("{x=") + std::to_string(r.x) + ", y=" + std::to_string(r.y) +
           ", w=" + std::to_string(r.w) + ", h=" + std::to_string(r.h) + "}";
}

SDL_BlendMode erase_alpha_blend_mode() {
#if SDL_VERSION_ATLEAST(2, 0, 6)
    static const SDL_BlendMode mode = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ZERO,
        SDL_BLENDFACTOR_ONE,
        SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO,
        SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        SDL_BLENDOPERATION_ADD);
    return mode;
#else
    return SDL_BLENDMODE_ADD;
#endif
}

struct LightStats {
    float min_strength   = 0.0f;
    float max_strength   = 0.0f;
    float average_strength = 0.0f;
};

LightStats compute_light_stats(SDL_Renderer* renderer, SDL_Texture* texture) {
    LightStats stats{};
    if (!renderer || !texture) {
        return stats;
    }

    int tex_w = 0;
    int tex_h = 0;
    if (SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h) != 0 || tex_w <= 0 || tex_h <= 0) {
        vibble::log::warn(std::string("[SceneRenderer] Failed to query chunk texture for light stats: ") + SDL_GetError());
        return stats;
    }

    std::unique_ptr<SDL_PixelFormat, decltype(&SDL_FreeFormat)> format(SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888),
                                                                       SDL_FreeFormat);
    if (!format) {
        vibble::log::warn("[SceneRenderer] Unable to allocate pixel format for light stats computation.");
        return stats;
    }

    const int pitch = tex_w * static_cast<int>(sizeof(std::uint32_t));
    std::vector<std::uint32_t> row(static_cast<std::size_t>(tex_w));

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, texture) != 0) {
        vibble::log::warn(std::string("[SceneRenderer] Failed to set chunk texture as render target for stats: ") + SDL_GetError());
        return stats;
    }

    double accum = 0.0;
    float min_strength = 1.0f;
    float max_strength = 0.0f;
    const double inv_255 = 1.0 / 255.0;
    bool any_samples = false;

    for (int y = 0; y < tex_h; ++y) {
        SDL_Rect row_rect{0, y, tex_w, 1};
        if (SDL_RenderReadPixels(renderer, &row_rect, SDL_PIXELFORMAT_RGBA8888, row.data(), pitch) != 0) {
            vibble::log::warn(std::string("[SceneRenderer] Failed to read pixels for light stats (row=") +
                              std::to_string(y) + "): " + SDL_GetError());
            SDL_SetRenderTarget(renderer, previous_target);
            return stats;
        }

        for (int x = 0; x < tex_w; ++x) {
            Uint8 a = 255;
            SDL_GetRGBA(row[static_cast<std::size_t>(x)], format.get(), nullptr, nullptr, nullptr, &a);
            const float brightness = std::clamp(1.0f - static_cast<float>(a) * static_cast<float>(inv_255), 0.0f, 1.0f);
            min_strength = std::min(min_strength, brightness);
            max_strength = std::max(max_strength, brightness);
            accum += brightness;
            any_samples = true;
        }
    }

    SDL_SetRenderTarget(renderer, previous_target);

    if (any_samples) {
        stats.min_strength     = std::clamp(min_strength, 0.0f, 1.0f);
        stats.max_strength     = std::clamp(max_strength, 0.0f, 1.0f);
        stats.average_strength = std::clamp(static_cast<float>(accum / static_cast<double>(tex_w * tex_h)), 0.0f, 1.0f);
    }
    return stats;
}

bool is_static_light_asset(const Asset* asset) {
    if (!asset || asset->is_hidden()) {
        return false;
    }
    if (!asset->info) {
        return false;
    }
    if (asset->info->light_sources.empty()) {
        return false;
    }
    return !asset->info->moving_asset;
}

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
    light_map_ = std::make_unique<LightMap>(assets_,
                                            screen_width_,
                                            screen_height_);
    if (light_map_) {
        initialize_static_light_chunks();
        // Use 2 grid-spaces per chunk by default
        light_map_->set_cells_per_chunk(2);
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

void SceneRenderer::set_virtual_light_map_chunks(int chunks) {
    if (light_map_) {
        light_map_->set_virtual_light_map_chunks(chunks);
    }
    force_virtual_light_map_refresh();
}

void SceneRenderer::set_virtual_light_map_chunk_size(int size_px) {
    if (light_map_) {
        light_map_->set_virtual_light_map_chunk_size(size_px);
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

void SceneRenderer::initialize_static_light_chunks() {
    if (!renderer_ || !assets_) {
        vibble::log::debug("[SceneRenderer] Skipping static light initialization (renderer or assets unavailable).");
        return;
    }

    world::Grid& grid = assets_->world_grid();
    std::vector<world::Chunk*> chunks = grid.all_chunks();
    if (chunks.empty()) {
        vibble::log::info("[SceneRenderer] No map chunks detected; static light precomputation skipped.");
        return;
    }

    const bool safe_mode = safe_loading_enabled();
    if (safe_mode) {
        vibble::log::warn("[SceneRenderer] SAFE LOADING enabled; static light textures will not be generated.");
        for (world::Chunk* chunk : chunks) {
            if (!chunk) {
                continue;
            }
            if (chunk->static_light_map) {
                SDL_DestroyTexture(chunk->static_light_map);
                chunk->static_light_map = nullptr;
            }
            chunk->base_brightness                 = 0.0f;
            chunk->light.min_static_avg_strength   = 0.0f;
            chunk->light.max_static_avg_strength   = 0.0f;
            chunk->light.needs_update              = true;
            chunk->lighting_dirty                  = false;
        }
        return;
    }

    std::vector<const Asset*> static_lights;
    static_lights.reserve(assets_->all.size());
    for (Asset* asset : assets_->all) {
        if (is_static_light_asset(asset)) {
            static_lights.push_back(asset);
        }
    }

    if (static_lights.empty()) {
        vibble::log::info("[SceneRenderer] No static light assets detected; clearing chunk light data.");
        for (world::Chunk* chunk : chunks) {
            if (!chunk) {
                continue;
            }
            if (chunk->static_light_map) {
                SDL_DestroyTexture(chunk->static_light_map);
                chunk->static_light_map = nullptr;
            }
            chunk->base_brightness                 = 0.0f;
            chunk->light.min_static_avg_strength   = 0.0f;
            chunk->light.max_static_avg_strength   = 0.0f;
            chunk->light.needs_update              = true;
            chunk->lighting_dirty                  = false;
        }
        return;
    }

    bool have_bounds = false;
    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;
    for (const world::Chunk* chunk : chunks) {
        if (!chunk) {
            continue;
        }
        const SDL_Rect& bounds = chunk->world_bounds;
        if (bounds.w <= 0 || bounds.h <= 0) {
            continue;
        }
        if (!have_bounds) {
            min_x = bounds.x;
            min_y = bounds.y;
            max_x = bounds.x + bounds.w;
            max_y = bounds.y + bounds.h;
            have_bounds = true;
        } else {
            min_x = std::min(min_x, bounds.x);
            min_y = std::min(min_y, bounds.y);
            max_x = std::max(max_x, bounds.x + bounds.w);
            max_y = std::max(max_y, bounds.y + bounds.h);
        }
    }

    if (!have_bounds) {
        vibble::log::warn("[SceneRenderer] Unable to determine full-map bounds for static lighting.");
        return;
    }

    const std::int64_t width64  = static_cast<std::int64_t>(max_x) - static_cast<std::int64_t>(min_x);
    const std::int64_t height64 = static_cast<std::int64_t>(max_y) - static_cast<std::int64_t>(min_y);
    if (width64 <= 0 || height64 <= 0) {
        vibble::log::warn(std::string("[SceneRenderer] Invalid full-map bounds for static lighting: ") +
                          std::to_string(width64) + "x" + std::to_string(height64));
        return;
    }

    bool overflow = width64 > std::numeric_limits<std::int64_t>::max() / std::max<std::int64_t>(height64, 1);
    const std::int64_t pixel_count64 = overflow ? 0 : width64 * height64;

    if (!overflow && pixel_count64 > kMaxStaticLightPixels) {
        vibble::log::warn(std::string("[SceneRenderer] Full-map light texture exceeds size cap: ") +
                          std::to_string(width64) + "x" + std::to_string(height64));
        overflow = true;
    }

    SDL_Rect full_bounds{min_x, min_y, static_cast<int>(width64), static_cast<int>(height64)};
    SDL_Texture* full_texture = nullptr;
    SDL_Texture* previous_target = nullptr;
    const SDL_BlendMode erase_blend = erase_alpha_blend_mode();
    bool using_full_texture = false;

    if (!overflow) {
        full_texture = SDL_CreateTexture(renderer_,
                                         SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET,
                                         full_bounds.w,
                                         full_bounds.h);
        if (!full_texture) {
            vibble::log::warn(std::string("[SceneRenderer] Failed to allocate full-map static light texture: ") + SDL_GetError());
        } else {
            SDL_SetTextureBlendMode(full_texture, SDL_BLENDMODE_BLEND);
            previous_target = SDL_GetRenderTarget(renderer_);
            if (SDL_SetRenderTarget(renderer_, full_texture) != 0) {
                vibble::log::warn(std::string("[SceneRenderer] Failed to bind full-map texture for rendering: ") + SDL_GetError());
                SDL_DestroyTexture(full_texture);
                full_texture = nullptr;
            } else {
                SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
                SDL_RenderClear(renderer_);

                LogLimiter limiter{};
                int stamped = 0;

                for (const Asset* asset : static_lights) {
                    if (!asset || !asset->info) {
                        continue;
                    }
                    for (const auto& light : asset->info->light_sources) {
                        SDL_Texture* source = light.texture;
                        if (!source) {
                            continue;
                        }
                        int src_w = light.cached_w > 0 ? light.cached_w : 0;
                        int src_h = light.cached_h > 0 ? light.cached_h : 0;
                        if (src_w <= 0 || src_h <= 0) {
                            SDL_QueryTexture(source, nullptr, nullptr, &src_w, &src_h);
                        }
                        if (src_w <= 0 || src_h <= 0) {
                            continue;
                        }

                        const int draw_w = std::max(1, src_w);
                        const int draw_h = std::max(1, src_h);
                        SDL_Rect world_dst{
                            asset->pos.x + light.offset_x - draw_w / 2,
                            asset->pos.y + light.offset_y - draw_h / 2,
                            draw_w,
                            draw_h
                        };
                        SDL_Rect local_dst = world_dst;
                        local_dst.x -= full_bounds.x;
                        local_dst.y -= full_bounds.y;

                        Uint8 saved_r = 255, saved_g = 255, saved_b = 255, saved_a = 255;
                        SDL_BlendMode saved_blend = SDL_BLENDMODE_BLEND;
                        SDL_GetTextureColorMod(source, &saved_r, &saved_g, &saved_b);
                        SDL_GetTextureAlphaMod(source, &saved_a);
                        SDL_GetTextureBlendMode(source, &saved_blend);
                        SDL_SetTextureBlendMode(source, erase_blend);
                        SDL_SetTextureColorMod(source, 255, 255, 255);
                        SDL_SetTextureAlphaMod(source, 255);
                        SDL_RenderCopy(renderer_, source, nullptr, &local_dst);
                        SDL_SetTextureBlendMode(source, saved_blend);
                        SDL_SetTextureColorMod(source, saved_r, saved_g, saved_b);
                        SDL_SetTextureAlphaMod(source, saved_a);

                        if (limiter(stamped)) {
                            vibble::log::debug(std::string("[SceneRenderer] Stamped static light onto full texture dst=") +
                                               rect_str(local_dst));
                        }
                        ++stamped;
                    }
                }

                SDL_SetRenderTarget(renderer_, previous_target);
                using_full_texture = true;
            }
        }
    } else {
        vibble::log::warn("[SceneRenderer] Falling back to per-chunk static light baking due to size limits.");
    }

    int baked_chunks = 0;
    int fallback_chunks = 0;
    LogLimiter chunk_log_limiter{6, 24};

    for (world::Chunk* chunk : chunks) {
        if (!chunk) {
            continue;
        }
        const SDL_Rect& bounds = chunk->world_bounds;
        if (bounds.w <= 0 || bounds.h <= 0) {
            continue;
        }

        if (chunk->static_light_map) {
            SDL_DestroyTexture(chunk->static_light_map);
            chunk->static_light_map = nullptr;
        }

        SDL_Texture* chunk_texture = SDL_CreateTexture(renderer_,
                                                       SDL_PIXELFORMAT_RGBA8888,
                                                       SDL_TEXTUREACCESS_TARGET,
                                                       bounds.w,
                                                       bounds.h);
        if (!chunk_texture) {
            vibble::log::warn(std::string("[SceneRenderer] Failed to allocate chunk light texture for bounds ") +
                              rect_str(bounds) + ": " + SDL_GetError());
            chunk->base_brightness               = 0.0f;
            chunk->light.min_static_avg_strength = 0.0f;
            chunk->light.max_static_avg_strength = 0.0f;
            chunk->light.needs_update            = true;
            chunk->lighting_dirty                = false;
            continue;
        }

        SDL_SetTextureBlendMode(chunk_texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(chunk_texture, SDL_ScaleModeBest);
#endif

        SDL_Texture* restore_target = SDL_GetRenderTarget(renderer_);
        if (SDL_SetRenderTarget(renderer_, chunk_texture) != 0) {
            vibble::log::warn(std::string("[SceneRenderer] Failed to bind chunk light texture for rendering: ") +
                              SDL_GetError());
            SDL_DestroyTexture(chunk_texture);
            chunk->static_light_map = nullptr;
            SDL_SetRenderTarget(renderer_, restore_target);
            continue;
        }

        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);

        if (using_full_texture && full_texture) {
            SDL_Rect src_rect{
                bounds.x - full_bounds.x,
                bounds.y - full_bounds.y,
                bounds.w,
                bounds.h
            };
            SDL_Rect full_rect{0, 0, full_bounds.w, full_bounds.h};
            SDL_Rect clipped_src{};
            if (SDL_IntersectRect(&src_rect, &full_rect, &clipped_src)) {
                SDL_Rect dst_rect{
                    clipped_src.x - src_rect.x,
                    clipped_src.y - src_rect.y,
                    clipped_src.w,
                    clipped_src.h
                };
                SDL_RenderCopy(renderer_, full_texture, &clipped_src, &dst_rect);
            } else if (chunk_log_limiter(baked_chunks)) {
                vibble::log::debug(std::string("[SceneRenderer] Chunk bounds outside full texture; left blank. bounds=") +
                                   rect_str(bounds));
            }
        } else {
            ++fallback_chunks;
            LogLimiter light_limiter{};
            for (const Asset* asset : static_lights) {
                if (!asset || !asset->info) {
                    continue;
                }
                for (const auto& light : asset->info->light_sources) {
                    SDL_Texture* source = light.texture;
                    if (!source) {
                        continue;
                    }
                    int src_w = light.cached_w > 0 ? light.cached_w : 0;
                    int src_h = light.cached_h > 0 ? light.cached_h : 0;
                    if (src_w <= 0 || src_h <= 0) {
                        SDL_QueryTexture(source, nullptr, nullptr, &src_w, &src_h);
                    }
                    if (src_w <= 0 || src_h <= 0) {
                        continue;
                    }

                    const int draw_w = std::max(1, src_w);
                    const int draw_h = std::max(1, src_h);
                    SDL_Rect world_dst{
                        asset->pos.x + light.offset_x - draw_w / 2,
                        asset->pos.y + light.offset_y - draw_h / 2,
                        draw_w,
                        draw_h
                    };
                    SDL_Rect intersection{};
                    if (!SDL_IntersectRect(&world_dst, &bounds, &intersection)) {
                        continue;
                    }

                    SDL_Rect local_dst = intersection;
                    local_dst.x -= bounds.x;
                    local_dst.y -= bounds.y;

                    SDL_Rect source_rect{
                        intersection.x - world_dst.x,
                        intersection.y - world_dst.y,
                        intersection.w,
                        intersection.h
                    };

                    Uint8 saved_r = 255, saved_g = 255, saved_b = 255, saved_a = 255;
                    SDL_BlendMode saved_blend = SDL_BLENDMODE_BLEND;
                    SDL_GetTextureColorMod(source, &saved_r, &saved_g, &saved_b);
                    SDL_GetTextureAlphaMod(source, &saved_a);
                    SDL_GetTextureBlendMode(source, &saved_blend);
                    SDL_SetTextureBlendMode(source, erase_blend);
                    SDL_SetTextureColorMod(source, 255, 255, 255);
                    SDL_SetTextureAlphaMod(source, 255);
                    SDL_RenderCopy(renderer_, source, &source_rect, &local_dst);
                    SDL_SetTextureBlendMode(source, saved_blend);
                    SDL_SetTextureColorMod(source, saved_r, saved_g, saved_b);
                    SDL_SetTextureAlphaMod(source, saved_a);

                    if (light_limiter(baked_chunks)) {
                        vibble::log::debug(std::string("[SceneRenderer] Stamped static light directly into chunk dst=") +
                                           rect_str(local_dst));
                    }
                }
            }
        }

        SDL_SetRenderTarget(renderer_, restore_target);

        chunk->static_light_map = chunk_texture;
        const LightStats stats = compute_light_stats(renderer_, chunk_texture);
        chunk->base_brightness               = stats.average_strength;
        chunk->light.min_static_avg_strength = stats.min_strength;
        chunk->light.max_static_avg_strength = stats.max_strength;
        chunk->light.needs_update            = true;
        chunk->lighting_dirty                = false;
        chunk->brightness_strength           = 1.0f;
        chunk->opacity_strength              = 1.0f;
        chunk->scale_strength                = 1.0f;
        chunk->offset_x                      = 0;
        chunk->offset_y                      = 0;
        chunk->has_dynamic_overlay           = false;
        ++baked_chunks;
    }

    if (full_texture) {
        SDL_DestroyTexture(full_texture);
    }

    vibble::log::info(std::string("[SceneRenderer] Static light initialization complete: chunks=") +
                      std::to_string(baked_chunks) +
                      (fallback_chunks > 0 ? (", fallback_chunks=" + std::to_string(fallback_chunks)) : "") +
                      " static_light_assets=" + std::to_string(static_lights.size()));
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
        // Update chunk tile masks so moving lights affecting them are reflected this frame.
        light_map_->update(renderer_, 0u);
    } else {
        render_pipeline_.lighting().light_map_sampler = nullptr;
    }
    render_pipeline_.lighting().reactive_shadow_settings = &reactive_shadow_settings_;

    SDL_SetRenderTarget(renderer_,nullptr);
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
    // In chunk debug mode we explicitly keep the normal background.
    const SDL_Color clear_color = chunk_debug_mode_ ? SLATE_COLOR : (light_map_only_mode_ ? SDL_Color{0,0,0,255} : SLATE_COLOR);
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
        if (!chunk_debug_mode_) {
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
        if (chunk_debug_mode_) {
            // Draw chunks with additive blending + red outlines
            light_map_->render_visible_chunks_debug(renderer_, screen_view, 1.0f);
        } else {
            light_map_->render_visible_chunks(renderer_, screen_view, alpha_mult);
        }
        SDL_SetRenderDrawBlendMode(renderer_,previous_mode);
        rendered_light_map = true;
    };

    if (!light_map_only_mode_ && !chunk_debug_mode_){
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

    if (!light_map_only_mode_ && !chunk_debug_mode_ && assets_){
        assets_->render_overlays(renderer_);
    }

    SDL_RenderPresent(renderer_);
}



