// Unified Chunk + LightMap implementation
#include "world/chunk.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "utils/log.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "render/global_light_source.hpp"
#include "world/grid.hpp"

namespace world {

Chunk::~Chunk() {
    if (static_light_map) {
        SDL_DestroyTexture(static_light_map);
        static_light_map = nullptr;
    }
    static_texture_set = false;
}

} // namespace world

namespace {
constexpr const char* kEnableChunkLightingEnv = "VIBBLE_ENABLE_CHUNK_LIGHTING";

bool env_truthy(const char* value) {
    if (!value || !value[0]) {
        return false;
    }
    const char c = value[0];
    return c == '1' || c == 'y' || c == 'Y' || c == 't' || c == 'T';
}

bool chunk_lighting_suspended_flag() {
    if (env_truthy(std::getenv(kEnableChunkLightingEnv))) {
        return false;
    }
    return true;
}

Uint8 clamp_alpha(float value) {
    return static_cast<Uint8>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

SDL_Rect world_rect_from_screen(const camera& cam, const SDL_Rect& screen_rect) {
    SDL_Point top_left     = cam.screen_to_map({screen_rect.x, screen_rect.y});
    SDL_Point bottom_right = cam.screen_to_map({screen_rect.x + screen_rect.w, screen_rect.y + screen_rect.h});
    SDL_Rect result{};
    result.x = std::min(top_left.x, bottom_right.x);
    result.y = std::min(top_left.y, bottom_right.y);
    result.w = std::abs(bottom_right.x - top_left.x);
    result.h = std::abs(bottom_right.y - top_left.y);
    return result;
}

bool intersects(const SDL_Rect& a, const SDL_Rect& b) {
    return SDL_HasIntersection(&a, &b) == SDL_TRUE;
}

float average_transparency(SDL_Renderer* renderer, SDL_Texture* texture) {
    if (!renderer || !texture) {
        return 0.0f;
    }

    int tex_w = 0;
    int tex_h = 0;
    if (SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h) != 0 || tex_w <= 0 || tex_h <= 0) {
        return 0.0f;
    }

    const std::size_t total_pixels =
        static_cast<std::size_t>(tex_w) * static_cast<std::size_t>(tex_h);
    if (total_pixels == 0) {
        return 0.0f;
    }
    if (tex_w != 0 && total_pixels / static_cast<std::size_t>(tex_w) != static_cast<std::size_t>(tex_h)) {
        vibble::log::warn("[LightMap] average_transparency: texture dimensions overflow sample area");
        return 0.0f;
    }

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, texture) != 0) {
        vibble::log::warn(std::string("[LightMap] average_transparency: SDL_SetRenderTarget failed: ") + SDL_GetError());
        return 0.0f;
    }
    struct RenderTargetReset {
        SDL_Renderer* renderer = nullptr;
        SDL_Texture*  previous = nullptr;
        ~RenderTargetReset() {
            if (renderer) {
                SDL_SetRenderTarget(renderer, previous);
            }
        }
    } reset{renderer, previous_target};

    std::unique_ptr<SDL_PixelFormat, decltype(&SDL_FreeFormat)> fmt(
        SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888), &SDL_FreeFormat);
    if (!fmt) {
        vibble::log::warn("[LightMap] average_transparency: SDL_AllocFormat failed");
        return 0.0f;
    }

    constexpr int kMaxReadTileEdge = 256;
    std::vector<std::uint32_t> tile_buffer;
    tile_buffer.reserve(static_cast<std::size_t>(kMaxReadTileEdge) * static_cast<std::size_t>(kMaxReadTileEdge));

    double accum_transparency = 0.0;
    const double inv_255 = 1.0 / 255.0;

    for (int y = 0; y < tex_h; y += kMaxReadTileEdge) {
        const int tile_h = std::min(kMaxReadTileEdge, tex_h - y);
        for (int x = 0; x < tex_w; x += kMaxReadTileEdge) {
            const int tile_w = std::min(kMaxReadTileEdge, tex_w - x);
            const std::size_t tile_pixels =
                static_cast<std::size_t>(tile_w) * static_cast<std::size_t>(tile_h);

            tile_buffer.resize(tile_pixels);

            SDL_Rect rect{x, y, tile_w, tile_h};
            const int pitch = tile_w * static_cast<int>(sizeof(std::uint32_t));
            if (SDL_RenderReadPixels(renderer,
                                     &rect,
                                     SDL_PIXELFORMAT_RGBA8888,
                                     tile_buffer.data(),
                                     pitch) != 0) {
                vibble::log::warn(std::string("[LightMap] average_transparency: SDL_RenderReadPixels failed: ") + SDL_GetError());
                return 0.0f;
            }

            for (std::uint32_t pixel : tile_buffer) {
                Uint8 alpha = 255;
                SDL_GetRGBA(pixel, fmt.get(), nullptr, nullptr, nullptr, &alpha);
                const double transparency = 1.0 - static_cast<double>(alpha) * inv_255;
                accum_transparency += transparency;
            }
        }
    }

    const double average = accum_transparency / static_cast<double>(total_pixels);
    return static_cast<float>(std::clamp(average, 0.0, 1.0));
}

