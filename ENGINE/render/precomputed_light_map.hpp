#pragma once

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <vector>

struct PrecomputedLightMapChunk {
    SDL_Rect                        world_rect{0, 0, 0, 0};
    SDL_Texture*                    texture = nullptr;
    std::vector<std::uint8_t>       light_samples{};
};

struct PrecomputedLightMap {
    int map_width          = 0;
    int map_height         = 0;
    int grid_spacing       = 0;
    int cells_per_chunk = 0;
    int chunk_cols      = 0;
    int chunk_rows      = 0;
    int grid_resolution    = 0;
    int padding_cells      = 0;
    SDL_Texture* full_texture = nullptr;
    std::vector<PrecomputedLightMapChunk> chunks{};

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
        cells_per_chunk = other.cells_per_chunk;
        chunk_cols      = other.chunk_cols;
        chunk_rows      = other.chunk_rows;
        grid_resolution    = other.grid_resolution;
        padding_cells      = other.padding_cells;
        full_texture       = other.full_texture;
        chunks          = std::move(other.chunks);
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
        for (auto& chunk : chunks) {
            if (chunk.texture) {
                SDL_DestroyTexture(chunk.texture);
                chunk.texture = nullptr;
            }
        }
    }
};

