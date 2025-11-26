#include "render/render.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#include <SDL_image.h>

#include "animation_update/animation_update.hpp"
#include "animation_update/child_attachment_math.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/animation.hpp"
#include "asset/animation_frame.hpp"
#include "asset/asset_types.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/depth_cue_settings.hpp"
#include "dev_mode/dev_ui_settings.hpp"
#include "render/camera_grid.hpp"
#include "render/light_flicker.hpp"
#include "tiling/grid_tile.hpp"
#include "utils/log.hpp"
#include "utils/grid.hpp"
#include "world/chunk.hpp"
#include "world/grid.hpp"




////////////////////////////////////////////////////////////////////////////////
// GridTileRenderer implementation
////////////////////////////////////////////////////////////////////////////////

void GridTileRenderer::render(SDL_Renderer* renderer) {
    if (!renderer || !assets_) return;
    render(renderer, assets_->getView(), assets_->world_grid());
}

void GridTileRenderer::render(SDL_Renderer* renderer, const camera_grid& cam, const world::Grid& grid) {
    if (!renderer) return;

    const auto& chunks = grid.active_chunks();
    if (chunks.empty()) return;

    // Depth-cue effects are intentionally not applied to tiles.

    const SDL_Color white{255, 255, 255, 255};
    int indices[6] = {0, 1, 2, 0, 2, 3};

    for (const world::Chunk* chunk : chunks) {
        if (!chunk) continue;
        for (const auto& tile : chunk->tiles) {
            if (!tile.texture || tile.world_rect.w <= 0 || tile.world_rect.h <= 0) continue;

            SDL_Point world_tl{ tile.world_rect.x, tile.world_rect.y };
            SDL_Point world_tr{ tile.world_rect.x + tile.world_rect.w, tile.world_rect.y };
            SDL_Point world_br{ tile.world_rect.x + tile.world_rect.w, tile.world_rect.y + tile.world_rect.h };
            SDL_Point world_bl{ tile.world_rect.x, tile.world_rect.y + tile.world_rect.h };

            SDL_FPoint screen_tl = grid.floor_warped_screen_position(cam, world_tl);
            SDL_FPoint screen_tr = grid.floor_warped_screen_position(cam, world_tr);
            SDL_FPoint screen_br = grid.floor_warped_screen_position(cam, world_br);
            SDL_FPoint screen_bl = grid.floor_warped_screen_position(cam, world_bl);

            // Drop degenerate quads from extreme warping/parallax.
            const float area_doubled =
                (screen_tr.x - screen_tl.x) * (screen_bl.y - screen_tl.y) -
                (screen_bl.x - screen_tl.x) * (screen_tr.y - screen_tl.y);
            if (std::fabs(area_doubled) < 1e-5f) {
                continue;
            }

            int tex_w_int = 0, tex_h_int = 0;
            if (SDL_QueryTexture(tile.texture, nullptr, nullptr, &tex_w_int, &tex_h_int) != 0) {
                continue;
            }
            const float tex_w = static_cast<float>(tex_w_int);
            const float tex_h = static_cast<float>(tex_h_int);
            if (tex_w <= 0.0f || tex_h <= 0.0f) {
                continue;
            }
            const float padding_x = 0.5f / tex_w;
            const float padding_y = 0.5f / tex_h;

            const float tx0 = padding_x;
            const float ty0 = padding_y;
            const float tx1 = 1.0f - padding_x;
            const float ty1 = 1.0f - padding_y;

            SDL_Vertex vertices[4]{};
            vertices[0].position = SDL_FPoint{ screen_tl.x, screen_tl.y };
            vertices[1].position = SDL_FPoint{ screen_tr.x, screen_tr.y };
            vertices[2].position = SDL_FPoint{ screen_br.x, screen_br.y };
            vertices[3].position = SDL_FPoint{ screen_bl.x, screen_bl.y };
            vertices[0].color = vertices[1].color = vertices[2].color = vertices[3].color = white;
            vertices[0].tex_coord = SDL_FPoint{ tx0, ty0 };
            vertices[1].tex_coord = SDL_FPoint{ tx1, ty0 };
            vertices[2].tex_coord = SDL_FPoint{ tx1, ty1 };
            vertices[3].tex_coord = SDL_FPoint{ tx0, ty1 };

            SDL_RenderGeometry(renderer, tile.texture, vertices, 4, indices, 6);
        }
    }
}


