#include "render/screen_projector.hpp"
#include "render/warped_screen_grid.hpp"
#include "world/grid_point.hpp"
#include <cmath>
#include <algorithm>

ScreenProjector::ScreenProjector(const WarpedScreenGrid& grid)
    : grid_(grid) {}

void ScreenProjector::project_to_screen(world::GridPoint& point) const {
    // 1. Map world to screen (linear)
    SDL_FPoint linear_screen = grid_.map_to_screen(point.world);
    
    // 2. Apply floor warping
    float warped_y = grid_.warp_floor_screen_y(static_cast<float>(point.world.y), linear_screen.y);
    
    // 3. Apply parallax
    // Note: Original WorldGrid parallax logic is being replaced here.
    // For now, we calculate a simple parallax based on the camera center.
    // Ideally this should use the parallax settings from WarpedScreenGrid.
    float parallax_dx = 0.0f; 
    
    // Example parallax logic (can be expanded):
    // float dist_x = point.world.x - grid_.get_view_center_f().x;
    // parallax_dx = dist_x * 0.0f; // 0.0 for now until we have depth info

    point.screen = SDL_FPoint{linear_screen.x + parallax_dx, warped_y};
    point.parallax_dx = parallax_dx;
    
    // Mark as valid for this frame (assuming frame 0 for now, caller should manage frames)
    // point.mark_screen_data_updated(0); 
}

WarpedScreenGrid::RenderEffects ScreenProjector::compute_effects(SDL_Point world, float asset_height, float reference_height) const {
    // Delegate to WarpedScreenGrid's existing logic for now
    return grid_.compute_render_effects(world, asset_height, reference_height, WarpedScreenGrid::RenderSmoothingKey(nullptr));
}

SDL_FPoint ScreenProjector::map_to_screen(SDL_Point world) const {
    return grid_.map_to_screen(world);
}

float ScreenProjector::warp_floor_screen_y(float world_y, float linear_screen_y) const {
    return grid_.warp_floor_screen_y(world_y, linear_screen_y);
}

SDL_FPoint ScreenProjector::apply_parallax(SDL_FPoint base_screen) const {
    return base_screen;
}
