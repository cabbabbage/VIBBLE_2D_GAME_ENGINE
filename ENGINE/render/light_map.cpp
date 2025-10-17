#include "light_map.hpp"
#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>
#include <optional>


namespace {
std::uint8_t clamp_byte(int value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

float clamp_unit(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

std::uint32_t pack_darkness_pixel(std::uint8_t darkness) {
    struct PixelFormatHolder {
        SDL_PixelFormat* format = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
        ~PixelFormatHolder() {
            if (format) {
                SDL_FreeFormat(format);
            }
        }
    };

    static PixelFormatHolder holder{};
    if (holder.format) {
        return SDL_MapRGBA(holder.format, 0, 0, 0, darkness);
    }
    return static_cast<std::uint32_t>(darkness) << 24;
}

std::optional<SDL_Rect> compute_light_screen_rect(const Assets* assets,
                                                  const Asset* asset,
                                                  const LightSource& light) {
    if (!assets || !asset || !asset->info) {
        return std::nullopt;
    }
    SDL_Texture* tex = light.texture;
    if (!tex) {
        return std::nullopt;
    }

    const auto& cam       = assets->getView();
    const float cam_scale = cam.get_scale();
    const float inv_scale = (std::isfinite(cam_scale) && cam_scale > 1e-6f) ? (1.0f / cam_scale) : 1.0f;

    int src_w = light.cached_w > 0 ? light.cached_w : 0;
    int src_h = light.cached_h > 0 ? light.cached_h : 0;
    if (src_w <= 0 || src_h <= 0) {
        SDL_QueryTexture(tex, nullptr, nullptr, &src_w, &src_h);
    }
    if (src_w <= 0 || src_h <= 0) {
        return std::nullopt;
    }

    int draw_w = std::max(1, static_cast<int>(std::lround(static_cast<float>(src_w) * inv_scale)));
    int draw_h = std::max(1, static_cast<int>(std::lround(static_cast<float>(src_h) * inv_scale)));
    if (draw_w <= 0 || draw_h <= 0) {
        return std::nullopt;
    }

    SDL_Point world_center{asset->pos.x + light.offset_x, asset->pos.y + light.offset_y};
    SDL_Point screen_center = cam.map_to_screen(world_center);

    SDL_Rect dst{screen_center.x - draw_w / 2,
                 screen_center.y - draw_h / 2,
                 draw_w,
                 draw_h};
    if (dst.w <= 0 || dst.h <= 0) {
        return std::nullopt;
    }
    return dst;
}

std::optional<SDL_Rect> compute_light_world_rect(const Asset* asset,
                                                 const LightSource& light) {
    if (!asset || !asset->info) {
        return std::nullopt;
    }
    SDL_Texture* tex = light.texture;
    if (!tex) {
        return std::nullopt;
    }

    int src_w = light.cached_w > 0 ? light.cached_w : 0;
    int src_h = light.cached_h > 0 ? light.cached_h : 0;
    if (src_w <= 0 || src_h <= 0) {
        SDL_QueryTexture(tex, nullptr, nullptr, &src_w, &src_h);
    }
    if (src_w <= 0 || src_h <= 0) {
        return std::nullopt;
    }

    float scale_factor = 1.0f;
    if (asset->info && std::isfinite(asset->info->scale_factor) && asset->info->scale_factor > 0.0f) {
        scale_factor = asset->info->scale_factor;
    }

    const int draw_w = std::max(1, static_cast<int>(std::lround(static_cast<float>(src_w) * scale_factor)));
    const int draw_h = std::max(1, static_cast<int>(std::lround(static_cast<float>(src_h) * scale_factor)));
    if (draw_w <= 0 || draw_h <= 0) {
        return std::nullopt;
    }

    SDL_Point world_center{asset->pos.x + light.offset_x, asset->pos.y + light.offset_y};
    SDL_Rect dst{world_center.x - draw_w / 2,
                 world_center.y - draw_h / 2,
                 draw_w,
                 draw_h};
    if (dst.w <= 0 || dst.h <= 0) {
        return std::nullopt;
    }
    return dst;
}

}  // namespace

LightMapQuadrant::LightMapQuadrant(LightMapQuadrant&& other) noexcept {
    *this = std::move(other);
}

LightMapQuadrant& LightMapQuadrant::operator=(LightMapQuadrant&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy_texture();
    world_rect_      = other.world_rect_;
    grid_width_      = other.grid_width_;
    grid_height_     = other.grid_height_;
    padding_cells_   = other.padding_cells_;
    stride_          = other.stride_;
    static_grid_     = std::move(other.static_grid_);
    tile_mask_       = other.tile_mask_;
    static_mask_     = other.static_mask_;
    base_brightness_ = other.base_brightness_;
    dirty_           = other.dirty_;
    active_          = other.active_;

    other.tile_mask_     = nullptr;
    other.static_mask_   = nullptr;
    other.grid_width_    = 0;
    other.grid_height_   = 0;
    other.padding_cells_ = 0;
    other.stride_        = 0;
    other.static_grid_.clear();
    other.base_brightness_ = 0.0f;
    other.dirty_           = true;
    other.active_          = false;
    other.world_rect_      = SDL_Rect{0, 0, 0, 0};
    return *this;
}

LightMapQuadrant::~LightMapQuadrant() {
    destroy_texture();
}

void LightMapQuadrant::destroy_texture() {
    if (tile_mask_) {
        SDL_DestroyTexture(tile_mask_);
        tile_mask_ = nullptr;
    }
    if (static_mask_) {
        SDL_DestroyTexture(static_mask_);
        static_mask_ = nullptr;
    }
}

void LightMapQuadrant::ensure_texture(SDL_Renderer* renderer) {
    if (!renderer) {
        destroy_texture();
        return;
    }
    if (tile_mask_) {
        return;
    }
    if (grid_width_ <= 0 || grid_height_ <= 0) {
        return;
    }
    // Create a texture that matches the quadrant's world-rect size.
    // We will resample our internal light grids into this texture so that
    // each quadrant owns a tile of size (world_rect_.w x world_rect_.h).
    const int tex_w = std::max(1, world_rect_.w);
    const int tex_h = std::max(1, world_rect_.h);
    // Use a render target so we can composite overlapping light textures directly
    tile_mask_ = SDL_CreateTexture(renderer,
                                   SDL_PIXELFORMAT_RGBA8888,
                                   SDL_TEXTUREACCESS_TARGET,
                                   tex_w,
                                   tex_h);
    if (tile_mask_) {
        // Default to alpha blend when drawing to screen; content will hold composited lights
        SDL_SetTextureBlendMode(tile_mask_, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(tile_mask_, SDL_ScaleModeBest);
#endif
    }
}

void LightMapQuadrant::ensure_static_mask(SDL_Renderer* renderer) {
    if (!renderer) {
        if (static_mask_) {
            SDL_DestroyTexture(static_mask_);
            static_mask_ = nullptr;
        }
        return;
    }
    if (static_mask_) {
        return;
    }
    if (world_rect_.w <= 0 || world_rect_.h <= 0) {
        return;
    }
    const int tex_w = std::max(1, world_rect_.w);
    const int tex_h = std::max(1, world_rect_.h);
    static_mask_ = SDL_CreateTexture(renderer,
                                     SDL_PIXELFORMAT_RGBA8888,
                                     SDL_TEXTUREACCESS_TARGET,
                                     tex_w,
                                     tex_h);
    if (static_mask_) {
        SDL_SetTextureBlendMode(static_mask_, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(static_mask_, SDL_ScaleModeBest);
#endif
    }
}

void LightMapQuadrant::configure(SDL_Renderer* renderer,
                                 const SDL_Rect& world_rect,
                                 int grid_resolution,
                                 int padding_cells) {
    destroy_texture();

    world_rect_    = world_rect;
    grid_width_    = std::max(1, grid_resolution);
    grid_height_   = std::max(1, grid_resolution);
    padding_cells_ = std::max(0, padding_cells);
    stride_        = grid_width_ + (padding_cells_ * 2);
    const int total_rows = grid_height_ + (padding_cells_ * 2);
    static_grid_.assign(static_cast<std::size_t>(stride_) * static_cast<std::size_t>(total_rows), 0);
    base_brightness_ = 0.0f;
    dirty_           = true;
    active_          = false;
    ensure_texture(renderer);
}

std::size_t LightMapQuadrant::index_from_cell(int cx, int cy) const {
    const int sx = std::clamp(cx + padding_cells_, 0, stride_ - 1);
    const int sy = std::clamp(cy + padding_cells_, 0, grid_height_ + (padding_cells_ * 2) - 1);
    return static_cast<std::size_t>(sy) * static_cast<std::size_t>(stride_) + static_cast<std::size_t>(sx);
}

void LightMapQuadrant::build_static(const std::vector<std::uint8_t>& grid, int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    const int expected = grid_width_ * grid_height_;
    if (static_cast<int>(grid.size()) < expected) {
        return;
    }

    double accum = 0.0;
    int    count = 0;
    for (int y = 0; y < grid_height_; ++y) {
        for (int x = 0; x < grid_width_; ++x) {
            const int src_index = y * width + x;
            const std::uint8_t value = grid[static_cast<std::size_t>(src_index)];
            const std::size_t dst_index = index_from_cell(x, y);
            static_grid_[dst_index] = value;
            accum += static_cast<double>(value) / 255.0;
            ++count;
        }
    }
    if (count > 0) {
        base_brightness_ = static_cast<float>(std::clamp(accum / static_cast<double>(count), 0.0, 1.0));
    } else {
        base_brightness_ = 0.0f;
    }
    dirty_ = true;
}

// Dynamic light ray stamping and fading removed.

LightMapQuadrant::GridStatistics LightMapQuadrant::static_grid_stats() const {
    GridStatistics stats{};
    if (grid_width_ <= 0 || grid_height_ <= 0) {
        return stats;
    }
    std::uint8_t min_value = 255;
    std::uint8_t max_value = 0;
    double        sum       = 0.0;
    int           count     = 0;
    for (int y = 0; y < grid_height_; ++y) {
        for (int x = 0; x < grid_width_; ++x) {
            const std::size_t idx   = index_from_cell(x, y);
            const std::uint8_t value = static_grid_[idx];
            min_value = std::min(min_value, value);
            max_value = std::max(max_value, value);
            sum += static_cast<double>(value);
            ++count;
        }
    }
    if (count > 0) {
        stats.empty   = false;
        stats.min     = static_cast<float>(min_value) / 255.0f;
        stats.max     = static_cast<float>(max_value) / 255.0f;
        stats.average = static_cast<float>(sum / static_cast<double>(count)) / 255.0f;
    }
    return stats;
}

// Dynamic statistics removed.

float LightMapQuadrant::combined_average(float static_weight, float /*dynamic_weight*/) const {
    const GridStatistics static_stats  = static_grid_stats();
    const float          s             = static_stats.empty ? 0.0f : static_stats.average;
    return clamp_unit(base_brightness_ + (s * static_weight));
}

float LightMapQuadrant::cell_sample(int cx, int cy, float static_weight, float /*dynamic_weight*/) const {
    if (stride_ <= 0) {
        return base_brightness_;
    }
    const std::size_t idx = index_from_cell(cx, cy);
    const float s = static_cast<float>(static_grid_[idx]) / 255.0f;
    return clamp_unit(base_brightness_ + (s * static_weight));
}

void LightMapQuadrant::clear_static_samples() {
    std::fill(static_grid_.begin(), static_grid_.end(), static_cast<std::uint8_t>(0));
    base_brightness_ = 0.0f;
}

bool LightMapQuadrant::sample_static_mask(SDL_Renderer* renderer) {
    if (!renderer || !static_mask_ || grid_width_ <= 0 || grid_height_ <= 0) {
        clear_static_samples();
        return false;
    }

    int tex_w = 0;
    int tex_h = 0;
    if (SDL_QueryTexture(static_mask_, nullptr, nullptr, &tex_w, &tex_h) != 0 || tex_w <= 0 || tex_h <= 0) {
        clear_static_samples();
        return false;
    }

    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(tex_w) * static_cast<std::size_t>(tex_h));
    if (pixels.empty()) {
        clear_static_samples();
        return false;
    }

    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, static_mask_);
    const int pitch = tex_w * static_cast<int>(sizeof(std::uint32_t));
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA8888, pixels.data(), pitch) != 0) {
        SDL_SetRenderTarget(renderer, prev_target);
        clear_static_samples();
        return false;
    }
    SDL_SetRenderTarget(renderer, prev_target);

