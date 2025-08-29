
#pragma once

#include <SDL.h>
#include <vector>
#include <random>
#include "core/Assets.hpp"
#include "global_light_source.hpp"
#include "utils/parallax.hpp"

class LightMap {
public:
    struct LightEntry {
        SDL_Texture* tex;
        SDL_Rect dst;
        Uint8 alpha;
        SDL_RendererFlip flip;
        bool apply_tint;
    };

    LightMap(SDL_Renderer* renderer,
             Assets* assets,
             Parallax& parallax,
             Global_Light_Source& main_light,
             int screen_width,
             int screen_height,
             SDL_Texture* fullscreen_light_tex);

    void render(bool debugging);

    ~LightMap();

private:
    void collect_layers(std::vector<LightEntry>& out, std::mt19937& rng);
    void build_lowres_mask(const std::vector<LightEntry>& layers);
    SDL_Rect get_scaled_position_rect(const std::pair<int,int>& pos,
                                      int fw, int fh,
                                      float inv_scale,
                                      int min_w, int min_h);

private:
    SDL_Renderer* renderer_;
    Assets* assets_;
    Parallax& parallax_;
    Global_Light_Source& main_light_;
    int screen_width_;
    int screen_height_;
    SDL_Texture* fullscreen_light_tex_;
    SDL_Texture* lowres_mask_;
    int downscale_;
    int low_w_;
    int low_h_;
};