void update_chunk_static_brightness_extrema(SDL_Renderer* renderer, world::Chunk& chunk) {
    const float avg_static = average_transparency(renderer, chunk.static_light_map);
    chunk.base_brightness = std::clamp(avg_static, 0.0f, 1.0f);
    const float case_a = 1.0f;
    const float case_b = chunk.base_brightness;
    chunk.light.min_static_avg_strength = std::min(case_a, case_b);
    chunk.light.max_static_avg_strength = std::max(case_a, case_b);
    chunk.light.needs_update = true;
}

template <typename T>
T lerp(T a, T b, float t) {
    return static_cast<T>(a + (b - a) * t);
}

std::pair<float, float> compute_brightness_gradient(const world::Chunk& center,
                                                    const world::Grid& grid,
                                                    int radius,
                                                    float falloff_x,
                                                    float falloff_y) {
    if (radius <= 0) return {0.0f, 0.0f};
    const float cb = std::clamp(center.base_brightness, 0.0f, 1.0f);
    float gx = 0.0f, gy = 0.0f;
    for (int dj = -radius; dj <= radius; ++dj) {
        for (int di = -radius; di <= radius; ++di) {
            if (di == 0 && dj == 0) continue;
            const int ni = center.i + di;
            const int nj = center.j + dj;
            const world::Chunk* n = grid.find_chunk_ij(ni, nj);
            const float nb = n ? std::clamp(n->base_brightness, 0.0f, 1.0f) : 0.0f;
            const float db = nb - cb;
            const float dx = static_cast<float>(di);
            const float dy = static_cast<float>(dj);
            const float dist = std::max(1.0f, std::sqrt(dx*dx + dy*dy));
            const float wx = falloff_x / dist;
            const float wy = falloff_y / dist;
            gx += db * (dx / dist) * wx;
            gy += db * (dy / dist) * wy;
        }
    }
    return {gx, gy};
}