    std::unique_ptr<SDL_PixelFormat, decltype(&SDL_FreeFormat)> format(
        SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888), &SDL_FreeFormat);
    if (!format) {
        clear_static_samples();
        return false;
    }

    clear_static_samples();

    double accum        = 0.0;
    int    sample_count = 0;

    auto compute_bounds = [](int coord, int divisions, int extent) {
        const double start = static_cast<double>(coord) * static_cast<double>(extent) /
                             static_cast<double>(divisions);
        const double end = static_cast<double>(coord + 1) * static_cast<double>(extent) /
                           static_cast<double>(divisions);
        int min_v = static_cast<int>(std::floor(start));
        int max_v = static_cast<int>(std::ceil(end));
        min_v     = std::clamp(min_v, 0, extent - 1);
        max_v     = std::clamp(std::max(min_v + 1, max_v), 1, extent);
        return std::pair<int, int>{min_v, max_v};
    };

    for (int gy = 0; gy < grid_height_; ++gy) {
        const auto [top, bottom] = compute_bounds(gy, grid_height_, tex_h);
        for (int gx = 0; gx < grid_width_; ++gx) {
            const auto [left, right] = compute_bounds(gx, grid_width_, tex_w);

            double cell_sum    = 0.0;
            int    pixel_count = 0;
            for (int py = top; py < bottom; ++py) {
                const std::size_t row_offset = static_cast<std::size_t>(py) * static_cast<std::size_t>(tex_w);
                for (int px = left; px < right; ++px) {
                    const std::uint32_t pixel = pixels[row_offset + static_cast<std::size_t>(px)];
                    Uint8               r = 0, g = 0, b = 0, a = 0;
                    SDL_GetRGBA(pixel, format.get(), &r, &g, &b, &a);
                    const double luminance = (0.2126 * static_cast<double>(r) +
                                              0.7152 * static_cast<double>(g) +
                                              0.0722 * static_cast<double>(b)) /
                                             255.0;
                    cell_sum += luminance;
                    ++pixel_count;
                }
            }

            if (pixel_count == 0) {
                const int sample_x = std::clamp((left + right) / 2, 0, tex_w - 1);
                const int sample_y = std::clamp((top + bottom) / 2, 0, tex_h - 1);
                const std::uint32_t pixel = pixels[static_cast<std::size_t>(sample_y) *
                                                    static_cast<std::size_t>(tex_w) +
                                                    static_cast<std::size_t>(sample_x)];
                Uint8 r = 0, g = 0, b = 0, a = 0;
                SDL_GetRGBA(pixel, format.get(), &r, &g, &b, &a);
                cell_sum    = (0.2126 * static_cast<double>(r) + 0.7152 * static_cast<double>(g) +
                            0.0722 * static_cast<double>(b)) /
                           255.0;
                pixel_count = 1;
            }

            const double average = (pixel_count > 0) ? (cell_sum / static_cast<double>(pixel_count)) : 0.0;
            const int    stored  = std::clamp(static_cast<int>(std::lround(average * 255.0)), 0, 255);
            const std::size_t dst_index = index_from_cell(gx, gy);
            static_grid_[dst_index]     = static_cast<std::uint8_t>(stored);
            accum += average;
            ++sample_count;
        }
    }

    if (sample_count > 0) {
        base_brightness_ = static_cast<float>(std::clamp(accum / static_cast<double>(sample_count), 0.0, 1.0));
    } else {
        base_brightness_ = 0.0f;
    }

    return true;
}

