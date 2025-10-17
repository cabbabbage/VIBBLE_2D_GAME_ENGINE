#include "light_map.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>


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
    dynamic_grid_    = std::move(other.dynamic_grid_);
    tile_mask_       = other.tile_mask_;
    base_brightness_ = other.base_brightness_;
    dirty_           = other.dirty_;
    active_          = other.active_;

    other.tile_mask_     = nullptr;
    other.grid_width_    = 0;
    other.grid_height_   = 0;
    other.padding_cells_ = 0;
    other.stride_        = 0;
    other.static_grid_.clear();
    other.dynamic_grid_.clear();
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
    tile_mask_ = SDL_CreateTexture(renderer,
                                   SDL_PIXELFORMAT_RGBA8888,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   tex_w,
                                   tex_h);
    if (tile_mask_) {
        SDL_SetTextureBlendMode(tile_mask_, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(tile_mask_, SDL_ScaleModeBest);
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
    dynamic_grid_.assign(static_cast<std::size_t>(stride_) * static_cast<std::size_t>(total_rows), 0);
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

void LightMapQuadrant::stamp_moving_lights(const std::vector<std::uint8_t>& grid,
                                           int width,
                                           int height,
                                           std::uint8_t clamp) {
    if (width <= 0 || height <= 0) {
        return;
    }
    const int expected = grid_width_ * grid_height_;
    if (static_cast<int>(grid.size()) < expected) {
        return;
    }
    for (int y = 0; y < grid_height_; ++y) {
        for (int x = 0; x < grid_width_; ++x) {
            const int src_index = y * width + x;
            const std::uint8_t value = clamp_byte(grid[static_cast<std::size_t>(src_index)]);
            const std::size_t dst_index = index_from_cell(x, y);
            const std::uint8_t clamped = std::min<std::uint8_t>(value, clamp);
            if (clamped > dynamic_grid_[dst_index]) {
                dynamic_grid_[dst_index] = clamped;
            }
        }
    }
    dirty_  = true;
    active_ = true;
}

void LightMapQuadrant::fade_dynamic(std::uint8_t fade) {
    if (fade == 0) {
        return;
    }
    const std::size_t total = dynamic_grid_.size();
    bool changed = false;
    for (std::size_t i = 0; i < total; ++i) {
        const std::uint8_t value = dynamic_grid_[i];
        if (value == 0) {
            continue;
        }
        const std::uint8_t next = (value > fade) ? static_cast<std::uint8_t>(value - fade) : 0;
        if (next != value) {
            dynamic_grid_[i] = next;
            changed = true;
        }
    }
    if (changed) {
        dirty_ = true;
    }
}

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

LightMapQuadrant::GridStatistics LightMapQuadrant::dynamic_grid_stats() const {
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
            const std::uint8_t value = dynamic_grid_[idx];
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

float LightMapQuadrant::combined_average(float static_weight, float dynamic_weight) const {
    const GridStatistics static_stats  = static_grid_stats();
    const GridStatistics dynamic_stats = dynamic_grid_stats();
    const float          s             = static_stats.empty ? 0.0f : static_stats.average;
    const float          d             = dynamic_stats.empty ? 0.0f : dynamic_stats.average;
    return clamp_unit(base_brightness_ + (s * static_weight) + (d * dynamic_weight));
}

float LightMapQuadrant::cell_sample(int cx, int cy, float static_weight, float dynamic_weight) const {
    if (stride_ <= 0) {
        return base_brightness_;
    }
    const std::size_t idx = index_from_cell(cx, cy);
    const float s = static_cast<float>(static_grid_[idx]) / 255.0f;
    const float d = static_cast<float>(dynamic_grid_[idx]) / 255.0f;
    return clamp_unit(base_brightness_ + (s * static_weight) + (d * dynamic_weight));
}

float LightMapQuadrant::sample_brightness(float local_x,
                                          float local_y,
                                          float static_weight,
                                          float dynamic_weight,
                                          bool bilinear) const {
    if (grid_width_ <= 0 || grid_height_ <= 0) {
        return 0.0f;
    }

    const float clamped_x = std::clamp(local_x, 0.0f, static_cast<float>(grid_width_ - 1));
    const float clamped_y = std::clamp(local_y, 0.0f, static_cast<float>(grid_height_ - 1));

    if (!bilinear) {
        const int ix = static_cast<int>(std::round(clamped_x));
        const int iy = static_cast<int>(std::round(clamped_y));
        return cell_sample(ix, iy, static_weight, dynamic_weight);
    }

    const int   x0 = static_cast<int>(std::floor(clamped_x));
    const int   y0 = static_cast<int>(std::floor(clamped_y));
    const int   x1 = std::min(x0 + 1, grid_width_ - 1);
    const int   y1 = std::min(y0 + 1, grid_height_ - 1);
    const float tx = clamped_x - static_cast<float>(x0);
    const float ty = clamped_y - static_cast<float>(y0);

    const float s00 = cell_sample(x0, y0, static_weight, dynamic_weight);
    const float s10 = cell_sample(x1, y0, static_weight, dynamic_weight);
    const float s01 = cell_sample(x0, y1, static_weight, dynamic_weight);
    const float s11 = cell_sample(x1, y1, static_weight, dynamic_weight);

    const float sx0 = s00 + (s10 - s00) * tx;
    const float sx1 = s01 + (s11 - s01) * tx;
    return sx0 + (sx1 - sx0) * ty;
}

void LightMapQuadrant::update_tile_mask(SDL_Renderer* renderer, float static_weight, float dynamic_weight) {
    ensure_texture(renderer);
    if (!tile_mask_) {
        return;
    }

    // Generate a tile that matches the quadrant pixel size by resampling the
    // internal light grids. This effectively "splits" the map-sized light into
    // quadrant-sized tiles so placement aligns with assets.
    int tex_w = 0;
    int tex_h = 0;
    SDL_QueryTexture(tile_mask_, nullptr, nullptr, &tex_w, &tex_h);
    if (tex_w <= 0 || tex_h <= 0) {
        return;
    }

    const std::size_t total_pixels = static_cast<std::size_t>(tex_w) * static_cast<std::size_t>(tex_h);
    std::vector<std::uint32_t> pixels(total_pixels, 0);

    const int gw = std::max(1, grid_width_);
    const int gh = std::max(1, grid_height_);

    // Map each destination pixel to a sample in grid-space. Use bilinear sampling
    // for smoother falloff and to better match full-map reconstruction.
    for (int py = 0; py < tex_h; ++py) {
        const float v = (tex_h > 1) ? (static_cast<float>(py) / static_cast<float>(tex_h - 1)) : 0.0f;
        const float gy = v * static_cast<float>(gh - 1);
        for (int px = 0; px < tex_w; ++px) {
            const float u = (tex_w > 1) ? (static_cast<float>(px) / static_cast<float>(tex_w - 1)) : 0.0f;
            const float gx = u * static_cast<float>(gw - 1);

            const float sample = sample_brightness(gx, gy, static_weight, dynamic_weight, /*bilinear=*/true);
            const std::uint8_t value    = clamp_byte(static_cast<int>(std::round(sample * 255.0f)));
            const std::uint8_t darkness = static_cast<std::uint8_t>(255 - value);
            const std::uint32_t rgba    = pack_darkness_pixel(darkness);

            const std::size_t idx = static_cast<std::size_t>(py) * static_cast<std::size_t>(tex_w) +
                                    static_cast<std::size_t>(px);
            pixels[idx] = rgba;
        }
    }

    void* pixels_ptr = nullptr;
    int   pitch      = 0;
    if (SDL_LockTexture(tile_mask_, nullptr, &pixels_ptr, &pitch) != 0) {
        return;
    }

    const int row_bytes = tex_w * static_cast<int>(sizeof(std::uint32_t));
    std::uint8_t* dst   = static_cast<std::uint8_t*>(pixels_ptr);
    const std::uint8_t* src = reinterpret_cast<const std::uint8_t*>(pixels.data());
    for (int y = 0; y < tex_h; ++y) {
        std::memcpy(dst + static_cast<std::size_t>(y) * pitch,
                    src + static_cast<std::size_t>(y) * row_bytes,
                    static_cast<std::size_t>(row_bytes));
    }
    SDL_UnlockTexture(tile_mask_);
    dirty_ = false;
}

void LightMapQuadrant::render_tile_mask(SDL_Renderer* renderer) const {
    if (!renderer || !tile_mask_) {
        return;
    }
    SDL_RenderCopy(renderer, tile_mask_, nullptr, &world_rect_);
}

LightMap::LightMap(Assets* assets, int screen_width, int screen_height)
    : assets_(assets),
      screen_width_(screen_width),
      screen_height_(screen_height) {}

LightMap::~LightMap() = default;

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
        return;
    }
    if (screen_width_ <= 0 || screen_height_ <= 0) {
        return;
    }

    const int desired_size = std::clamp(requested_quadrant_size_px_, kMinQuadrantSizePx, kMaxQuadrantSizePx);
    quadrant_cols_ = std::max(1, (screen_width_ + desired_size - 1) / desired_size);
    quadrant_rows_ = std::max(1, (screen_height_ + desired_size - 1) / desired_size);
    requested_quadrants_ = std::max(quadrant_cols_, quadrant_rows_);

    const int base_width = std::max(1, (screen_width_ + quadrant_cols_ - 1) / quadrant_cols_);
    const int base_height = std::max(1, (screen_height_ + quadrant_rows_ - 1) / quadrant_rows_);
    quadrant_size_px_ = std::max(base_width, base_height);

    const int total_quadrants = quadrant_cols_ * quadrant_rows_;
    quadrants_.clear();
    quadrants_.reserve(static_cast<std::size_t>(total_quadrants));

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
            quadrant.update_tile_mask(renderer, kDefaultStaticWeight, kDefaultDynamicWeight);
            quadrants_.push_back(std::move(quadrant));
        }
    }
}