// Compute average brightness in front of the chunk (negative j direction),
// adjusted by anisotropic horizontal/vertical falloff. Returns [0,1].
// Compute weighted averages of light strength in-front (negative j) and behind (positive j).
static std::pair<float, float> compute_directional_average_strengths(const LightMap::ShadowSettings& settings,
                                                                     const world::Grid& grid,
                                                                     const world::Chunk& center) {
    const int   R  = std::max(0, settings.search_radius_cells);
    const float fh = std::max(0.0f, settings.falloff_horizontal);
    const float fv = std::max(0.0f, settings.falloff_vertical);

    auto sample_dir = [&](int j_begin, int j_end) -> float {
        double accum_w = 0.0;
        double accum_v = 0.0;
        const int step = (j_begin <= j_end) ? 1 : -1;
        for (int dj = j_begin; dj != j_end + step; dj += step) {
            for (int di = -R; di <= R; ++di) {
                if (dj == 0) continue; // skip same row
                const int ni = center.i + di;
                const int nj = center.j + dj;
                const world::Chunk* n = grid.find_chunk_ij(ni, nj);
                if (!n) continue;
                const float sx = std::abs(static_cast<float>(di));
                const float sy = std::abs(static_cast<float>(dj));
                const float w  = 1.0f / (1.0f + sx * fh + sy * fv);
                const float s  = (n->light.is_active ? n->light.current_strength : n->base_brightness);
                accum_w += static_cast<double>(w);
                accum_v += static_cast<double>(w) * static_cast<double>(std::clamp(s, 0.0f, 1.0f));
            }
        }
        if (accum_w <= 1e-8) {
            return std::clamp(center.light.current_strength, 0.0f, 1.0f);
        }
        return static_cast<float>(std::clamp(accum_v / accum_w, 0.0, 1.0));
    };

    const float front_avg  = (R > 0) ? sample_dir(-R, -1) : std::clamp(center.light.current_strength, 0.0f, 1.0f);
    const float behind_avg = (R > 0) ? sample_dir(1,  R)  : std::clamp(center.light.current_strength, 0.0f, 1.0f);
    return {front_avg, behind_avg};
}

static void compute_use_shadow_data_for_chunk(const LightMap::ShadowSettings& settings,
                                              const world::Grid& grid,
                                              const std::pair<float,float>& grad,
                                              int map_dir_sign_x,
                                              float map_light_opacity_norm,
                                              world::Chunk& chunk) {
    // Opacity: inverse of front average strength.
    const auto [front_avg, behind_avg] = compute_directional_average_strengths(settings, grid, chunk);
    chunk.shadow.opacity  = std::clamp(1.0f - front_avg, 0.0f, 1.0f);

    // Scale: grow with front dominance, shrink with behind dominance (nonlinear towards min).
    const float d = std::clamp(front_avg - behind_avg, -1.0f, 1.0f); // [-1,1]
    const int min_p = std::clamp(settings.min_scale_percent, 50, 200);
    const int max_p = std::clamp(settings.max_scale_percent, 50, 200);
    const float base_p = 100.0f;
    float scale_percent = base_p;
    if (d >= 0.0f) {
        const float t = d; // more front light -> larger scale
        scale_percent = base_p + t * (static_cast<float>(max_p) - base_p);
    } else {
        const float b = -d; // more behind light -> smaller scale
        // Ease-out towards min: fast at first, slower as approaching min.
        const float ease_out = 1.0f - std::pow(1.0f - b, 2.0f); // gamma=2.0
        scale_percent = base_p - ease_out * (base_p - static_cast<float>(min_p));
    }
    scale_percent = std::clamp(scale_percent, static_cast<float>(min_p), static_cast<float>(max_p));
    chunk.shadow.scale = std::max(0.0f, scale_percent / 100.0f);

    // Base offset away from brightest direction (opposite brightness gradient)
    float gx = grad.first, gy = grad.second;
    float mag = std::sqrt(gx*gx + gy*gy);
    float nx = (mag > 1e-4f) ? (gx / mag) : 0.0f;
    float ny = (mag > 1e-4f) ? (gy / mag) : 0.0f;

    // Move opposite the gradient (away from brighter areas)
    float px = -nx * 100.0f;
    float py = -ny * 100.0f;

    // Map-light directional X adjustment: push away from map-light direction
    if (map_dir_sign_x != 0) {
        const float dir_push = std::clamp(map_light_opacity_norm, 0.0f, 1.0f) *
                               std::max(0.0f, settings.map_light_dir_offset_strength) * 100.0f;
        // If light direction points +X, push left (negative X), and vice-versa.
        px += static_cast<float>(-map_dir_sign_x) * dir_push;
    }

    chunk.shadow.offset_x_percent = std::clamp(px, -100.0f, 100.0f);
    chunk.shadow.offset_y_percent = std::clamp(py, -100.0f, 100.0f);

    chunk.shadow.parallax_intensity_percent = std::clamp(settings.parallax_percent, 0.0f, 100.0f);
}

} // namespace

