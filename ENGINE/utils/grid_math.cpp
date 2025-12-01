#include "utils/grid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibble::grid {
namespace {

using int64 = std::int64_t;

constexpr int64 delta64(int r) noexcept {
    return int64{1} << clamp_resolution(r);
}

int clamp_to_int(int64 value) noexcept {
    if (value > std::numeric_limits<int>::max()) {
        return std::numeric_limits<int>::max();
    }
    if (value < std::numeric_limits<int>::min()) {
        return std::numeric_limits<int>::min();
    }
    return static_cast<int>(value);
}

int round_div_nearest(int64 numerator, int64 denominator) noexcept {
    if (denominator == 0) {
        return 0;
    }
    const double ratio = static_cast<double>(numerator) / static_cast<double>(denominator);
    const auto rounded = std::llround(ratio);
    if (rounded > static_cast<double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    if (rounded < static_cast<double>(std::numeric_limits<int>::min())) {
        return std::numeric_limits<int>::min();
    }
    return static_cast<int>(rounded);
}

}

SDL_Point grid_index_to_world(int i, int j, int r, SDL_Point origin) noexcept {
    const int64 step = delta64(r);
    const int64 x = static_cast<int64>(i) * step + static_cast<int64>(origin.x);
    const int64 y = static_cast<int64>(j) * step + static_cast<int64>(origin.y);
    return SDL_Point{clamp_to_int(x), clamp_to_int(y)};
}

SDL_Point grid_index_to_world(SDL_Point ij, int r, SDL_Point origin) noexcept {
    return grid_index_to_world(ij.x, ij.y, r, origin);
}

SDL_Point snap_world_to_vertex(SDL_Point world, int r, SDL_Point origin) noexcept {
    const int64 step = delta64(r);
    const int64 dx = static_cast<int64>(world.x) - static_cast<int64>(origin.x);
    const int64 dy = static_cast<int64>(world.y) - static_cast<int64>(origin.y);
    const int snapped_i = round_div_nearest(dx, step);
    const int snapped_j = round_div_nearest(dy, step);
    return grid_index_to_world(snapped_i, snapped_j, r, origin);
}

SDL_Point world_to_grid_index(SDL_Point world, int r, SDL_Point origin) noexcept {
    const double step = static_cast<double>(delta64(r));
    const double gx = (static_cast<double>(world.x) - static_cast<double>(origin.x)) / step;
    const double gy = (static_cast<double>(world.y) - static_cast<double>(origin.y)) / step;
    const double floored_x = std::floor(gx);
    const double floored_y = std::floor(gy);
    return SDL_Point{
        clamp_to_int(static_cast<int64>(floored_x)), clamp_to_int(static_cast<int64>(floored_y)) };
}

SDL_Point change_resolution(SDL_Point indices, int from_resolution, int to_resolution) noexcept {
    if (from_resolution == to_resolution) {
        return indices;
    }
    const int diff = from_resolution - to_resolution;
    if (diff > 0) {
        const int64 factor = delta64(diff);
        const int64 ix = static_cast<int64>(indices.x) * factor;
        const int64 iy = static_cast<int64>(indices.y) * factor;
        return SDL_Point{clamp_to_int(ix), clamp_to_int(iy)};
    }
    const int shift = -diff;
    const int64 divisor = delta64(shift);
    const int new_x = round_div_nearest(indices.x, divisor);
    const int new_y = round_div_nearest(indices.y, divisor);
    return SDL_Point{new_x, new_y};
}

bool is_vertex_on_grid(SDL_Point world, int r, SDL_Point origin) noexcept {
    const int step = delta(r);
    if (step == 0) {
        return true;
    }
    const int dx = world.x - origin.x;
    const int dy = world.y - origin.y;
    return (dx % step) == 0 && (dy % step) == 0;
}

Grid::Grid(SDL_Point origin, int default_resolution) noexcept {
    set_origin(origin);
    set_default_resolution(default_resolution);
}

void Grid::set_origin(SDL_Point origin) noexcept {
    origin_ = origin;
}

void Grid::set_default_resolution(int resolution) noexcept {
    default_resolution_ = clamp_resolution(resolution);
}

SDL_Point Grid::index_to_world(SDL_Point ij, int r) const noexcept {
    return grid_index_to_world(ij, r, origin_);
}

SDL_Point Grid::index_to_world(int i, int j, int r) const noexcept {
    return grid_index_to_world(i, j, r, origin_);
}

SDL_Point Grid::world_to_index(SDL_Point world, int r) const noexcept {
    return world_to_grid_index(world, r, origin_);
}

SDL_Point Grid::snap_to_vertex(SDL_Point world, int r) const noexcept {
    return snap_world_to_vertex(world, r, origin_);
}

bool Grid::is_vertex(SDL_Point world, int r) const noexcept {
    return is_vertex_on_grid(world, r, origin_);
}

SDL_Point Grid::convert_resolution(SDL_Point indices, int from_resolution, int to_resolution) const noexcept {
    return change_resolution(indices, from_resolution, to_resolution);
}

Grid& global_grid() noexcept {
    static Grid grid_instance{};
    return grid_instance;
}

}

