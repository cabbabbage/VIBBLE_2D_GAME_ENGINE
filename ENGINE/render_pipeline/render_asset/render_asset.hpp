#pragma once

#include <SDL.h>
#include <string>

class Asset;

class RenderAsset {

        public:
    explicit RenderAsset(SDL_Renderer* renderer);
    SDL_Texture* texture_for_scale(Asset* asset, SDL_Texture* base_tex, int base_w, int base_h, int target_w, int target_h);

	private:
    SDL_Renderer* renderer_;
};
