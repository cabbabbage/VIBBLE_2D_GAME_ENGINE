#pragma once

#include <vector>
#include <random>
#include <SDL.h>
#include "utils/area.hpp"

class MapGrid {
public:
    struct Point {
        SDL_Point pos{0,0};
        bool occupied{false};
};

public:

    MapGrid(int width, int height, int spacing, SDL_Point top_left);

    static MapGrid from_area_bounds(const Area& area, int spacing);

    Point* get_nearest_point(SDL_Point p);

    Point* get_rnd_point_in_area(const Area& area, std::mt19937& rng);

    std::vector<Point*> get_all_points_in_area(const Area& area) const;

    void set_occupied(Point* pt, bool occ = true);

    void set_occupied_at(SDL_Point p, bool occ = true);

    Point* point_at(SDL_Point p);

    int free_count() const { return free_count_; }

private:
    int width_ = 0;
    int height_ = 0;
    int spacing_ = 1;
    SDL_Point origin_{};
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