// LightMap implementation
// ctor/dtor inlined in header

void LightMap::destroy_chunk_texture(world::Chunk& chunk) const {
    if (chunk.static_light_map) {
        SDL_DestroyTexture(chunk.static_light_map);
        chunk.static_light_map = nullptr;
    }
    chunk.static_texture_set = false;
    chunk.lighting_dirty     = true;
    chunk.light.needs_update = true;
}

void LightMap::ensure_chunk_static_texture(SDL_Renderer* renderer, world::Chunk& chunk) const {
    if (!renderer) {
        vibble::log::debug("[LightMap] ensure_chunk_static_texture: missing renderer");
        return;
    }
    if (chunk_lighting_suspended_flag()) {
        return;
    }
    if (!chunk.lighting_dirty && chunk.static_texture_set) {
        return;
    }

    destroy_chunk_texture(chunk);

    const int width  = std::max(1, chunk.world_bounds.w);
    const int height = std::max(1, chunk.world_bounds.h);
    vibble::log::debug(std::string("[LightMap] ensure_chunk_static_texture: CREATE size=") + std::to_string(width) + "x" +
                       std::to_string(height) + " for chunk(" + std::to_string(chunk.i) + "," + std::to_string(chunk.j) + ")");

    SDL_Texture* texture = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_TARGET,
                                             width,
                                             height);
    if (!texture) {
        vibble::log::warn(std::string("[LightMap] ensure_chunk_static_texture: SDL_CreateTexture failed: ") + SDL_GetError());
        chunk.static_texture_set = false;
        chunk.lighting_dirty     = true;
        return;
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, texture) != 0) {
        SDL_DestroyTexture(texture);
        SDL_SetRenderTarget(renderer, previous_target);
        vibble::log::warn(std::string("[LightMap] ensure_chunk_static_texture: SDL_SetRenderTarget failed: ") + SDL_GetError());
        chunk.static_texture_set = false;
        chunk.lighting_dirty     = true;
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

#if SDL_VERSION_ATLEAST(2, 0, 6)
    const SDL_BlendMode erase_alpha_blend = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ZERO,
        SDL_BLENDFACTOR_ONE,
        SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO,
        SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        SDL_BLENDOPERATION_ADD);
#else
    const SDL_BlendMode erase_alpha_blend = SDL_BLENDMODE_ADD;
