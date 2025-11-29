#pragma once

#include <SDL.h>
#include "render/warped_screen_grid.hpp"
#include "world/grid_point.hpp"

class ScreenProjector {
public:
    explicit ScreenProjector(const WarpedScreenGrid& grid);

    // Updates the screen fields of the GridPoint
    void project_to_screen(world::GridPoint& point) const;

    // Computes screen position and effects without modifying a GridPoint
    WarpedScreenGrid::RenderEffects compute_effects(SDL_Point world, float asset_height, float reference_height) const;

    // Basic coordinate mapping
    SDL_FPoint map_to_screen(SDL_Point world) const;
    
    // Floor warping logic
    float warp_floor_screen_y(float world_y, float linear_screen_y) const;

private:
    const WarpedScreenGrid& grid_;
    
    // Helper for parallax
    SDL_FPoint apply_parallax(SDL_FPoint base_screen) const;
};