float LightMapQuadrant::sample_brightness(float local_x,
                                          float local_y,
                                          float static_weight,
                                          float /*dynamic_weight*/,
                                          bool bilinear) const {
    if (grid_width_ <= 0 || grid_height_ <= 0) {
        return 0.0f;
    }

    const float clamped_x = std::clamp(local_x, 0.0f, static_cast<float>(grid_width_ - 1));
    const float clamped_y = std::clamp(local_y, 0.0f, static_cast<float>(grid_height_ - 1));

    if (!bilinear) {
        const int ix = static_cast<int>(std::round(clamped_x));
        const int iy = static_cast<int>(std::round(clamped_y));
        return cell_sample(ix, iy, static_weight, 0.0f);
    }

    const int   x0 = static_cast<int>(std::floor(clamped_x));
    const int   y0 = static_cast<int>(std::floor(clamped_y));
    const int   x1 = std::min(x0 + 1, grid_width_ - 1);
    const int   y1 = std::min(y0 + 1, grid_height_ - 1);
    const float tx = clamped_x - static_cast<float>(x0);
    const float ty = clamped_y - static_cast<float>(y0);

    const float s00 = cell_sample(x0, y0, static_weight, 0.0f);
    const float s10 = cell_sample(x1, y0, static_weight, 0.0f);
    const float s01 = cell_sample(x0, y1, static_weight, 0.0f);
    const float s11 = cell_sample(x1, y1, static_weight, 0.0f);

    const float sx0 = s00 + (s10 - s00) * tx;
    const float sx1 = s01 + (s11 - s01) * tx;
    return sx0 + (sx1 - sx0) * ty;
}

