#include "render/render.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <array>
#include <iostream>

#include <SDL_image.h>

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "render/warped_screen_grid.hpp"
#include "animation_update/animation_update.hpp"
#include "tiling/grid_tile.hpp"
#include "asset/animation.hpp"
#include "asset/animation_frame.hpp"
#include "utils/log.hpp"
#include "world/chunk.hpp"
#include "world/world_grid.hpp"

void GridTileRenderer::render(SDL_Renderer* renderer) {
    if (!renderer || !assets_) return;
    render(renderer, assets_->getView(), assets_->world_grid());
}

void GridTileRenderer::render(SDL_Renderer* renderer, const WarpedScreenGrid& cam, const world::WorldGrid& grid) {
    if (!renderer) return;

    const auto& chunks = grid.active_chunks();
    if (chunks.empty()) return;

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

            auto floor_warped_screen_position = [&](SDL_Point world_pos) -> SDL_FPoint {
                auto effects = cam.compute_render_effects(world_pos, 0, 0, {});
                return {std::floor(effects.screen_position.x), std::floor(effects.screen_position.y)};
};

            SDL_FPoint screen_tl = floor_warped_screen_position(world_tl);
            SDL_FPoint screen_tr = floor_warped_screen_position(world_tr);
            SDL_FPoint screen_br = floor_warped_screen_position(world_br);
            SDL_FPoint screen_bl = floor_warped_screen_position(world_bl);

            const float area_doubled =
                (screen_tr.x - screen_tl.x) * (screen_bl.y - screen_tl.y) - (screen_bl.x - screen_tl.x) * (screen_tr.y - screen_tl.y);
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

namespace {

inline float ticks_to_seconds(Uint64 ticks) {
    return static_cast<float>(ticks) * 0.001f;
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
  tile_renderer_(std::make_unique<GridTileRenderer>(assets)),
  sky_texture_path_(std::filesystem::path("SRC") / "misc_content" / "sky.png"),
  composite_renderer_(renderer, assets)
{

    bool color_set = false;
    if (map_manifest.contains("maps") && map_manifest["maps"].contains(map_id)) {
        const auto& map_data = map_manifest["maps"][map_id];
        if (map_data.contains("map_light_data") && map_data["map_light_data"].is_object()) {
            const auto& mld = map_data["map_light_data"];
            if (mld.contains("map_color")) {
                const auto& mc = mld["map_color"];
                if (mc.contains("r") && mc["r"].contains("max") && mc["r"]["max"].is_number_integer() &&
                    mc.contains("g") && mc["g"].contains("max") && mc["g"]["max"].is_number_integer() &&
                    mc.contains("b") && mc["b"].contains("max") && mc["b"]["max"].is_number_integer() &&
                    mc.contains("a") && mc["a"].contains("max") && mc["a"]["max"].is_number_integer()) {
                    int r_max = mc["r"]["max"];
                    int g_max = mc["g"]["max"];
                    int b_max = mc["b"]["max"];
                    int a_max = mc["a"]["max"];
                    if (r_max >= 0 && r_max <= 255 && g_max >= 0 && g_max <= 255 &&
                        b_max >= 0 && b_max <= 255 && a_max >= 0 && a_max <= 255) {
                        map_clear_color_ = SDL_Color{static_cast<Uint8>(r_max), static_cast<Uint8>(g_max), static_cast<Uint8>(b_max), static_cast<Uint8>(a_max)};
                        color_set = true;
                    }
                }
            }
            if (mld.contains("intensity") && mld["intensity"].is_number()) {
                const int intensity_raw = static_cast<int>(mld["intensity"]);
                const int clamped       = std::clamp(intensity_raw, 0, 255);
                map_light_opacity_      = static_cast<float>(clamped) / 255.0f;
                if (!std::isfinite(map_light_opacity_)) {
                    map_light_opacity_ = SceneRenderer::kDefaultMapLightOpacity;
                }
            }
        }
    }
    if (!color_set) {

        map_clear_color_ = SDL_Color{69, 101, 74, 255};
    }

    vibble::log::debug(std::string{"[SceneRenderer] Initializing for map '"} + map_id +
                       "' with screen " + std::to_string(screen_width_) + "x" + std::to_string(screen_height_) + ".");

    if (const char* override_frames = std::getenv("VIBBLE_DEPTHCUE_WARMUP_FRAMES")) {
        const int v = std::atoi(override_frames);
        if (v >= 0 && v <= 120) {
            depthcue_warmup_frames_ = static_cast<std::uint32_t>(v);
        }
    }
    std::cout<<"[SceneRenderer] Init complete. Depth-cue warmup frames: "<<depthcue_warmup_frames_<<std::endl;
}

SceneRenderer::~SceneRenderer() {
    destroy_darkness_overlay();
    destroy_sky_texture();
    if (scene_composite_tex_) { SDL_DestroyTexture(scene_composite_tex_); scene_composite_tex_ = nullptr; }
    if (postprocess_tex_)     { SDL_DestroyTexture(postprocess_tex_);     postprocess_tex_     = nullptr; }
    if (blur_tex_)            { SDL_DestroyTexture(blur_tex_);            blur_tex_            = nullptr; }
}

SDL_Renderer* SceneRenderer::get_renderer() const {
    return renderer_;
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

void SceneRenderer::render() {
    if (!renderer_ || !assets_ || screen_width_ <= 0 || screen_height_ <= 0) {
        return;
    }

    ++frame_counter_;

    WarpedScreenGrid& cam = assets_->getView();
    world::WorldGrid& grid = assets_->world_grid();
    cam.rebuild_grid(grid, assets_->frame_delta_seconds());

    SDL_SetRenderTarget(renderer_, nullptr);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, map_clear_color_.r, map_clear_color_.g, map_clear_color_.b, map_clear_color_.a);
    SDL_RenderClear(renderer_);

    const bool depth_effects_enabled = assets_->depth_effects_enabled();
    render_sky_layer(cam, depth_effects_enabled);

    if (tile_renderer_) {
        tile_renderer_->render(renderer_, cam, grid);
    }

    const float flicker_time_seconds = ticks_to_seconds(SDL_GetTicks64());
    const float inv_scale = 1.0f / std::max(0.000001f, cam.get_scale());

    struct ScreenRenderData {
        SDL_Rect  rect{};
        SDL_Point center{};
        bool      use_center = false;
};

    auto build_screen_render_data = [&](const RenderObject& obj,
                                        const SDL_FPoint& base,
                                        int asset_world_x,
                                        int asset_world_y) -> std::optional<ScreenRenderData> {
        if (!obj.texture) {
            return std::nullopt;
        }

        const int raw_width  = obj.screen_rect.w;
        const int raw_height = obj.screen_rect.h;
        if (raw_width <= 0 || raw_height <= 0) {
            return std::nullopt;
        }

        const int offset_x = obj.screen_rect.x - asset_world_x;
        const int offset_y = obj.screen_rect.y - asset_world_y;

        const double scaled_width  = static_cast<double>(raw_width)  * static_cast<double>(inv_scale);
        const double scaled_height = static_cast<double>(raw_height) * static_cast<double>(inv_scale);

        if (!std::isfinite(scaled_width) || !std::isfinite(scaled_height)) {
            return std::nullopt;
        }

        const int screen_w = std::max(1, static_cast<int>(std::lround(scaled_width)));
        const int screen_h = std::max(1, static_cast<int>(std::lround(scaled_height)));

        SDL_Rect screen_rect{
            static_cast<int>(std::lround(base.x + static_cast<double>(offset_x) * static_cast<double>(inv_scale) - scaled_width * 0.5)), static_cast<int>(std::lround(base.y + static_cast<double>(offset_y) * static_cast<double>(inv_scale) - scaled_height)), screen_w, screen_h };

        SDL_Point center = obj.center;
        if (obj.use_custom_center) {
            center.x = static_cast<int>(std::lround(static_cast<double>(center.x) * static_cast<double>(inv_scale)));
            center.y = static_cast<int>(std::lround(static_cast<double>(center.y) * static_cast<double>(inv_scale)));
        }

        ScreenRenderData data;
        data.rect       = screen_rect;
        data.center     = center;
        data.use_center = obj.use_custom_center;
        return data;
};

    const auto& active_assets = assets_->getActive();
    std::vector<DarkMaskSprite> dark_mask_sprites;
    dark_mask_sprites.reserve(std::max<std::size_t>(active_assets.size(), 8u));
    for (Asset* asset : active_assets) {
        if (!asset || asset->is_hidden() || !asset->info) {
            continue;
        }

        if (const auto& tiling = asset->tiling_info(); tiling && tiling->is_valid()) {

            continue;
        }

        composite_renderer_.update(asset, nullptr, flicker_time_seconds);

        SDL_Point world_pos{ asset->pos.x, asset->pos.y };
        SDL_FPoint screen_base = cam.map_to_screen(world_pos);
        if (!std::isfinite(screen_base.x) || !std::isfinite(screen_base.y)) {
            continue;
        }

        const float perspective_scale = 1.0f;
        const float vertical_scale    = 1.0f;

        const int asset_world_x = asset->pos.x;
        const int asset_world_y = asset->pos.y;

        if (dark_mask_enabled_ && !asset->scene_mask_lights.empty()) {
            for (const RenderObject& mask_obj : asset->scene_mask_lights) {
                auto screen_data = build_screen_render_data(mask_obj, screen_base, asset_world_x, asset_world_y);
                if (!screen_data) {
                    continue;
                }
                DarkMaskSprite sprite;
                sprite.texture     = mask_obj.texture;
                sprite.screen_rect = screen_data->rect;
                sprite.color_mod   = mask_obj.color_mod;
                sprite.flip        = mask_obj.flip;
                dark_mask_sprites.push_back(sprite);
            }
        }

        for (const RenderObject& obj : asset->render_package) {
            auto screen_data = build_screen_render_data(obj, screen_base, asset_world_x, asset_world_y);
            if (!screen_data) {
                continue;
            }

            SDL_SetTextureBlendMode(obj.texture, obj.blend_mode);

            Uint8 final_alpha = obj.color_mod.a;

            SDL_SetTextureColorMod(obj.texture, obj.color_mod.r, obj.color_mod.g, obj.color_mod.b);
            SDL_SetTextureAlphaMod(obj.texture, final_alpha);

            if (obj.angle != 0.0 || obj.use_custom_center || obj.flip != SDL_FLIP_NONE) {
                const SDL_Point* center_ptr = screen_data->use_center ? &screen_data->center : nullptr;
                SDL_RenderCopyEx(renderer_, obj.texture, nullptr, &screen_data->rect, obj.angle, center_ptr, obj.flip);
            } else {
                SDL_RenderCopy(renderer_, obj.texture, nullptr, &screen_data->rect);
            }
        }
    }

    if (dark_mask_enabled_) {
        render_dynamic_darkness_overlay(map_light_opacity_, dark_mask_sprites);
    }

    if (debug_auto_paths_) {
        static const std::array<SDL_Color, 6> kPathColors{{
            SDL_Color{255, 99, 71, 255},
            SDL_Color{50, 205, 50, 255},
            SDL_Color{65, 105, 225, 255},
            SDL_Color{255, 215, 0, 255},
            SDL_Color{199, 21, 133, 255},
            SDL_Color{0, 206, 209, 255},
        }};

        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        for (Asset* asset : active_assets) {
            if (!asset || asset->is_hidden() || !asset->info || !asset->anim_) {
                continue;
            }
            const Plan* plan = asset->anim_->current_plan();
            if (!plan || plan->sanitized_checkpoints.empty()) {
                continue;
            }

            SDL_SetRenderDrawColor(renderer_, 160, 32, 240, 160);
            if (asset->info) {
                for (const auto& [anim_id, anim] : asset->info->animations) {
                    const std::size_t paths = anim.movement_path_count();
                    for (std::size_t path_idx = 0; path_idx < paths; ++path_idx) {
                        const auto& path_frames = anim.movement_path(path_idx);
                        SDL_Point cursor = asset->pos;
                        for (const AnimationFrame& frame : path_frames) {
                            SDL_Point next{ cursor.x + frame.dx, cursor.y + frame.dy };
                            SDL_FPoint screen_cur  = cam.map_to_screen(cursor);
                            SDL_FPoint screen_next = cam.map_to_screen(next);
                            SDL_RenderDrawLine(renderer_, static_cast<int>(std::lround(screen_cur.x)), static_cast<int>(std::lround(screen_cur.y)), static_cast<int>(std::lround(screen_next.x)), static_cast<int>(std::lround(screen_next.y)));
                            SDL_Rect dot{
                                static_cast<int>(std::lround(screen_next.x)) - 2, static_cast<int>(std::lround(screen_next.y)) - 2, 4, 4 };
                            SDL_RenderFillRect(renderer_, &dot);
                            cursor = next;
                        }
                    }
                }
            }

            if (!plan->strides.empty()) {
                SDL_SetRenderDrawColor(renderer_, 0, 0, 255, 160);
                SDL_Point cursor = plan->world_start;
                for (const auto& stride : plan->strides) {
                    auto it = asset->info->animations.find(stride.animation_id);
                    if (it != asset->info->animations.end()) {
                        const auto& anim = it->second;
                        const auto& path_frames = anim.movement_path(stride.path_index);
                        int count = std::min(static_cast<int>(path_frames.size()), stride.frames);
                        for (int i = 0; i < count; ++i) {
                            const AnimationFrame& frame = path_frames[i];
                            SDL_Point next{ cursor.x + frame.dx, cursor.y + frame.dy };
                            SDL_FPoint screen_cur = cam.map_to_screen(cursor);
                            SDL_FPoint screen_next = cam.map_to_screen(next);
                            SDL_RenderDrawLine(renderer_, static_cast<int>(std::lround(screen_cur.x)), static_cast<int>(std::lround(screen_cur.y)), static_cast<int>(std::lround(screen_next.x)), static_cast<int>(std::lround(screen_next.y)));
                            cursor = next;
                        }
                    }
                }
            }

            if (!plan->sanitized_checkpoints.empty()) {
                const int visit_threshold = asset->anim_->visit_threshold_px();
                int threshold = visit_threshold;
                if (visit_threshold == 0) threshold = 32;
                const int segments = 24;
                std::vector<SDL_FPoint> ring;
                ring.reserve(static_cast<std::size_t>(segments) + 1);
                SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 180);
                for (std::size_t idx = 0; idx < plan->sanitized_checkpoints.size(); ++idx) {
                    const SDL_Point wp = plan->sanitized_checkpoints[idx];
                    ring.clear();
                    for (int i = 0; i <= segments; ++i) {
                        const double angle = (6.28318530717958647692 * static_cast<double>(i)) / static_cast<double>(segments);
                        SDL_Point pt{
                            wp.x + static_cast<int>(std::lround(static_cast<double>(threshold) * std::cos(angle))), wp.y + static_cast<int>(std::lround(static_cast<double>(threshold) * std::sin(angle))) };
                        ring.push_back(cam.map_to_screen(pt));
                    }
                    for (std::size_t i = 1; i < ring.size(); ++i) {
                        SDL_RenderDrawLine(renderer_, static_cast<int>(std::lround(ring[i - 1].x)), static_cast<int>(std::lround(ring[i - 1].y)), static_cast<int>(std::lround(ring[i].x)), static_cast<int>(std::lround(ring[i].y)));
                    }
        }
    }

        }
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

void SceneRenderer::render_sky_layer(const WarpedScreenGrid& cam, bool depth_effects_enabled) {
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
        static_cast<float>(horizon_y) - target_h, target_w, target_h };

    SDL_SetTextureColorMod(sky_texture_, 255, 255, 255);
    SDL_SetTextureAlphaMod(sky_texture_, 255);
    SDL_RenderCopyF(renderer_, sky_texture_, nullptr, &dst);
}

void SceneRenderer::render_dynamic_darkness_overlay(float map_light_opacity,
                                                    const std::vector<DarkMaskSprite>& sprites) {
    if (!renderer_) {
        return;
    }

    const float overlay_alpha = std::clamp(map_light_opacity, 0.0f, 1.0f);
    if (overlay_alpha <= 0.0f) {
        ++darkness_overlay_skipped_frames_;
        darkness_overlay_skip_logged_ = true;
        return;
    }

    if (!ensure_darkness_overlay()) {
        ++darkness_overlay_skipped_frames_;
        darkness_overlay_skip_logged_ = true;
        return;
    }

    ++darkness_overlay_rendered_frames_;
    darkness_overlay_skip_logged_ = false;

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, darkness_overlay_texture_);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    const Uint8 overlay_alpha_byte = static_cast<Uint8>(std::clamp(std::lround(overlay_alpha * 255.0f), 0L, 255L));
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, overlay_alpha_byte);
    SDL_RenderClear(renderer_);

    if (!sprites.empty()) {
        SDL_BlendMode carve_mode = SDL_ComposeCustomBlendMode( SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);

        for (const DarkMaskSprite& sprite : sprites) {
            if (!sprite.texture) {
                continue;
            }
            SDL_SetTextureBlendMode(sprite.texture, carve_mode);
            SDL_SetTextureColorMod(sprite.texture, sprite.color_mod.r, sprite.color_mod.g, sprite.color_mod.b);
            SDL_SetTextureAlphaMod(sprite.texture, sprite.color_mod.a);
            if (sprite.flip != SDL_FLIP_NONE) {
                SDL_RenderCopyEx(renderer_, sprite.texture, nullptr, &sprite.screen_rect, 0.0, nullptr, sprite.flip);
            } else {
                SDL_RenderCopy(renderer_, sprite.texture, nullptr, &sprite.screen_rect);
            }
        }
    }

    SDL_SetRenderTarget(renderer_, previous_target);

    SDL_SetTextureBlendMode(darkness_overlay_texture_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(darkness_overlay_texture_, overlay_alpha_byte);
    SDL_SetTextureColorMod(darkness_overlay_texture_, 0, 0, 0);

    SDL_Rect screen_dst{0, 0, screen_width_, screen_height_};
    SDL_RenderCopy(renderer_, darkness_overlay_texture_, nullptr, &screen_dst);
}