// TODO for asset light renderer make sure actual textures are not drawn here we only add light textures to the dark mask thing in scene renderer if render to mask is true for a light source

////////////////////////////////////////////////////////////////////////////////
// AssetLightRenderer implementation
////////////////////////////////////////////////////////////////////////////////




namespace {
constexpr float kTwoPi       = 6.28318530718f;
constexpr int   kRadialSteps = 12;

SDL_Rect clamp_rect_to_bounds(const SDL_Rect& rect, int width, int height) {
    SDL_Rect clamped = rect;
    const int min_x  = std::max(rect.x, 0);
    const int min_y  = std::max(rect.y, 0);
    const int max_x  = std::min(rect.x + rect.w, width);
    const int max_y  = std::min(rect.y + rect.h, height);
    clamped.x        = min_x;
    clamped.y        = min_y;
    clamped.w        = std::max(0, max_x - min_x);
    clamped.h        = std::max(0, max_y - min_y);
    return clamped;
}

SDL_BlendMode mask_alpha_multiply_blend() {
    static SDL_BlendMode cached = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO,
                                                             SDL_BLENDFACTOR_SRC_ALPHA,
                                                             SDL_BLENDOPERATION_ADD,
                                                             SDL_BLENDFACTOR_ZERO,
                                                             SDL_BLENDFACTOR_SRC_ALPHA,
                                                             SDL_BLENDOPERATION_ADD);
    return cached;
}
}

AssetLightRenderer::AssetLightRenderer(SDL_Renderer* renderer,
                                       const runtime_lighting::AssetLight& source,
                                       std::vector<SDL_Vertex>& scratch_vertices,
                                       std::vector<int>& scratch_indices,
                                       float light_visibility,
                                       float flicker_time_seconds)
    : renderer_(renderer),
      source_(source),
      asset_(source.asset),
      scratch_vertices_(scratch_vertices),
      scratch_indices_(scratch_indices),
      overlay_visibility_(std::clamp(light_visibility, 0.0f, 1.0f)),
      flicker_time_seconds_(std::isfinite(flicker_time_seconds) ? flicker_time_seconds : 0.0f) {
    if (!renderer_ || !asset_ || !asset_->info) {
        return;
    }

    const auto& lights = asset_->info->light_sources;
    if (lights.empty()) {
        return;
    }
    lights_ = &lights;

    const float base_width  = static_cast<float>(std::max(1, source.base_width));
    const float base_height = static_cast<float>(std::max(1, source.base_height));
    scale_x_                = std::isfinite(static_cast<float>(source.asset_rect.w) / base_width)
                               ? static_cast<float>(source.asset_rect.w) / base_width
                               : 1.0f;
    const float scale_y_base = std::isfinite(static_cast<float>(source.asset_rect.h) / base_height)
                                   ? static_cast<float>(source.asset_rect.h) / base_height
                                   : scale_x_;
    scale_y_                = (source.base_height > 0) ? scale_y_base : scale_x_;
    if (!std::isfinite(scale_x_) || !std::isfinite(scale_y_)) {
        lights_ = nullptr;
        return;
    }

    const float safe_base_scale =
        (std::isfinite(source.asset_base_scale) && source.asset_base_scale > 0.0f)
            ? source.asset_base_scale
            : 1.0f;
    const float zoom_scale_x = scale_x_ / safe_base_scale;
    const float zoom_scale_y = scale_y_ / safe_base_scale;
    safe_zoom_scale_x_       = (std::isfinite(zoom_scale_x) && zoom_scale_x > 0.0f) ? zoom_scale_x : 1.0f;
    safe_zoom_scale_y_       = (std::isfinite(zoom_scale_y) && zoom_scale_y > 0.0f) ? zoom_scale_y : 1.0f;

    center_base_x_ = static_cast<float>(source.asset_rect.x) + static_cast<float>(source.asset_rect.w) * 0.5f;
    center_base_y_ = static_cast<float>(source.asset_rect.y + source.asset_rect.h);

    valid_ = true;
}