#endif

    if (assets_) {
        const auto& static_lights = assets_->getActiveStaticLightAssets();
        vibble::log::debug(std::string("[LightMap] ensure_chunk_static_texture: stamping static lights (count=") +
                           std::to_string(static_lights.size()) + ") for chunk(" + std::to_string(chunk.i) + "," +
                           std::to_string(chunk.j) + ")");
        for (const Asset* asset : static_lights) {
            if (!asset || !asset->info) {
                continue;
            }
            if (asset->info->light_sources.empty()) {
                continue;
            }

            for (const auto& light : asset->info->light_sources) {
                SDL_Texture* tex = light.texture;
                if (!tex) {
                    continue;
                }

                int src_w = light.cached_w > 0 ? light.cached_w : 0;
                int src_h = light.cached_h > 0 ? light.cached_h : 0;
                if (src_w <= 0 || src_h <= 0) {
                    SDL_QueryTexture(tex, nullptr, nullptr, &src_w, &src_h);
                }
                if (src_w <= 0 || src_h <= 0) {
                    continue;
                }

                const int draw_w = std::max(1, src_w);
                const int draw_h = std::max(1, src_h);

                SDL_Point world_center{ asset->pos.x + light.offset_x, asset->pos.y + light.offset_y };
                SDL_Rect  world_dst{ world_center.x - draw_w / 2,
                                     world_center.y - draw_h / 2,
                                     draw_w,
                                     draw_h };

                SDL_Rect intersection{};
                if (!SDL_IntersectRect(&world_dst, &chunk.world_bounds, &intersection)) {
                    continue;
                }

                Uint8 save_r = 255, save_g = 255, save_b = 255, save_a = 255;
                SDL_BlendMode save_bm = SDL_BLENDMODE_BLEND;
                SDL_GetTextureColorMod(tex, &save_r, &save_g, &save_b);
                SDL_GetTextureAlphaMod(tex, &save_a);
                SDL_GetTextureBlendMode(tex, &save_bm);

                SDL_SetTextureBlendMode(tex, erase_alpha_blend);
                SDL_SetTextureColorMod(tex, 255, 255, 255);
                SDL_SetTextureAlphaMod(tex, 255);

                SDL_Rect local_dst = world_dst;
                local_dst.x -= chunk.world_bounds.x;
                local_dst.y -= chunk.world_bounds.y;
                SDL_RenderCopy(renderer, tex, nullptr, &local_dst);

                SDL_SetTextureBlendMode(tex, save_bm);
                SDL_SetTextureColorMod(tex, save_r, save_g, save_b);
                SDL_SetTextureAlphaMod(tex, save_a);
            }
        }
    }

    SDL_SetRenderTarget(renderer, previous_target);

    chunk.static_light_map   = texture;
    chunk.static_texture_set = true;
    chunk.lighting_dirty     = false;
    update_chunk_static_brightness_extrema(renderer, chunk);
    vibble::log::debug(std::string("[LightMap] ensure_chunk_static_texture: COMPLETE chunk(") + std::to_string(chunk.i) +
                       "," + std::to_string(chunk.j) + ") base_brightness=" + std::to_string(chunk.base_brightness));
}

void LightMap::rebuild(SDL_Renderer* /*renderer*/) {
    std::scoped_lock lock(mutex_);
    if (!assets_) {
        return;
    }
    world::Grid& grid = assets_->world_grid();
    for (world::Chunk* chunk : grid.active_chunks()) {
        if (!chunk) {
            continue;
        }
        chunk->lighting_dirty     = true;
        chunk->static_texture_set = false;
        chunk->light.needs_update = true;
    }
}

