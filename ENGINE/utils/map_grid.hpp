#pragma once

#include <vector>
#include <random>
#include <SDL.h>
#include "utils/area.hpp"

// Map-wide grid for spacing asset spawns.
// Each grid point is an SDL_Point in global map coordinates and has an occupied flag.
class MapGrid {
public:
    struct Point {
        SDL_Point pos{0,0};
        bool occupied{false};
    };

public:
    // Create a grid that covers a rectangle starting at top_left with size (width x height),
    // placing grid points every `spacing` pixels in X and Y.
    MapGrid(int width, int height, int spacing, SDL_Point top_left);

    // Factory to build from Area bounds.
    static MapGrid from_area_bounds(const Area& area, int spacing);

    // Returns the nearest unoccupied grid point to `p` (may be outside any specific Area).
    // Returns nullptr if all points are occupied.
    Point* get_nearest_point(SDL_Point p);

    // Returns a random unoccupied grid point inside `area` or nullptr if none.
    Point* get_rnd_point_in_area(const Area& area, std::mt19937& rng);

    // Returns all unoccupied points inside `area`.
    std::vector<Point*> get_all_points_in_area(const Area& area) const;

    // Mark a grid point as occupied (no-op if null).
    void set_occupied(Point* pt, bool occ = true);

    // Convenience: mark the grid point corresponding to a world coordinate
    // as occupied.  The coordinate is clamped to the nearest valid grid cell.
    void set_occupied_at(SDL_Point p, bool occ = true);

    // Retrieve the grid point that corresponds to the provided world
    // coordinate.  The coordinate is clamped to the nearest valid grid cell.
    Point* point_at(SDL_Point p);

    // Number of unoccupied points remaining.
    int free_count() const { return free_count_; }

private:
    int width_ = 0;
    int height_ = 0;
    int spacing_ = 1;
    SDL_Point origin_{}; // top-left corner of the covered rectangle
    int cols_ = 0;
    int rows_ = 0;
    int free_count_ = 0;
    std::vector<Point> grid_;

    inline bool in_bounds_idx(int ix, int iy) const {
        return ix >= 0 && ix < cols_ && iy >= 0 && iy < rows_;
    }
    inline int idx(int ix, int iy) const { return iy * cols_ + ix; }
    inline SDL_Point to_world(int ix, int iy) const {
        return SDL_Point{ origin_.x + ix * spacing_, origin_.y + iy * spacing_ };
    }
    inline void to_grid_indices(SDL_Point p, int& ix, int& iy) const {
        const double gx = static_cast<double>(p.x - origin_.x) / static_cast<double>(spacing_);
        const double gy = static_cast<double>(p.y - origin_.y) / static_cast<double>(spacing_);
        ix = static_cast<int>(std::llround(gx));
        iy = static_cast<int>(std::llround(gy));
        if (ix < 0) ix = 0; if (ix >= cols_) ix = cols_ - 1;
        if (iy < 0) iy = 0; if (iy >= rows_) iy = rows_ - 1;
    }
};