AssetLightRenderer::~AssetLightRenderer() {
    if (mask_composite_texture_) {
        SDL_DestroyTexture(mask_composite_texture_);
        mask_composite_texture_ = nullptr;
    }
}

bool AssetLightRenderer::prepare_light(const LightSource& light, ComputedLight& out) const {
    const int raw_radius = light.radius;
    if (raw_radius <= 0) {
        return false;
    }

    int intensity = std::clamp(light.intensity, 0, 255);
    if (intensity <= 0) {
        return false;
    }

    const float flicker_multiplier = LightFlickerCalculator::compute_multiplier(light, flicker_time_seconds_);
    intensity = static_cast<int>(std::lround(static_cast<float>(intensity) * flicker_multiplier));
    intensity = std::clamp(intensity, 0, 255);
    if (intensity <= 0) {
        return false;
    }

    const float radius_base = static_cast<float>(std::max(1, raw_radius));
    const float radius_x    = std::max(1.0f, radius_base * safe_zoom_scale_x_);
    const float radius_y    = std::max(1.0f, radius_base * safe_zoom_scale_y_);
    if (!std::isfinite(radius_x) || !std::isfinite(radius_y)) {
        return false;
    }

    const float offset_x = static_cast<float>(source_.flipped ? -light.offset_x : light.offset_x);
    const float offset_y = static_cast<float>(light.offset_y);
    const float center_x = center_base_x_ + offset_x * scale_x_;
    const float center_y = center_base_y_ + offset_y * scale_y_;

    SDL_Rect dst{};
    dst.w = std::max(1, static_cast<int>(std::lround(radius_x * 2.0f)));
    dst.h = std::max(1, static_cast<int>(std::lround(radius_y * 2.0f)));
    dst.x = static_cast<int>(std::lround(center_x - static_cast<float>(dst.w) * 0.5f));
    dst.y = static_cast<int>(std::lround(center_y - static_cast<float>(dst.h) * 0.5f));

    const float falloff_norm  = std::clamp(static_cast<float>(light.fall_off) / 100.0f, 0.0f, 1.0f);
    const float fade_exponent = 0.6f + 3.4f * falloff_norm;

    out.source        = &light;
    out.intensity     = intensity;
    out.center_x      = center_x;
    out.center_y      = center_y;
    out.radius_x      = radius_x;
    out.radius_y      = radius_y;
    out.bounds        = dst;
    out.fade_exponent = fade_exponent;
    out.textured      = false;
    out.texture_dst   = SDL_Rect{0, 0, 0, 0};

    const float width_f  = static_cast<float>(std::max(1, source_.asset_rect.w));
    const float height_f = static_cast<float>(std::max(1, source_.asset_rect.h));
    out.center_ratio_x   = (center_x - static_cast<float>(source_.asset_rect.x)) / width_f;
    out.center_ratio_y   = (center_y - static_cast<float>(source_.asset_rect.y)) / height_f;
    out.radius_ratio_x   = radius_x / width_f;
    out.radius_ratio_y   = radius_y / height_f;

    if (light.texture) {
        int base_w = light.cached_w;
        int base_h = light.cached_h;
        if (base_w <= 0 || base_h <= 0) {
            SDL_QueryTexture(light.texture, nullptr, nullptr, &base_w, &base_h);
        }
        if (base_w <= 0 || base_h <= 0) {
            base_w = static_cast<int>(std::lround(radius_base * 2.0f));
            base_h = static_cast<int>(std::lround(radius_base * 2.0f));
        }

        const int scaled_w = std::max(1, static_cast<int>(std::lround(static_cast<float>(base_w) * safe_zoom_scale_x_)));
        const int scaled_h = std::max(1, static_cast<int>(std::lround(static_cast<float>(base_h) * safe_zoom_scale_y_)));

        SDL_Rect tex_dst{};
        tex_dst.w = scaled_w;
        tex_dst.h = scaled_h;
        tex_dst.x = static_cast<int>(std::lround(center_x - static_cast<float>(tex_dst.w) * 0.5f));
        tex_dst.y = static_cast<int>(std::lround(center_y - static_cast<float>(tex_dst.h) * 0.5f));

        out.textured        = true;
        out.texture_dst     = tex_dst;
        out.texture_ratio_x = (static_cast<float>(tex_dst.x) - static_cast<float>(source_.asset_rect.x)) / width_f;
        out.texture_ratio_y = (static_cast<float>(tex_dst.y) - static_cast<float>(source_.asset_rect.y)) / height_f;
        out.texture_ratio_w = static_cast<float>(tex_dst.w) / width_f;
        out.texture_ratio_h = static_cast<float>(tex_dst.h) / height_f;
    }

    return true;
}