void LightMap::update(SDL_Renderer* renderer, std::uint32_t delta_ms) {
    if (!renderer) {
        return;
    }
    const std::uint8_t fade = static_cast<std::uint8_t>(std::min(delta_ms / 6, 20u));
    for (auto& quadrant : quadrants_) {
        if (!quadrant.active()) {
            quadrant.fade_dynamic(fade);
        }
        if (quadrant.dirty()) {
            quadrant.update_tile_mask(renderer, kDefaultStaticWeight, kDefaultDynamicWeight);
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

void LightMap::stamp_moving_light(SDL_FPoint world_center,
                                  float      radius_px,
                                  std::uint8_t intensity,
                                  std::uint8_t clamp) {
    if (quadrants_.empty() || radius_px <= 0.0f || intensity == 0) {
        return;
    }

    const float r  = std::max(1.0f, radius_px);
    const float r2 = r * r;

    // For each quadrant, project the light into its dynamic grid.
    for (auto& q : quadrants_) {
        const SDL_Rect& rect = q.world_rect();
        if (rect.w <= 0 || rect.h <= 0) {
            continue;
        }

        // Quick reject using bounding box of the light.
        SDL_Rect light_bounds{
            static_cast<int>(std::floor(world_center.x - r)),
            static_cast<int>(std::floor(world_center.y - r)),
            static_cast<int>(std::ceil(r * 2.0f)),
            static_cast<int>(std::ceil(r * 2.0f))
        };
        SDL_Rect overlap{};
        if (!SDL_IntersectRect(&rect, &light_bounds, &overlap)) {
            continue;
        }

        const int gw = std::max(1, q.grid_width());
        const int gh = std::max(1, q.grid_height());
        // Map grid cell centers to world space.
        const float inv_gw = (gw > 1) ? 1.0f / static_cast<float>(gw - 1) : 0.0f;
        const float inv_gh = (gh > 1) ? 1.0f / static_cast<float>(gh - 1) : 0.0f;

        // Build a temporary dynamic grid for this quadrant and stamp into it,
        // then merge by taking the max value with any existing dynamic values.
        std::vector<std::uint8_t> temp(static_cast<std::size_t>(gw * gh), 0);

        for (int gy = 0; gy < gh; ++gy) {
            const float ny = static_cast<float>(gy) * inv_gh; // [0..1]
            const float wy = static_cast<float>(rect.y) + ny * static_cast<float>(std::max(1, rect.h) - 1);
            const float dy = wy - world_center.y;
            for (int gx = 0; gx < gw; ++gx) {
                const float nx = static_cast<float>(gx) * inv_gw; // [0..1]
                const float wx = static_cast<float>(rect.x) + nx * static_cast<float>(std::max(1, rect.w) - 1);
                const float dx = wx - world_center.x;
                const float d2 = dx * dx + dy * dy;
                if (d2 > r2) {
                    continue;
                }
                // Simple linear falloff to the radius.
                const float d        = std::sqrt(d2);
                const float falloff  = std::clamp(1.0f - (d / r), 0.0f, 1.0f);
                const int   raw      = static_cast<int>(std::lround(static_cast<float>(intensity) * falloff));
                const std::uint8_t v = clamp_byte(raw);
                temp[static_cast<std::size_t>(gy * gw + gx)] = std::max(temp[static_cast<std::size_t>(gy * gw + gx)], v);
            }
        }

        // Stamp into the quadrant's dynamic grid with the provided clamp.
        q.stamp_moving_lights(temp, gw, gh, clamp);
    }
}

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

