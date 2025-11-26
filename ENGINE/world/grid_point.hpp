#pragma once

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <vector>

class Asset;

namespace world {

class Chunk;

using GridId = std::uint64_t;

/**
 * Shared grid point representation used by both the world Grid and the warped
 * ScreenGrid. Stores world/screen coordinates, occupants, and precomputed
 * render data so other systems don't need to recompute per-asset geometry.
 */
struct GridPoint {
    GridId     id           = 0;
    SDL_Point  world        = SDL_Point{0, 0};
    SDL_Point  grid_index   = SDL_Point{0, 0};
    SDL_Point  chunk_index  = SDL_Point{0, 0};
    Chunk*     chunk        = nullptr;
    float depth_cue_foreground_opacity = 0.0f;
    float depth_cue_background_opacity = 1.0f;

    // Screen/wrapping state populated by ScreenGrid.
    SDL_FPoint screen       = SDL_FPoint{0.0f, 0.0f};
    float      parallax_dx  = 0.0f;
    float      vertical_scale  = 1.0f;
    float      perspective_scale = 1.0f;
    float      distance_to_camera = 0.0f;
    float      tilt_radians      = 0.0f;
    bool       on_screen         = false;

    std::vector<std::unique_ptr<Asset>> occupants;
};

}  // namespace world





//TODO do these somewhere: 

float compute_depthcue_opacity(const DepthCueSample& sample,
                               int max_opacity,
                               camera_grid::BlurFalloffMethod method) {
    if (sample.plane == DepthCuePlane::None || max_opacity <= 0) {
        return 0.0f;
    }
    const float curve = evaluate_depth_curve(method, sample.t);
    return curve * static_cast<float>(std::clamp(max_opacity, 0, 255)) / 255.0f;
}