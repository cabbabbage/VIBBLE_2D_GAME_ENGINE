#pragma once

#include <SDL.h>
#include <cstdint>
#include <vector>
#include <unordered_map>

#include "world/grid_point.hpp"

// Forward declarations to avoid heavy includes
class Asset;
class camera_grid;
namespace world {
class Grid;
class Chunk;
struct GridPoint;
}

/**
 * Grid camera for handling grid projection, visibility culling, and rendering.
 * Manages warped grid points, asset visibility, and chunk activation.
 */
class grid_camera {
public:
    // Grid bounds for screen-space culling
    struct GridBounds {
        float left   = 0.0f;
        float right  = 0.0f;
        float top    = 0.0f;
        float bottom = 0.0f;
    };

    grid_camera(int screen_width, int screen_height);

    // Update screen dimensions
    void set_screen_dimensions(int width, int height);

    // Main grid rebuilding function - processes all assets and grid points
    void rebuild_grid(camera_grid& camera, world::Grid& world_grid, float dt_seconds);

    // Grid state management
    void clear_grid_state();

    // Access to grid projection results
    const std::vector<world::GridPoint*>& grid_warped_points() const { return warped_points_; }
    const std::vector<Asset*>& grid_visible_assets() const { return visible_assets_; }
    const std::vector<world::GridPoint*>& grid_visible_points() const { return visible_points_; }
    const std::vector<world::Chunk*>& grid_active_chunks() const { return active_chunks_; }
    const GridBounds& grid_bounds() const { return bounds_; }
    SDL_Rect grid_world_rect() const { return cached_world_rect_; }

    // Asset lookup in grid
    world::GridPoint* grid_point_for_asset(const Asset* asset);
    const world::GridPoint* grid_point_for_asset(const Asset* asset) const;

    // Get screen dimensions
    int screen_width() const { return screen_width_; }
    int screen_height() const { return screen_height_; }

private:
    // Grid bounds calculation
    void rebuild_grid_bounds();

    int screen_width_  = 0;
    int screen_height_ = 0;

    // Grid projection state
    GridBounds bounds_{};
    SDL_Rect cached_world_rect_{0, 0, 0, 0};
    std::vector<world::GridPoint*> warped_points_{};
    std::vector<Asset*> visible_assets_{};
    std::vector<world::GridPoint*> visible_points_{};
    std::vector<world::Chunk*> active_chunks_{};
    std::unordered_map<world::GridId, std::size_t> id_to_index_{};
};