void LightMap::update(SDL_Renderer* renderer, std::uint32_t /*delta_ms*/) {
    std::scoped_lock lock(mutex_);
    if (chunk_lighting_suspended_flag()) {
        return;
    }
    const auto& chunks = active_chunks();
    for (world::Chunk* chunk : chunks) {
        if (!chunk) {
            continue;
        }
        ensure_chunk_static_texture(renderer, *chunk);
    }

    if (!assets_) {
        return;
    }

    float screen_light_opacity = 0.0f;
    {
        const Global_Light_Source* gl = assets_->map_light_source();
        if (gl) {
            const int min_a = gl->min_opacity();
            const int max_a = gl->max_opacity();
            const int cur_a = std::clamp(static_cast<int>(gl->get_current_color().a), min_a, max_a);
            const int range = std::max(1, max_a - min_a);
            screen_light_opacity = std::clamp(static_cast<float>(cur_a - min_a) / static_cast<float>(range), 0.0f, 1.0f);
        }
    }

    const bool screen_changed = (std::abs(screen_light_opacity - last_screen_light_opacity_) > 1e-4f);
    last_screen_light_opacity_ = screen_light_opacity;

    int min_i = INT32_MAX, max_i = INT32_MIN, min_j = INT32_MAX, max_j = INT32_MIN;
    for (const world::Chunk* c : chunks) {
        if (!c) continue;
        min_i = std::min(min_i, c->i); max_i = std::max(max_i, c->i);
        min_j = std::min(min_j, c->j); max_j = std::max(max_j, c->j);
    }

    std::vector<world::Chunk*> update_set;
    update_set.reserve(chunks.size());
    auto add_unique = [&](world::Chunk* c){ if (c && std::find(update_set.begin(), update_set.end(), c) == update_set.end()) update_set.push_back(c); };
    for (world::Chunk* c : chunks) add_unique(c);

    const world::Grid& grid = assets_->world_grid();
    for (world::Chunk* c : chunks) {
        if (!c) continue;
        const bool is_edge = (c->i == min_i) || (c->i == max_i) || (c->j == min_j) || (c->j == max_j);
        if (!is_edge) continue;
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                if (di == 0 && dj == 0) continue;
                if (world::Chunk* n = grid.find_chunk_ij(c->i + di, c->j + dj)) {
                    add_unique(n);
                }
            }
        }
    }

    const auto& moving = assets_->getActiveMovingLightAssets();
    for (world::Chunk* chunk : update_set) {
        if (!chunk) continue;
        chunk->light.is_active = true;
        if (screen_changed) chunk->light.needs_update = true;

        bool occupied = false;
        for (const Asset* a : moving) {
            if (!a) continue;
            SDL_Point p{a->pos.x, a->pos.y};
            if (SDL_PointInRect(&p, &chunk->world_bounds)) { occupied = true; break; }
        }
        if (occupied != chunk->light.is_occupied_by_moving_source) {
            chunk->light.needs_update = true;
        }
        chunk->light.is_occupied_by_moving_source = occupied;

        if (!chunk->light.needs_update) continue;

        if (chunk->light.is_occupied_by_moving_source) {
            const float static_avg = chunk->static_light_map
                                         ? average_transparency(renderer, chunk->static_light_map)
                                         : chunk->base_brightness;
            chunk->light.current_strength = static_avg;
        } else {
            chunk->light.current_strength = lerp(chunk->light.min_static_avg_strength,
                                                 chunk->light.max_static_avg_strength,
                                                 screen_light_opacity);
        }

        const ShadowSettings settings{};
        const int   radius = std::max(0, settings.search_radius_cells);
        const float fx     = std::max(0.0f, settings.falloff_horizontal);
        const float fy     = std::max(0.0f, settings.falloff_vertical);
        const auto grad    = compute_brightness_gradient(*chunk, grid, radius, fx, fy);
        int map_dir_sign_x = 0;
        if (const Global_Light_Source* gl = assets_->map_light_source()) {
            const SDL_Point ref = gl->get_direction_reference();
            const SDL_Point tgt = gl->get_direction_target();
            const int dx = tgt.x - ref.x;
            map_dir_sign_x = (dx > 0) ? 1 : ((dx < 0) ? -1 : 0);
        }
        compute_use_shadow_data_for_chunk(settings, grid, grad, map_dir_sign_x, screen_light_opacity, *chunk);

        chunk->light.needs_update = false;
    }
}

float LightMap::sample_brightness(int world_x,
                                  int world_y,
                                  float static_weight,
                                  float dynamic_weight) const {
    std::scoped_lock lock(mutex_);
    (void)dynamic_weight;
    world::Chunk* chunk = ensure_chunk_from_world(SDL_Point{world_x, world_y});
    if (!chunk) {
        vibble::log::warn("[LightMap] sample_brightness missing chunk for world point (" +
                          std::to_string(world_x) + ", " + std::to_string(world_y) + ")");
        return 1.0f;
    }
    const float weight = std::clamp(static_weight, 0.0f, 1.0f);
    return std::clamp(chunk->base_brightness * weight, 0.0f, 1.0f);
}

float LightMap::sample_brightness_bilinear(float world_x,
                                           float world_y,
                                           float static_weight,
                                           float dynamic_weight) const {
    std::scoped_lock lock(mutex_);
    return sample_brightness(static_cast<int>(std::lround(world_x)),
                             static_cast<int>(std::lround(world_y)),
                             static_weight,
                             dynamic_weight);
}

