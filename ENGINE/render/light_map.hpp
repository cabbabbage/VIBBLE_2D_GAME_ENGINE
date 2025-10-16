#pragma once

#include <SDL.h>
#include <vector>
#include <array>
#include <cstddef>
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"

struct VirtualLightMap {
    static constexpr int kGridWidth      = 50;
    static constexpr int kGridHeight     = 50;
    static constexpr int kQuadrantCols   = 2;
    static constexpr int kQuadrantRows   = 2;
    static constexpr int kQuadrantCount  = kQuadrantCols * kQuadrantRows;
    static constexpr int kQuadrantWidth  = kGridWidth / kQuadrantCols;
    static constexpr int kQuadrantHeight = kGridHeight / kQuadrantRows;

    struct QuadrantSettings {
        float      base_light = 0.0f;
        float      opacity    = 0.0f;
        float      scale      = 1.0f;
        SDL_FPoint offset{ 0.0f, 0.0f };
    };

    std::array<float, kGridWidth * kGridHeight>        cells{};
    std::array<QuadrantSettings, kQuadrantCount>       quadrants{};
    int                                                screen_width  = 0;
    int                                                screen_height = 0;

    void clear(float value = 0.0f) {
        cells.fill(value);
        for (auto& q : quadrants) {
            q.base_light = value;
            q.opacity    = 0.0f;
            q.scale      = 1.0f;
            q.offset     = SDL_FPoint{ 0.0f, 0.0f };
        }
    }

    float& at(int x, int y) {
        return cells[static_cast<std::size_t>(y) * kGridWidth + static_cast<std::size_t>(x)];
    }

    float at(int x, int y) const {
        return cells[static_cast<std::size_t>(y) * kGridWidth + static_cast<std::size_t>(x)];
    }

    const QuadrantSettings& quadrant_settings(int index) const {
        static const QuadrantSettings kFallback{};
        if (index < 0 || index >= kQuadrantCount) {
            return kFallback;
        }
        return quadrants[static_cast<std::size_t>(index)];
    }

    SDL_Rect quadrant_bounds(int index) const;
    int      quadrant_for_point(float x, float y) const;
    int      quadrant_for_rect(const SDL_Rect& rect) const;
};

class LightMap {

public:
    struct LightEntry {
        SDL_Rect     dst;
        Uint8        alpha;
        SDL_Color    color_mod;
        SDL_Texture* texture = nullptr;
    };

    LightMap(Assets* assets, int screen_width, int screen_height);
    ~LightMap();

    void prepare_fullscreen_light_map(SDL_Renderer* renderer);
    void render_fullscreen_light_map(SDL_Renderer* renderer) const;
    void update_virtual_light_map(SDL_Renderer* renderer);
    const VirtualLightMap& virtual_light_map() const { return virtual_light_map_; }

private:
    void collect_layers(std::vector<LightEntry>& out);
    SDL_Rect get_scaled_position_rect(SDL_Point pos, int fw, int fh, float inv_scale, int min_w, int min_h);
    void compute_fullscreen_texture(SDL_Renderer* renderer, const std::vector<LightEntry>& layers);
    void compute_virtual_light_map(SDL_Renderer* renderer);

private:
    Assets* assets_;
    int screen_width_;
    int screen_height_;
    std::vector<LightEntry> scratch_layers_;
    VirtualLightMap virtual_light_map_{};
    SDL_Texture* fullscreen_texture_ = nullptr;
    std::vector<Uint32> pixel_buffer_;
    SDL_PixelFormat* capture_format_ = nullptr;
};
