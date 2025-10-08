#include "map_grid.hpp"
#include <algorithm>
#include <cmath>

MapGrid::MapGrid(int width, int height, int spacing, SDL_Point top_left)
    : width_(std::max(0, width)),
      height_(std::max(0, height)),
      spacing_(std::max(1, spacing)),
      origin_(top_left)
{
    cols_ = (width_  > 0) ? (width_  / spacing_) + 1 : 1;
    rows_ = (height_ > 0) ? (height_ / spacing_) + 1 : 1;
    const int total = cols_ * rows_;
    grid_.resize(total);
    for (int iy = 0; iy < rows_; ++iy) {
        for (int ix = 0; ix < cols_; ++ix) {
            grid_[idx(ix, iy)].pos = to_world(ix, iy);
            grid_[idx(ix, iy)].occupied = false;
        }
    }
    free_count_ = static_cast<int>(grid_.size());
}

MapGrid MapGrid::from_area_bounds(const Area& area, int spacing) {
    auto [minx, miny, maxx, maxy] = area.get_bounds();
    const int w = std::max(0, maxx - minx);
    const int h = std::max(0, maxy - miny);
    return MapGrid(w, h, spacing, SDL_Point{minx, miny});
}

MapGrid::Point* MapGrid::get_nearest_point(SDL_Point p) {
    if (free_count_ <= 0) return nullptr;
    int cx, cy;
    to_grid_indices(p, cx, cy);

    if (!grid_[idx(cx, cy)].occupied) return &grid_[idx(cx, cy)];

    const int max_r = std::max(cols_, rows_);
    for (int r = 1; r <= max_r; ++r) {

        for (int dx = -r; dx <= r; ++dx) {
            const int ix1 = cx + dx;
            const int iy1 = cy - r;
            const int iy2 = cy + r;
            if (in_bounds_idx(ix1, iy1)) {
                auto& pt = grid_[idx(ix1, iy1)];
                if (!pt.occupied) return &pt;
            }
            if (in_bounds_idx(ix1, iy2)) {
                auto& pt = grid_[idx(ix1, iy2)];
                if (!pt.occupied) return &pt;
            }
        }
        for (int dy = -r + 1; dy <= r - 1; ++dy) {
            const int iy1 = cy + dy;
            const int ix1 = cx - r;
            const int ix2 = cx + r;
            if (in_bounds_idx(ix1, iy1)) {
                auto& pt = grid_[idx(ix1, iy1)];
                if (!pt.occupied) return &pt;
            }
            if (in_bounds_idx(ix2, iy1)) {
                auto& pt = grid_[idx(ix2, iy1)];
                if (!pt.occupied) return &pt;
            }
        }
    }
    return nullptr;
}

MapGrid::Point* MapGrid::get_rnd_point_in_area(const Area& area, std::mt19937& rng) {
    if (free_count_ <= 0) return nullptr;
    auto [minx, miny, maxx, maxy] = area.get_bounds();
    int min_ix = 0, min_iy = 0;
    int max_ix = 0, max_iy = 0;
    to_grid_indices(SDL_Point{minx, miny}, min_ix, min_iy);
    to_grid_indices(SDL_Point{maxx, maxy}, max_ix, max_iy);

    min_ix = std::clamp(min_ix, 0, cols_ - 1);
    max_ix = std::clamp(max_ix, 0, cols_ - 1);
    min_iy = std::clamp(min_iy, 0, rows_ - 1);
    max_iy = std::clamp(max_iy, 0, rows_ - 1);

    if (min_ix > max_ix) std::swap(min_ix, max_ix);
    if (min_iy > max_iy) std::swap(min_iy, max_iy);

    Point* chosen = nullptr;
    int seen = 0;
    for (int iy = min_iy; iy <= max_iy; ++iy) {
        for (int ix = min_ix; ix <= max_ix; ++ix) {
            auto& pt = grid_[idx(ix, iy)];
            if (pt.occupied || !area.contains_point(pt.pos)) continue;
            ++seen;
            std::uniform_int_distribution<int> pick(0, seen - 1);
            if (pick(rng) == 0) {
                chosen = &pt;
            }
        }
    }

    return chosen;
}

std::vector<MapGrid::Point*> MapGrid::get_all_points_in_area(const Area& area) const {
    if (grid_.empty()) return {};

    auto [minx, miny, maxx, maxy] = area.get_bounds();
    int min_ix = 0, min_iy = 0;
    int max_ix = 0, max_iy = 0;
    to_grid_indices(SDL_Point{minx, miny}, min_ix, min_iy);
    to_grid_indices(SDL_Point{maxx, maxy}, max_ix, max_iy);

    min_ix = std::clamp(min_ix, 0, cols_ - 1);
    max_ix = std::clamp(max_ix, 0, cols_ - 1);
    min_iy = std::clamp(min_iy, 0, rows_ - 1);
    max_iy = std::clamp(max_iy, 0, rows_ - 1);

    if (min_ix > max_ix) std::swap(min_ix, max_ix);
    if (min_iy > max_iy) std::swap(min_iy, max_iy);

    const int span_x = max_ix - min_ix + 1;
    const int span_y = max_iy - min_iy + 1;
    const int reserve = span_x * span_y;
    std::vector<Point*> out;
    if (reserve > 0) {
        out.reserve(reserve);
    }

    for (int iy = min_iy; iy <= max_iy; ++iy) {
        for (int ix = min_ix; ix <= max_ix; ++ix) {
            const auto& pt = grid_[idx(ix, iy)];
            if (!pt.occupied && area.contains_point(pt.pos)) {
                out.push_back(const_cast<Point*>(&pt));
            }
        }
    }
    return out;
}

void MapGrid::set_occupied(Point* pt, bool occ) {
    if (!pt) return;
    const bool was = pt->occupied;
    pt->occupied = occ;
    if (was != occ) free_count_ += occ ? -1 : 1;
}

MapGrid::Point* MapGrid::point_at(SDL_Point p) {
    if (grid_.empty()) return nullptr;
    int ix = 0;
    int iy = 0;
    to_grid_indices(p, ix, iy);
    if (!in_bounds_idx(ix, iy)) return nullptr;
    return &grid_[idx(ix, iy)];
}

void MapGrid::set_occupied_at(SDL_Point p, bool occ) {
    Point* pt = point_at(p);
    set_occupied(pt, occ);
}