void AssetLightRenderer::render_textured_light(const ComputedLight& info, const SDL_Rect& dst) {
    if (!info.source || !info.source->texture || dst.w <= 0 || dst.h <= 0) {
        return;
    }

    SDL_Texture* tex = info.source->texture;
    Uint8        prev_a = 255;
    Uint8        prev_r = 255;
    Uint8        prev_g = 255;
    Uint8        prev_b = 255;
    SDL_BlendMode prev_blend = SDL_BLENDMODE_BLEND;
    SDL_GetTextureAlphaMod(tex, &prev_a);
    SDL_GetTextureColorMod(tex, &prev_r, &prev_g, &prev_b);
    SDL_GetTextureBlendMode(tex, &prev_blend);

    SDL_Color draw_color{255, 255, 255, 255};
    if (info.source) {
        draw_color = info.source->color;
    }

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(tex, draw_color.r, draw_color.g, draw_color.b);
    SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(info.intensity));
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    SDL_SetTextureAlphaMod(tex, prev_a);
    SDL_SetTextureColorMod(tex, prev_r, prev_g, prev_b);
    SDL_SetTextureBlendMode(tex, prev_blend);
}
////////////////////////////////////////////////////////////////////////////////
// SceneRenderer core render loop
////////////////////////////////////////////////////////////////////////////////


static constexpr float kDefaultMinVisibleScreenRatio = 0.015f;