void LightMapQuadrant::populate_static_base(SDL_Renderer* renderer, SDL_Texture* static_full_map) {
    if (!renderer) {
        ensure_static_mask(nullptr);
        clear_static_samples();
        dirty_ = true;
        return;
    }
    if (!static_full_map) {
        ensure_static_mask(nullptr);
        clear_static_samples();
        dirty_ = true;
        return;
    }
    if (world_rect_.w <= 0 || world_rect_.h <= 0) {
        ensure_static_mask(nullptr);
        clear_static_samples();
        return;
    }

    ensure_texture(renderer);
    ensure_static_mask(renderer);
    if (!static_mask_) {
        clear_static_samples();
        dirty_ = true;
        return;
    }

    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, static_mask_);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_Rect src = world_rect_;
    SDL_Rect dst{0, 0, world_rect_.w, world_rect_.h};
    SDL_RenderCopy(renderer, static_full_map, &src, &dst);

    SDL_SetRenderTarget(renderer, prev_target);

    if (!sample_static_mask(renderer)) {
        clear_static_samples();
    }
    dirty_ = true;
}

void LightMapQuadrant::copy_static_mask(SDL_Renderer* renderer) const {
    if (!renderer || !static_mask_) {
        return;
    }
    SDL_RenderCopy(renderer, static_mask_, nullptr, nullptr);
}

void LightMapQuadrant::adopt_static_mask(SDL_Texture* texture) {
    if (static_mask_ && static_mask_ != texture) {
        SDL_DestroyTexture(static_mask_);
    }
    static_mask_ = texture;
    dirty_ = true;
}

void LightMapQuadrant::set_base_brightness(float value) {
    base_brightness_ = clamp_unit(value);
}

void LightMapQuadrant::update_tile_mask(SDL_Renderer* renderer,
                                        const Assets* assets,
                                        float static_weight,
                                        float dynamic_weight) {
    (void)static_weight; (void)dynamic_weight; // static grid path no longer used for tile compositing
    ensure_texture(renderer);
    if (!tile_mask_ || !renderer) {
        return;
    }

    // Prepare render target
    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, tile_mask_);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    copy_static_mask(renderer);

    // If we have assets, composite every moving light texture that overlaps this quadrant
    if (assets) {
        for (Asset* asset : assets->getActiveMovingLightAssets()) {
            if (!asset || !asset->info) {
                continue;
            }
            for (const auto& light : asset->info->light_sources) {
                SDL_Texture* tex = light.texture;
                if (!tex) {
                    continue;
                }

                const std::optional<SDL_Rect> dst_screen = compute_light_screen_rect(assets, asset, light);
                if (!dst_screen) {
                    continue;
                }
                if (!SDL_HasIntersection(&*dst_screen, &world_rect_)) {
                    continue;
                }

                SDL_Rect dst_local{dst_screen->x - world_rect_.x,
                                   dst_screen->y - world_rect_.y,
                                   dst_screen->w,
                                   dst_screen->h};

                // Draw with additive blending so dynamic lights punch through static cache
                Uint8 save_r=255, save_g=255, save_b=255, save_a=255; SDL_BlendMode save_bm=SDL_BLENDMODE_BLEND;
                SDL_GetTextureColorMod(tex, &save_r, &save_g, &save_b);
                SDL_GetTextureAlphaMod(tex, &save_a);
                SDL_GetTextureBlendMode(tex, &save_bm);

                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);

                const Uint8 intensity_mod = clamp_byte(light.intensity);
                const Uint8 color_r       = clamp_byte(light.color.r);
                const Uint8 color_g       = clamp_byte(light.color.g);
                const Uint8 color_b       = clamp_byte(light.color.b);

                SDL_SetTextureColorMod(tex, color_r, color_g, color_b);
                SDL_SetTextureAlphaMod(tex, intensity_mod);
                SDL_RenderCopy(renderer, tex, nullptr, &dst_local);

                SDL_SetTextureBlendMode(tex, save_bm);
                SDL_SetTextureColorMod(tex, save_r, save_g, save_b);
                SDL_SetTextureAlphaMod(tex, save_a);
            }
        }
    }

    // Restore target and mark clean
    SDL_SetRenderTarget(renderer, prev_target);
    dirty_ = false;
}

