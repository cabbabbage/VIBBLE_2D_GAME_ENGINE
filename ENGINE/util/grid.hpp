#pragma once

#include <SDL.h>

#include <cstdint>
#include <limits>

namespace vibble::grid {

constexpr int kMaxResolution = 30;

constexpr int clamp_resolution(int r) noexcept {
    return r < 0 ? 0 : (r > kMaxResolution ? kMaxResolution : r);
}

constexpr int delta(int r) noexcept {
    return 1 << clamp_resolution(r);
}

constexpr bool is_multiple_of_delta(int value, int r) noexcept {
    const int step = delta(r);
    return step == 0 ? true : (value % step) == 0;
}

SDL_Point grid_index_to_world(int i, int j, int r, SDL_Point origin = SDL_Point{0, 0}) noexcept;
SDL_Point grid_index_to_world(SDL_Point ij, int r, SDL_Point origin = SDL_Point{0, 0}) noexcept;

SDL_Point snap_world_to_vertex(SDL_Point world, int r, SDL_Point origin = SDL_Point{0, 0}) noexcept;
SDL_Point world_to_grid_index(SDL_Point world, int r, SDL_Point origin = SDL_Point{0, 0}) noexcept;
SDL_Point change_resolution(SDL_Point indices, int from_resolution, int to_resolution) noexcept;
bool is_vertex_on_grid(SDL_Point world, int r, SDL_Point origin = SDL_Point{0, 0}) noexcept;

class Grid {
public:
    Grid(SDL_Point origin = SDL_Point{0, 0}, int default_resolution = 0) noexcept;

    void set_origin(SDL_Point origin) noexcept;
    [[nodiscard]] SDL_Point origin() const noexcept { return origin_; }

    void set_default_resolution(int resolution) noexcept;
    [[nodiscard]] int default_resolution() const noexcept { return default_resolution_; }

    [[nodiscard]] SDL_Point index_to_world(SDL_Point ij, int r) const noexcept;
    [[nodiscard]] SDL_Point index_to_world(int i, int j, int r) const noexcept;
    [[nodiscard]] SDL_Point world_to_index(SDL_Point world, int r) const noexcept;
    [[nodiscard]] SDL_Point snap_to_vertex(SDL_Point world, int r) const noexcept;
    [[nodiscard]] bool is_vertex(SDL_Point world, int r) const noexcept;
    [[nodiscard]] SDL_Point convert_resolution(SDL_Point indices, int from_resolution, int to_resolution) const noexcept;

private:
    SDL_Point origin_{0, 0};
    int default_resolution_ = 0;
};

Grid& global_grid() noexcept;

}

