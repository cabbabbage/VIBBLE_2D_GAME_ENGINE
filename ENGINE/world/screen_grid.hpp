#pragma once

#include <SDL.h>

#include <unordered_map>
#include <vector>

#include "world/grid_point.hpp"

class camera;
class Asset;

namespace world {

class Grid;

class ScreenGrid {
public:
    struct Bounds {
        float left   = 0.0f;
        float right  = 0.0f;
        float top    = 0.0f;
        float bottom = 0.0f;
    };

    ScreenGrid() = default;

    void rebuild(Grid& world_grid, const camera& cam, float dt_seconds);

    const std::vector<GridPoint>& warped_points() const { return warped_points_; }
    const std::vector<Asset*>& visible_assets() const { return visible_assets_; }
    const std::vector<GridPoint*>& visible_points() const { return visible_points_; }
    const Bounds& bounds() const { return bounds_; }
    const std::vector<Chunk*>& active_chunks() const { return active_chunks_; }

    GridPoint* point_for_asset(const Asset* asset);
    const GridPoint* point_for_asset(const Asset* asset) const;

private:
    void clear();
    void rebuild_bounds(const camera& cam);
    SDL_Rect compute_world_rect(const camera& cam) const;
    void collect_candidates(const Grid& world_grid);
    void warp_points(const Grid& world_grid, const camera& cam);
    void rebuild_active_chunks(const Grid& world_grid);

    Bounds bounds_{};
    std::vector<GridPoint> warped_points_{};
    std::vector<Asset*> visible_assets_{};
    std::vector<GridPoint*> visible_points_{};
    std::vector<Chunk*> active_chunks_{};
    std::unordered_map<GridId, std::size_t> id_to_index_{};
};

}  // namespace world
