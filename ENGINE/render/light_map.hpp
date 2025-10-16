#pragma once

#include <SDL.h>
#include <vector>
#include <array>
#include <cstddef>
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"

struct VirtualLightMap {
    static constexpr int kGridWidth  = 50;
    static constexpr int kGridHeight = 50;

    std::array<float, kGridWidth * kGridHeight> cells{};

    void clear(float value = 0.0f) {
        cells.fill(value);
    }

    float& at(int x, int y) {
        return cells[static_cast<std::size_t>(y) * kGridWidth + static_cast<std::size_t>(x)];
    }

    float at(int x, int y) const {
        return cells[static_cast<std::size_t>(y) * kGridWidth + static_cast<std::size_t>(x)];
    }
};

class LightMap {

public:
    struct LightEntry {
        SDL_Rect dst;
        Uint8 alpha;
        SDL_Color color_mod;
    };

    LightMap(Assets* assets, int screen_width, int screen_height);

    void update_virtual_light_map();
    const VirtualLightMap& virtual_light_map() const { return virtual_light_map_; }

private:
    void collect_layers(std::vector<LightEntry>& out);
    SDL_Rect get_scaled_position_rect(SDL_Point pos, int fw, int fh, float inv_scale, int min_w, int min_h);
    void compute_virtual_light_map(const std::vector<LightEntry>& layers);

private:
    Assets* assets_;
    int screen_width_;
    int screen_height_;
    std::vector<LightEntry> scratch_layers_;
    VirtualLightMap virtual_light_map_{};
};