void LightMapQuadrant::render_tile_mask(SDL_Renderer* renderer) const {
    if (!renderer || !tile_mask_) {
        return;
    }
    SDL_RenderCopy(renderer, tile_mask_, nullptr, &world_rect_);
}

void LightMapQuadrant::render_tile_mask(SDL_Renderer* renderer, Uint8 alpha_mod) const {
    if (!renderer || !tile_mask_) {
        return;
    }
    Uint8 saved_alpha = 255;
    SDL_GetTextureAlphaMod(tile_mask_, &saved_alpha);
    SDL_SetTextureAlphaMod(tile_mask_, alpha_mod);
    SDL_RenderCopy(renderer, tile_mask_, nullptr, &world_rect_);
    SDL_SetTextureAlphaMod(tile_mask_, saved_alpha);
}

LightMap::LightMap(Assets* assets,
                   int screen_width,
                   int screen_height,
                   std::unique_ptr<PrecomputedLightMap> precomputed_map)
    : assets_(assets),
      screen_width_(screen_width),
      screen_height_(screen_height),
      pending_precomputed_map_(std::move(precomputed_map)) {
    layout_.map_width = screen_width_;
    layout_.map_height = screen_height_;
    layout_.grid_resolution = static_grid_resolution_;
    layout_.padding_cells = padding_cells_;
}

LightMap::~LightMap() {
    destroy_static_full_map();
}

void LightMap::destroy_static_full_map() {
    if (static_full_map_) {
        SDL_DestroyTexture(static_full_map_);
        static_full_map_ = nullptr;
    }
    static_cache_dirty_ = true;
}

bool LightMap::adopt_precomputed_map(SDL_Renderer* renderer) {
    if (!renderer || !pending_precomputed_map_) {
        pending_precomputed_map_.reset();
        return false;
    }

    PrecomputedLightMap& map = *pending_precomputed_map_;
    if (map.map_width <= 0 || map.map_height <= 0 || map.quadrant_cols <= 0 || map.quadrant_rows <= 0) {
        pending_precomputed_map_.reset();
        return false;
    }

    const int base_size = (map.grid_spacing > 0 && map.cells_per_quadrant > 0)
                              ? map.grid_spacing * map.cells_per_quadrant
                              : 0;
    if (base_size <= 0) {
        pending_precomputed_map_.reset();
        return false;
    }

    destroy_static_full_map();

    screen_width_  = std::max(1, map.map_width);
    screen_height_ = std::max(1, map.map_height);
    quadrant_cols_ = std::max(1, map.quadrant_cols);
    quadrant_rows_ = std::max(1, map.quadrant_rows);
    quadrant_size_px_       = base_size;
    static_grid_resolution_ = std::max(1, map.grid_resolution);
    padding_cells_          = std::max(0, map.padding_cells);
    requested_quadrants_    = quadrant_cols_;
    requested_quadrant_size_px_ = quadrant_size_px_;

    layout_.map_width          = screen_width_;
    layout_.map_height         = screen_height_;
    layout_.grid_spacing       = map.grid_spacing;
    layout_.cells_per_quadrant = map.cells_per_quadrant;
    layout_.grid_resolution    = static_grid_resolution_;
    layout_.padding_cells      = padding_cells_;

    static_full_map_ = map.full_texture;
    map.full_texture = nullptr;

    const int expected_quadrants = quadrant_cols_ * quadrant_rows_;
    quadrants_.clear();
    quadrants_.reserve(expected_quadrants);

    for (int row = 0; row < quadrant_rows_; ++row) {
        for (int col = 0; col < quadrant_cols_; ++col) {
            const int index = row * quadrant_cols_ + col;
            if (index >= static_cast<int>(map.quadrants.size())) {
                continue;
            }
            PrecomputedLightMapQuadrant& source = map.quadrants[static_cast<std::size_t>(index)];

            LightMapQuadrant quadrant;
            quadrant.configure(renderer, source.world_rect, static_grid_resolution_, padding_cells_);
            if (!source.light_samples.empty()) {
                quadrant.build_static(source.light_samples, static_grid_resolution_, static_grid_resolution_);
            }
            quadrant.set_base_brightness(source.base_brightness);
            quadrant.adopt_static_mask(source.texture);
            source.texture = nullptr;
            quadrant.update_tile_mask(renderer, assets_, kDefaultStaticWeight, kDefaultDynamicWeight);
            quadrants_.push_back(std::move(quadrant));
            source.light_samples.clear();
        }
    }

    pending_precomputed_map_.reset();
    static_cache_dirty_ = false;
    return true;
}