namespace {








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
: SceneRenderer(require_prerequisites(renderer, assets),
                renderer,
                assets,
                screen_width,
                screen_height,
                map_manifest,
                map_id) {}

SceneRenderer::PrevalidatedTag SceneRenderer::require_prerequisites(SDL_Renderer* renderer, Assets* assets) {
    std::string reason;
    if (!SceneRenderer::prerequisites_ready(renderer, assets, &reason)) {
        const std::string message = reason.empty() ? "SceneRenderer prerequisites missing." : reason;
        vibble::log::error(std::string{"[SceneRenderer] Initialization aborted: "} + message);
        if (!renderer) { SDL_assert(renderer != nullptr); }
        if (!assets)   { SDL_assert(assets != nullptr); }
        throw std::invalid_argument(message);
    }
    return PrevalidatedTag{};
}

SceneRenderer::SceneRenderer(PrevalidatedTag,
                             SDL_Renderer* renderer,
                             Assets* assets,
                             int screen_width,
                             int screen_height,
                             const nlohmann::json& map_manifest,
                             const std::string& map_id)
: renderer_(renderer),
  assets_(assets),
  screen_width_(screen_width),
  screen_height_(screen_height),
  sky_texture_path_(std::filesystem::path("SRC") / "misc_content" / "sky.png"),
  composite_renderer_(renderer, assets)
{
    vibble::log::debug(std::string{"[SceneRenderer] Initializing for map '"} + map_id +
                       "' with screen " + std::to_string(screen_width_) + "x" + std::to_string(screen_height_) + ".");


    // Allow override of warmup frames via env var (optional): VIBBLE_DEPTHCUE_WARMUP_FRAMES
    if (const char* override_frames = std::getenv("VIBBLE_DEPTHCUE_WARMUP_FRAMES")) {
        const int v = std::atoi(override_frames);
        if (v >= 0 && v <= 120) {
            depthcue_warmup_frames_ = static_cast<std::uint32_t>(v);
        }
    }
                    bool has_dark_mask_lights = false;
                    for (const auto& light : asset->info->light_sources) {
                        if (light.render_to_dark_mask) {
                            has_dark_mask_lights = true;
                            break;
                        }
                    }

                    if (has_dark_mask_lights) {
                        LightOverlaySource source;
                        source.asset = asset;
                        // asset_rect will be calculated based on gp->screen in the dark mask pass
                        source.base_width = asset->cached_w;
                        source.base_height = asset->cached_h;
                        source.flipped = asset->flipped;
                        source.asset_base_scale = (asset->info && asset->info->scale_factor > 0) ? asset->info->scale_factor : 1.0f;
                        source.has_dark_mask_lights = true;
                        
                        light_overlay_sources_.push_back(source);
                        light_overlay_sources_have_dark_mask_cached_ = true;
                    }
                }
            }
        }
    }

    // 2. Render Dark Mask
    const float map_light_opacity = 1.0f; 
    const float frame_flicker_time_seconds = static_cast<float>(SDL_GetTicks64() % 1000000ULL) * 0.001f;
    
    render_dynamic_darkness_overlay(map_light_opacity, frame_flicker_time_seconds);

    // 3. Render Composite Packages
    std::sort(renderables.begin(), renderables.end(), [](const Renderable& a, const Renderable& b) {
        if (a.z_index != b.z_index) return a.z_index < b.z_index;
        return a.sort_y < b.sort_y;
    });

    for (const auto& item : renderables) {
        Asset* asset = item.asset;
        world::GridPoint* gp = item.gp;

        for (const auto& render_obj : asset->render_package) {
            SDL_Rect screen_rect = render_obj.screen_rect;
            screen_rect.x += gp->screen.x;
            screen_rect.y += gp->screen.y;

            SDL_SetTextureColorMod(render_obj.texture, render_obj.color_mod.r, render_obj.color_mod.g, render_obj.color_mod.b);
            SDL_SetTextureAlphaMod(render_obj.texture, render_obj.color_mod.a);
            SDL_SetTextureBlendMode(render_obj.texture, render_obj.blend_mode);
            SDL_RenderCopy(renderer_, render_obj.texture, nullptr, &screen_rect);
        }
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

bool SceneRenderer::ensure_sky_texture() {
    if (sky_texture_ || sky_texture_failed_) {
        return sky_texture_ != nullptr;
    }
    if (!renderer_) {
        return false;
    }

    std::filesystem::path path = sky_texture_path_;
    if (!path.is_absolute()) {
        path = std::filesystem::current_path() / path;
    }

    const std::string path_str = path.string();
    SDL_Texture* tex = IMG_LoadTexture(renderer_, path_str.c_str());
    if (!tex) {
        vibble::log::warn(std::string{"[SceneRenderer] Failed to load sky texture '"} +
                          path_str + "': " + IMG_GetError());
        sky_texture_failed_ = true;
        return false;
    }

    int tex_w = 0;
    int tex_h = 0;
    if (SDL_QueryTexture(tex, nullptr, nullptr, &tex_w, &tex_h) != 0 || tex_w <= 0 || tex_h <= 0) {
        vibble::log::warn(std::string{"[SceneRenderer] Invalid sky texture '"} +
                          path_str + "': " + SDL_GetError());
        SDL_DestroyTexture(tex);
        sky_texture_failed_ = true;
        return false;
    }

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    sky_texture_        = tex;
    sky_texture_width_  = tex_w;
    sky_texture_height_ = tex_h;
    return true;
}

void SceneRenderer::destroy_sky_texture() {
    if (sky_texture_) {
        SDL_DestroyTexture(sky_texture_);
        sky_texture_ = nullptr;
    }
    sky_texture_width_  = 0;
    sky_texture_height_ = 0;
}

void SceneRenderer::render_sky_layer(const camera_grid& cam, bool depth_effects_enabled) {
    if (!depth_effects_enabled) {
        return;
    }
    if (!renderer_ || screen_width_ <= 0 || screen_height_ <= 0) {
        return;
    }

    const double horizon_y = cam.horizon_screen_y_for_scale();
    if (!std::isfinite(horizon_y)) {
        return;
    }
    if (horizon_y < 0.0 || horizon_y > static_cast<double>(screen_height_)) {
        return;
    }

    if (!ensure_sky_texture() || !sky_texture_) {
        return;
    }

    const float tex_w = static_cast<float>(sky_texture_width_);
    const float tex_h = static_cast<float>(sky_texture_height_);
    if (tex_w <= 0.0f || tex_h <= 0.0f) {
        return;
    }

    const float target_w = static_cast<float>(screen_width_);
    const float scale    = target_w / tex_w;
    const float target_h = tex_h * scale;
    if (!std::isfinite(target_h) || target_h <= 0.0f || !std::isfinite(scale)) {
        return;
    }

    SDL_FRect dst{
        0.0f,
        static_cast<float>(horizon_y) - target_h,
        target_w,
        target_h
    };

    SDL_SetTextureColorMod(sky_texture_, 255, 255, 255);
    SDL_SetTextureAlphaMod(sky_texture_, 255);
    SDL_RenderCopyF(renderer_, sky_texture_, nullptr, &dst);
}

void SceneRenderer::render_dynamic_darkness_overlay(float map_light_opacity, float flicker_time_seconds) {
    if (!renderer_) {
        return;
    }

    if (!has_dark_mask_overlay_sources()) {
        ++darkness_overlay_skipped_frames_;
        if (!darkness_overlay_skip_logged_) {
            vibble::log::debug(std::string{"[SceneRenderer] Skipping dynamic darkness overlay; no dark-mask lights. skipped_frames="} +
                               std::to_string(darkness_overlay_skipped_frames_));
            darkness_overlay_skip_logged_ = true;
        }
        return;
    }

    ++darkness_overlay_rendered_frames_;
    if (darkness_overlay_skip_logged_) {
        vibble::log::debug(std::string{"[SceneRenderer] Dynamic darkness overlay pass resumed. rendered_frames="} +
                           std::to_string(darkness_overlay_rendered_frames_));
        darkness_overlay_skip_logged_ = false;
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
        // Asset rect for dark mask needs to be calculated relative to screen
        world::GridPoint* gp = assets_->world_grid().point_for_asset(source.asset);
        if (!gp) continue;

        runtime_lighting::AssetLight current_source = source;
        current_source.asset_rect.x = gp->screen.x;
        current_source.asset_rect.y = gp->screen.y;
        // The w/h from the original collection is probably fine, it was based on composite rect.
        // Let's re-verify that. The original code did this:
        // static_cast<int>(std::round(composite_rect.w * correction_factor))
        // This is now part of the render package, so we don't have it here.
        // Let's just use the asset's pos for now.
        // The AssetLightRenderer will calculate offsets from this.
        
        AssetLightRenderer light_renderer(renderer_,
                                          current_source,
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

bool SceneRenderer::has_dark_mask_overlay_sources() {
    if (light_overlay_sources_dark_mask_cache_dirty_) {
        light_overlay_sources_have_dark_mask_cached_ = std::any_of(
            light_overlay_sources_.begin(),
            light_overlay_sources_.end(),
            [](const LightOverlaySource& source) { return source.has_dark_mask_lights; });
        light_overlay_sources_dark_mask_cache_dirty_ = false;
    }
    return light_overlay_sources_have_dark_mask_cached_;
}








