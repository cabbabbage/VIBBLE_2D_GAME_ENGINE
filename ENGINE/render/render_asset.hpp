#pragma once

#include <SDL.h>
#include <string>
#include "render/camera.hpp"

class Asset;
class Global_Light_Source;
class Assets;

class RenderAsset {

        public:
    RenderAsset(SDL_Renderer* renderer, Assets* assets, camera& cam, Global_Light_Source& main_light, Asset* player);
    SDL_Texture* regenerateFinalTexture(Asset* a);
    SDL_Texture* texture_for_scale(Asset* asset,
                                   SDL_Texture* base_tex,
                                   int base_w,
                                   int base_h,
                                   int target_w,
                                   int target_h,
                                   float camera_scale);

	private:
    Asset* p;
    SDL_Texture* render_shadow_mask(Asset* a, int bw, int bh);
    void render_shadow_moving_lights(Asset* a, const SDL_Rect& bounds, Uint8 alpha);
    void render_shadow_orbital_lights(Asset* a, const SDL_Rect& bounds, Uint8 alpha);
    void render_shadow_received_static_lights(Asset* a, const SDL_Rect& bounds, Uint8 alpha);

	private:
    SDL_Renderer* renderer_;
    Assets* assets_ = nullptr;
    camera& cam_;
    Global_Light_Source& main_light_source_;
};