void LightMap::build_static_full_map(SDL_Renderer* renderer) {
    if (!renderer) {
        destroy_static_full_map();
        for (auto& quadrant : quadrants_) {
            quadrant.populate_static_base(nullptr, nullptr);
            quadrant.set_dirty(true);
        }
        return;
    }
    const int target_w = layout_.map_width > 0 ? layout_.map_width : screen_width_;
    const int target_h = layout_.map_height > 0 ? layout_.map_height : screen_height_;
    if (target_w <= 0 || target_h <= 0) {
        destroy_static_full_map();
        return;
    }

    const int tex_w = std::max(1, target_w);
    const int tex_h = std::max(1, target_h);
    bool       needs_create = false;
    if (!static_full_map_) {
        needs_create = true;
    } else {
        int    current_w = 0;
        int    current_h = 0;
        Uint32 current_format = 0;
        int    current_access = 0;
        if (SDL_QueryTexture(static_full_map_, &current_format, &current_access, &current_w, &current_h) != 0 ||
            current_w != tex_w || current_h != tex_h) {
            destroy_static_full_map();
            needs_create = true;
        }
    }

    if (needs_create) {
        static_full_map_ = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_TARGET,
                                             tex_w,
                                             tex_h);
        if (!static_full_map_) {
            return;
        }
        SDL_SetTextureBlendMode(static_full_map_, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(static_full_map_, SDL_ScaleModeBest);
#endif
    }

    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, static_full_map_);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);

    const bool use_world_space = (layout_.grid_spacing > 0 && layout_.cells_per_quadrant > 0);

    if (assets_) {
        for (Asset* asset : assets_->getActiveStaticLightAssets()) {
            if (!asset || !asset->info) {
                continue;
            }
            for (const auto& light : asset->info->light_sources) {
                SDL_Texture* tex = light.texture;
                if (!tex) {
                    continue;
                }

                const std::optional<SDL_Rect> dst = use_world_space
                    ? compute_light_world_rect(asset, light)
                    : compute_light_screen_rect(assets_, asset, light);
                if (!dst) {
                    continue;
                }

                Uint8 save_r=255, save_g=255, save_b=255, save_a=255; SDL_BlendMode save_bm=SDL_BLENDMODE_BLEND;
                SDL_GetTextureColorMod(tex, &save_r, &save_g, &save_b);
                SDL_GetTextureAlphaMod(tex, &save_a);
                SDL_GetTextureBlendMode(tex, &save_bm);

                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
                SDL_RenderCopy(renderer, tex, nullptr, &*dst);

                SDL_SetTextureBlendMode(tex, save_bm);
                SDL_SetTextureColorMod(tex, save_r, save_g, save_b);
                SDL_SetTextureAlphaMod(tex, save_a);
            }
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(renderer, prev_target);

    for (auto& quadrant : quadrants_) {
        quadrant.populate_static_base(renderer, static_full_map_);
        quadrant.set_dirty(true);
    }
    static_cache_dirty_ = false;
}

void LightMap::set_virtual_light_map_quadrants(int quadrants) {
    requested_quadrants_ = std::clamp(quadrants, kMinQuadrantCount, kMaxQuadrantCount);
    if (requested_quadrants_ <= 0) {
        requested_quadrants_ = kMinQuadrantCount;
    }
    if (screen_width_ > 0 || screen_height_ > 0) {
        const int denom = std::max(1, requested_quadrants_);
        const int approx_w = (screen_width_ > 0) ? std::max(1, screen_width_ / denom) : kDefaultQuadrantSizePx;
        const int approx_h = (screen_height_ > 0) ? std::max(1, screen_height_ / denom) : kDefaultQuadrantSizePx;
        set_virtual_light_map_quadrant_size(std::max(approx_w, approx_h));
    }
}

void LightMap::set_virtual_light_map_quadrant_size(int size_px) {
    requested_quadrant_size_px_ = std::clamp(size_px, kMinQuadrantSizePx, kMaxQuadrantSizePx);
}

void LightMap::rebuild(SDL_Renderer* renderer) {
    if (!renderer) {
        build_static_full_map(nullptr);
        return;
    }
    if (pending_precomputed_map_) {
        if (adopt_precomputed_map(renderer)) {
            return;
        }
    }

    int base_width = 0;
    int base_height = 0;
    if (layout_.map_width > 0 && layout_.map_height > 0 && layout_.grid_spacing > 0 && layout_.cells_per_quadrant > 0) {
        screen_width_  = std::max(1, layout_.map_width);
        screen_height_ = std::max(1, layout_.map_height);
        base_width  = layout_.grid_spacing * layout_.cells_per_quadrant;
        base_height = base_width;
        quadrant_cols_ = std::max(1, screen_width_ / std::max(1, base_width));
        quadrant_rows_ = std::max(1, screen_height_ / std::max(1, base_height));
        quadrant_size_px_       = base_width;
        static_grid_resolution_ = std::max(1, layout_.grid_resolution);
        padding_cells_          = std::max(0, layout_.padding_cells);
        requested_quadrants_    = quadrant_cols_;
        requested_quadrant_size_px_ = quadrant_size_px_;
        layout_.map_width  = quadrant_cols_ * base_width;
        layout_.map_height = quadrant_rows_ * base_height;
    } else {
        if (screen_width_ <= 0 || screen_height_ <= 0) {
            build_static_full_map(renderer);
            return;
        }
        const int desired_size = std::clamp(requested_quadrant_size_px_, kMinQuadrantSizePx, kMaxQuadrantSizePx);
        quadrant_cols_ = std::max(1, (screen_width_ + desired_size - 1) / desired_size);
        quadrant_rows_ = std::max(1, (screen_height_ + desired_size - 1) / desired_size);
        requested_quadrants_ = std::max(quadrant_cols_, quadrant_rows_);

        base_width  = std::max(1, (screen_width_ + quadrant_cols_ - 1) / quadrant_cols_);
        base_height = std::max(1, (screen_height_ + quadrant_rows_ - 1) / quadrant_rows_);
        quadrant_size_px_ = std::max(base_width, base_height);
        layout_.map_width          = screen_width_;
        layout_.map_height         = screen_height_;
        layout_.grid_spacing       = 0;
        layout_.cells_per_quadrant = 0;
        layout_.grid_resolution    = static_grid_resolution_;
        layout_.padding_cells      = padding_cells_;
    }

    const int total_quadrants = quadrant_cols_ * quadrant_rows_;
    quadrants_.clear();
    quadrants_.reserve(static_cast<std::size_t>(std::max(0, total_quadrants)));

    build_static_full_map(renderer);

    for (int row = 0; row < quadrant_rows_; ++row) {
        for (int col = 0; col < quadrant_cols_; ++col) {
            SDL_Rect rect{};
            rect.x = col * base_width;
            rect.y = row * base_height;
            rect.w = (col == quadrant_cols_ - 1) ? (screen_width_ - rect.x) : base_width;
            rect.h = (row == quadrant_rows_ - 1) ? (screen_height_ - rect.y) : base_height;

            LightMapQuadrant quadrant;
            quadrant.configure(renderer, rect, static_grid_resolution_, padding_cells_);

            std::vector<std::uint8_t> static_grid(static_cast<std::size_t>(static_grid_resolution_) *
                                                  static_cast<std::size_t>(static_grid_resolution_),
                                                  0);
            quadrant.build_static(static_grid, static_grid_resolution_, static_grid_resolution_);
            quadrant.populate_static_base(renderer, static_full_map_);
            quadrant.update_tile_mask(renderer, assets_, kDefaultStaticWeight, kDefaultDynamicWeight);
            quadrants_.push_back(std::move(quadrant));
        }
    }
}

void LightMap::update(SDL_Renderer* renderer, std::uint32_t /*delta_ms*/) {
    if (!renderer) {
        build_static_full_map(nullptr);
        for (auto& quadrant : quadrants_) {
            quadrant.populate_static_base(nullptr, nullptr);
            quadrant.update_tile_mask(nullptr, assets_, kDefaultStaticWeight, kDefaultDynamicWeight);
            quadrant.set_active(false);
        }
        return;
    }

    if (static_cache_dirty_ || !static_full_map_) {
        build_static_full_map(renderer);
    }

    if (assets_) {
        for (Asset* asset : assets_->getActiveMovingLightAssets()) {
            mark_asset_lights_dirty(asset);
        }
    }

    for (auto& quadrant : quadrants_) {
        if (quadrant.dirty()) {
            quadrant.update_tile_mask(renderer, assets_, kDefaultStaticWeight, kDefaultDynamicWeight);
        }
        quadrant.set_active(false);
    }
}

const LightMapQuadrant* LightMap::quadrant(int index) const {
    if (index < 0 || index >= static_cast<int>(quadrants_.size())) {
        return nullptr;
    }
    return &quadrants_[static_cast<std::size_t>(index)];
}

SDL_Rect LightMap::quadrant_bounds(int index) const {
    const LightMapQuadrant* quad = quadrant(index);
    if (!quad) {
        return SDL_Rect{0, 0, 0, 0};
    }
    return quad->world_rect();
}

int LightMap::quadrant_for_point(float x, float y) const {
    SDL_Point point{static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y))};
    for (std::size_t i = 0; i < quadrants_.size(); ++i) {
        const SDL_Rect& rect = quadrants_[i].world_rect();
        if (SDL_PointInRect(&point, &rect)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int LightMap::find_quadrant_index(int world_x, int world_y) const {
    SDL_Point point{world_x, world_y};
    for (std::size_t i = 0; i < quadrants_.size(); ++i) {
        const SDL_Rect& rect = quadrants_[i].world_rect();
        if (SDL_PointInRect(&point, &rect)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

float LightMap::sample_internal(int quadrant_index,
                                float local_x,
                                float local_y,
                                bool  bilinear,
                                float static_weight,
                                float dynamic_weight) const {
    const LightMapQuadrant* quadrant_ptr = quadrant(quadrant_index);
    if (!quadrant_ptr) {
        return 0.0f;
    }
    return quadrant_ptr->sample_brightness(local_x,
                                           local_y,
                                           static_weight,
                                           dynamic_weight,
                                           bilinear);
}

// Dynamic moving light stamping removed.

float LightMap::sample_brightness(int world_x, int world_y, float static_weight, float dynamic_weight) const {
    const int quadrant_index = find_quadrant_index(world_x, world_y);
    if (quadrant_index < 0) {
        return 0.0f;
    }
    const LightMapQuadrant* quadrant_ptr = quadrant(quadrant_index);
    if (!quadrant_ptr) {
        return 0.0f;
    }
    const SDL_Rect& rect = quadrant_ptr->world_rect();
    if (rect.w <= 0 || rect.h <= 0) {
        return 0.0f;
    }
    const float nx = static_cast<float>(world_x - rect.x) / static_cast<float>(rect.w);
    const float ny = static_cast<float>(world_y - rect.y) / static_cast<float>(rect.h);
    const float local_x = nx * static_cast<float>(quadrant_ptr->grid_width() - 1);
    const float local_y = ny * static_cast<float>(quadrant_ptr->grid_height() - 1);
    return sample_internal(quadrant_index, local_x, local_y, false, static_weight, dynamic_weight);
}

float LightMap::sample_brightness_bilinear(float world_x,
                                           float world_y,
                                           float static_weight,
                                           float dynamic_weight) const {
    const int ix = static_cast<int>(std::round(world_x));
    const int iy = static_cast<int>(std::round(world_y));
    const int quadrant_index = find_quadrant_index(ix, iy);
    if (quadrant_index < 0) {
        return 0.0f;
    }
    const LightMapQuadrant* quadrant_ptr = quadrant(quadrant_index);
    if (!quadrant_ptr) {
        return 0.0f;
    }
    const SDL_Rect& rect = quadrant_ptr->world_rect();
    if (rect.w <= 0 || rect.h <= 0) {
        return 0.0f;
    }
    const float nx = (world_x - static_cast<float>(rect.x)) / static_cast<float>(rect.w);
    const float ny = (world_y - static_cast<float>(rect.y)) / static_cast<float>(rect.h);
    const float local_x = std::clamp(nx, 0.0f, 1.0f) * static_cast<float>(quadrant_ptr->grid_width() - 1);
    const float local_y = std::clamp(ny, 0.0f, 1.0f) * static_cast<float>(quadrant_ptr->grid_height() - 1);
    return sample_internal(quadrant_index, local_x, local_y, true, static_weight, dynamic_weight);
}

std::pair<int, int> LightMap::padding_pixels() const {
    if (quadrants_.empty()) {
        return {0, 0};
    }
    const LightMapQuadrant& reference = quadrants_.front();
    if (reference.grid_width() <= 0 || reference.grid_height() <= 0) {
        return {0, 0};
    }
    const SDL_Rect& rect = reference.world_rect();
    if (rect.w <= 0 || rect.h <= 0) {
        return {0, 0};
    }

    const double cell_width  = static_cast<double>(rect.w) / static_cast<double>(reference.grid_width());
    const double cell_height = static_cast<double>(rect.h) / static_cast<double>(reference.grid_height());
    const int    pad_cells   = reference.padding();
    if (pad_cells <= 0) {
        return {0, 0};
    }

    const int pad_x = static_cast<int>(std::ceil(cell_width * static_cast<double>(pad_cells)));
    const int pad_y = static_cast<int>(std::ceil(cell_height * static_cast<double>(pad_cells)));
    return {pad_x, pad_y};
}

void LightMap::render_visible_quadrants(SDL_Renderer* renderer, const SDL_Rect& view_rect) const {
    if (!renderer || quadrants_.empty()) {
        return;
    }
    if (view_rect.w <= 0 || view_rect.h <= 0) {
        return;
    }

    SDL_Rect expanded = view_rect;
    auto [pad_x, pad_y] = padding_pixels();
    if (pad_x > 0) {
        expanded.x -= pad_x;
        expanded.w += pad_x * 2;
    }
    if (pad_y > 0) {
        expanded.y -= pad_y;
        expanded.h += pad_y * 2;
    }

    for (const auto& quadrant : quadrants_) {
        const SDL_Rect& rect = quadrant.world_rect();
        if (rect.w <= 0 || rect.h <= 0) {
            continue;
        }
        if (SDL_HasIntersection(&rect, &expanded)) {
            quadrant.render_tile_mask(renderer);
        }
    }
}

void LightMap::render_visible_quadrants(SDL_Renderer* renderer, const SDL_Rect& view_rect, float alpha_multiplier) const {
    if (!renderer || quadrants_.empty()) {
        return;
    }
    if (view_rect.w <= 0 || view_rect.h <= 0) {
        return;
    }

    SDL_Rect expanded = view_rect;
    auto [pad_x, pad_y] = padding_pixels();
    if (pad_x > 0) {
        expanded.x -= pad_x;
        expanded.w += pad_x * 2;
    }
    if (pad_y > 0) {
        expanded.y -= pad_y;
        expanded.h += pad_y * 2;
    }

    const float clamped = std::clamp(alpha_multiplier, 0.0f, 1.0f);
    const Uint8 alpha    = static_cast<Uint8>(std::lround(clamped * 255.0f));

    for (const auto& quadrant : quadrants_) {
        const SDL_Rect& rect = quadrant.world_rect();
        if (rect.w <= 0 || rect.h <= 0) {
            continue;
        }
        if (SDL_HasIntersection(&rect, &expanded)) {
            quadrant.render_tile_mask(renderer, alpha);
        }
    }
}

void LightMap::mark_region_dirty(const SDL_Rect& screen_rect) {
    if (quadrants_.empty()) {
        return;
    }
    for (auto& quadrant : quadrants_) {
        if (SDL_HasIntersection(&screen_rect, &quadrant.world_rect())) {
            quadrant.set_dirty(true);
        }
    }
}

void LightMap::mark_asset_lights_dirty(const Asset* asset) {
    if (!asset || !asset->info || asset->info->light_sources.empty()) {
        return;
    }
    for (const auto& light : asset->info->light_sources) {
        const std::optional<SDL_Rect> rect = compute_light_screen_rect(assets_, asset, light);
        if (rect) {
            mark_region_dirty(*rect);
        }
    }
}

void LightMap::mark_static_cache_dirty() {
    static_cache_dirty_ = true;
}

