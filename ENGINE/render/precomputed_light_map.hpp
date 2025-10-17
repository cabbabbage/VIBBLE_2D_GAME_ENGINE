#pragma once

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <vector>

struct PrecomputedLightMapQuadrant {
    SDL_Rect                        world_rect{0, 0, 0, 0};
    SDL_Texture*                    texture = nullptr;
    std::vector<std::uint8_t>       light_samples{};
    float                           base_brightness = 0.0f;
};

struct PrecomputedLightMap {
    int map_width          = 0;
    int map_height         = 0;
    int grid_spacing       = 0;
    int cells_per_quadrant = 0;
    int quadrant_cols      = 0;
    int quadrant_rows      = 0;
    int grid_resolution    = 0;
    int padding_cells      = 0;
    SDL_Texture* full_texture = nullptr;
    std::vector<PrecomputedLightMapQuadrant> quadrants{};

    PrecomputedLightMap() = default;
    PrecomputedLightMap(PrecomputedLightMap&& other) noexcept { *this = std::move(other); }
    PrecomputedLightMap& operator=(PrecomputedLightMap&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        release_textures();
        map_width          = other.map_width;
        map_height         = other.map_height;
        grid_spacing       = other.grid_spacing;
        cells_per_quadrant = other.cells_per_quadrant;
        quadrant_cols      = other.quadrant_cols;
        quadrant_rows      = other.quadrant_rows;
        grid_resolution    = other.grid_resolution;
        padding_cells      = other.padding_cells;
        full_texture       = other.full_texture;
        quadrants          = std::move(other.quadrants);
        other.full_texture = nullptr;
        return *this;
    }
    PrecomputedLightMap(const PrecomputedLightMap&) = delete;
    PrecomputedLightMap& operator=(const PrecomputedLightMap&) = delete;
    ~PrecomputedLightMap() { release_textures(); }

    void release_textures() {
        if (full_texture) {
            SDL_DestroyTexture(full_texture);
            full_texture = nullptr;
        }
        for (auto& quadrant : quadrants) {
            if (quadrant.texture) {
                SDL_DestroyTexture(quadrant.texture);
                quadrant.texture = nullptr;
            }
        }
    }
};