void LightMap::render_visible_chunks(SDL_Renderer* renderer, const SDL_Rect& view_rect) const {
    render_visible_chunks(renderer, view_rect, 1.0f);
}

void LightMap::render_visible_chunks(SDL_Renderer* renderer,
                                        const SDL_Rect& view_rect,
                                        float alpha_multiplier) const {
    std::scoped_lock lock(mutex_);
    if (!renderer || !assets_) {
        return;
    }
    const camera& cam = assets_->getView();

    SDL_Rect world_view = world_rect_from_screen(cam, view_rect);

    for (world::Chunk* chunk : active_chunks()) {
        if (!chunk || !chunk->static_light_map) {
            continue;
        }
        if (!intersects(chunk->world_bounds, world_view)) {
            continue;
        }

        SDL_Point top_left     = cam.map_to_screen({chunk->world_bounds.x, chunk->world_bounds.y});
        SDL_Point bottom_right = cam.map_to_screen({chunk->world_bounds.x + chunk->world_bounds.w,
                                                    chunk->world_bounds.y + chunk->world_bounds.h});
        SDL_Rect dst{};
        dst.x = std::min(top_left.x, bottom_right.x);
        dst.y = std::min(top_left.y, bottom_right.y);
        dst.w = std::abs(bottom_right.x - top_left.x);
        dst.h = std::abs(bottom_right.y - top_left.y);
        if (dst.w <= 0 || dst.h <= 0) {
            continue;
        }

        const float chunk_alpha_multiplier = std::clamp(chunk->opacity_strength, 0.0f, 1.0f);
        const Uint8 chunk_alpha = clamp_alpha(alpha_multiplier * chunk_alpha_multiplier);

        const float scale_strength = std::max(0.0f, chunk->scale_strength);
        if (std::abs(scale_strength - 1.0f) > 1e-4f) {
            const float center_x = static_cast<float>(dst.x) + static_cast<float>(dst.w) / 2.0f;
            const float center_y = static_cast<float>(dst.y) + static_cast<float>(dst.h) / 2.0f;
            const float scaled_w = static_cast<float>(dst.w) * scale_strength;
            const float scaled_h = static_cast<float>(dst.h) * scale_strength;
            dst.w = std::max(1, static_cast<int>(std::lround(scaled_w)));
            dst.h = std::max(1, static_cast<int>(std::lround(scaled_h)));
            dst.x = static_cast<int>(std::lround(center_x - static_cast<float>(dst.w) / 2.0f));
            dst.y = static_cast<int>(std::lround(center_y - static_cast<float>(dst.h) / 2.0f));
        }

        if (chunk->offset_x != 0 || chunk->offset_y != 0) {
            SDL_Point origin_screen = cam.map_to_screen({chunk->world_bounds.x, chunk->world_bounds.y});
            SDL_Point offset_screen = cam.map_to_screen({chunk->world_bounds.x + chunk->offset_x,
                                                         chunk->world_bounds.y + chunk->offset_y});
            dst.x += offset_screen.x - origin_screen.x;
            dst.y += offset_screen.y - origin_screen.y;
        }

        SDL_SetTextureAlphaMod(chunk->static_light_map, chunk_alpha);
        SDL_RenderCopy(renderer, chunk->static_light_map, nullptr, &dst);
    }
}

void LightMap::render_visible_chunks_debug(SDL_Renderer* renderer,
                                              const SDL_Rect& view_rect,
                                              float alpha_multiplier) const {
    render_visible_chunks(renderer, view_rect, alpha_multiplier);
}

