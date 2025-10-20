#pragma once

#include <SDL.h>

#include <vector>

class Asset;

namespace world {

struct Chunk {
    int i = 0;
    int j = 0;
    int r_chunk = 9;
    SDL_Rect world_bounds{0, 0, 0, 0};

    std::vector<Asset*> assets;

    SDL_Texture* static_light_map = nullptr;
    float base_brightness = 1.0f;
    float brightness_strength = 1.0f;
    float opacity_strength = 1.0f;
    float scale_strength = 1.0f;
    int offset_x = 0;
    int offset_y = 0;

    struct ShadowData {
        float scale = 1.0f;
        float opacity = 1.0f;
        float offset_x_percent = 0.0f;
        float offset_y_percent = 0.0f;
        float parallax_intensity_percent = 0.0f;
    } shadow;

    bool lighting_dirty = true;
    bool has_dynamic_overlay = false;

    Chunk() = default;
    Chunk(int in_i, int in_j, int r, SDL_Rect bounds) : i(in_i), j(in_j), r_chunk(r), world_bounds(bounds) {}
    ~Chunk();

    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;
    Chunk(Chunk&&) noexcept = default;
    Chunk& operator=(Chunk&&) noexcept = default;
};

} // namespace world

