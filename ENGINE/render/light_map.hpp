#pragma once

#include <SDL.h>
#include <vector>
#include <array>
#include <cstddef>
#include "core/AssetsManager.hpp"
#include "global_light_source.hpp"
#include "render/camera.hpp"

struct VirtualLightMap {
    static constexpr int kGridWidth  = 12;
    static constexpr int kGridHeight = 12;

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
        SDL_Texture* tex;
        SDL_Rect dst;
        Uint8 alpha;
        SDL_RendererFlip flip;
        SDL_Color color_mod;
};
    LightMap(SDL_Renderer* renderer, Assets* assets, Global_Light_Source& main_light, int screen_width, int screen_height, SDL_Texture* fullscreen_light_tex);
    void render(bool debugging, bool light_map_only);
    void set_fullscreen_light_settings(SDL_Color color, int min_opacity, int max_opacity);
    void update_virtual_light_map();
    const VirtualLightMap& virtual_light_map() const { return virtual_light_map_; }

        private:
    void collect_layers(std::vector<LightEntry>& out);
    SDL_Texture* build_lowres_mask(const std::vector<LightEntry>& layers, int low_w, int low_h, int downscale);
    SDL_Rect get_scaled_position_rect(SDL_Point pos, int fw, int fh, float inv_scale, int min_w, int min_h);
    void compute_virtual_light_map(const std::vector<LightEntry>& layers);

        private:
    SDL_Renderer* renderer_;
    Assets* assets_;
    Global_Light_Source& main_light_;
    int screen_width_;
    int screen_height_;
    SDL_Texture* fullscreen_light_tex_;
    Uint8 last_main_light_alpha_ = 0;
    SDL_Color fullscreen_light_color_{255, 255, 255, 255};
    int fullscreen_light_min_opacity_ = 0;
    int fullscreen_light_max_opacity_ = 255;
    std::vector<LightEntry> cached_layers_;
    bool cached_layers_ready_ = false;
    VirtualLightMap virtual_light_map_{};
};