void LightMap::mark_region_dirty(const SDL_Rect& screen_rect) {
    std::scoped_lock lock(mutex_);
    if (!assets_) {
        return;
    }
    const camera& cam     = assets_->getView();
    SDL_Rect      world_r = world_rect_from_screen(cam, screen_rect);
    for (world::Chunk* chunk : active_chunks()) {
        if (chunk && intersects(chunk->world_bounds, world_r)) {
            chunk->light.needs_update = true;
        }
    }
}

void LightMap::mark_asset_lights_dirty(const Asset* asset) {
    std::scoped_lock lock(mutex_);
    if (!asset) {
        return;
    }
    if (world::Chunk* chunk = ensure_chunk_from_world(asset->pos)) {
        chunk->light.needs_update = true;
    } else {
        vibble::log::warn("[LightMap] mark_asset_lights_dirty missing chunk for asset at (" +
                          std::to_string(asset->pos.x) + ", " + std::to_string(asset->pos.y) + ")");
    }
}

void LightMap::mark_static_cache_dirty() {
    std::scoped_lock lock(mutex_);
    if (!assets_) {
        return;
    }
    for (world::Chunk* chunk : active_chunks()) {
        if (chunk) {
            chunk->lighting_dirty     = true;
            chunk->static_texture_set = false;
        }
    }
}

const std::vector<world::Chunk*>& LightMap::active_chunks() const {
    static const std::vector<world::Chunk*> kEmpty{};
    if (!assets_) {
        return kEmpty;
    }
    return assets_->world_grid().active_chunks();
}

world::Chunk* LightMap::chunk_from_world(SDL_Point world_px) const {
    std::scoped_lock lock(mutex_);
    if (!assets_) {
        return nullptr;
    }
    return assets_->world_grid().chunk_from_world(world_px);
}

world::Chunk* LightMap::ensure_chunk_from_world(SDL_Point world_px) const {
    std::scoped_lock lock(mutex_);
    if (!assets_) {
        return nullptr;
    }
    return assets_->world_grid().ensure_chunk_from_world(world_px);
}

int LightMap::chunk_count() const {
    std::scoped_lock lock(mutex_);
    return static_cast<int>(active_chunks().size());
}

int LightMap::chunk_columns() const {
    std::scoped_lock lock(mutex_);
    const int count = chunk_count();
    if (count <= 0) {
        return 0;
    }
    const int columns = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
    return std::max(1, columns);
}

int LightMap::chunk_rows() const {
    std::scoped_lock lock(mutex_);
    const int count = chunk_count();
    if (count <= 0) {
        return 0;
    }
    const int columns = chunk_columns();
    return std::max(1, (count + columns - 1) / columns);
}

const world::Chunk* LightMap::chunk_at(int index) const {
    std::scoped_lock lock(mutex_);
    const auto& chunks = active_chunks();
    if (index < 0 || static_cast<std::size_t>(index) >= chunks.size()) {
        return nullptr;
    }
    return chunks[static_cast<std::size_t>(index)];
}

SDL_Rect LightMap::chunk_bounds(int index) const {
    std::scoped_lock lock(mutex_);
    if (const world::Chunk* chunk = chunk_at(index)) {
        return chunk->world_bounds;
    }
    return SDL_Rect{0, 0, 0, 0};
}

std::optional<world::Chunk::UseShadowData> LightMap::get_shadow_data(SDL_FPoint world_or_screen_pos) const {
    std::scoped_lock lock(mutex_);
    world::Chunk* chunk = nullptr;
    if (assets_) {
        chunk = chunk_from_world(SDL_Point{static_cast<int>(std::lround(world_or_screen_pos.x)),
                                           static_cast<int>(std::lround(world_or_screen_pos.y))});
        if (!chunk) {
            const camera& cam = assets_->getView();
            SDL_Point from_screen = cam.screen_to_map({static_cast<int>(std::lround(world_or_screen_pos.x)),
                                                       static_cast<int>(std::lround(world_or_screen_pos.y))});
            chunk = chunk_from_world(from_screen);
        }
    }
    if (!chunk) return std::nullopt;
    return chunk->shadow;
}


