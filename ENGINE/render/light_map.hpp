#pragma once

#include <SDL.h>
#include <vector>
#include <random>
#include "core/assetsManager.hpp"
#include "global_light_source.hpp"
#include "render/camera.hpp"

class LightMap {

	public:
    struct LightEntry {
    SDL_Texture* tex;
    SDL_Rect dst;
    Uint8 alpha;
    SDL_RendererFlip flip;
    bool apply_tint;
	};
    LightMap(SDL_Renderer* renderer, Assets* assets, Global_Light_Source& main_light, int screen_width, int screen_height, SDL_Texture* fullscreen_light_tex);
    void render(bool debugging);

	private:
    void collect_layers(std::vector<LightEntry>& out, std::mt19937& rng);
    SDL_Texture* build_lowres_mask(const std::vector<LightEntry>& layers, int low_w, int low_h, int downscale);
    SDL_Rect get_scaled_position_rect(SDL_Point pos, int fw, int fh, float inv_scale, int min_w, int min_h);

	private:
    SDL_Renderer* renderer_;
    Assets* assets_;
    Global_Light_Source& main_light_;
    int screen_width_;
    int screen_height_;
    SDL_Texture* fullscreen_light_tex_;
};
