#pragma once

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <vector>

class Asset;

namespace world {

class Chunk;

using GridId = std::uint64_t;

struct GridPoint {
    GridId     id           = 0;
    SDL_Point  world        = SDL_Point{0, 0};
    SDL_Point  grid_index   = SDL_Point{0, 0};
    SDL_Point  chunk_index  = SDL_Point{0, 0};
    Chunk*     chunk        = nullptr;

    SDL_FPoint screen       = SDL_FPoint{0.0f, 0.0f};
    float      parallax_dx  = 0.0f;
    float      vertical_scale  = 1.0f;
    float      horizon_fade_alpha = 1.0f;
    float      perspective_scale = 1.0f;
    float      distance_to_camera = 0.0f;
    float      tilt_radians      = 0.0f;
    bool       on_screen         = false;

    mutable std::uint64_t screen_data_frame_updated = 0;
    mutable bool          screen_data_valid         = false;

    void invalidate_screen_data() {
        screen_data_valid = false;
    }

    void mark_screen_data_updated(std::uint64_t frame) {
        screen_data_frame_updated = frame;
        screen_data_valid = true;
    }

    bool has_valid_screen_data(std::uint64_t current_frame) const {
        return screen_data_valid && screen_data_frame_updated == current_frame;
    }

    std::vector<std::unique_ptr<Asset>> occupants;
};

}