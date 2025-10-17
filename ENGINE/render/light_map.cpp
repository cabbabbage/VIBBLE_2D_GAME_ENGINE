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
    tile_mask_ = SDL_CreateTexture(renderer,
                                   SDL_PIXELFORMAT_RGBA8888,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   grid_width_,
                                   grid_height_);
    if (tile_mask_) {
        SDL_SetTextureBlendMode(tile_mask_, SDL_BLENDMODE_MOD);
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
            dynamic_grid_[dst_index] = std::min<std::uint8_t>(value, clamp);
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
    for (std::size_t i = 0; i < total; ++i) {
        const std::uint8_t value = dynamic_grid_[i];
        if (value == 0) {
            continue;
        }
        dynamic_grid_[i] = (value > fade) ? (value - fade) : 0;
    }
    dirty_ = true;
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

    const std::size_t total_pixels = static_cast<std::size_t>(grid_width_) * static_cast<std::size_t>(grid_height_);
    std::vector<std::uint32_t> pixels(total_pixels, 0);
    for (int y = 0; y < grid_height_; ++y) {
        for (int x = 0; x < grid_width_; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(grid_width_) +
                                    static_cast<std::size_t>(x);
            const float sample = cell_sample(x, y, static_weight, dynamic_weight);
            const std::uint8_t value = clamp_byte(static_cast<int>(std::round(sample * 255.0f)));
            const std::uint32_t rgba = (static_cast<std::uint32_t>(value) << 24) |
                                       (static_cast<std::uint32_t>(value) << 16) |
                                       (static_cast<std::uint32_t>(value) << 8) |
                                       static_cast<std::uint32_t>(value);
            pixels[idx] = rgba;
        }
    }

    void*   pixels_ptr = nullptr;
    int     pitch      = 0;
    if (SDL_LockTexture(tile_mask_, nullptr, &pixels_ptr, &pitch) != 0) {
        return;
    }

    const int row_bytes = grid_width_ * static_cast<int>(sizeof(std::uint32_t));
    std::uint8_t* dst   = static_cast<std::uint8_t*>(pixels_ptr);
    const std::uint8_t* src = reinterpret_cast<const std::uint8_t*>(pixels.data());
    for (int y = 0; y < grid_height_; ++y) {
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
}

void LightMap::rebuild(SDL_Renderer* renderer) {
    if (!renderer) {
        return;
    }
    if (screen_width_ <= 0 || screen_height_ <= 0) {
        return;
    }

    quadrant_cols_ = std::max(1, requested_quadrants_);
    quadrant_rows_ = std::max(1, requested_quadrants_);

    const int base_width  = std::max(1, screen_width_ / quadrant_cols_);
    const int base_height = std::max(1, screen_height_ / quadrant_rows_);
    quadrant_size_px_     = std::max(base_width, base_height);

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

